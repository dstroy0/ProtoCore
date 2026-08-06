// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file net_addr.c
 * @brief The stack address to pc_ip mapping. See net_addr.h.
 */

#include "network_drivers/transport/net_addr.h"
#include "mmgr/rawmemcpy.h" // proto_raw_read: the byte reads of the stack's address words

#if PROTOCORE_HOT

PROTO_BEGIN_DECLS

/**
 * @brief Read the stack's address into @p out, network-order bytes preserved.
 *
 * The v6 address is four network-order words, so its sixteen bytes are already the address. The v4
 * accessor yields one word holding the four octets in network order, so the octets are read out of
 * that word's bytes rather than shifted out of its value.
 */
static void to_ip(const pc_net_ip *a, pc_ip *out)
{
    if (out == NULL)
    {
        return;
    }
    pc_ip empty = {PC_IP_NONE, {0}};
    *out = empty;
    if (a == NULL)
    {
        return;
    }
#if PC_NET_HAS_IPV6
    if (pc_net_ip_is_v6(a))
    {
        *out = pc_ip_from_v6_bytes(pc_net_ip6_bytes(a));
        return;
    }
#endif
    uint32_t raw = pc_net_ip4_u32(pc_net_ip_as_v4(a));
    *out = pc_ip_from_v4_octets(0, 0, 0, 0);
    proto_raw_read(out->bytes, (const uint8_t *)&raw, 4);
}

/**
 * @brief Write @p a into the stack's own address type.
 *
 * The v4 setter takes the four octets, so it composes the word the way the stack stores it. The v6
 * address is tagged first and then takes its sixteen bytes, which are the four network-order words.
 * A v6 address on a stack built without v6 leaves @p out unspecified and reports false.
 */
static proto_bool from_ip(const pc_ip *a, pc_net_ip *out)
{
    if (out == NULL)
    {
        return PROTO_FALSE;
    }
    pc_net_ip4_set(out, 0, 0, 0, 0);
    if (a == NULL)
    {
        return PROTO_FALSE;
    }
    if (a->family == PC_IP_V6)
    {
#if PC_NET_HAS_IPV6
        pc_net_ip6_mark(out);
        proto_raw_read(pc_net_ip6_wbytes(out), a->bytes, 16);
        return PROTO_TRUE;
#else
        return PROTO_FALSE;
#endif
    }
    if (a->family != PC_IP_V4)
    {
        return PROTO_FALSE;
    }
    pc_net_ip4_set(out, a->bytes[0], a->bytes[1], a->bytes[2], a->bytes[3]);
    return PROTO_TRUE;
}

const NetAddrNs NetAddr = {to_ip, from_ip};

PROTO_END_DECLS

#else

// A build with no stack has no address to convert. The typedef keeps the translation unit non-empty.
typedef int pc_net_addr_no_stack;

#endif // PROTOCORE_HOT
