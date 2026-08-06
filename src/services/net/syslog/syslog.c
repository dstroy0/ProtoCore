// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file syslog.c
 * @brief RFC 5424 syslog client: line formatter + UDP send.
 */

#include "services/net/syslog/syslog.h"

#if PC_ENABLE_SYSLOG

#include "network_drivers/transport/udp.h"
#include <stdio.h>

// All syslog client state, owned by one instance (internal linkage): the collector endpoint,
// the host/app identity, the facility, the ready flag, and the format scratch (all BSS, no
// heap), grouped so it is one named owner, unreachable from any other translation unit.
typedef struct
{
    pc_ip server; // the collector, parsed once by pc_syslog_init()
    uint16_t port; // set by pc_syslog_init(); every read is gated on `ready`
    char hostname[PC_SYSLOG_FIELD_MAX];
    char appname[PC_SYSLOG_FIELD_MAX];
    SyslogFacility facility; // likewise set by pc_syslog_init()
    proto_bool ready;
    char buf[PC_SYSLOG_MSG_MAX];
} SyslogCtx;
static SyslogCtx s_syslog;

static void copy_field(char *dst, size_t cap, const char *src)
{
    if (!src || !src[0])
    {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, cap - 1);
    dst[cap - 1] = '\0';
}

void pc_syslog_init(const char *server_ip, uint16_t port, const char *hostname, const char *appname,
                    SyslogFacility facility)
{
    s_syslog.ready = Ip.parse(server_ip, &s_syslog.server);
    s_syslog.port = port;
    copy_field(s_syslog.hostname, sizeof(s_syslog.hostname), hostname);
    copy_field(s_syslog.appname, sizeof(s_syslog.appname), appname);
    s_syslog.facility = facility;
}

// Append `len` bytes at *pos if the line still leaves room for a trailing NUL (content fits in cap-1).
// Returns false the moment it would not fit, so an oversized line reports 0 (never a truncated datagram).
static inline proto_bool sl_append(char *out, size_t cap, size_t *pos, const char *src, size_t len)
{
    if (*pos + len > cap - 1)
    {
        return PROTO_FALSE;
    }
    memcpy(out + *pos, src, len);
    *pos += len;
    return PROTO_TRUE;
}

size_t pc_syslog_format(char *out, size_t cap, SyslogFacility facility, SyslogSeverity severity, const char *hostname,
                        const char *appname, const char *msg)
{
    if (!out || cap == 0)
    {
        return 0;
    }
    // RFC 5424 6.2.1: PRIVAL = facility*8 + severity, range 0..191. The numeric priority is the wire
    // encoding of the level pair, so cast to int here (the boundary). facility/severity are unsigned
    // enums, so PRI is never negative; clamp only the top so an over-range value cast in by the caller
    // can never emit a malformed PRI (e.g. <400>).
    int pri = (int)facility * 8 + (int)severity;
    if (pri > 191)
    {
        pri = 191;
    }
    const char *h = (hostname && hostname[0]) ? hostname : "-";
    const char *a = (appname && appname[0]) ? appname : "-";
    const char *m = msg ? msg : "";
    // <PRI>VERSION SP TIMESTAMP SP HOSTNAME SP APP-NAME SP PROCID SP MSGID SP SD SP MSG, with
    // TIMESTAMP/PROCID/MSGID/STRUCTURED-DATA as NILVALUE ("-"). A branchless memcpy framer (fixed spans +
    // strnlen/memcpy of each field + a small decimal PRI writer) instead of snprintf("%d ... %s %s ... %s"),
    // which avoids the expensive Xtensa vsnprintf path on this per-log-line hot op. Byte-identical output.
    char prib[3]; // PRI is 0..191 -> 1..3 decimal digits
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

    // Bounded lengths: a field can never exceed the output buffer (an over-long field makes the append
    // below fail and the whole line report 0), and strnlen never reads past `cap` even if a field is not
    // NUL-terminated within it.
    size_t pos = 0;
    if (!sl_append(out, cap, &pos, "<", 1) || !sl_append(out, cap, &pos, prib, pl) ||
        !sl_append(out, cap, &pos, ">1 - ", 5) || !sl_append(out, cap, &pos, h, strnlen(h, cap)) ||
        !sl_append(out, cap, &pos, " ", 1) || !sl_append(out, cap, &pos, a, strnlen(a, cap)) ||
        !sl_append(out, cap, &pos, " - - - ", 7) || !sl_append(out, cap, &pos, m, strnlen(m, cap)))
    {
        return 0;
    }
    out[pos] = '\0'; // pos <= cap-1 by construction
    return pos;
}

proto_bool pc_syslog_log(SyslogSeverity severity, const char *msg)
{
    if (!s_syslog.ready)
    {
        return PROTO_FALSE;
    }
    size_t n = pc_syslog_format(s_syslog.buf, sizeof(s_syslog.buf), s_syslog.facility, severity, s_syslog.hostname,
                                s_syslog.appname, msg);
    if (n == 0)
    {
        return PROTO_FALSE;
    }
    return Udp.client->sendto(&s_syslog.server, s_syslog.port, (const uint8_t *)s_syslog.buf, n);
}

#endif // PC_ENABLE_SYSLOG
