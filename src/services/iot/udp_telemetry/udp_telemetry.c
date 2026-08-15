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

#if PROTOCORE_ENABLE_UDP_TELEMETRY

#include "mmgr/membuild.h" // Sb: the field set's numeric renderings
#include "mmgr/protomem.h" // mem.cpy: the spans a line is assembled from
#include "mmgr/protostr.h" // str.len: the bounded measure of an appended span

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

/**
 * @brief The caster's state and the calls that reach it - what UdpTelemetryNs points at.
 *
 * @var UdpTelemetryInternal::store  the collector and the line being built
 * @var UdpTelemetryInternal::ns     the handle a caller sets a call's members on
 */
struct UdpTelemetryInternal
{
    struct UdpTelemetryStorage *store;
    UdpTelemetryNs *ns;
};

static struct UdpTelemetryStorage s_store;

static struct UdpTelemetryInternal s_udp_telemetry = {.store = &s_store, .ns = &UdpTelemetry};

// Append s while the line still leaves room for a trailing NUL, latching overflow the first time it
// does not, so every later append is a no-op.
static void line_append(struct UdpTelemetryInternal *restrict ctx, const char *s)
{
    struct UdpTelemetryStorage *store = ctx->store;
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
static void line_append_escaped(struct UdpTelemetryInternal *restrict ctx, const char *s)
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
            line_append(ctx, esc);
        }
        else
        {
            char one[2] = {*p, '\0'};
            line_append(ctx, one);
        }
    }
}

// The separator before a field set entry: a space before the first, a comma before the rest.
static void line_sep(struct UdpTelemetryInternal *restrict ctx)
{
    line_append(ctx, ctx->store->have_field ? "," : " ");
    ctx->store->have_field = PROTO_TRUE;
}

// Publish the line's state on the handle: its octet count, its overflow latch, and whether it is a
// complete point - nothing overflowed and the field set holds at least one entry.
static void line_result(struct UdpTelemetryInternal *restrict ctx)
{
    ctx->ns->n = ctx->store->pos;
    ctx->ns->overflow = ctx->store->overflow;
    ctx->ns->ok = !ctx->store->overflow && ctx->store->have_field;
}

// Terminate the number built in b, and empty num when that build overflowed.
static void num_finish(protocore_sb *b, char *num)
{
    if (Sb.finish(b) == 0)
    {
        num[0] = '\0';
    }
}

// Parse the collector address and store it with its port. Without a network stack nothing parses and
// every send refuses.
static void udp_telemetry_begin(struct UdpTelemetryInternal *restrict ctx)
{
#if PROTOCORE_HAS_NET_STACK
    Ip.args.text = ctx->ns->collector.addr;
    Ip.args.out = &ctx->store->collector;
    Ip.parse(Ip.internal);
    ctx->store->ready = Ip.ok;
    ctx->store->port = ctx->ns->collector.port;
    ctx->ns->ok = ctx->store->ready;
#else
    ctx->ns->ok = PROTO_FALSE;
#endif
}

// Bind the caller's buffer and open the line with the measurement (line protocol element 1).
static void udp_telemetry_measurement(struct UdpTelemetryInternal *restrict ctx)
{
    struct UdpTelemetryStorage *store = ctx->store;
    store->buf = ctx->ns->line.buf;
    store->cap = ctx->ns->line.cap;
    store->pos = 0;
    store->have_field = PROTO_FALSE;
    store->overflow = (store->buf == NULL);
    if (!store->overflow && store->cap)
    {
        store->buf[0] = '\0';
    }
    line_append(ctx, ctx->ns->line.measurement ? ctx->ns->line.measurement : "");
    line_result(ctx);
}

// Append `,tag_key=tag_value` (line protocol element 2).
static void udp_telemetry_tag(struct UdpTelemetryInternal *restrict ctx)
{
    // The tag set is comma separated and sits between the measurement and the space that opens the
    // field set, so an entry appended after a field would read as a field. The line latches
    // overflow instead.
    if (ctx->store->have_field)
    {
        ctx->store->overflow = PROTO_TRUE;
    }
    else
    {
        line_append(ctx, ",");
        line_append_escaped(ctx, ctx->ns->tags.key);
        line_append(ctx, "=");
        line_append_escaped(ctx, ctx->ns->tags.value);
    }
    line_result(ctx);
}

// Append `field_key=<i64>i`, the signed integer field value (line protocol element 3).
static void udp_telemetry_field_int(struct UdpTelemetryInternal *restrict ctx)
{
    char num[24];
    protocore_sb b = {num, sizeof(num), 0, PROTO_TRUE};
    Sb.i64(&b, ctx->ns->fields.i64);
    Sb.put(&b, "i");
    num_finish(&b, num);
    line_sep(ctx);
    line_append(ctx, ctx->ns->fields.key ? ctx->ns->fields.key : "");
    line_append(ctx, "=");
    line_append(ctx, num);
    line_result(ctx);
}

// Append `field_key=<u64>u`, the unsigned integer field value.
static void udp_telemetry_field_uint(struct UdpTelemetryInternal *restrict ctx)
{
    char num[24];
    protocore_sb b = {num, sizeof(num), 0, PROTO_TRUE};
    Sb.u64(&b, ctx->ns->fields.u64);
    Sb.put(&b, "u");
    num_finish(&b, num);
    line_sep(ctx);
    line_append(ctx, ctx->ns->fields.key ? ctx->ns->fields.key : "");
    line_append(ctx, "=");
    line_append(ctx, num);
    line_result(ctx);
}

// Append `field_key=<f32>` to decimals places, the unsuffixed float field value.
static void udp_telemetry_field_float(struct UdpTelemetryInternal *restrict ctx)
{
    char num[32];
    protocore_sb b = {num, sizeof(num), 0, PROTO_TRUE};
    Sb.fixed(&b, (double)ctx->ns->fields.f32, (unsigned)ctx->ns->fields.decimals);
    num_finish(&b, num);
    line_sep(ctx);
    line_append(ctx, ctx->ns->fields.key ? ctx->ns->fields.key : "");
    line_append(ctx, "=");
    line_append(ctx, num);
    line_result(ctx);
}

// Append ` <timestamp>` (line protocol element 4), Unix nanoseconds.
static void udp_telemetry_timestamp(struct UdpTelemetryInternal *restrict ctx)
{
    // The timestamp trails the field set, one space between them, so a line with no field has no
    // point to stamp. The line latches overflow instead.
    if (!ctx->store->have_field)
    {
        ctx->store->overflow = PROTO_TRUE;
    }
    else
    {
        char num[24];
        protocore_sb b = {num, sizeof(num), 0, PROTO_TRUE};
        Sb.put(&b, " ");
        Sb.i64(&b, ctx->ns->time.unix_ns);
        num_finish(&b, num);
        line_append(ctx, num);
    }
    line_result(ctx);
}

// Send the payload to the collector as one datagram (RFC 768 "User Interface": the data, and the
// destination port and address). Nothing is acknowledged (RFC 768 "Introduction": delivery and
// duplicate protection are not guaranteed), so ok reports only that the stack took the octets.
static void udp_telemetry_send(struct UdpTelemetryInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
#if PROTOCORE_HAS_NET_STACK
    if (!ctx->store->ready || !ctx->ns->payload.data)
    {
        return;
    }
    UdpClient.dst = &ctx->store->collector;
    UdpClient.dst_port = ctx->store->port;
    UdpClient.data = (const uint8_t *)ctx->ns->payload.data;
    UdpClient.len = ctx->ns->payload.len;
    UdpClient.sendto(UdpClient.internal);
    ctx->ns->ok = UdpClient.ok;
#endif
}

// Send the built line as one datagram. A line that overflowed, or whose field set is empty, is not a
// point, and nothing leaves.
static void udp_telemetry_write(struct UdpTelemetryInternal *restrict ctx)
{
    if (ctx->store->overflow || !ctx->store->have_field)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    ctx->ns->payload.data = ctx->store->buf;
    ctx->ns->payload.len = ctx->store->pos;
    udp_telemetry_send(ctx);
}

// Designated, so a member's position in the struct does not decide what it binds to.
UdpTelemetryNs UdpTelemetry = {.begin = udp_telemetry_begin,
                               .measurement = udp_telemetry_measurement,
                               .tag = udp_telemetry_tag,
                               .field_int = udp_telemetry_field_int,
                               .field_uint = udp_telemetry_field_uint,
                               .field_float = udp_telemetry_field_float,
                               .timestamp = udp_telemetry_timestamp,
                               .send = udp_telemetry_send,
                               .write = udp_telemetry_write,
                               .internal = &s_udp_telemetry};

#endif // PROTOCORE_ENABLE_UDP_TELEMETRY
