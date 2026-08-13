// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file forward.c
 * @brief Layer 3 (Network) - the interface forwarding plane, implementation.
 *
 * RFC 1812 sec 5.2 "FORWARDING WALK-THROUGH", run over opaque bytes. A frame is matched against
 * the access list (sec 5.3.9), then against the policy routes, then against the controls on
 * forwarding (sec 5.3.11) for every registered interface other than the one it arrived on. A
 * DENY control wins, a matching ALLOW forwards, and no match blocks. Each forward passes a fixed
 * one-second rate window (sec 5.3.6) before layer 1's send takes the bytes. Static tables, no heap.
 */

#include "network_drivers/network/forward/forward.h"
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_FORWARD

#include "server/clock/clock.h" // Clock.millis: the one time source the rate cap reads

// One control on forwarding: a (src, dst) pair, its verdict, and its rate window. RFC 1812 sec 5.3.11.
typedef struct
{
    uint32_t window_start; // ms at the start of the current rate window
    uint16_t rate_cap;     // frames per second (0 = uncapped)
    uint16_t count;        // frames forwarded in the current window
    uint8_t src;
    uint8_t dst;
    protocore_fwd_action action;
    proto_bool used;
} rule;

// One access-list entry: a source interface and a masked byte pattern. RFC 1812 sec 5.3.9.
typedef struct
{
    uint8_t pattern[PROTOCORE_FWD_ACL_PATLEN];
    uint8_t mask[PROTOCORE_FWD_ACL_PATLEN];
    uint16_t offset;
    uint8_t src;    // source interface, or PROTOCORE_FWD_IF_ANY
    uint8_t patlen; // 0 = match any content
    protocore_fwd_action action;
    proto_bool used;
} acl_entry;

// One policy route: the same masked byte pattern, bound to one next hop. RFC 1812 sec 5.2.4.3.
typedef struct
{
    uint32_t window_start; // ms at the start of the current rate window
    uint8_t pattern[PROTOCORE_FWD_ACL_PATLEN];
    uint8_t mask[PROTOCORE_FWD_ACL_PATLEN];
    uint16_t offset;
    uint16_t rate_cap; // frames per second to the next hop (0 = uncapped)
    uint16_t count;    // frames routed in the current window
    uint8_t src;       // source interface, or PROTOCORE_FWD_IF_ANY
    uint8_t patlen;    // 0 = match any content
    uint8_t egress;    // the interface a matched frame leaves on
    proto_bool used;
} route;

/**
 * @brief The plane's compile-time storage: every table it keeps, and the counters over them.
 *
 * All of it BSS. The interfaces are not here: an interface is a physical thing and layer 1 owns
 * that registry, which the fan-out reads.
 */
struct ForwardStorage
{
    rule rules[PROTOCORE_FWD_MAX_RULES];    ///< the controls on forwarding
    acl_entry acl[PROTOCORE_FWD_MAX_ACL];   ///< the access list, first match decides
    route routes[PROTOCORE_FWD_MAX_ROUTES]; ///< the policy routes, first match decides
    protocore_fwd_action acl_default;       ///< what a frame matching no access-list entry takes
#if PROTOCORE_FWD_INSPECT
    protocore_fwd_inspect_fn inspector; ///< the ingress inspection hook; NULL leaves it off
    void *inspect_ctx;                  ///< handed back to the hook unread
#endif
    protocore_forward_stats stats; ///< the counters
};

/**
 * @brief The plane's tables and the calls that reach them - what ForwardNs points at.
 *
 * @var ForwardInternal::store  the controls, the access list, the routes and the counters
 * @var ForwardInternal::ns     the handle a caller sets a call's members on
 */
struct ForwardInternal
{
    struct ForwardStorage *store;
    ForwardNs *ns;
};

// acl_default starts at PROTOCORE_FWD_ALLOW, so an empty access list passes every frame; a zero
// fill would read as PROTOCORE_FWD_DENY.
static struct ForwardStorage s_store = {
    .acl_default = PROTOCORE_FWD_ALLOW,
};

static struct ForwardInternal s_forward = {.store = &s_store, .ns = &Forward};

// The one time source (server/clock/clock.h): read the millisecond count off the clock handle.
static uint32_t fwd_now(void)
{
    Clock.millis(Clock.internal);
    return Clock.ms;
}

// Layer 1 owns the interface registry; these three set its call members and read its outcome.

// Whether layer 1 holds interface @p id.
static proto_bool fwd_iface_present(uint8_t id)
{
    Physical.iface.id = id;
    Physical.iface_present(Physical.internal);
    return Physical.ok;
}

// The interface id held in layer 1 registry row @p i, or PROTOCORE_IF_NONE.
static int16_t fwd_iface_at(uint8_t i)
{
    Physical.iface.i = i;
    Physical.iface_at(Physical.internal);
    return Physical.i16;
}

// Hand the held frame to layer 1 for interface @p id.
static proto_bool fwd_iface_send(struct ForwardInternal *restrict ctx, uint8_t id)
{
    Physical.iface.id = id;
    Physical.iface.data = ctx->ns->frame.data;
    Physical.iface.len = ctx->ns->frame.len;
    Physical.iface_send(Physical.internal);
    return Physical.ok;
}

// What the controls on forwarding say about one (src, dst) pair. RFC 1812 sec 5.3.11.
typedef enum PROTO_ENUM_PACKED
{
    RESOLVE_RESULT_R_NOROUTE,
    RESOLVE_RESULT_R_DENY,
    RESOLVE_RESULT_R_ALLOW,
} resolve_result;

// Resolve the control on forwarding for (src_if -> dst): a DENY wins; otherwise the first matching
// ALLOW governs and its index lands in @p allow_idx; otherwise nothing matched.
static resolve_result fwd_resolve(struct ForwardInternal *restrict ctx, uint8_t dst, int *allow_idx)
{
    const uint8_t src = ctx->ns->src_if;
    int allow = -1;
    proto_bool deny = PROTO_FALSE;
    for (uint8_t i = 0; i < PROTOCORE_FWD_MAX_RULES; i++)
    {
        const rule *r = &ctx->store->rules[i];
        if (!r->used || r->src != src || r->dst != dst)
        {
            continue;
        }
        if (r->action == PROTOCORE_FWD_DENY)
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

// Fixed one-second window rate cap: true once the cap is reached, which drops the frame.
// RFC 1812 sec 5.3.6. Shared by the controls and the policy routes, which keep the same two fields.
static proto_bool rate_gate(uint32_t *window_start, uint16_t *count, uint16_t rate_cap)
{
    if (rate_cap == 0)
    {
        return PROTO_FALSE; // uncapped
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

// Does a stored byte pattern match this frame? @p patlen bytes at @p offset, compared under
// @p mask against an already-masked @p pattern. patlen 0 matches any content; a frame shorter
// than offset + patlen does not match.
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

// Does an access-list entry match this frame? Its source interface, then its byte pattern.
static proto_bool acl_match(const acl_entry *a, uint8_t src, const uint8_t *data, uint16_t len)
{
    if (a->src != PROTOCORE_FWD_IF_ANY && a->src != src)
    {
        return PROTO_FALSE;
    }
    return pat_match(a->offset, a->pattern, a->mask, a->patlen, data, len);
}

// The access list: the first matching entry's action decides, otherwise the fallback.
// RFC 1812 sec 5.3.9.
static proto_bool fwd_acl_permits(struct ForwardInternal *restrict ctx)
{
    for (uint8_t i = 0; i < PROTOCORE_FWD_MAX_ACL; i++)
    {
        const acl_entry *a = &ctx->store->acl[i];
        if (a->used && acl_match(a, ctx->ns->src_if, ctx->ns->frame.data, ctx->ns->frame.len))
        {
            return a->action == PROTOCORE_FWD_ALLOW;
        }
    }
    return ctx->store->acl_default == PROTOCORE_FWD_ALLOW;
}

// Copy @p patlen already-masked bytes of @p pattern and @p mask into @p dst_pattern / @p dst_mask,
// zeroing the rest of both.
static void pat_store(uint8_t *dst_pattern, uint8_t *dst_mask, const uint8_t *pattern, const uint8_t *mask,
                      uint8_t patlen)
{
    mem.set(dst_pattern, 0, PROTOCORE_FWD_ACL_PATLEN);
    mem.set(dst_mask, 0, PROTOCORE_FWD_ACL_PATLEN);
    for (uint8_t k = 0; k < patlen; k++)
    {
        dst_pattern[k] = (uint8_t)(pattern[k] & mask[k]);
        dst_mask[k] = mask[k];
    }
}

// A pattern longer than the row, or a null pattern / mask with a non-zero length, is not storable.
static proto_bool pat_args_valid(const FwdMatchArgs *m)
{
    if (m->patlen > PROTOCORE_FWD_ACL_PATLEN)
    {
        return PROTO_FALSE;
    }
    return m->patlen == 0 || (m->pattern != NULL && m->mask != NULL);
}

static void forward_reset(struct ForwardInternal *restrict ctx)
{
    // The interfaces are layer 1's; emptying this plane leaves them registered.
    mem.set(ctx->store->rules, 0, sizeof(ctx->store->rules));
    mem.set(ctx->store->acl, 0, sizeof(ctx->store->acl));
    mem.set(ctx->store->routes, 0, sizeof(ctx->store->routes));
    ctx->store->acl_default = PROTOCORE_FWD_ALLOW;
#if PROTOCORE_FWD_INSPECT
    ctx->store->inspector = NULL;
    ctx->store->inspect_ctx = NULL;
#endif
    mem.set(&ctx->store->stats, 0, sizeof(ctx->store->stats));
}

static void forward_acl_set_default(struct ForwardInternal *restrict ctx)
{
    ctx->store->acl_default = ctx->ns->acl.fallback;
}

static void forward_acl_add(struct ForwardInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    if (!pat_args_valid(&ctx->ns->match))
    {
        return;
    }
    for (uint8_t i = 0; i < PROTOCORE_FWD_MAX_ACL; i++)
    {
        acl_entry *a = &ctx->store->acl[i];
        if (a->used)
        {
            continue;
        }
        pat_store(a->pattern, a->mask, ctx->ns->match.pattern, ctx->ns->match.mask, ctx->ns->match.patlen);
        a->offset = ctx->ns->match.offset;
        a->src = ctx->ns->src_if;
        a->patlen = ctx->ns->match.patlen;
        a->action = ctx->ns->acl.action;
        a->used = PROTO_TRUE;
        ctx->ns->ok = PROTO_TRUE;
        return;
    }
    // table full
}

static void forward_route_add(struct ForwardInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    if (!pat_args_valid(&ctx->ns->match))
    {
        return;
    }
    for (uint8_t i = 0; i < PROTOCORE_FWD_MAX_ROUTES; i++)
    {
        route *rt = &ctx->store->routes[i];
        if (rt->used)
        {
            continue;
        }
        pat_store(rt->pattern, rt->mask, ctx->ns->match.pattern, ctx->ns->match.mask, ctx->ns->match.patlen);
        rt->window_start = 0;
        rt->offset = ctx->ns->match.offset;
        rt->rate_cap = ctx->ns->route.rate_cap_per_sec;
        rt->count = 0;
        rt->src = ctx->ns->src_if;
        rt->patlen = ctx->ns->match.patlen;
        rt->egress = ctx->ns->route.egress_if;
        rt->used = PROTO_TRUE;
        ctx->ns->ok = PROTO_TRUE;
        return;
    }
    // table full
}

static void forward_add_rule(struct ForwardInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    for (uint8_t i = 0; i < PROTOCORE_FWD_MAX_RULES; i++)
    {
        rule *r = &ctx->store->rules[i];
        if (r->used)
        {
            continue;
        }
        r->window_start = 0;
        r->rate_cap = ctx->ns->rule.rate_cap_per_sec;
        r->count = 0;
        r->src = ctx->ns->src_if;
        r->dst = ctx->ns->rule.dst_if;
        r->action = ctx->ns->rule.action;
        r->used = PROTO_TRUE;
        ctx->ns->ok = PROTO_TRUE;
        return;
    }
    // table full
}

// The first matching policy route decides the frame: it leaves on that one next hop, or it is
// dropped. Returns true when a route matched, and writes the next hops reached to ns->n.
// RFC 1812 sec 5.2.4.3.
static proto_bool fwd_policy_route(struct ForwardInternal *restrict ctx)
{
    for (uint8_t i = 0; i < PROTOCORE_FWD_MAX_ROUTES; i++)
    {
        route *rt = &ctx->store->routes[i];
        if (!rt->used || (rt->src != PROTOCORE_FWD_IF_ANY && rt->src != ctx->ns->src_if))
        {
            continue;
        }
        if (!pat_match(rt->offset, rt->pattern, rt->mask, rt->patlen, ctx->ns->frame.data, ctx->ns->frame.len))
        {
            continue;
        }
        ctx->store->stats.policy_routed++;
        if (rt->egress == ctx->ns->src_if) // a frame never leaves on the interface it arrived on
        {
            return PROTO_TRUE;
        }
        if (!fwd_iface_present(rt->egress)) // an unregistered next hop drops the frame
        {
            ctx->store->stats.send_fail++;
            return PROTO_TRUE;
        }
        if (rate_gate(&rt->window_start, &rt->count, rt->rate_cap))
        {
            ctx->store->stats.rate_dropped++;
            return PROTO_TRUE;
        }
        if (fwd_iface_send(ctx, rt->egress))
        {
            ctx->store->stats.forwarded++;
            ctx->ns->n = 1;
            return PROTO_TRUE;
        }
        ctx->store->stats.send_fail++;
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

static void forward_ingress(struct ForwardInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->store->stats.frames_in++;
    if (!fwd_acl_permits(ctx)) // the access list runs before any control on forwarding
    {
        ctx->store->stats.acl_denied++;
        return;
    }
#if PROTOCORE_FWD_INSPECT
    // The inspection hook reads the frame between the access list and the route lookup.
    if (ctx->store->inspector && ctx->store->inspector(ctx->ns->src_if, ctx->ns->frame.data, ctx->ns->frame.len,
                                                       ctx->store->inspect_ctx) == PROTOCORE_FWD_INSPECT_DROP)
    {
        ctx->store->stats.inspect_dropped++;
        return;
    }
#endif
    // A policy route is taken ahead of the (src, dst) fan-out: the first matching route sends the
    // frame to its one next hop and ends the walk-through.
    if (fwd_policy_route(ctx))
    {
        return;
    }
    uint8_t n = 0;
    for (uint8_t i = 0; i < PROTOCORE_PHY_MAX_IFACES; i++)
    {
        int16_t dst = fwd_iface_at(i);
        if (dst == PROTOCORE_IF_NONE || (uint8_t)dst == ctx->ns->src_if) // never back out the source
        {
            continue;
        }
        int idx = -1;
        resolve_result r = fwd_resolve(ctx, (uint8_t)dst, &idx);
        if (r == RESOLVE_RESULT_R_NOROUTE)
        {
            continue;
        }
        if (r == RESOLVE_RESULT_R_DENY)
        {
            ctx->store->stats.blocked++;
            continue;
        }
        if (rate_exceeded(&ctx->store->rules[idx]))
        {
            ctx->store->stats.rate_dropped++;
            continue;
        }
        if (fwd_iface_send(ctx, (uint8_t)dst))
        {
            ctx->store->stats.forwarded++;
            n++;
        }
        else
        {
            ctx->store->stats.send_fail++;
        }
    }
    ctx->ns->n = n;
}

static void forward_get_stats(struct ForwardInternal *restrict ctx)
{
    ctx->ns->stats = ctx->store->stats;
}

#if PROTOCORE_FWD_INSPECT
static void forward_set_inspector(struct ForwardInternal *restrict ctx)
{
    ctx->store->inspector = ctx->ns->inspect.fn;
    ctx->store->inspect_ctx = ctx->ns->inspect.ctx;
}
#endif

// Designated, so a member's position in the struct does not decide what it binds to - the table is
// split by a feature flag, where a positional list shifts every member below the arm at once.
ForwardNs Forward = {.reset = forward_reset,
                     .add_rule = forward_add_rule,
                     .acl_set_default = forward_acl_set_default,
                     .acl_add = forward_acl_add,
                     .route_add = forward_route_add,
#if PROTOCORE_FWD_INSPECT
                     .set_inspector = forward_set_inspector,
#endif
                     .ingress = forward_ingress,
                     .get_stats = forward_get_stats,
                     .internal = &s_forward};

#endif // PROTOCORE_ENABLE_FORWARD
