// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "services/security/forwarded_trust/forwarded_trust.h"

#if PC_ENABLE_FORWARDED_TRUST

namespace
{

struct pc_forwarded_trust_rule
{
    pc_ip network;      // network address (family V4/V6; PC_NONE marks unused).
    uint8_t prefix_len; // CIDR prefix length: 0..32 for v4, 0..128 for v6.
};

// Trusted-upstream state, owned by one instance (internal linkage): the CIDR rule table and its
// count (empty = trust no forwarded header). One named owner, unreachable from any other unit.
struct pc_forwarded_trust_ctx
{
    pc_forwarded_trust_rule rules[PC_TRUSTED_PROXY_MAX];
    uint8_t count = 0;
};
pc_forwarded_trust_ctx s_trust;

} // namespace

void pc_forwarded_trust_reset(void)
{
    s_trust.count = 0;
}

bool pc_forwarded_trust_add(const pc_ip *network, uint8_t prefix_len)
{
    if (!network)
    {
        return false;
    }
    int bits = -1; // stays negative for a family we do not recognize
    if (network->family == pc_ip_family::PC_IP_V4)
    {
        bits = 32;
    }
    else if (network->family == pc_ip_family::PC_IP_V6)
    {
        bits = 128;
    }
    if (bits < 0 || prefix_len > (uint8_t)bits)
    {
        return false; // reject a malformed family or an over-long prefix
    }
    if (s_trust.count >= PC_TRUSTED_PROXY_MAX)
    {
        return false;
    }
    s_trust.rules[s_trust.count].network = *network;
    s_trust.rules[s_trust.count].prefix_len = prefix_len;
    s_trust.count++;
    return true;
}

bool pc_forwarded_trust_add_cidr(const char *cidr)
{
    if (!cidr)
    {
        return false;
    }

    // Split "address/prefix" at the slash. The address half is copied into a bounded buffer (a CIDR
    // string is never longer than an address plus "/128") for the parser.
    char addr[PC_IP_STR_MAX];
    const char *slash = nullptr;
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
            return false; // address text too long to be valid
        }
        addr[n++] = *p;
    }
    addr[n] = '\0';

    pc_ip net;
    net.family = pc_ip_family::PC_IP_NONE;
    if (!pc_ip_parse(addr, &net))
    {
        return false;
    }

    uint8_t width = (net.family == pc_ip_family::PC_IP_V4) ? 32 : 128;
    uint8_t prefix = width; // bare address -> host route
    if (slash)
    {
        // Parse the decimal prefix by hand (no stdlib in src/); reject empty or non-digit.
        uint32_t v = 0;
        const char *p = slash + 1;
        if (!*p)
        {
            return false;
        }
        for (; *p; p++)
        {
            if (*p < '0' || *p > '9')
            {
                return false;
            }
            v = v * 10 + (uint32_t)(*p - '0');
            if (v > width)
            {
                return false; // out of range for the family
            }
        }
        prefix = (uint8_t)v;
    }

    return pc_forwarded_trust_add(&net, prefix);
}

bool pc_forwarded_trust_contains(const pc_ip *peer)
{
    if (!peer)
    {
        return false;
    }
    for (uint8_t i = 0; i < s_trust.count; i++)
    {
        if (pc_ip_prefix_match(peer, &s_trust.rules[i].network, s_trust.rules[i].prefix_len))
        {
            return true;
        }
    }
    return false;
}

bool pc_forwarded_effective_ip(const pc_ip *peer, const char *fwd_ip_str, pc_ip *out)
{
    if (!out)
    {
        return false;
    }
    if (peer)
    {
        *out = *peer; // default: the real TCP source
    }
    else
    {
        out->family = pc_ip_family::PC_IP_NONE;
    }

    if (!peer || !pc_forwarded_trust_contains(peer))
    {
        return false; // peer is not a trusted upstream -> ignore the spoofable header
    }
    if (!fwd_ip_str || !fwd_ip_str[0])
    {
        return false; // no forwarded client present
    }

    pc_ip fip;
    fip.family = pc_ip_family::PC_IP_NONE;
    if (!pc_ip_parse(fwd_ip_str, &fip) || pc_ip_is_unspecified(&fip))
    {
        return false; // malformed / obfuscated / unspecified -> keep the proxy's address
    }

    *out = fip;
    return true;
}

#endif // PC_ENABLE_FORWARDED_TRUST
