// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "server/security/forwarded_trust/forwarded_trust.h"

#if PROTOCORE_ENABLE_FORWARDED_TRUST

typedef struct
{
    protocore_ip network; // network address (family V4/V6; PROTOCORE_NONE marks unused).
    uint8_t prefix_len;   // CIDR prefix length: 0..32 for v4, 0..128 for v6.
} protocore_forwarded_trust_rule;

// Trusted-upstream state, owned by one instance (internal linkage): the CIDR rule table and its
// count (empty = trust no forwarded header). One named owner, unreachable from any other unit.
typedef struct
{
    protocore_forwarded_trust_rule rules[PROTOCORE_TRUSTED_PROXY_MAX];
    uint8_t count;
} protocore_forwarded_trust_ctx;
static protocore_forwarded_trust_ctx s_trust;

// Read @p text as an address into @p out.
static proto_bool ip_parse(const char *text, protocore_ip *out)
{
    Ip.args.text = text;
    Ip.args.out = out;
    Ip.parse(Ip.internal);
    return Ip.ok;
}

// Whether @p addr falls inside @p net at @p prefix_len bits.
static proto_bool ip_in_prefix(const protocore_ip *addr, const protocore_ip *net, uint8_t prefix_len)
{
    Ip.args.ip = addr;
    Ip.args.b = net;
    Ip.args.prefix_len = prefix_len;
    Ip.prefix_match(Ip.internal);
    return Ip.ok;
}

// Whether @p ip names nothing: no family, or the all-zero address.
static proto_bool ip_none(const protocore_ip *ip)
{
    Ip.args.ip = ip;
    Ip.is_unspecified(Ip.internal);
    return Ip.ok;
}

void protocore_forwarded_trust_reset(void)
{
    s_trust.count = 0;
}

proto_bool protocore_forwarded_trust_add(const protocore_ip *network, uint8_t prefix_len)
{
    if (!network)
    {
        return PROTO_FALSE;
    }
    int bits = -1; // stays negative for a family we do not recognize
    if (network->family == PROTOCORE_IP_V4)
    {
        bits = 32;
    }
    else if (network->family == PROTOCORE_IP_V6)
    {
        bits = 128;
    }
    if (bits < 0 || prefix_len > (uint8_t)bits)
    {
        return PROTO_FALSE; // reject a malformed family or an over-long prefix
    }
    if (s_trust.count >= PROTOCORE_TRUSTED_PROXY_MAX)
    {
        return PROTO_FALSE;
    }
    s_trust.rules[s_trust.count].network = *network;
    s_trust.rules[s_trust.count].prefix_len = prefix_len;
    s_trust.count++;
    return PROTO_TRUE;
}

proto_bool protocore_forwarded_trust_add_cidr(const char *cidr)
{
    if (!cidr)
    {
        return PROTO_FALSE;
    }

    // Split "address/prefix" at the slash. The address half is copied into a bounded buffer (a CIDR
    // string is never longer than an address plus "/128") for the parser.
    char addr[PROTOCORE_IP_STR_MAX];
    const char *slash = NULL;
    size_t n = 0;
    for (const char *p = cidr; *p; p++)
    {
        if (*p == '/')
        {
            slash = p;
            break;
        }
        if (n + 1 >= sizeof(addr))
        {
            return PROTO_FALSE; // address text too long to be valid
        }
        addr[n++] = *p;
    }
    addr[n] = '\0';

    protocore_ip net;
    net.family = PROTOCORE_IP_NONE;
    if (!ip_parse(addr, &net))
    {
        return PROTO_FALSE;
    }

    uint8_t width = (net.family == PROTOCORE_IP_V4) ? 32 : 128;
    uint8_t prefix = width; // bare address -> host route
    if (slash)
    {
        // Parse the decimal prefix by hand (no stdlib in src/); reject empty or non-digit.
        uint32_t v = 0;
        const char *p = slash + 1;
        if (!*p)
        {
            return PROTO_FALSE;
        }
        for (; *p; p++)
        {
            if (*p < '0' || *p > '9')
            {
                return PROTO_FALSE;
            }
            v = v * 10 + (uint32_t)(*p - '0');
            if (v > width)
            {
                return PROTO_FALSE; // out of range for the family
            }
        }
        prefix = (uint8_t)v;
    }

    return protocore_forwarded_trust_add(&net, prefix);
}

proto_bool protocore_forwarded_trust_contains(const protocore_ip *peer)
{
    if (!peer)
    {
        return PROTO_FALSE;
    }
    for (uint8_t i = 0; i < s_trust.count; i++)
    {
        if (ip_in_prefix(peer, &s_trust.rules[i].network, s_trust.rules[i].prefix_len))
        {
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

proto_bool protocore_forwarded_effective_ip(const protocore_ip *peer, const char *fwd_ip_str, protocore_ip *out)
{
    if (!out)
    {
        return PROTO_FALSE;
    }
    if (peer)
    {
        *out = *peer; // default: the real TCP source
    }
    else
    {
        out->family = PROTOCORE_IP_NONE;
    }

    if (!peer || !protocore_forwarded_trust_contains(peer))
    {
        return PROTO_FALSE; // peer is not a trusted upstream -> ignore the spoofable header
    }
    if (!fwd_ip_str || !fwd_ip_str[0])
    {
        return PROTO_FALSE; // no forwarded client present
    }

    protocore_ip fip;
    fip.family = PROTOCORE_IP_NONE;
    if (!ip_parse(fwd_ip_str, &fip) || ip_none(&fip))
    {
        return PROTO_FALSE; // malformed / obfuscated / unspecified -> keep the proxy's address
    }

    *out = fip;
    return PROTO_TRUE;
}

#endif // PROTOCORE_ENABLE_FORWARDED_TRUST
