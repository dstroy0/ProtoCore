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

#include "network_drivers/application/ntp/ntp.h" // the packet this role answers on
#include "protocore_config.h" // the entry point: protocore_types.h for the widths and PROTOCORE_INLINE

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_NTP_SERVER

/**
 * @brief Build a server (mode 4) reply to a client NTP request. Pure - no clock, no I/O.
 *
 * Echoes the request's protocol version, copies the client's transmit timestamp into the
 * response's origin field, and stamps the reference / receive / transmit times with
 * (@p protocore_ntp_secs, @p protocore_ntp_frac). Leap indicator 0, root dispersion ~1 s.
 *
 * @param req       the received request bytes.
 * @param req_len   length of @p req (must be >= PROTOCORE_NTP_PACKET_LEN).
 * @param stratum   stratum to advertise (1-15).
 * @param refid     reference identifier (e.g. PROTOCORE_NTP_REFID_LOCL).
 * @param protocore_ntp_secs  current time, seconds since the NTP epoch (Unix seconds + PROTOCORE_NTP_UNIX_OFFSET).
 * @param protocore_ntp_frac  sub-second fraction as a 32-bit binary fraction of a second.
 * @param out       output buffer.
 * @param out_cap   capacity of @p out (must be >= PROTOCORE_NTP_PACKET_LEN).
 * @return          PROTOCORE_NTP_PACKET_LEN on success, or 0 if a length is too small.
 */
size_t protocore_ntp_server_build_response(const uint8_t *req, size_t req_len, uint8_t stratum, uint32_t refid,
                                           uint32_t protocore_ntp_secs, uint32_t protocore_ntp_frac, uint8_t *out,
                                           size_t out_cap);

/**
 * @brief Start answering NTP requests on UDP/123 from the device's own clock.
 *
 * Uses `protocore_time_now()` for the seconds and `protocore_millis()` for the sub-second fraction.
 * While `protocore_time_now()` returns 0 the server does not reply.
 *
 * @param stratum the stratum to advertise (1 for a GPS/reference clock, 2-15 for a relay).
 * @param refid   the reference identifier to advertise (PROTOCORE_NTP_REFID_LOCL, PROTOCORE_NTP_REFID_GPS, ...).
 * @return true if the UDP listener bound; false on a host build or if the port is taken.
 */
proto_bool protocore_ntp_server_begin(uint8_t stratum, uint32_t refid);

#endif // PROTOCORE_ENABLE_NTP_SERVER

PROTOCORE_END_DECLS

#endif // PROTOCORE_NTP_SERVER_H
