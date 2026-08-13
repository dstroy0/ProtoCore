// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#if PROTOCORE_ENABLE_SYSLOG

#include "mmgr/protomem.h"                               // mem.cpy: the spans a line is assembled from
#include "mmgr/protostr.h"                               // str.copy / str.len: the bounded field moves
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

/**
 * @brief The client's state and the calls that reach it - what SyslogNs points at.
 *
 * @var SyslogInternal::store  the collector, the stored HEADER fields, and the line scratch
 * @var SyslogInternal::ns     the handle a caller sets a call's members on
 */
struct SyslogInternal
{
    struct SyslogStorage *store;
    SyslogNs *ns;
};

static struct SyslogStorage s_store;

static struct SyslogInternal s_syslog = {.store = &s_store, .ns = &Syslog};

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

// Parse the collector address and store the HEADER fields every later line carries.
static void syslog_init(struct SyslogInternal *restrict ctx)
{
    Ip.args.text = ctx->ns->collector.addr;
    Ip.args.out = &ctx->store->collector;
    Ip.parse(Ip.internal);
    ctx->store->ready = Ip.ok;
    ctx->store->port = ctx->ns->collector.port;
    copy_field(ctx->store->hostname, sizeof(ctx->store->hostname), ctx->ns->header.hostname);
    copy_field(ctx->store->app_name, sizeof(ctx->store->app_name), ctx->ns->header.app_name);
    ctx->store->facility = ctx->ns->header.facility;
    ctx->ns->ok = ctx->store->ready;
}

// Build one SYSLOG-MSG (RFC 5424 sec 6) into ns->line, and report its length in ns->n.
static void syslog_format(struct SyslogInternal *restrict ctx)
{
    char *out = ctx->ns->line.out;
    const size_t cap = ctx->ns->line.cap;
    ctx->ns->n = 0;
    if (!out || cap == 0)
    {
        return;
    }
    // RFC 5424 sec 6.2.1: PRIVAL = Facility * 8 + Severity, 1*3DIGIT over 0..191. Both enums are
    // unsigned, so only the top is clamped.
    int pri = (int)ctx->ns->header.facility * 8 + (int)ctx->ns->record.severity;
    if (pri > 191)
    {
        pri = 191;
    }
    const char *h = (ctx->ns->header.hostname && ctx->ns->header.hostname[0]) ? ctx->ns->header.hostname : "-";
    const char *a = (ctx->ns->header.app_name && ctx->ns->header.app_name[0]) ? ctx->ns->header.app_name : "-";
    const char *m = ctx->ns->record.msg ? ctx->ns->record.msg : "";

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
    ctx->ns->n = pos;
}

// Format one record with the stored HEADER fields and send it as one datagram (RFC 5426 sec 3.1).
static void syslog_log(struct SyslogInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    ctx->ns->n = 0;
    if (!ctx->store->ready)
    {
        return;
    }
    ctx->ns->header.hostname = ctx->store->hostname;
    ctx->ns->header.app_name = ctx->store->app_name;
    ctx->ns->header.facility = ctx->store->facility;
    ctx->ns->line.out = ctx->store->buf;
    ctx->ns->line.cap = sizeof(ctx->store->buf);
    syslog_format(ctx);
    if (ctx->ns->n == 0)
    {
        return;
    }
    // The datagram payload is the message and nothing else (RFC 5426 sec 3.1). Nothing acknowledges
    // it (RFC 5426 sec 4.1), so ok reports only that the stack took the octets.
    UdpClient.dst = &ctx->store->collector;
    UdpClient.dst_port = ctx->store->port;
    UdpClient.data = (const uint8_t *)ctx->store->buf;
    UdpClient.len = ctx->ns->n;
    UdpClient.sendto(UdpClient.internal);
    ctx->ns->ok = UdpClient.ok;
}

// Designated, so a member's position in the struct does not decide what it binds to.
SyslogNs Syslog = {.init = syslog_init, .format = syslog_format, .log = syslog_log, .internal = &s_syslog};

#endif // PROTOCORE_ENABLE_SYSLOG
