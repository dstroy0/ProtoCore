// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ethernet.h
 * @brief Ethernet II framing, as RFC 894 defines it for IP.
 */

#ifndef PROTOCORE_IDEMIP_ETHERNET_H
#define PROTOCORE_IDEMIP_ETHERNET_H

#include "idemIP/common.h"

PROTOCORE_BEGIN_DECLS

/** @brief An Ethernet address: 48 bits (RFC 894 "48-bit addresses"). */
#define IDEMIP_MAC_LEN 6u

/**
 * @brief Ethernet II header: destination, source, type. The FCS is the link's, not ours.
 *
 * RFC 894: "IP datagrams are transmitted in standard Ethernet frames. The type field of the
 * Ethernet frame must contain the value hexadecimal 0800. The data field contains the IP header
 * followed immediately by the IP data."
 */
#define IDEMIP_ETH_HDR_LEN 14u

/** @brief Type field values. RFC 894 fixes IPv4 at 0x0800; RFC 826 sec "Packet format" ARP at 0x0806. */
typedef enum PROTO_ENUM_PACKED
{
    IDEMIP_ETHERTYPE_IPV4 = 0x0800,
    IDEMIP_ETHERTYPE_ARP = 0x0806,
    IDEMIP_ETHERTYPE_IPV6 = 0x86DD,
} IdemIpEtherType;

/**
 * @brief Smallest data field a frame may carry (RFC 894).
 *
 * "The minimum length of the data field of a packet sent over an Ethernet is 46 octets. If
 * necessary, the data field should be padded (with octets of zero) to meet the Ethernet minimum
 * frame size. This padding is not part of the IP packet and is not included in the total length
 * field of the IP header." A receiver therefore takes the length from the IP header, never from
 * the frame.
 */
#define IDEMIP_ETH_MIN_PAYLOAD 46u

/**
 * @brief Largest data field a frame may carry (RFC 894).
 *
 * "the maximum length of an IP datagram sent over an Ethernet is 1500 octets. Implementations are
 * encouraged to support full-length packets."
 */
#define IDEMIP_ETH_MAX_PAYLOAD 1500u

/** @brief Whole frame on the wire, header included. The FCS is added and checked by the MAC. */
#define IDEMIP_ETH_FRAME_MAX (IDEMIP_ETH_HDR_LEN + IDEMIP_ETH_MAX_PAYLOAD)

// RFC 894 encourages full-length packets, and a receiver that cannot take one has to advertise a
// smaller MSS instead. The link buffer therefore carries a whole frame or the tree does not build.
static_assert(IDEMIP_ETH_MAX_PAYLOAD >= IDEMIP_IPV4_MIN_MTU,
              "an Ethernet frame carries at least the IPv4 minimum reassembly buffer (RFC 1122 sec 3.3.3)");

PROTOCORE_END_DECLS

#endif // PROTOCORE_IDEMIP_ETHERNET_H
