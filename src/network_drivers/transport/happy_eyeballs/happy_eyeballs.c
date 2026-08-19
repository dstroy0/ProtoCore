// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file happy_eyeballs.c
 * @brief Dual-stack destination selection + Happy Eyeballs fallback (see happy_eyeballs.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

static uint8_t ip_work[16]; // the borrow an entry takes; Ip never reads it

#if PROTOCORE_ENABLE_HAPPY_EYEBALLS

#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "network_drivers/transport/happy_eyeballs/happy_eyeballs.h"
#include "shared/ip/ip.h"

PROTOCORE_BEGIN_DECLS

/**
 * @brief The one address a preference step reads.
 *
 * The sort asks for the preference of a key it holds and of the element it is comparing against, so
 * the operand differs per call and cannot be read off the namespace's own args. It rides here
 * instead, which is the one parameter every private step takes.
 */
typedef struct
{
    const protocore_ip *ip; ///< the address a preference step scores
} HappyEyeballsCtx;

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define HAPPY_EYEBALLS_OFF_CTX 0u
static_assert(HAPPY_EYEBALLS_OFF_CTX + sizeof(HappyEyeballsCtx) <= PROTOCORE_HAPPY_EYEBALLS_BORROW,
              "PROTOCORE_HAPPY_EYEBALLS_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define HAPPY_EYEBALLS_CTX(w) ((HappyEyeballsCtx *)(void *)((w) + HAPPY_EYEBALLS_OFF_CTX))

// Effective family for interleave: an IPv4-mapped IPv6 address is treated as IPv4.
static proto_bool eff_is_v6(uint8_t *restrict work)
{
    return HAPPY_EYEBALLS_CTX(work)->ip->family == PROTOCORE_IP_V6 &&
           !protocore_ip_is_v4_mapped(HAPPY_EYEBALLS_CTX(work)->ip);
}

static int scope_rank(uint8_t *restrict work)
{
    Ip.args.ip = HAPPY_EYEBALLS_CTX(work)->ip;
    Ip.classify(ip_work);
    switch (Ip.scope)
    {
    case PROTOCORE_IP_SCOPE_GLOBAL:
        return 5;
    case PROTOCORE_IP_SCOPE_PRIVATE:
        return 4;
    case PROTOCORE_IP_SCOPE_LINK_LOCAL:
        return 3;
    case PROTOCORE_IP_SCOPE_LOOPBACK:
        return 2;
    case PROTOCORE_IP_SCOPE_MULTICAST:
        return 1;
    default:
        return 0; // unspecified
    }
}

// The score of the address staged on the context. Scope dominates; within a scope a native IPv6
// outranks IPv4 (RFC 6724 default policy).
static int pref_of(uint8_t *restrict work)
{
    if (!HAPPY_EYEBALLS_CTX(work)->ip || HAPPY_EYEBALLS_CTX(work)->ip->family == PROTOCORE_IP_NONE)
    {
        return -1;
    }
    return scope_rank(work) * 2 + (eff_is_v6(work) ? 1 : 0);
}

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_HAPPY_EYEBALLS_BORROW persistent bytes
} HappyEyeballsOwnCtx;
static HappyEyeballsOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_happy_eyeballs_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_HAPPY_EYEBALLS_BORROW).buf;
    }
    return s_own.span;
}

static void happy_eyeballs_pref(uint8_t *restrict work)
{
    const protocore_ip *ip = HappyEyeballs.pref_args.ip;

    HAPPY_EYEBALLS_CTX(work)->ip = ip;
    HappyEyeballs.n = pref_of(work);
}

static void happy_eyeballs_order(uint8_t *restrict work)
{
    protocore_ip *list = HappyEyeballs.order_args.list;
    size_t n = HappyEyeballs.order_args.n;

    if (!list || n < 2)
    {
        return;
    }

    // Stable insertion sort by preference (descending). The comparison stages its operand and then
    // scores it, so neither score sits in the loop's own condition.
    for (size_t i = 1; i < n; i++)
    {
        protocore_ip key = list[i];
        HAPPY_EYEBALLS_CTX(work)->ip = &key;
        int kp = pref_of(work);
        size_t j = i;
        while (j > 0)
        {
            HAPPY_EYEBALLS_CTX(work)->ip = &list[j - 1];
            if (pref_of(work) >= kp)
            {
                break;
            }
            list[j] = list[j - 1];
            j--;
        }
        list[j] = key;
    }

    if (n > PROTOCORE_HE_MAX)
    {
        return; // too large to interleave in the fixed scratch; sorted order stands.
    }

    // RFC 8305 sec 4: interleave families so successive attempts alternate v6/v4. Preserve the
    // preference order within each family; start with the family of the top-preference address.
    protocore_ip out[PROTOCORE_HE_MAX];
    size_t o = 0;
    size_t iv6 = 0;
    size_t iv4 = 0;
    // Collect indices per family in preference order.
    size_t v6[PROTOCORE_HE_MAX];
    size_t v4[PROTOCORE_HE_MAX];
    size_t nv6 = 0;
    size_t nv4 = 0;
    for (size_t i = 0; i < n; i++)
    {
        HAPPY_EYEBALLS_CTX(work)->ip = &list[i];
        if (eff_is_v6(work))
        {
            v6[nv6++] = i;
        }
        else
        {
            v4[nv4++] = i;
        }
    }
    HAPPY_EYEBALLS_CTX(work)->ip = &list[0];
    proto_bool take_v6 = eff_is_v6(work); // whichever family the best address belongs to goes first
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

static void happy_eyeballs_attempt_due(uint8_t *restrict work)
{
    (void)work;
    uint32_t last_start_ms = HappyEyeballs.attempt_due_args.last_start_ms;
    uint32_t now_ms = HappyEyeballs.attempt_due_args.now_ms;
    uint32_t attempt_delay_ms = HappyEyeballs.attempt_due_args.attempt_delay_ms;

    uint32_t elapsed = now_ms - last_start_ms; // wrap-safe modular subtraction
    HappyEyeballs.ok = elapsed >= attempt_delay_ms;
}

HappyEyeballsNs HappyEyeballs = {
    .pref = happy_eyeballs_pref,
    .order = happy_eyeballs_order,
    .attempt_due = happy_eyeballs_attempt_due,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HAPPY_EYEBALLS
