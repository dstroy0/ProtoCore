// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ntp_server.h
 * @brief NTP/SNTP time server (RFC 5905 / RFC 4330 server mode) on UDP/123.
 *
 * Answers client NTP requests from the device's own clock. Stateless request/response: a
 * client sends a 48-octet packet, the server fills in the reference/receive/transmit
 * timestamps, echoes the client's transmit stamp as the origin, and sends it back. Zero
 * heap; gated by PROTOCORE_ENABLE_NTP_SERVER (default off).
 *
 * protocore_ntp_server_build_response() is pure: request bytes and an NTP-epoch time in, reply
 * bytes out. protocore_ntp_server_begin() binds UDP/123 via the transport UDP service and drives
 * it from `protocore_time_now()` (seconds) plus a `protocore_millis()`-derived sub-second fraction.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_NTP_SERVER_H
#define PROTOCORE_NTP_SERVER_H

#include "network_drivers/application/ntp/ntp.h" // the complete type a public struct below holds by value
#include "protocore_config.h"                    // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_NTP_SERVER

PROTOCORE_BEGIN_DECLS

// PROTOCORE_NTP_SERVER_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

/** @brief What build_response takes: req, req_len, stratum, refid, ... */
typedef struct
{
    const uint8_t *req; ///< the received request bytes
    size_t req_len;     ///< length of req (must be >= PROTOCORE_NTP_PACKET_LEN)
    uint8_t stratum;    ///< stratum to advertise (1-15)
    uint32_t refid;     ///< reference identifier (e.g. PROTOCORE_NTP_REFID_LOCL)
    uint32_t
        protocore_ntp_secs; ///< current time, seconds since the NTP epoch (Unix seconds + PROTOCORE_NTP_UNIX_OFFSET)
    uint32_t protocore_ntp_frac; ///< sub-second fraction as a 32-bit binary fraction of a second
    uint8_t *out;                ///< output buffer
    size_t out_cap;              ///< capacity of out (must be >= PROTOCORE_NTP_PACKET_LEN)
} NtpServerBuildResponseArgs;

/** @brief What begin takes: stratum, refid. */
typedef struct
{
    uint8_t stratum; ///< the stratum to advertise (1 for a GPS/reference clock, 2-15 for a relay)
    uint32_t refid;  ///< the reference identifier to advertise (PROTOCORE_NTP_REFID_LOCL, PROTOCORE_NTP_REFID_GPS, ...
} NtpServerBeginArgs;

/**
 * @brief NTP/SNTP time server (RFC 5905 / RFC 4330 server mode) on UDP/123.
 *
 * A caller sets the members a call takes, invokes it through ::NtpServer with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   NtpServer.build_response_args.req = ...;
 *   NtpServer.build_response_args.req_len = ...;
 *   NtpServer.build_response_args.stratum = ...;
 *   NtpServer.build_response_args.refid = ...;
 *   NtpServer.build_response_args.protocore_ntp_secs = ...;
 *   NtpServer.build_response_args.protocore_ntp_frac = ...;
 *   NtpServer.build_response_args.out = ...;
 *   NtpServer.build_response_args.out_cap = ...;
 *   NtpServer.build_response(work);
 *   // NtpServer.n is what the call reports
 *
 * @var NtpServerNs::build_response_args  what build_response takes: req, req_len, stratum, refid,
 * @var NtpServerNs::begin_args  what begin takes: stratum, refid
 * @var NtpServerNs::ok  true if the UDP listener bound; false on a host build or if the ...
 * @var NtpServerNs::n  PROTOCORE_NTP_PACKET_LEN on success, or 0 if a length is too small
 * @var NtpServerNs::build_response  build a server (mode 4) reply to a client NTP request. Pure - no ...
 * @var NtpServerNs::begin  start answering NTP requests on UDP/123 from the device's own ...
 *
 * @c work is PROTOCORE_NTP_SERVER_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    NtpServerBuildResponseArgs build_response_args;
    NtpServerBeginArgs begin_args;

    proto_bool ok;
    size_t n;

    void (*const build_response)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
} NtpServerNs;

/** @brief The one symbol this module exports. */
extern NtpServerNs NtpServer;

/**
 * @brief The PROTOCORE_NTP_SERVER_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span, or NULL while the pool was short - which every entry refuses.
 */
uint8_t *protocore_ntp_server_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_NTP_SERVER

#endif // PROTOCORE_NTP_SERVER_H
