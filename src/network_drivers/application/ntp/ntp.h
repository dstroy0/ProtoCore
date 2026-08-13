// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ntp.h
 * @brief The NTP packet on the wire (RFC 5905 sec 7.3, Figure 8), shared by the client and server.
 *
 * Layout, field offsets, and the enumerated values either role tests. The client role lives in
 * ntp_service/, the server role in ntp_server/; both read the format from here and neither states
 * any of it a second time.
 *
 * The header is 48 octets, twelve 32-bit words:
 *
 *      0                   1                   2                   3
 *      0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     |LI | VN  |Mode |    Stratum    |     Poll      |   Precision   |
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     |                          Root Delay                           |
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     |                       Root Dispersion                         |
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     |                         Reference ID                          |
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     |                    Reference Timestamp (64)                   |
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     |                     Origin Timestamp (64)                     |
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     |                     Receive Timestamp (64)                    |
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *     |                     Transmit Timestamp (64)                   |
 *     +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 * A timestamp is 32 bits of seconds since the prime epoch then 32 bits of fraction, so each one
 * takes two offsets here. Every multi-octet field is big-endian.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_NTP_H
#define PROTOCORE_NTP_H

#include "protocore_config.h"
#include <stdint.h>

PROTOCORE_BEGIN_DECLS

/** @brief One NTP packet on the wire is exactly 48 octets (no extension or MAC fields). */
#define PROTOCORE_NTP_PACKET_LEN 48u

/** @brief Seconds between the NTP prime epoch (1900-01-01) and the Unix epoch (1970-01-01). */
#define PROTOCORE_NTP_UNIX_OFFSET 2208988800u

/** @brief UDP port NTP answers on (RFC 5905 sec 7.2). */
#define PROTOCORE_NTP_PORT 123u

// --- field offsets, RFC 5905 Figure 8 -----------------------------------------------------------

/** @brief Leap indicator, version and mode, packed into the first octet. */
#define PROTOCORE_NTP_OFF_LI_VN_MODE 0u

/** @brief Stratum of the sender. */
#define PROTOCORE_NTP_OFF_STRATUM 1u

/** @brief Poll interval, log2 seconds, signed. */
#define PROTOCORE_NTP_OFF_POLL 2u

/** @brief Clock precision, log2 seconds, signed. */
#define PROTOCORE_NTP_OFF_PRECISION 3u

/** @brief Round-trip delay to the reference clock, 16.16 seconds. */
#define PROTOCORE_NTP_OFF_ROOT_DELAY 4u

/** @brief Dispersion to the reference clock, 16.16 seconds. */
#define PROTOCORE_NTP_OFF_ROOT_DISP 8u

/** @brief Reference identifier: a kiss code at stratum 0, a source ID at stratum 1. */
#define PROTOCORE_NTP_OFF_REFID 12u

/** @brief Reference timestamp, when the sender's clock was last set. */
#define PROTOCORE_NTP_OFF_REF_SEC 16u
#define PROTOCORE_NTP_OFF_REF_FRAC 20u

/** @brief Origin timestamp: the client's transmit stamp, echoed by the server. */
#define PROTOCORE_NTP_OFF_ORIGIN_SEC 24u
#define PROTOCORE_NTP_OFF_ORIGIN_FRAC 28u

/** @brief Receive timestamp: when the request reached the server. */
#define PROTOCORE_NTP_OFF_RX_SEC 32u
#define PROTOCORE_NTP_OFF_RX_FRAC 36u

/** @brief Transmit timestamp: when the reply left, and the field a client reads the time from. */
#define PROTOCORE_NTP_OFF_TX_SEC 40u
#define PROTOCORE_NTP_OFF_TX_FRAC 44u

// --- first octet --------------------------------------------------------------------------------

/** @brief li (2) | vn (3) | mode (3), packed into the first octet. */
#define PROTOCORE_NTP_LI_VN_MODE(li, vn, mode) ((uint8_t)(((li) << 6) | ((vn) << 3) | (mode)))

/** @brief The leap indicator carried in the first octet. */
#define PROTOCORE_NTP_LI_OF(b) ((uint8_t)((b) >> 6))

/** @brief The version carried in the first octet. */
#define PROTOCORE_NTP_VN_OF(b) ((uint8_t)(((b) >> 3) & 0x07u))

/** @brief The mode carried in the first octet. */
#define PROTOCORE_NTP_MODE_OF(b) ((uint8_t)((b) & 0x07u))

/** @brief NTP version this speaks (RFC 5905). */
#define PROTOCORE_NTP_VERSION 4u

// Modes, RFC 5905 sec 7.3. Only client and server are exchanged here; the rest are named so a
// packet carrying one is recognised rather than silently treated as a reply.
#define PROTOCORE_NTP_MODE_RESERVED 0u
#define PROTOCORE_NTP_MODE_SYM_ACTIVE 1u
#define PROTOCORE_NTP_MODE_SYM_PASSIVE 2u
#define PROTOCORE_NTP_MODE_CLIENT 3u
#define PROTOCORE_NTP_MODE_SERVER 4u
#define PROTOCORE_NTP_MODE_BROADCAST 5u
#define PROTOCORE_NTP_MODE_CONTROL 6u
#define PROTOCORE_NTP_MODE_PRIVATE 7u

// Leap indicator, RFC 5905 sec 7.3.
#define PROTOCORE_NTP_LI_NONE 0u
#define PROTOCORE_NTP_LI_ADD_SEC 1u
#define PROTOCORE_NTP_LI_DEL_SEC 2u

/** @brief Leap indicator 3, clock unsynchronized: RFC 4330 sec 5 discards a reply carrying it. */
#define PROTOCORE_NTP_LI_UNSYNC 3u

// Stratum, RFC 5905 sec 7.3. 0 is unspecified and carries a kiss code in the reference ID; 1 is a
// primary reference; 2-15 is a secondary server; 16 is unsynchronized and above that is reserved.
#define PROTOCORE_NTP_STRATUM_KOD 0u
#define PROTOCORE_NTP_STRATUM_PRIMARY 1u
#define PROTOCORE_NTP_STRATUM_MAX 15u
#define PROTOCORE_NTP_STRATUM_UNSYNC 16u

// --- reference identifiers ----------------------------------------------------------------------

/** @brief Reference ID "LOCL" - an undisciplined local clock (RFC 5905 sec 7.3). */
#define PROTOCORE_NTP_REFID_LOCL 0x4C4F434Cu

/** @brief Reference ID "GPS " - a GPS-disciplined reference clock (use with stratum 1). */
#define PROTOCORE_NTP_REFID_GPS 0x47505320u

PROTOCORE_END_DECLS

#endif // PROTOCORE_NTP_H
