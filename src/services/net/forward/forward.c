// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file forward.c
 * @brief Interface forwarding plane - implementation.
 *
 * Static interface + rule tables. A frame on one interface is resolved against the rules
 * (a DENY wins, otherwise a matching ALLOW forwards, otherwise default-deny) for every
 * other registered interface, rate-capped per rule, then handed to that interface's send
 * callback. Zero heap, fail-closed.
 */

#include "services/net/forward/forward.h"

#if PC_ENABLE_FORWARD

#include <string.h>

#if PROTOCORE_HOT
#include "server/clock/clock.h" // pc_millis()
#endif

typedef struct
{
    pc_if_send_fn send;
    void *ctx;
    uint8_t id;
    pc_if_kind kind;
    proto_bool used;
} iface;

typedef struct
{
    uint32_t window_start; // ms of the current rate window
    uint16_t rate_cap;     // frames per second (0 = unlimited)
    uint16_t count;        // frames forwarded in the current window
    uint8_t src;
    uint8_t dst;
    pc_fwd_action action;
    proto_bool used;
} rule;

typedef struct
{
    uint8_t pattern[PC_FWD_ACL_PATLEN];
    uint8_t mask[PC_FWD_ACL_PATLEN];
    uint16_t offset;
    uint8_t src;    // source interface, or PC_FWD_IF_ANY
    uint8_t patlen; // 0 = match any content
    pc_fwd_action action;
    proto_bool used;
} acl_entry;

// A policy route: match a frame by byte pattern (as the ACL does) and bind it to one egress.
typedef struct
{
    uint32_t window_start; // ms of the current rate window
    uint8_t pattern[PC_FWD_ACL_PATLEN];
    uint8_t mask[PC_FWD_ACL_PATLEN];
    uint16_t offset;
    uint16_t rate_cap; // frames per second to the egress (0 = unlimited)
    uint16_t count;    // frames routed in the current window
    uint8_t src;       // source interface, or PC_FWD_IF_ANY
    uint8_t patlen;    // 0 = match any content
    uint8_t egress;    // egress interface id
    proto_bool used;
} route;

// All forwarding-plane state, owned by one instance (internal linkage): interfaces,
// rules, ACL, and stats grouped so it is one named owner, unreachable cross-TU.
typedef struct
{
    iface if_[PC_FWD_MAX_IFACES];
    rule rules[PC_FWD_MAX_RULES];
    acl_entry acl[PC_FWD_MAX_ACL];
    route routes[PC_FWD_MAX_ROUTES];
    pc_fwd_action acl_default; // frames matching no ACL entry (opt-in ACL)
#if PC_FWD_INSPECT
    pc_fwd_inspect_fn inspector; // opt-in ingress inspection hook
    void *inspect_ctx;
#endif
    pc_forward_stats stats;
#if !PROTOCORE_HOT
    uint32_t now_ms; // host test clock (real builds use pc_millis())
#endif
} ForwardCtx;
// acl_default must start at PC_FWD_ALLOW, which is 1: the ACL is opt-in, so an empty table passes
// everything. pc_forward_reset() also sets it, but nothing in src/ calls that - it is the
// application's to call - so the zero fill would silently make PC_FWD_DENY the default instead.
static ForwardCtx s_fwd = {
    .acl_default = PC_FWD_ALLOW,
};

#if PROTOCORE_HOT
static uint32_t fwd_now()
{
    return pc_millis();
}
#else
static uint32_t fwd_now()
{
    return s_fwd.now_ms;
}
#endif

static const iface *find_if(const ForwardCtx *f, uint8_t id)
{
    for (uint8_t i = 0; i < PC_FWD_MAX_IFACES; i++)
    {
        if (f->if_[i].used && f->if_[i].id == id)
        {
            return &f->if_[i];
        }
    }
    return NULL;
}

// Resolve the action for (src -> dst): a DENY wins; otherwise the first matching ALLOW
// governs (its index is returned via @p allow_idx); otherwise default-deny (no route).
typedef enum PROTO_ENUM_PACKED
{
    RESOLVE_RESULT_R_NOROUTE,
    RESOLVE_RESULT_R_DENY,
    RESOLVE_RESULT_R_ALLOW,
} resolve_result;
static resolve_result resolve(const ForwardCtx *f, uint8_t src, uint8_t dst, int *allow_idx)
{
    int allow = -1;
    proto_bool deny = PROTO_FALSE;
    for (uint8_t i = 0; i < PC_FWD_MAX_RULES; i++)
    {
        if (!f->rules[i].used || f->rules[i].src != src || f->rules[i].dst != dst)
        {
            continue;
        }
        if (f->rules[i].action == PC_FWD_DENY)
        {
            deny = PROTO_TRUE;
        }
        else if (allow < 0)
        {
            allow = (int)i;
        }
    }
    if (deny)
    {
        return RESOLVE_RESULT_R_DENY;
    }
    if (allow >= 0)
    {
        *allow_idx = allow;
        return RESOLVE_RESULT_R_ALLOW;
    }
    return RESOLVE_RESULT_R_NOROUTE;
}

// Fixed 1-second window rate cap; fail-closed (returns true = drop) once the cap is hit.
// Shared by the src->dst rules and the policy routes (same window bookkeeping fields).
static proto_bool rate_gate(uint32_t *window_start, uint16_t *count, uint16_t rate_cap)
{
    if (rate_cap == 0)
    {
        return PROTO_FALSE; // unlimited
    }
    uint32_t now = fwd_now();
    if ((now - *window_start) >= 1000)
    {
        *window_start = now;
        *count = 0;
    }
    if (*count >= rate_cap)
    {
        return PROTO_TRUE;
    }
    (*count)++;
    return PROTO_FALSE;
}

static proto_bool rate_exceeded(rule *r)
{
    return rate_gate(&r->window_start, &r->count, r->rate_cap);
}

// Does a stored byte pattern match this frame? (already-masked @p pattern under @p mask at
// @p offset). @p patlen 0 matches any content; a frame too short for the pattern does not match.
static proto_bool pat_match(uint16_t offset, const uint8_t *pattern, const uint8_t *mask, uint8_t patlen,
                            const uint8_t *data, uint16_t len)
{
    if (patlen == 0)
    {
        return PROTO_TRUE;
    }
    if ((uint32_t)offset + patlen > len)
    {
        return PROTO_FALSE;
    }
    for (uint8_t i = 0; i < patlen; i++)
    {
        if ((data[offset + i] & mask[i]) != pattern[i])
        {
            return PROTO_FALSE;
        }
    }
    return PROTO_TRUE;
}

// Does an ACL entry match this frame? (interface + byte pattern under mask).
static proto_bool acl_match(const acl_entry *a, uint8_t src, const uint8_t *data, uint16_t len)
{
    if (a->src != PC_FWD_IF_ANY && a->src != src)
    {
        return PROTO_FALSE;
    }
    return pat_match(a->offset, a->pattern, a->mask, a->patlen, data, len);
}

// Ingress ACL: the first matching entry's action decides; otherwise the default.
static proto_bool acl_permits(const ForwardCtx *f, uint8_t src, const uint8_t *data, uint16_t len)
{
    for (uint8_t i = 0; i < PC_FWD_MAX_ACL; i++)
    {
        if (f->acl[i].used && acl_match(&f->acl[i], src, data, len))
        {
            return f->acl[i].action == PC_FWD_ALLOW;
        }
    }
    return f->acl_default == PC_FWD_ALLOW;
}

void pc_forward_reset(void)
{
    memset(s_fwd.if_, 0, sizeof(s_fwd.if_));
    memset(s_fwd.rules, 0, sizeof(s_fwd.rules));
    memset(s_fwd.acl, 0, sizeof(s_fwd.acl));
    memset(s_fwd.routes, 0, sizeof(s_fwd.routes));
    s_fwd.acl_default = PC_FWD_ALLOW;
#if PC_FWD_INSPECT
    s_fwd.inspector = NULL;
    s_fwd.inspect_ctx = NULL;
#endif
    memset(&s_fwd.stats, 0, sizeof(s_fwd.stats));
}

void pc_forward_acl_set_default(pc_fwd_action action)
{
    s_fwd.acl_default = action;
}

proto_bool pc_forward_acl_add(uint8_t src_if, uint16_t offset, const uint8_t *pattern, const uint8_t *mask,
                              uint8_t patlen, pc_fwd_action action)
{
    if (patlen > PC_FWD_ACL_PATLEN || (patlen > 0 && (!pattern || !mask)))
    {
        return PROTO_FALSE;
    }
    for (uint8_t i = 0; i < PC_FWD_MAX_ACL; i++)
    {
        if (s_fwd.acl[i].used)
        {
            continue;
        }
        memset(s_fwd.acl[i].pattern, 0, sizeof(s_fwd.acl[i].pattern));
        memset(s_fwd.acl[i].mask, 0, sizeof(s_fwd.acl[i].mask));
        for (uint8_t k = 0; k < patlen; k++)
        {
            s_fwd.acl[i].pattern[k] = (uint8_t)(pattern[k] & mask[k]); // store already masked
            s_fwd.acl[i].mask[k] = mask[k];
        }
        s_fwd.acl[i].offset = offset;
        s_fwd.acl[i].src = src_if;
        s_fwd.acl[i].patlen = patlen;
        s_fwd.acl[i].action = action;
        s_fwd.acl[i].used = PROTO_TRUE;
        return PROTO_TRUE;
    }
    return PROTO_FALSE; // table full
}

proto_bool pc_forward_route_add(uint8_t src_if, uint16_t offset, const uint8_t *pattern, const uint8_t *mask,
                                uint8_t patlen, uint8_t egress_if, uint16_t rate_cap_per_sec)
{
    if (patlen > PC_FWD_ACL_PATLEN || (patlen > 0 && (!pattern || !mask)))
    {
        return PROTO_FALSE;
    }
    for (uint8_t i = 0; i < PC_FWD_MAX_ROUTES; i++)
    {
        if (s_fwd.routes[i].used)
        {
            continue;
        }
        memset(s_fwd.routes[i].pattern, 0, sizeof(s_fwd.routes[i].pattern));
        memset(s_fwd.routes[i].mask, 0, sizeof(s_fwd.routes[i].mask));
        for (uint8_t k = 0; k < patlen; k++)
        {
            s_fwd.routes[i].pattern[k] = (uint8_t)(pattern[k] & mask[k]); // store already masked
            s_fwd.routes[i].mask[k] = mask[k];
        }
        s_fwd.routes[i].window_start = 0;
        s_fwd.routes[i].offset = offset;
        s_fwd.routes[i].rate_cap = rate_cap_per_sec;
        s_fwd.routes[i].count = 0;
        s_fwd.routes[i].src = src_if;
        s_fwd.routes[i].patlen = patlen;
        s_fwd.routes[i].egress = egress_if;
        s_fwd.routes[i].used = PROTO_TRUE;
        return PROTO_TRUE;
    }
    return PROTO_FALSE; // table full
}

proto_bool pc_forward_add_if(uint8_t if_id, pc_if_kind kind, pc_if_send_fn send, void *ctx)
{
    if (!send || find_if(&s_fwd, if_id))
    {
        return PROTO_FALSE;
    }
    for (uint8_t i = 0; i < PC_FWD_MAX_IFACES; i++)
    {
        if (s_fwd.if_[i].used)
        {
            continue;
        }
        s_fwd.if_[i].send = send;
        s_fwd.if_[i].ctx = ctx;
        s_fwd.if_[i].id = if_id;
        s_fwd.if_[i].kind = kind;
        s_fwd.if_[i].used = PROTO_TRUE;
        return PROTO_TRUE;
    }
    return PROTO_FALSE; // table full
}

proto_bool pc_forward_add_rule(uint8_t src_if, uint8_t dst_if, pc_fwd_action action, uint16_t rate_cap_per_sec)
{
    for (uint8_t i = 0; i < PC_FWD_MAX_RULES; i++)
    {
        if (s_fwd.rules[i].used)
        {
            continue;
        }
        s_fwd.rules[i].window_start = 0;
        s_fwd.rules[i].rate_cap = rate_cap_per_sec;
        s_fwd.rules[i].count = 0;
        s_fwd.rules[i].src = src_if;
        s_fwd.rules[i].dst = dst_if;
        s_fwd.rules[i].action = action;
        s_fwd.rules[i].used = PROTO_TRUE;
        return PROTO_TRUE;
    }
    return PROTO_FALSE; // table full
}

// First matching policy route decides the frame (send to its egress only, or drop) - the same
// precedence and guarantees as a rule. *handled=false means no route matched; run normal fan-out.
static uint8_t forward_policy_route(uint8_t src_if, const uint8_t *data, uint16_t len, proto_bool *handled)
{
    *handled = PROTO_TRUE;
    for (uint8_t i = 0; i < PC_FWD_MAX_ROUTES; i++)
    {
        route *rt = &s_fwd.routes[i];
        if (!rt->used || (rt->src != PC_FWD_IF_ANY && rt->src != src_if))
        {
            continue;
        }
        if (!pat_match(rt->offset, rt->pattern, rt->mask, rt->patlen, data, len))
        {
            continue;
        }
        s_fwd.stats.policy_routed++;
        if (rt->egress == src_if) // never reflect to the source interface
        {
            return 0;
        }
        const iface *out = find_if(&s_fwd, rt->egress);
        if (!out) // egress not registered -> drop, fail-closed
        {
            s_fwd.stats.send_fail++;
            return 0;
        }
        if (rate_gate(&rt->window_start, &rt->count, rt->rate_cap))
        {
            s_fwd.stats.rate_dropped++;
            return 0;
        }
        if (out->send(out->id, data, len, out->ctx))
        {
            s_fwd.stats.forwarded++;
            return 1;
        }
        s_fwd.stats.send_fail++;
        return 0;
    }
    *handled = PROTO_FALSE;
    return 0;
}

uint8_t pc_forward_ingress(uint8_t src_if, const uint8_t *data, uint16_t len)
{
    s_fwd.stats.frames_in++;
    if (!acl_permits(&s_fwd, src_if, data, len)) // ingress ACL runs before any forwarding rule
    {
        s_fwd.stats.acl_denied++;
        return 0;
    }
#if PC_FWD_INSPECT
    // Opt-in inspection hook: an app callback observes/filters the frame before routing.
    if (s_fwd.inspector && s_fwd.inspector(src_if, data, len, s_fwd.inspect_ctx) == PC_FWD_INSPECT_DROP)
    {
        s_fwd.stats.inspect_dropped++;
        return 0;
    }
#endif
    // Policy routes take precedence over the src->dst fan-out: the first matching route sends
    // the frame only to its chosen egress and ends the decision (same guarantees as a rule).
    proto_bool routed = PROTO_FALSE;
    uint8_t verdict = forward_policy_route(src_if, data, len, &routed);
    if (routed)
    {
        return verdict;
    }
    uint8_t n = 0;
    for (uint8_t i = 0; i < PC_FWD_MAX_IFACES; i++)
    {
        if (!s_fwd.if_[i].used || s_fwd.if_[i].id == src_if) // never reflect to the source interface
        {
            continue;
        }
        int idx = -1;
        resolve_result r = resolve(&s_fwd, src_if, s_fwd.if_[i].id, &idx);
        if (r == RESOLVE_RESULT_R_NOROUTE)
        {
            continue; // default-deny, silent
        }
        if (r == RESOLVE_RESULT_R_DENY)
        {
            s_fwd.stats.blocked++;
            continue;
        }
        if (rate_exceeded(&s_fwd.rules[idx]))
        {
            s_fwd.stats.rate_dropped++;
            continue;
        }
        if (s_fwd.if_[i].send(s_fwd.if_[i].id, data, len, s_fwd.if_[i].ctx))
        {
            s_fwd.stats.forwarded++;
            n++;
        }
        else
        {
            s_fwd.stats.send_fail++;
        }
    }
    return n;
}

void pc_forward_get_stats(pc_forward_stats *out)
{
    if (out)
    {
        *out = s_fwd.stats;
    }
}

#if PC_FWD_INSPECT
void pc_forward_set_inspector(pc_fwd_inspect_fn fn, void *ctx)
{
    s_fwd.inspector = fn;
    s_fwd.inspect_ctx = ctx;
}
#endif

#if !PROTOCORE_HOT
void pc_forward_test_set_now(uint32_t ms)
{
    s_fwd.now_ms = ms;
}
#endif

#endif // PC_ENABLE_FORWARD
