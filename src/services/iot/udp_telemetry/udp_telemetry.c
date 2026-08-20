// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file udp_telemetry.c
 * @brief The line protocol builder (InfluxData "Line protocol") and the RFC 768 send.
 *
 * The build calls append the four elements into the caller's buffer in order - measurement, tag
 * set, field set, timestamp - and touch no socket. A write hands those octets to the UDP sending
 * side as one datagram. A build without a network stack keeps the builder and refuses every send.
 */

#include "services/iot/udp_telemetry/udp_telemetry.h"
#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from

static uint8_t ip_work[16]; // the borrow an entry takes; Ip never reads it

#if PROTOCORE_ENABLE_UDP_TELEMETRY

#include "mmgr/membuild/membuild.h" // Sb: the field set's numeric renderings
#include "mmgr/protomem/protomem.h" // mem.cpy: the spans a line is assembled from
#include "mmgr/protostr/protostr.h" // str.len: the bounded measure of an appended span

#if PROTOCORE_HAS_NET_STACK
#include "network_drivers/transport/udp/client/client.h" // UdpClient.sendto: one line, one datagram
#include "shared/ip/ip.h"                                // Ip.parse: the collector address, once
#endif

/**
 * @brief The caster's compile-time storage: the collector, and the line being built.
 *
 * All of it BSS, and the line itself lives in the caller's buffer, so a point costs no heap.
 *
 * @var UdpTelemetryStorage::collector   the collector address, parsed by a begin
 * @var UdpTelemetryStorage::port        its UDP port
 * @var UdpTelemetryStorage::ready       the address parsed; every send is gated on it
 * @var UdpTelemetryStorage::buf         the caller's buffer the current line is built in
 * @var UdpTelemetryStorage::cap         how much room it has, the NUL included
 * @var UdpTelemetryStorage::pos         octets built so far, the NUL excluded
 * @var UdpTelemetryStorage::overflow    an append did not fit; every later append is a no-op
 * @var UdpTelemetryStorage::have_field  the field set holds at least one entry
 */
struct UdpTelemetryStorage
{
#if PROTOCORE_HAS_NET_STACK
    protocore_ip collector;
    uint16_t port;
    proto_bool ready;
#endif
    char *buf;
    size_t cap;
    size_t pos;
    proto_bool overflow;
    proto_bool have_field;
};

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define UDP_TELEMETRY_OFF_CTX 0u
static_assert(UDP_TELEMETRY_OFF_CTX + sizeof(struct UdpTelemetryStorage) <= PROTOCORE_UDP_TELEMETRY_BORROW,
              "PROTOCORE_UDP_TELEMETRY_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define UDP_TELEMETRY_CTX(w) ((struct UdpTelemetryStorage *)(void *)((w) + UDP_TELEMETRY_OFF_CTX))

// Append s while the line still leaves room for a trailing NUL, latching overflow the first time it
// does not, so every later append is a no-op.
static void line_append(uint8_t *restrict work, const char *s)
{
    struct UdpTelemetryStorage *store = UDP_TELEMETRY_CTX(work);
    if (store->overflow)
    {
        return;
    }
    size_t n = str.len(s, store->cap + 1);
    if (store->pos + n >= store->cap)
    {
        store->overflow = PROTO_TRUE;
        return;
    }
    mem.cpy(store->buf + store->pos, s, n);
    store->pos += n;
    store->buf[store->pos] = '\0';
}

// Append s with the tag set escaping ("Special characters"): comma, equals and space each take a
// leading backslash. A NULL s appends nothing.
static void line_append_escaped(uint8_t *restrict work, const char *s)
{
    if (!s)
    {
        return;
    }
    for (const char *p = s; *p; p++)
    {
        if (*p == ',' || *p == '=' || *p == ' ')
        {
            char esc[3] = {'\\', *p, '\0'};
            line_append(work, esc);
        }
        else
        {
            char one[2] = {*p, '\0'};
            line_append(work, one);
        }
    }
}

// The separator before a field set entry: a space before the first, a comma before the rest.
static void line_sep(uint8_t *restrict work)
{
    line_append(work, UDP_TELEMETRY_CTX(work)->have_field ? "," : " ");
    UDP_TELEMETRY_CTX(work)->have_field = PROTO_TRUE;
}

// Publish the line's state on the handle: its octet count, its overflow latch, and whether it is a
// complete point - nothing overflowed and the field set holds at least one entry.
static void line_result(uint8_t *restrict work)
{
    UdpTelemetryV.n = UDP_TELEMETRY_CTX(work)->pos;
    UdpTelemetryV.overflow = UDP_TELEMETRY_CTX(work)->overflow;
    UdpTelemetryV.ok = !UDP_TELEMETRY_CTX(work)->overflow && UDP_TELEMETRY_CTX(work)->have_field;
}

// Terminate the number built in b, and empty num when that build overflowed.
static void num_finish(protocore_sb *b, char *num)
{
    if (Sb.finish(b) == 0)
    {
        num[0] = '\0';
    }
}

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_UDP_TELEMETRY_BORROW persistent bytes
} UdpTelemetryOwnCtx;
static UdpTelemetryOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_udp_telemetry_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_UDP_TELEMETRY_BORROW).buf;
    }
    return s_own.span;
}

// Parse the collector address and store it with its port. Without a network stack nothing parses and
// every send refuses.
void protocore_udp_telemetry_begin(uint8_t *restrict work)
{
#if PROTOCORE_HAS_NET_STACK
    Ip.args.text = UdpTelemetryV.collector.addr;
    Ip.args.out = &UDP_TELEMETRY_CTX(work)->collector;
    Ip.parse(ip_work);
    UDP_TELEMETRY_CTX(work)->ready = Ip.ok;
    UDP_TELEMETRY_CTX(work)->port = UdpTelemetryV.collector.port;
    UdpTelemetryV.ok = UDP_TELEMETRY_CTX(work)->ready;
#else
    UdpTelemetryV.ok = PROTO_FALSE;
#endif
}

// Bind the caller's buffer and open the line with the measurement (line protocol element 1).
void protocore_udp_telemetry_measurement(uint8_t *restrict work)
{
    struct UdpTelemetryStorage *store = UDP_TELEMETRY_CTX(work);
    store->buf = UdpTelemetryV.line.buf;
    store->cap = UdpTelemetryV.line.cap;
    store->pos = 0;
    store->have_field = PROTO_FALSE;
    store->overflow = (store->buf == NULL);
    if (!store->overflow && store->cap)
    {
        store->buf[0] = '\0';
    }
    line_append(work, UdpTelemetryV.line.measurement ? UdpTelemetryV.line.measurement : "");
    line_result(work);
}

// Append `,tag_key=tag_value` (line protocol element 2).
void protocore_udp_telemetry_tag(uint8_t *restrict work)
{
    // The tag set is comma separated and sits between the measurement and the space that opens the
    // field set, so an entry appended after a field would read as a field. The line latches
    // overflow instead.
    if (UDP_TELEMETRY_CTX(work)->have_field)
    {
        UDP_TELEMETRY_CTX(work)->overflow = PROTO_TRUE;
    }
    else
    {
        line_append(work, ",");
        line_append_escaped(work, UdpTelemetryV.tags.key);
        line_append(work, "=");
        line_append_escaped(work, UdpTelemetryV.tags.value);
    }
    line_result(work);
}

// Append `field_key=<i64>i`, the signed integer field value (line protocol element 3).
void protocore_udp_telemetry_field_int(uint8_t *restrict work)
{
    char num[24];
    protocore_sb b = {num, sizeof(num), 0, PROTO_TRUE};
    Sb.i64(&b, UdpTelemetryV.fields.i64);
    Sb.put(&b, "i");
    num_finish(&b, num);
    line_sep(work);
    line_append(work, UdpTelemetryV.fields.key ? UdpTelemetryV.fields.key : "");
    line_append(work, "=");
    line_append(work, num);
    line_result(work);
}

// Append `field_key=<u64>u`, the unsigned integer field value.
void protocore_udp_telemetry_field_uint(uint8_t *restrict work)
{
    char num[24];
    protocore_sb b = {num, sizeof(num), 0, PROTO_TRUE};
    Sb.u64(&b, UdpTelemetryV.fields.u64);
    Sb.put(&b, "u");
    num_finish(&b, num);
    line_sep(work);
    line_append(work, UdpTelemetryV.fields.key ? UdpTelemetryV.fields.key : "");
    line_append(work, "=");
    line_append(work, num);
    line_result(work);
}

// Append `field_key=<f32>` to decimals places, the unsuffixed float field value.
void protocore_udp_telemetry_field_float(uint8_t *restrict work)
{
    char num[32];
    protocore_sb b = {num, sizeof(num), 0, PROTO_TRUE};
    Sb.fixed(&b, (double)UdpTelemetryV.fields.f32, (unsigned)UdpTelemetryV.fields.decimals);
    num_finish(&b, num);
    line_sep(work);
    line_append(work, UdpTelemetryV.fields.key ? UdpTelemetryV.fields.key : "");
    line_append(work, "=");
    line_append(work, num);
    line_result(work);
}

// Append ` <timestamp>` (line protocol element 4), Unix nanoseconds.
void protocore_udp_telemetry_timestamp(uint8_t *restrict work)
{
    // The timestamp trails the field set, one space between them, so a line with no field has no
    // point to stamp. The line latches overflow instead.
    if (!UDP_TELEMETRY_CTX(work)->have_field)
    {
        UDP_TELEMETRY_CTX(work)->overflow = PROTO_TRUE;
    }
    else
    {
        char num[24];
        protocore_sb b = {num, sizeof(num), 0, PROTO_TRUE};
        Sb.put(&b, " ");
        Sb.i64(&b, UdpTelemetryV.time.unix_ns);
        num_finish(&b, num);
        line_append(work, num);
    }
    line_result(work);
}

// Send the payload to the collector as one datagram (RFC 768 "User Interface": the data, and the
// destination port and address). Nothing is acknowledged (RFC 768 "Introduction": delivery and
// duplicate protection are not guaranteed), so ok reports only that the stack took the octets.
void protocore_udp_telemetry_send(uint8_t *restrict work)
{
    UdpTelemetryV.ok = PROTO_FALSE;
#if PROTOCORE_HAS_NET_STACK
    if (!UDP_TELEMETRY_CTX(work)->ready || !UdpTelemetryV.payload.data)
    {
        return;
    }
    UdpClientV.dst = &UDP_TELEMETRY_CTX(work)->collector;
    UdpClientV.dst_port = UDP_TELEMETRY_CTX(work)->port;
    UdpClientV.data = (const uint8_t *)UdpTelemetryV.payload.data;
    UdpClientV.len = UdpTelemetryV.payload.len;
    UdpClient.sendto(protocore_udp_client_span());
    UdpTelemetryV.ok = UdpClientV.ok;
#endif
}

// Send the built line as one datagram. A line that overflowed, or whose field set is empty, is not a
// point, and nothing leaves.
void protocore_udp_telemetry_write(uint8_t *restrict work)
{
    if (UDP_TELEMETRY_CTX(work)->overflow || !UDP_TELEMETRY_CTX(work)->have_field)
    {
        UdpTelemetryV.ok = PROTO_FALSE;
        return;
    }
    UdpTelemetryV.payload.data = UDP_TELEMETRY_CTX(work)->buf;
    UdpTelemetryV.payload.len = UDP_TELEMETRY_CTX(work)->pos;
    protocore_udp_telemetry_send(work);
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
UdpTelemetryVars UdpTelemetryV;

#endif // PROTOCORE_ENABLE_UDP_TELEMETRY
