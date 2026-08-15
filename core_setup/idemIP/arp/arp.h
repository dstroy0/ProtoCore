// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file arp.h
 * @brief Address resolution, RFC 826: a protocol address to a 48-bit Ethernet address.
 */

#ifndef PROTOCORE_IDEMIP_ARP_H
#define PROTOCORE_IDEMIP_ARP_H

#include "idemIP/endian.h"
#include "idemIP/ethernet/ethernet.h"

PROTOCORE_BEGIN_DECLS

/**
 * @brief Hardware address space (ar$hrd). RFC 826 names ares_hrd$Ethernet; IANA assigns it 1.
 */
#define IDEMIP_ARP_HRD_ETHERNET 1u

/**
 * @brief Protocol address space (ar$pro). RFC 826: "For Ethernet hardware, this is from the set of
 * type fields ether_typ$<protocol>", so IPv4 carries the Ethernet type value.
 */
#define IDEMIP_ARP_PRO_IPV4 IDEMIP_ETHERTYPE_IPV4

/** @brief Opcode (ar$op). RFC 826: ares_op$REQUEST | ares_op$REPLY. */
typedef enum PROTO_ENUM_PACKED
{
    IDEMIP_ARP_OP_REQUEST = 1,
    IDEMIP_ARP_OP_REPLY = 2,
} IdemIpArpOp;

/** @brief Length of an IPv4 protocol address (ar$pln), in bytes. */
#define IDEMIP_ARP_PLN_IPV4 4u

/**
 * @brief Offsets into the ARP payload, in the order RFC 826 lists them.
 *
 * The address fields are variable: ar$sha and ar$tha are ar$hln bytes, ar$spa and ar$tpa are
 * ar$pln. The offsets below are the Ethernet/IPv4 case, which is the only pairing this end
 * resolves; a packet whose ar$hln or ar$pln disagrees is not that pairing and is discarded rather
 * than reinterpreted.
 */
#define IDEMIP_ARP_OFF_HRD 0u  ///< 16-bit hardware address space
#define IDEMIP_ARP_OFF_PRO 2u  ///< 16-bit protocol address space
#define IDEMIP_ARP_OFF_HLN 4u  ///< 8-bit hardware address length
#define IDEMIP_ARP_OFF_PLN 5u  ///< 8-bit protocol address length
#define IDEMIP_ARP_OFF_OP 6u   ///< 16-bit opcode
#define IDEMIP_ARP_OFF_SHA 8u  ///< sender hardware address, ar$hln bytes
#define IDEMIP_ARP_OFF_SPA 14u ///< sender protocol address, ar$pln bytes
#define IDEMIP_ARP_OFF_THA 18u ///< target hardware address, ar$hln bytes
#define IDEMIP_ARP_OFF_TPA 24u ///< target protocol address, ar$pln bytes

/** @brief An Ethernet/IPv4 ARP payload, header excluded. */
#define IDEMIP_ARP_LEN 28u

static_assert(IDEMIP_ARP_OFF_TPA + IDEMIP_ARP_PLN_IPV4 == IDEMIP_ARP_LEN,
              "the RFC 826 field offsets must sum to the Ethernet/IPv4 payload length");

// RFC 894 pads a short frame to 46 octets, and an ARP payload is 28, so an ARP frame is always
// padded on the wire. The pad is not part of the packet, so a parser reads the fields at their
// offsets and never takes a length from the frame.
static_assert(IDEMIP_ARP_LEN < IDEMIP_ETH_MIN_PAYLOAD,
              "an ARP payload is shorter than the RFC 894 minimum data field and is padded to it");

PROTOCORE_END_DECLS

#endif // PROTOCORE_IDEMIP_ARP_H
