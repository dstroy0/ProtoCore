// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file net_addr.h
 * @brief Layer 4 (Transport) - the stack's address type, as the library's ::protocore_ip.
 *
 * The stack keeps a peer address in its own family-tagged union, laid out however the vendor chose.
 * Everything above the transport carries a ::protocore_ip: a one-byte family tag and sixteen bytes in
 * network order, the same on every target. TCP needs that mapping on accept and on the per-slot
 * address accessor; UDP needs it once per received datagram. One conversion serves both, so it sits
 * beside them rather than inside either.
 *
 * RFC 9293 sec 3.9.2: "Any lower-level protocol will have to provide the source address,
 * destination address, and protocol fields." These two convert between the form the lower-level
 * module states them in and the family-tagged form every layer above reads.
 *
 * The address bytes are read out of the vendor value byte by byte rather than shifted out of it. A
 * shift reads the value's arithmetic, which is the host's byte order; the stack holds the address in
 * network order, so the bytes are the address and the number is not.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_NET_ADDR_H
#define PROTOCORE_NET_ADDR_H

#include "protocore_config.h"

#include "core_setup/board_profiles/protocore_platform.h" // protocore_net_ip: the stack's own address type
#include "shared/ip/ip.h"                                 // protocore_ip: the address everything above carries

PROTOCORE_BEGIN_DECLS

/** @brief Inbound: the stack's address, and the library address it lands in. */
typedef struct
{
    const protocore_net_ip *addr; ///< the stack's address a read starts from
    protocore_ip *out_ip;         ///< where a read lands
} NetAddrInArgs;

/** @brief Outbound: the library address, and the stack address it lands in. */
typedef struct
{
    const protocore_ip *ip;     ///< the library address a write starts from
    protocore_net_ip *out_addr; ///< where a write lands
} NetAddrOutArgs;

/**
 * @brief The stack's address, as the library's address.
 *
 * A caller sets the members a call takes, invokes it through ::NetAddr, and reads the outcome off
 * the same handle.
 *
 * @var NetAddrNs::in       what a read carries across
 * @var NetAddrNs::out      what a write carries across
 * @var NetAddrNs::ok        whether the address could be represented
 * @var NetAddrNs::to_ip     read @c in.addr into @c in.out_ip, network-order bytes preserved
 * @var NetAddrNs::from_ip   write @c out.ip into @c out.out_addr, ready to hand to a send; false when the
 *                           address names a family this stack cannot send to
 *
 * Both families cross both ways. IPv6 is carried where the stack has it (::PROTOCORE_NET_HAS_IPV6); where
 * it does not, a v6 address converts to nothing rather than to a wrong v4.
 *
 * No storage member: the conversions read their operands and hold nothing.
 */
typedef struct
{
    NetAddrInArgs in;   ///< what a read carries across
    NetAddrOutArgs out; ///< what a write carries across

    proto_bool ok;

    void (*const to_ip)(uint8_t *restrict work);
    void (*const from_ip)(uint8_t *restrict work);
} NetAddrNs;

/** @brief The one symbol this module exports. */
extern NetAddrNs NetAddr;

/** @brief Read the stack's address into @p out, network-order bytes preserved. */
void protocore_net_addr_to_ip(const protocore_net_ip *a, protocore_ip *out);

/** @brief Write @p a into the stack's own address type; false when it cannot be represented. */
proto_bool protocore_net_addr_from_ip(const protocore_ip *a, protocore_net_ip *out);

PROTOCORE_END_DECLS

#endif // PROTOCORE_NET_ADDR_H
