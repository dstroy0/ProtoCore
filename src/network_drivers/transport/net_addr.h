// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file net_addr.h
 * @brief Layer 4 (Transport) - the stack's address type, as the library's ::pc_ip.
 *
 * The stack keeps a peer address in its own family-tagged union, laid out however the vendor chose.
 * Everything above the transport carries a ::pc_ip: a one-byte family tag and sixteen bytes in
 * network order, the same on every target. TCP needs that mapping on accept and on the per-slot
 * address accessor; UDP needs it once per received datagram. One conversion serves both, so it sits
 * beside them rather than inside either.
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

#if PROTOCORE_HOT
#include "core_setup/board_profiles/pc_platform.h" // pc_net_ip: the stack's own address type
#include "shared_primitives/ip.h"                  // pc_ip: the address everything above carries

PROTO_BEGIN_DECLS

/**
 * @brief The stack's address, as the library's address.
 *
 * @var NetAddrNs::to_ip    read the stack's @c a into @c out, network-order bytes preserved
 * @var NetAddrNs::from_ip  write @c a back into the stack's own type, ready to hand to a send;
 *                          false when the address names a family this stack cannot send to
 *
 * Both families cross both ways. IPv6 is carried where the stack has it (::PC_NET_HAS_IPV6); where
 * it does not, a v6 address converts to nothing rather than to a wrong v4.
 *
 * No storage member: the conversions read their operands and hold nothing. Present only on a build
 * that has a stack to convert against.
 */
typedef struct
{
    void (*to_ip)(const pc_net_ip *a, pc_ip *out);
    proto_bool (*from_ip)(const pc_ip *a, pc_net_ip *out);
} NetAddrNs;

/** @brief The one symbol this module exports. */
extern const NetAddrNs NetAddr;

PROTO_END_DECLS

#endif // PROTOCORE_HOT

#endif // PROTOCORE_NET_ADDR_H
