// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file net_addr.c
 * @brief The stack address to protocore_ip mapping. See net_addr.h.
 */

#include "network_drivers/transport/net_addr/net_addr.h"
#include "mmgr/rawmemcpy.h" // raw.read: the byte reads of the stack's address words

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

/**
 * @brief Carrying an address between the stack's form and this library's.
 *
 * RFC 9293 sec 3.9.2: "Any lower-level protocol will have to provide the source address,
 * destination address, and protocol fields." These two convert between the form the lower-level
 * module states them in and the family-tagged form every layer above reads. No state - the
 * conversion is a pure function of its operands.
 *
 * @var NetAddrInternal::ns  the handle a caller sets a call's operands on
 */
struct NetAddrInternal
{
    NetAddrNs *ns;
};

static struct NetAddrInternal s_net_addr = {.ns = &NetAddr};

static void to_ip(struct NetAddrInternal *restrict ctx)
{
    protocore_net_addr_to_ip(ctx->ns->in.addr, ctx->ns->in.out_ip);
}

static void from_ip(struct NetAddrInternal *restrict ctx)
{
    ctx->ns->ok = protocore_net_addr_from_ip(ctx->ns->out.ip, ctx->ns->out.out_addr);
}

// Designated, so a member's position in the struct does not decide what it binds to.
NetAddrNs NetAddr = {.to_ip = to_ip, .from_ip = from_ip, .internal = &s_net_addr};

PROTOCORE_END_DECLS
