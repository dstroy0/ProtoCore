// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "server/security/forwarded_trust/forwarded_trust.h"
#include "mmgr/secure/secure.h" // the persistent end this module's state is taken from
#include "shared/ip/ip.h"

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

static uint8_t ip_work[16]; // the borrow an entry takes; Ip never reads it

#if PROTOCORE_ENABLE_FORWARDED_TRUST

PROTOCORE_BEGIN_DECLS

typedef struct
{
    protocore_ip network; // network address (family V4/V6; PROTOCORE_NONE marks unused).
    uint8_t prefix_len;   // CIDR prefix length: 0..32 for v4, 0..128 for v6.
} protocore_forwarded_trust_rule;

// The CIDR rule table and its count (empty = trust no forwarded header). Only what is not
// derivable: the region lives at a fixed offset in the caller's borrow, so the macro below computes
// it from the pointer rather than the context storing it.
typedef struct
{
    protocore_forwarded_trust_rule rules[PROTOCORE_TRUSTED_PROXY_MAX];
    uint8_t count;
} protocore_forwarded_trust_ctx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define FORWARDED_TRUST_OFF_CTX 0u
static_assert(
    FORWARDED_TRUST_OFF_CTX + sizeof(protocore_forwarded_trust_ctx) <= PROTOCORE_FORWARDED_TRUST_BORROW,
    "PROTOCORE_FORWARDED_TRUST_BORROW is short of the module context - raise it in protocore_config.h, which\n"
    " sums it into its arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(FORWARDED_TRUST_OFF_CTX % _Alignof(protocore_forwarded_trust_ctx) == 0,
              "FORWARDED_TRUST_OFF_CTX is not a multiple of alignof(protocore_forwarded_trust_ctx) - "
              "FORWARDED_TRUST_CTX() would return a misaligned "
              "pointer; pad the region ahead of it");

// The region, at its offset in the caller's borrow.
#define FORWARDED_TRUST_CTX(w) ((protocore_forwarded_trust_ctx *)(void *)((w) + FORWARDED_TRUST_OFF_CTX))

// Read @p text as an address into @p out.
static proto_bool ip_parse(const char *text, protocore_ip *out)
{
    Ip.args.text = text;
    Ip.args.out = out;
    Ip.parse(ip_work);
    return Ip.ok;
}

// Whether @p addr falls inside @p net at @p prefix_len bits.
static proto_bool ip_in_prefix(const protocore_ip *addr, const protocore_ip *net, uint8_t prefix_len)
{
    Ip.args.ip = addr;
    Ip.args.b = net;
    Ip.args.prefix_len = prefix_len;
    Ip.prefix_match(ip_work);
    return Ip.ok;
}

// Whether @p ip names nothing: no family, or the all-zero address.
static proto_bool ip_none(const protocore_ip *ip)
{
    Ip.args.ip = ip;
    Ip.is_unspecified(ip_work);
    return Ip.ok;
}

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_FORWARDED_TRUST_BORROW persistent bytes
} ForwardedTrustOwnCtx;
static ForwardedTrustOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_forwarded_trust_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_FORWARDED_TRUST_BORROW).buf;
    }
    return s_own.span;
}

static void forwarded_trust_reset(uint8_t *restrict work)
{
    (void)work;

    FORWARDED_TRUST_CTX(work)->count = 0;
}

static void forwarded_trust_add(uint8_t *restrict work)
{
    (void)work;
    const protocore_ip *network = ForwardedTrust.add_args.network;
    uint8_t prefix_len = ForwardedTrust.add_args.prefix_len;

    if (!network)
    {
        ForwardedTrust.ok = PROTO_FALSE;
        return;
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
        ForwardedTrust.ok = PROTO_FALSE;
        return; // reject a malformed family or an over-long prefix
    }
    if (FORWARDED_TRUST_CTX(work)->count >= PROTOCORE_TRUSTED_PROXY_MAX)
    {
        ForwardedTrust.ok = PROTO_FALSE;
        return;
    }
    FORWARDED_TRUST_CTX(work)->rules[FORWARDED_TRUST_CTX(work)->count].network = *network;
    FORWARDED_TRUST_CTX(work)->rules[FORWARDED_TRUST_CTX(work)->count].prefix_len = prefix_len;
    FORWARDED_TRUST_CTX(work)->count++;
    ForwardedTrust.ok = PROTO_TRUE;
    return;
}

static void forwarded_trust_add_cidr(uint8_t *restrict work)
{
    (void)work;
    const char *cidr = ForwardedTrust.add_cidr_args.cidr;

    if (!cidr)
    {
        ForwardedTrust.ok = PROTO_FALSE;
        return;
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
            ForwardedTrust.ok = PROTO_FALSE;
            return; // address text too long to be valid
        }
        addr[n++] = *p;
    }
    addr[n] = '\0';

    protocore_ip net;
    net.family = PROTOCORE_IP_NONE;
    if (!ip_parse(addr, &net))
    {
        ForwardedTrust.ok = PROTO_FALSE;
        return;
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
            ForwardedTrust.ok = PROTO_FALSE;
            return;
        }
        for (; *p; p++)
        {
            if (*p < '0' || *p > '9')
            {
                ForwardedTrust.ok = PROTO_FALSE;
                return;
            }
            v = v * 10 + (uint32_t)(*p - '0');
            if (v > width)
            {
                ForwardedTrust.ok = PROTO_FALSE;
                return; // out of range for the family
            }
        }
        prefix = (uint8_t)v;
    }

    ForwardedTrust.add_args.network = &net;
    ForwardedTrust.add_args.prefix_len = prefix;
    forwarded_trust_add(work);
    return;
}

static void forwarded_trust_contains(uint8_t *restrict work)
{
    (void)work;
    const protocore_ip *peer = ForwardedTrust.contains_args.peer;

    if (!peer)
    {
        ForwardedTrust.ok = PROTO_FALSE;
        return;
    }
    for (uint8_t i = 0; i < FORWARDED_TRUST_CTX(work)->count; i++)
    {
        if (ip_in_prefix(peer, &FORWARDED_TRUST_CTX(work)->rules[i].network,
                         FORWARDED_TRUST_CTX(work)->rules[i].prefix_len))
        {
            ForwardedTrust.ok = PROTO_TRUE;
            return;
        }
    }
    ForwardedTrust.ok = PROTO_FALSE;
    return;
}

static void forwarded_trust_protocore_forwarded_effective_ip(uint8_t *restrict work)
{
    (void)work;
    const protocore_ip *peer = ForwardedTrust.protocore_forwarded_effective_ip_args.peer;
    const char *fwd_ip_str = ForwardedTrust.protocore_forwarded_effective_ip_args.fwd_ip_str;
    protocore_ip *out = ForwardedTrust.protocore_forwarded_effective_ip_args.out;

    if (!out)
    {
        ForwardedTrust.ok = PROTO_FALSE;
        return;
    }
    if (peer)
    {
        *out = *peer; // default: the real TCP source
    }
    else
    {
        out->family = PROTOCORE_IP_NONE;
    }

    ForwardedTrust.contains_args.peer = peer;
    forwarded_trust_contains(work);
    if (!peer || !ForwardedTrust.ok)
    {
        ForwardedTrust.ok = PROTO_FALSE;
        return; // peer is not a trusted upstream -> ignore the spoofable header
    }
    if (!fwd_ip_str || !fwd_ip_str[0])
    {
        ForwardedTrust.ok = PROTO_FALSE;
        return; // no forwarded client present
    }

    protocore_ip fip;
    fip.family = PROTOCORE_IP_NONE;
    if (!ip_parse(fwd_ip_str, &fip) || ip_none(&fip))
    {
        ForwardedTrust.ok = PROTO_FALSE;
        return; // malformed / obfuscated / unspecified -> keep the proxy's address
    }

    *out = fip;
    ForwardedTrust.ok = PROTO_TRUE;
    return;
}

ForwardedTrustNs ForwardedTrust = {.reset = forwarded_trust_reset,
                                   .add = forwarded_trust_add,
                                   .add_cidr = forwarded_trust_add_cidr,
                                   .contains = forwarded_trust_contains,
                                   .protocore_forwarded_effective_ip =
                                       forwarded_trust_protocore_forwarded_effective_ip};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_FORWARDED_TRUST
