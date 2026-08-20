// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file net_addr.c
 * @brief The stack address to protocore_ip mapping. See net_addr.h.
 */

#include "network_drivers/transport/net_addr/net_addr.h"
#include "mmgr/rawmemcpy/rawmemcpy.h" // raw.read: the byte reads of the stack's address words

PROTOCORE_BEGIN_DECLS

/**
 * @brief Read the stack's address into @p out, network-order bytes preserved.
 *
 * The v6 address is four network-order words, so its sixteen bytes are already the address. The v4
 * accessor yields one word holding the four octets in network order, so the octets are read out of
 * that word's bytes rather than shifted out of its value.
 */
void protocore_net_addr_to_ip(const protocore_net_ip *a, protocore_ip *out)
{
    if (out == NULL)
    {
        return;
    }
    protocore_ip empty = {PROTOCORE_IP_NONE, {0}};
    *out = empty;
    if (a == NULL)
    {
        return;
    }
#if PROTOCORE_NET_HAS_IPV6
    if (protocore_net_ip_is_v6(a))
    {
        *out = protocore_ip_from_v6_bytes(protocore_net_ip6_bytes(a));
        return;
    }
#endif
    if (!protocore_net_ip_is_v4(a))
    {
        return; // a family this stack did not tag v4; out stays PROTOCORE_IP_NONE
    }
    uint32_t word = protocore_net_ip4_u32(protocore_net_ip_as_v4(a));
    *out = protocore_ip_from_v4_octets(0, 0, 0, 0);
    raw.read(out->bytes, (const uint8_t *)&word, 4);
}

/**
 * @brief Write @p a into the stack's own address type.
 *
 * The v4 setter takes the four octets, so it composes the word the way the stack stores it. The v6
 * address is tagged first and then takes its sixteen bytes, which are the four network-order words.
 * A v6 address on a stack built without v6 leaves @p out unspecified and reports false.
 */
proto_bool protocore_net_addr_from_ip(const protocore_ip *a, protocore_net_ip *out)
{
    if (out == NULL)
    {
        return PROTO_FALSE;
    }
    protocore_net_ip4_set(out, 0, 0, 0, 0);
    if (a == NULL)
    {
        return PROTO_FALSE;
    }
    if (a->family == PROTOCORE_IP_V6)
    {
#if PROTOCORE_NET_HAS_IPV6
        protocore_net_ip6_mark(out);
        raw.read(protocore_net_ip6_wbytes(out), a->bytes, 16);
        return PROTO_TRUE;
#else
        return PROTO_FALSE;
#endif
    }
    if (a->family != PROTOCORE_IP_V4)
    {
        return PROTO_FALSE;
    }
    protocore_net_ip4_set(out, a->bytes[0], a->bytes[1], a->bytes[2], a->bytes[3]);
    return PROTO_TRUE;
}

static void to_ip(uint8_t *restrict work)
{
    (void)work;
    protocore_net_addr_to_ip(NetAddr.in.addr, NetAddr.in.out_ip);
}

static void from_ip(uint8_t *restrict work)
{
    (void)work;
    NetAddr.ok = protocore_net_addr_from_ip(NetAddr.out.ip, NetAddr.out.out_addr);
}

// Designated, so a member's position in the struct does not decide what it binds to.
NetAddrNs NetAddr = {.to_ip = to_ip, .from_ip = from_ip};

PROTOCORE_END_DECLS
