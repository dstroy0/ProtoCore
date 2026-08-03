// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ntp_server.h
 * @brief NTP/SNTP time server (RFC 5905 / RFC 4330 server mode) on UDP/123.
 *
 * The device answers client NTP requests from its own clock, so a LAN with no path to the
 * public NTP pool (offline, air-gapped, or behind a strict firewall) can still keep its
 * devices in sync against this one. It is a stateless request/response: a client sends a
 * 48-octet packet, the server fills in the reference/receive/transmit timestamps (echoing
 * the client's transmit stamp as the origin so the client can compute round-trip delay)
 * and sends it back. Zero heap; gated by PC_ENABLE_NTP_SERVER (default off).
 *
 * The response builder (pc_ntp_server_build_response) is pure - it takes the request bytes and
 * the current NTP-epoch time and writes the reply - so it is fully host-tested with no lwIP.
 * pc_ntp_server_begin() binds UDP/123 via the transport UDP service and drives it from
 * `pc_time_now()` (seconds) plus a `pc_millis()`-derived sub-second fraction.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_NTP_SERVER_H
#define PROTOCORE_NTP_SERVER_H

#include "protocore_config.h" // the entry point: types.h for the widths and PC_INLINE

PROTO_BEGIN_DECLS

#if PC_ENABLE_NTP_SERVER

/** @brief One NTP packet on the wire is exactly 48 octets (no extension/auth fields). */
#define NTP_PACKET_LEN 48u

/** @brief Seconds between the NTP epoch (1900-01-01) and the Unix epoch (1970-01-01). */
#define NTP_UNIX_OFFSET 2208988800u

/** @brief Reference ID "LOCL" - an undisciplined local clock (RFC 5905 sec 7.3). */
#define NTP_REFID_LOCL 0x4C4F434Cu

/** @brief Reference ID "GPS " - a GPS-disciplined reference clock (use with stratum 1). */
#define NTP_REFID_GPS 0x47505320u

/**
 * @brief Build a server (mode 4) reply to a client NTP request. Pure - no clock, no I/O.
 *
 * Echoes the request's protocol version, copies the client's transmit timestamp into the
 * response's origin field, and stamps the reference / receive / transmit times with
 * (@p pc_ntp_secs, @p pc_ntp_frac). Leap indicator 0, root dispersion ~1 s (a coarse clock).
 *
 * @param req       the received request bytes.
 * @param req_len   length of @p req (must be >= NTP_PACKET_LEN).
 * @param stratum   stratum to advertise (1-15).
 * @param refid     reference identifier (e.g. NTP_REFID_LOCL).
 * @param pc_ntp_secs  current time, seconds since the NTP epoch (Unix seconds + NTP_UNIX_OFFSET).
 * @param pc_ntp_frac  sub-second fraction as a 32-bit binary fraction of a second.
 * @param out       output buffer.
 * @param out_cap   capacity of @p out (must be >= NTP_PACKET_LEN).
 * @return          NTP_PACKET_LEN on success, or 0 if a length is too small.
 */
size_t pc_ntp_server_build_response(const uint8_t *req, size_t req_len, uint8_t stratum, uint32_t refid,
                                    uint32_t pc_ntp_secs, uint32_t pc_ntp_frac, uint8_t *out, size_t out_cap);

/**
 * @brief Start answering NTP requests on UDP/123 from the device's own clock.
 *
 * Uses `pc_time_now()` for the seconds and `pc_millis()` for the sub-second fraction.
 * While the device has no time (`pc_time_now()` returns 0) the server does not reply, so
 * a client falls through to its next configured source rather than trusting an unset clock.
 *
 * @param stratum the stratum to advertise (1 for a GPS/reference clock, 2-15 for a relay).
 * @param refid   the reference identifier to advertise (NTP_REFID_LOCL, NTP_REFID_GPS, ...).
 * @return true if the UDP listener bound; false on a host build or if the port is taken.
 */
proto_bool pc_ntp_server_begin(uint8_t stratum, uint32_t refid);

#endif // PC_ENABLE_NTP_SERVER

PROTO_END_DECLS

#endif // PROTOCORE_NTP_SERVER_H
