// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file syslog.c
 * @brief The syslog originator: the RFC 5424 sec 6 line framer and the RFC 5426 sec 3.1 send.
 *
 * format() builds `<PRIVAL>1 - HOSTNAME APP-NAME - - - MSG` into the caller's buffer and touches no
 * socket. log() stamps the fields init stored, formats into the client's scratch, and hands those
 * octets to the UDP sending side as one datagram.
 */

#include "services/net/syslog/syslog.h"
#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from

static uint8_t ip_work[16]; // the borrow an entry takes; Ip never reads it

#if PROTOCORE_ENABLE_SYSLOG

#include "mmgr/protomem/protomem.h"                      // mem.cpy: the spans a line is assembled from
#include "mmgr/protostr/protostr.h"                      // str.copy / str.len: the bounded field moves
#include "network_drivers/transport/udp/client/client.h" // UdpClient.sendto: one message, one datagram
#include "shared/ip/ip.h"                                // Ip.parse: the collector address, once

/**
 * @brief The originator's compile-time storage: the collector, the stored HEADER, and the line scratch.
 *
 * All of it BSS, so a log line costs no heap.
 */
struct SyslogStorage
{
    protocore_ip collector;                    ///< the collector address (RFC 5424 sec 3), parsed by an init
    uint16_t port;                             ///< its UDP port (RFC 5426 sec 3.3)
    char hostname[PROTOCORE_SYSLOG_FIELD_MAX]; ///< HOSTNAME (RFC 5424 sec 6.2.4)
    char app_name[PROTOCORE_SYSLOG_FIELD_MAX]; ///< APP-NAME (RFC 5424 sec 6.2.5)
    SyslogFacility facility;                   ///< the Facility half of PRIVAL (RFC 5424 sec 6.2.1)
    proto_bool ready;                          ///< the collector address parsed; every send is gated on it
    char buf[PROTOCORE_SYSLOG_MSG_MAX];        ///< the SYSLOG-MSG a log builds
};

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define SYSLOG_OFF_CTX 0u
static_assert(SYSLOG_OFF_CTX + sizeof(struct SyslogStorage) <= PROTOCORE_SYSLOG_BORROW,
              "PROTOCORE_SYSLOG_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define SYSLOG_CTX(w) ((struct SyslogStorage *)(void *)((w) + SYSLOG_OFF_CTX))

// Copy a NUL-terminated field into a fixed slot, truncating at cap-1, or empty the slot when the
// source is absent. An empty slot emits the NILVALUE "-" (RFC 5424 sec 6).
static void copy_field(char *dst, size_t cap, const char *src)
{
    if (!src || !src[0])
    {
        dst[0] = '\0';
        return;
    }
    (void)str.copy(dst, src, cap);
}

// Append len bytes at *pos while the line still leaves room for a trailing NUL. False the moment it
// would not fit, so an over-long line reports 0 rather than a truncated SYSLOG-MSG.
static inline proto_bool line_append(char *out, size_t cap, size_t *pos, const char *src, size_t len)
{
    if (*pos + len > cap - 1)
    {
        return PROTO_FALSE;
    }
    mem.cpy(out + *pos, src, len);
    *pos += len;
    return PROTO_TRUE;
}

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_SYSLOG_BORROW persistent bytes
} SyslogOwnCtx;
static SyslogOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_syslog_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_SYSLOG_BORROW).buf;
    }
    return s_own.span;
}

// Parse the collector address and store the HEADER fields every later line carries.
static void syslog_init(uint8_t *restrict work)
{
    Ip.args.text = Syslog.collector.addr;
    Ip.args.out = &SYSLOG_CTX(work)->collector;
    Ip.parse(ip_work);
    SYSLOG_CTX(work)->ready = Ip.ok;
    SYSLOG_CTX(work)->port = Syslog.collector.port;
    copy_field(SYSLOG_CTX(work)->hostname, sizeof(SYSLOG_CTX(work)->hostname), Syslog.header.hostname);
    copy_field(SYSLOG_CTX(work)->app_name, sizeof(SYSLOG_CTX(work)->app_name), Syslog.header.app_name);
    SYSLOG_CTX(work)->facility = Syslog.header.facility;
    Syslog.ok = SYSLOG_CTX(work)->ready;
}

// Build one SYSLOG-MSG (RFC 5424 sec 6) into ns->line, and report its length in ns->n.
static void syslog_format(uint8_t *restrict work)
{
    (void)work;
    char *out = Syslog.line.out;
    const size_t cap = Syslog.line.cap;
    Syslog.n = 0;
    if (!out || cap == 0)
    {
        return;
    }
    // RFC 5424 sec 6.2.1: PRIVAL = Facility * 8 + Severity, 1*3DIGIT over 0..191. Both enums are
    // unsigned, so only the top is clamped.
    int pri = (int)Syslog.header.facility * 8 + (int)Syslog.record.severity;
    if (pri > 191)
    {
        pri = 191;
    }
    const char *h = (Syslog.header.hostname && Syslog.header.hostname[0]) ? Syslog.header.hostname : "-";
    const char *a = (Syslog.header.app_name && Syslog.header.app_name[0]) ? Syslog.header.app_name : "-";
    const char *m = Syslog.record.msg ? Syslog.record.msg : "";

    // PRIVAL as 1..3 decimal digits, the leading digit dropped below its decade: RFC 5424 sec 6.2.1
    // allows a '0' after '<' only for the Priority value 0, and forbids leading '0's otherwise.
    char prib[3];
    size_t pl = 0;
    if (pri >= 100)
    {
        prib[pl++] = (char)('0' + pri / 100);
    }
    if (pri >= 10)
    {
        prib[pl++] = (char)('0' + (pri / 10) % 10);
    }
    prib[pl++] = (char)('0' + pri % 10);

    // HEADER SP STRUCTURED-DATA SP MSG, with TIMESTAMP, PROCID, MSGID and STRUCTURED-DATA as the
    // NILVALUE: "<" PRIVAL ">1 - " HOSTNAME " " APP-NAME " - - - " MSG. str.len bounds each field to
    // cap, so a field longer than the buffer fails the append and the whole line reports 0.
    size_t pos = 0;
    if (!line_append(out, cap, &pos, "<", 1) || !line_append(out, cap, &pos, prib, pl) ||
        !line_append(out, cap, &pos, ">1 - ", 5) || !line_append(out, cap, &pos, h, str.len(h, cap)) ||
        !line_append(out, cap, &pos, " ", 1) || !line_append(out, cap, &pos, a, str.len(a, cap)) ||
        !line_append(out, cap, &pos, " - - - ", 7) || !line_append(out, cap, &pos, m, str.len(m, cap)))
    {
        return;
    }
    out[pos] = '\0'; // pos <= cap-1 by construction
    Syslog.n = pos;
}

// Format one record with the stored HEADER fields and send it as one datagram (RFC 5426 sec 3.1).
static void syslog_log(uint8_t *restrict work)
{
    Syslog.ok = PROTO_FALSE;
    Syslog.n = 0;
    if (!SYSLOG_CTX(work)->ready)
    {
        return;
    }
    Syslog.header.hostname = SYSLOG_CTX(work)->hostname;
    Syslog.header.app_name = SYSLOG_CTX(work)->app_name;
    Syslog.header.facility = SYSLOG_CTX(work)->facility;
    Syslog.line.out = SYSLOG_CTX(work)->buf;
    Syslog.line.cap = sizeof(SYSLOG_CTX(work)->buf);
    syslog_format(work);
    if (Syslog.n == 0)
    {
        return;
    }
    // The datagram payload is the message and nothing else (RFC 5426 sec 3.1). Nothing acknowledges
    // it (RFC 5426 sec 4.1), so ok reports only that the stack took the octets.
    UdpClientV.dst = &SYSLOG_CTX(work)->collector;
    UdpClientV.dst_port = SYSLOG_CTX(work)->port;
    UdpClientV.data = (const uint8_t *)SYSLOG_CTX(work)->buf;
    UdpClientV.len = Syslog.n;
    UdpClient.sendto(protocore_udp_client_span());
    Syslog.ok = UdpClientV.ok;
}

// Designated, so a member's position in the struct does not decide what it binds to.
SyslogNs Syslog = {.init = syslog_init, .format = syslog_format, .log = syslog_log};

#endif // PROTOCORE_ENABLE_SYSLOG
