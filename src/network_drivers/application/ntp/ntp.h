// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ntp.h
 * @brief The NTP packet on the wire (RFC 5905), shared by the client and the server.
 *
 * Layout, epoch offset and reference identifiers only. The client role lives in
 * ntp_service/, the server role in ntp_server/; both read the format from here.
 */

#ifndef PROTOCORE_NTP_H
#define PROTOCORE_NTP_H

#include "protocore_config.h"
#include <stdint.h>

PROTO_BEGIN_DECLS

/** @brief One NTP packet on the wire is exactly 48 octets (no extension/auth fields). */
#define PC_NTP_PACKET_LEN 48u

/** @brief Seconds between the NTP epoch (1900-01-01) and the Unix epoch (1970-01-01). */
#define PC_NTP_UNIX_OFFSET 2208988800u

/** @brief UDP port NTP answers on (RFC 5905 sec 7.2). */
#define PC_NTP_PORT 123u

/** @brief Reference ID "LOCL" - an undisciplined local clock (RFC 5905 sec 7.3). */
#define PC_NTP_REFID_LOCL 0x4C4F434Cu

/** @brief Reference ID "GPS " - a GPS-disciplined reference clock (use with stratum 1). */
#define PC_NTP_REFID_GPS 0x47505320u

/** @brief NTP version this speaks (RFC 5905). */
#define PC_NTP_VERSION 4u

/** @brief Mode 3, client (RFC 5905 sec 7.3). */
#define PC_NTP_MODE_CLIENT 3u

/** @brief Mode 4, server (RFC 5905 sec 7.3). */
#define PC_NTP_MODE_SERVER 4u

/** @brief Leap indicator 3, clock unsynchronized: a reply carrying it is refused. */
#define PC_NTP_LI_UNSYNC 3u

/** @brief Byte offset of the origin timestamp, which a server echoes from the request. */
#define PC_NTP_OFF_ORIGIN_SEC 24u

/** @brief Byte offset of the transmit timestamp, the field a client reads the time from. */
#define PC_NTP_OFF_TX_SEC 40u

/** @brief Byte offset of the transmit timestamp's fraction word. */
#define PC_NTP_OFF_TX_FRAC 44u

/** @brief li (2) | vn (3) | mode (3), packed into the first octet. */
#define PC_NTP_LI_VN_MODE(li, vn, mode) ((uint8_t)(((li) << 6) | ((vn) << 3) | (mode)))

/** @brief The leap indicator carried in the first octet. */
#define PC_NTP_LI_OF(b) ((uint8_t)((b) >> 6))

/** @brief The version carried in the first octet. */
#define PC_NTP_VN_OF(b) ((uint8_t)(((b) >> 3) & 0x07u))

/** @brief The mode carried in the first octet. */
#define PC_NTP_MODE_OF(b) ((uint8_t)((b) & 0x07u))

PROTO_END_DECLS

#endif // PROTOCORE_NTP_H
