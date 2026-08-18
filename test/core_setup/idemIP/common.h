// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file common.h
 * @brief The sizes and widths each standard fixes, for every layer here.
 *
 * Constants only. This tree parses and builds headers in a caller's bytes; it owns no storage,
 * moves nothing, and decides nothing about buffering. Anything that needs a buffer already has one
 * above, and a copy here would be a second one for no reason.
 */

#ifndef PROTOCORE_IDEMIP_COMMON_H
#define PROTOCORE_IDEMIP_COMMON_H

#include "idemIP/endian.h" // the wire-integer accessors, and through them the fixed widths

PROTOCORE_BEGIN_DECLS

/**
 * @brief Smallest datagram every IPv4 host must be able to reassemble.
 *
 * RFC 1122 sec 3.3.2: "We designate the largest datagram size that can be reassembled by EMTU_R
 * ("Effective MTU to receive")... EMTU_R MUST be greater than or equal to 576".
 */
#define IDEMIP_IPV4_MIN_MTU 576u

/** @brief Smallest IPv6 link MTU (RFC 8200 sec 5). */
#define IDEMIP_IPV6_MIN_MTU 1280u

/**
 * @brief Largest packet every IPv6 node must be able to reassemble.
 *
 * RFC 8200 sec 5: "A node must be able to accept a fragmented packet that, after reassembly, is as
 * large as 1500 octets."
 */
#define IDEMIP_IPV6_MIN_REASSEMBLY 1500u

/**
 * @brief Send MSS assumed when the peer sent no MSS Option.
 *
 * RFC 9293 sec 3.7.1 MUST-15: "If an MSS Option is not received at connection setup, TCP
 * implementations MUST assume a default send MSS of 536 (576 - 40) for IPv4 or 1220 (1280 - 60)
 * for IPv6".
 */
#define IDEMIP_IPV4_DEFAULT_SEND_MSS 536u
#define IDEMIP_IPV6_DEFAULT_SEND_MSS 1220u

/** @brief Fixed TCP header, no options (RFC 9293 sec 3.1). */
#define IDEMIP_TCP_HDR_LEN 20u

/** @brief Fixed IPv4 header, no options (RFC 791 sec 3.1). */
#define IDEMIP_IPV4_HDR_LEN 20u

/** @brief Fixed IPv6 header, extension headers excluded (RFC 8200 sec 3). */
#define IDEMIP_IPV6_HDR_LEN 40u

/**
 * @brief RFC 8200 sec 8.3: over IPv6 "the MSS must be computed as the maximum packet size minus 60
 * octets", the minimum IPv6 header plus the minimum TCP header.
 */
static_assert(IDEMIP_IPV6_MIN_MTU - IDEMIP_IPV6_HDR_LEN - IDEMIP_TCP_HDR_LEN == IDEMIP_IPV6_DEFAULT_SEND_MSS,
              "RFC 9293 sec 3.7.1 MUST-15's IPv6 default of 1220 is the minimum link MTU less 60");
static_assert(IDEMIP_IPV4_MIN_MTU - IDEMIP_IPV4_HDR_LEN - IDEMIP_TCP_HDR_LEN == IDEMIP_IPV4_DEFAULT_SEND_MSS,
              "RFC 9293 sec 3.7.1 MUST-15's IPv4 default of 536 is EMTU_R less 40");

PROTOCORE_END_DECLS

#endif // PROTOCORE_IDEMIP_COMMON_H
