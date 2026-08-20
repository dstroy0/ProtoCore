// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file syslog.h
 * @brief The syslog originator (RFC 5424), carried one message per UDP datagram (RFC 5426).
 *
 * RFC 5424 sec 3 names the ends: an originator "generates syslog content to be carried in a
 * message", a collector "gathers syslog content for further analysis". This module is the
 * originator, and the collector is a UDP endpoint on the well-known port 514 (RFC 5426 sec 3.3).
 *
 * RFC 5424 sec 6 gives the message:
 *
 *     SYSLOG-MSG = HEADER SP STRUCTURED-DATA [SP MSG]
 *     HEADER     = PRI VERSION SP TIMESTAMP SP HOSTNAME SP APP-NAME SP PROCID SP MSGID
 *
 * The line built here is `<PRIVAL>1 - HOSTNAME APP-NAME - - - MSG`. VERSION is "1" (sec 6.2.2),
 * PRIVAL is Facility * 8 + Severity in 0..191 (sec 6.2.1), and TIMESTAMP (sec 6.2.3), PROCID
 * (sec 6.2.6), MSGID (sec 6.2.7) and STRUCTURED-DATA (sec 6.3) are each the NILVALUE "-"
 * (sec 6, `NILVALUE = "-"`). MSG is MSG-ANY, the caller's octets with no leading BOM (sec 6.4).
 *
 * RFC 5426 sec 3.1: one syslog message per datagram, no additional data in the payload. Nothing is
 * acknowledged (RFC 5426 sec 4.1), so a send reports only that the stack took the octets.
 *
 * @ref PROTOCORE_SYSLOG_MSG_MAX bounds the line a log builds; RFC 5426 sec 3.2 puts the receiver floor
 * at 480 octets for IPv4 and 1180 for IPv6. A line that does not fit reports 0 bytes and no
 * datagram leaves.
 *
 * HOSTNAME and APP-NAME are copied into fixed storage at init, so nothing a caller passes has to
 * outlive the call.
 *
 * The module exports one symbol, @ref Syslog. Everything in syslog.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SYSLOG_H
#define PROTOCORE_SYSLOG_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_SYSLOG

PROTOCORE_BEGIN_DECLS

/**
 * @brief RFC 5424 sec 6.2.1 Severity, the low three bits of PRIVAL (lower is more severe).
 *
 * The section states these values "are not normative but often used".
 */
typedef enum PROTO_ENUM_PACKED
{
    SYSLOG_EMERG = 0,   ///< Emergency: system is unusable
    SYSLOG_ALERT = 1,   ///< Alert: action must be taken immediately
    SYSLOG_CRIT = 2,    ///< Critical: critical conditions
    SYSLOG_ERR = 3,     ///< Error: error conditions
    SYSLOG_WARNING = 4, ///< Warning: warning conditions
    SYSLOG_NOTICE = 5,  ///< Notice: normal but significant condition
    SYSLOG_INFO = 6,    ///< Informational: informational messages
    SYSLOG_DEBUG = 7,   ///< Debug: debug-level messages
} SyslogSeverity;

/**
 * @brief RFC 5424 sec 6.2.1 Facility, the value PRIVAL multiplies by 8.
 *
 * The section states these values "are not normative but often used".
 */
typedef enum PROTO_ENUM_PACKED
{
    SYSLOG_FAC_USER = 1,    ///< user-level messages
    SYSLOG_FAC_DAEMON = 3,  ///< system daemons
    SYSLOG_FAC_LOCAL0 = 16, ///< local use 0 (local0)
    SYSLOG_FAC_LOCAL1 = 17, ///< local use 1 (local1)
    SYSLOG_FAC_LOCAL7 = 23, ///< local use 7 (local7)
} SyslogFacility;

/** @brief RFC 5426 sec 3.3: the collector endpoint every datagram is sent to. */
typedef struct
{
    const char *addr; ///< the collector's address as text, v4 or v6, parsed once by an init
    uint16_t port;    ///< its UDP port; 514 is the well-known one
} SyslogCollectorArgs;

/** @brief RFC 5424 sec 6.2: the HEADER fields a line carries, less the per-record Severity. */
typedef struct
{
    const char *hostname;    ///< HOSTNAME (sec 6.2.4); NULL or "" emits the NILVALUE "-"
    const char *app_name;    ///< APP-NAME (sec 6.2.5); NULL or "" emits the NILVALUE "-"
    SyslogFacility facility; ///< the Facility half of PRIVAL (sec 6.2.1)
} SyslogHeaderArgs;

/** @brief One record: the Severity half of PRIVAL (RFC 5424 sec 6.2.1) and its MSG (sec 6.4). */
typedef struct
{
    SyslogSeverity severity; ///< the Severity half of PRIVAL
    const char *msg;         ///< MSG-ANY, free-form octets; NULL emits an empty MSG
} SyslogRecordArgs;

/** @brief Where a formatted SYSLOG-MSG lands. */
typedef struct
{
    char *out;  ///< the buffer a format writes the line into
    size_t cap; ///< how much room it has, the NUL included
} SyslogLineArgs;

/**
 * @brief The syslog originator.
 *
 * A caller sets the members a call takes, invokes it through ::Syslog, and reads the outcome off
 * the same handle.
 *
 * No slot member: one originator sends to one collector, so no call names a row.
 *
 * @var SyslogNs::collector  the collector an init parses and a log sends to (RFC 5426 sec 3.3)
 * @var SyslogNs::header     the HEADER fields a format stamps (RFC 5424 sec 6.2)
 * @var SyslogNs::record     the Severity and MSG one record carries (RFC 5424 sec 6.2.1, sec 6.4)
 * @var SyslogNs::line       the buffer a format writes into
 * @var SyslogNs::ok         a call's true/false outcome
 * @var SyslogNs::n          the SYSLOG-MSG length a format wrote, excluding the NUL, 0 if it did not fit
 * @var SyslogNs::init       parse the collector address and copy HOSTNAME, APP-NAME and Facility into storage
 * @var SyslogNs::format     build one SYSLOG-MSG from @c header and @c record into @c line
 * @var SyslogNs::log        stamp the stored HEADER fields onto @c header, format into the client's
 *                           scratch, and send that one message as one datagram (RFC 5426 sec 3.1)
 */
typedef struct
{
    SyslogCollectorArgs collector; ///< where the datagrams go
    SyslogHeaderArgs header;       ///< what every line's HEADER says
    SyslogRecordArgs record;       ///< what one record says
    SyslogLineArgs line;           ///< where the formatted line lands

    proto_bool ok;
    size_t n;

    void (*const init)(uint8_t *restrict work);
    void (*const format)(uint8_t *restrict work);
    void (*const log)(uint8_t *restrict work);
} SyslogNs;

/** @brief The one symbol this module exports. */
extern SyslogNs Syslog;

/**
 * @brief The PROTOCORE_SYSLOG_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_syslog_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SYSLOG

#endif // PROTOCORE_SYSLOG_H
