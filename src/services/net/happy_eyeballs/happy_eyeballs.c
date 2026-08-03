// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file happy_eyeballs.c
 * @brief Dual-stack destination selection + Happy Eyeballs fallback (see happy_eyeballs.h).
 */

#include "services/net/happy_eyeballs/happy_eyeballs.h"

#if PC_ENABLE_HAPPY_EYEBALLS

// Effective family for interleave: an IPv4-mapped IPv6 address is treated as IPv4.
static proto_bool eff_is_v6(const pc_ip *ip)
{
    return ip->family == PC_IP_V6 && !pc_ip_is_v4_mapped(ip);
}

static int scope_rank(const pc_ip *ip)
{
    switch (pc_ip_classify(ip))
    {
    case PC_IP_SCOPE_GLOBAL:
        return 5;
    case PC_IP_SCOPE_PRIVATE:
        return 4;
    case PC_IP_SCOPE_LINK_LOCAL:
        return 3;
    case PC_IP_SCOPE_LOOPBACK:
        return 2;
    case PC_IP_SCOPE_MULTICAST:
        return 1;
    default:
        return 0; // unspecified
    }
}

int pc_he_pref(const pc_ip *ip)
{
    if (!ip || ip->family == PC_IP_NONE)
    {
        return -1;
    }
    // Scope dominates; within a scope a native IPv6 outranks IPv4 (RFC 6724 default policy).
    return scope_rank(ip) * 2 + (eff_is_v6(ip) ? 1 : 0);
}

void pc_he_order(pc_ip *list, size_t n)
{
    if (!list || n < 2)
    {
        return;
    }

    // Stable insertion sort by preference (descending).
    for (size_t i = 1; i < n; i++)
    {
        pc_ip key = list[i];
        int kp = pc_he_pref(&key);
        size_t j = i;
        while (j > 0 && pc_he_pref(&list[j - 1]) < kp)
        {
            list[j] = list[j - 1];
            j--;
        }
        list[j] = key;
    }

    if (n > PC_HE_MAX)
    {
        return; // too large to interleave in the fixed scratch; sorted order stands.
    }

    // RFC 8305 sec 4: interleave families so successive attempts alternate v6/v4. Preserve the
    // preference order within each family; start with the family of the top-preference address.
    pc_ip out[PC_HE_MAX];
    size_t o = 0;
    size_t iv6 = 0;
    size_t iv4 = 0;
    // Collect indices per family in preference order.
    size_t v6[PC_HE_MAX];
    size_t v4[PC_HE_MAX];
    size_t nv6 = 0;
    size_t nv4 = 0;
    for (size_t i = 0; i < n; i++)
    {
        if (eff_is_v6(&list[i]))
        {
            v6[nv6++] = i;
        }
        else
        {
            v4[nv4++] = i;
        }
    }
    proto_bool take_v6 = eff_is_v6(&list[0]); // whichever family the best address belongs to goes first
    while (iv6 < nv6 || iv4 < nv4)
    {
        if (take_v6 && iv6 < nv6)
        {
            out[o++] = list[v6[iv6++]];
        }
        else if (!take_v6 && iv4 < nv4)
        {
            out[o++] = list[v4[iv4++]];
        }
        else if (iv6 < nv6) // the preferred family is exhausted; drain the other
        {
            out[o++] = list[v6[iv6++]];
        }
        else
        {
            out[o++] = list[v4[iv4++]];
        }
        take_v6 = !take_v6;
    }
    for (size_t i = 0; i < n; i++)
    {
        list[i] = out[i];
    }
}

proto_bool pc_he_attempt_due(uint32_t last_start_ms, uint32_t now_ms, uint32_t attempt_delay_ms)
{
    uint32_t elapsed = now_ms - last_start_ms; // wrap-safe modular subtraction
    return elapsed >= attempt_delay_ms;
}

#endif // PC_ENABLE_HAPPY_EYEBALLS
