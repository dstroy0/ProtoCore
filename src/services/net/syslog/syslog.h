// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file syslog.h
 * @brief Zero-heap RFC 5424 syslog client over UDP.
 *
 * Ships device log lines to a remote syslog server as RFC 5424 UDP datagrams via
 * the transport-layer UDP service (pc_udp_sendto). Split, like the other
 * network services, into a pure host-testable formatter and an ESP32-only send:
 *
 *  - pc_syslog_format() builds one RFC 5424 line into a caller buffer (no sockets,
 *    no heap) - unit-tested on the host (env:native_syslog).
 *  - pc_syslog_log() formats into a static scratch buffer and sends it to the
 *    configured server (a no-op stub off-target).
 *
 * Emitted line: `<PRI>1 - HOSTNAME APP-NAME - - - MSG`, where PRI = facility*8 +
 * severity. TIMESTAMP/PROCID/MSGID/STRUCTURED-DATA are the RFC 5424 NILVALUE
 * ("-"); the server stamps its own receipt time. Strings are copied into fixed
 * BSS buffers at init, so nothing must outlive the call.
 */

#ifndef PROTOCORE_SYSLOG_H
#define PROTOCORE_SYSLOG_H

#include "protocore_config.h"

#if PC_ENABLE_SYSLOG

/** @brief RFC 5424 §6.2.1 severity levels (numerically lower = more severe). */
typedef enum PROTO_ENUM_PACKED
{
    SYSLOG_EMERG = 0,   ///< system is unusable
    SYSLOG_ALERT = 1,   ///< action must be taken immediately
    SYSLOG_CRIT = 2,    ///< critical conditions
    SYSLOG_ERR = 3,     ///< error conditions
    SYSLOG_WARNING = 4, ///< warning conditions
    SYSLOG_NOTICE = 5,  ///< normal but significant
    SYSLOG_INFO = 6,    ///< informational
    SYSLOG_DEBUG = 7,   ///< debug-level messages
} SyslogSeverity;

/** @brief Common RFC 5424 §6.2.1 facilities (the default is LOCAL0). */
typedef enum PROTO_ENUM_PACKED
{
    SYSLOG_FAC_USER = 1,    ///< user-level messages
    SYSLOG_FAC_DAEMON = 3,  ///< system daemons
    SYSLOG_FAC_LOCAL0 = 16, ///< local use 0 (default)
    SYSLOG_FAC_LOCAL1 = 17,
    SYSLOG_FAC_LOCAL7 = 23,
} SyslogFacility;

/**
 * @brief Configure the syslog client (call after WiFi is up).
 *
 * @param server_ip dotted-quad IPv4 of the syslog server (e.g. "192.168.1.10").
 * @param port      server UDP port (514 is the IANA syslog port).
 * @param hostname  this device's HOSTNAME field (copied; pass NULL/"" for "-").
 * @param appname   APP-NAME field (copied; pass NULL/"" for "-").
 * @param facility  syslog facility, e.g. SYSLOG_FAC_LOCAL0.
 */
void pc_syslog_init(const char *server_ip, uint16_t port, const char *hostname, const char *appname,
                    SyslogFacility facility);

/**
 * @brief Format one RFC 5424 line into @p out (host-testable; no sockets/heap).
 *
 * @return number of bytes written (excl. NUL), or 0 if it would not fit @p cap.
 */
size_t pc_syslog_format(char *out, size_t cap, SyslogFacility facility, SyslogSeverity severity, const char *hostname,
                        const char *appname, const char *msg);

/**
 * @brief Format @p msg at @p severity and send it to the configured server.
 *
 * @return true if the datagram was queued; false if not yet configured, the line
 *         overflowed PC_SYSLOG_MSG_MAX, or the send failed (host build).
 */
proto_bool pc_syslog_log(SyslogSeverity severity, const char *msg);

#endif // PC_ENABLE_SYSLOG

#endif // PROTOCORE_SYSLOG_H
