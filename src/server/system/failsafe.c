// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file failsafe.c
 * @brief Software watchdog / deadlock detection + safe-state (see failsafe.h).
 */

#include "server/system/failsafe.h"

#if PROTOCORE_ENABLE_FAILSAFE


/**
 * @brief The lifelines' compile-time storage: the table, and what a breach fires.
 */
struct FailsafeStorage
{
    protocore_lifeline lines[PROTOCORE_FAILSAFE_MAX_LIFELINES];
    protocore_failsafe_cb cb;
    void *cb_arg;
};

/**
 * @brief The lifelines and the calls that reach them - what FailsafeNs points at.
 *
 * @var FailsafeInternal::store  the lifeline table, and what a breach fires
 * @var FailsafeInternal::ns     the handle a caller sets a call's members on
 */
struct FailsafeInternal
{
    struct FailsafeStorage *store;
    FailsafeNs *ns;
};

static struct FailsafeStorage s_store;

static struct FailsafeInternal s_fs_ctx = {.store = &s_store, .ns = &Failsafe};

// Minimal unsigned -> decimal, no stdlib; returns chars written.
static size_t u32_dec(uint32_t v, char *out)
{
    char tmp[10];
    size_t n = 0;
    do
    {
        tmp[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    for (size_t i = 0; i < n; i++)
    {
        out[i] = tmp[n - 1 - i];
    }
    return n;
}

static void failsafe_reset(struct FailsafeInternal *restrict ctx)
{
    const protocore_lifeline blank = {0};
    for (int i = 0; i < PROTOCORE_FAILSAFE_MAX_LIFELINES; i++)
    {
        ctx->store->lines[i] = blank;
    }
    ctx->store->cb = NULL;
    ctx->store->cb_arg = NULL;
}

static void failsafe_add(struct FailsafeInternal *restrict ctx)
{
    const char *name = ctx->ns->args.name;
    const uint32_t deadline_ms = ctx->ns->args.deadline_ms;
    const uint32_t now = ctx->ns->args.now;

    for (int i = 0; i < PROTOCORE_FAILSAFE_MAX_LIFELINES; i++)
    {
        if (!ctx->store->lines[i].armed)
        {
            ctx->store->lines[i].name = name;
            ctx->store->lines[i].deadline_ms = deadline_ms;
            ctx->store->lines[i].last_feed_ms = now; // starts fed, so it is not instantly overdue
            ctx->store->lines[i].armed = PROTO_TRUE;
            ctx->store->lines[i].breached = PROTO_FALSE;
            ctx->ns->i32 = i;
            return;
        }
    }
    ctx->ns->i32 = -1;
}

static void failsafe_feed(struct FailsafeInternal *restrict ctx)
{
    const int id = ctx->ns->args.id;
    const uint32_t now = ctx->ns->args.now;

    if (id < 0 || id >= PROTOCORE_FAILSAFE_MAX_LIFELINES || !ctx->store->lines[id].armed)
    {
        ctx->ns->ok = PROTO_FALSE;
        return;
    }
    ctx->store->lines[id].last_feed_ms = now;
    ctx->store->lines[id].breached = PROTO_FALSE; // a fresh check-in clears the breach so it can fire again next time
    ctx->ns->ok = PROTO_TRUE;
}

static void failsafe_on_breach(struct FailsafeInternal *restrict ctx)
{
    ctx->store->cb = ctx->ns->out_args.cb;
    ctx->store->cb_arg = ctx->ns->out_args.arg;
}

static void failsafe_check(struct FailsafeInternal *restrict ctx)
{
    const uint32_t now = ctx->ns->args.now;

    uint32_t mask = 0;
    for (int i = 0; i < PROTOCORE_FAILSAFE_MAX_LIFELINES; i++)
    {
        protocore_lifeline *l = &ctx->store->lines[i];
        if (!l->armed)
        {
            continue;
        }
        if (!protocore_lifeline_overdue(now, l->last_feed_ms, l->deadline_ms))
        {
            continue;
        }
        mask |= (1u << i);
        if (l->breached) // fire once per stuck episode
        {
            continue;
        }
        l->breached = PROTO_TRUE;
        if (ctx->store->cb)
        {
            ctx->store->cb(i, l->name, ctx->store->cb_arg);
        }
    }
    ctx->ns->breached = mask;
}

// append a literal into out[*n], bounded by cap (leaving room for the NUL); truncates safely on overflow.
static void fs_put(char *out, size_t cap, size_t *n, const char *s)
{
    while (*s && *n + 1 < cap)
    {
        out[(*n)++] = *s++;
    }
}
// append @p v as decimal into out[*n], same bound.
static void fs_put_u32(char *out, size_t cap, size_t *n, uint32_t v)
{
    char b[10];
    size_t k = u32_dec(v, b);
    for (size_t i = 0; i < k && *n + 1 < cap; i++)
    {
        out[(*n)++] = b[i];
    }
}

static void failsafe_json(struct FailsafeInternal *restrict ctx)
{
    const uint32_t now = ctx->ns->args.now;
    char *out = ctx->ns->out_args.out;
    const size_t cap = ctx->ns->out_args.cap;

    // {"lifelines":[{"name":"...","overdue":false,"age_ms":N,"deadline_ms":N},...]}
    ctx->ns->n = 0;
    if (!out || cap == 0)
    {
        return;
    }
    size_t n = 0;
    fs_put(out, cap, &n, "{\"lifelines\":[");
    proto_bool first = PROTO_TRUE;
    for (int i = 0; i < PROTOCORE_FAILSAFE_MAX_LIFELINES; i++)
    {
        const protocore_lifeline *l = &ctx->store->lines[i];
        if (!l->armed)
        {
            continue;
        }
        if (!first)
        {
            fs_put(out, cap, &n, ",");
        }
        first = PROTO_FALSE;
        fs_put(out, cap, &n, "{\"name\":\"");
        fs_put(out, cap, &n, l->name ? l->name : "");
        fs_put(out, cap, &n, "\",\"overdue\":");
        fs_put(out, cap, &n, protocore_lifeline_overdue(now, l->last_feed_ms, l->deadline_ms) ? "true" : "false");
        fs_put(out, cap, &n, ",\"age_ms\":");
        fs_put_u32(out, cap, &n, now - l->last_feed_ms);
        fs_put(out, cap, &n, ",\"deadline_ms\":");
        fs_put_u32(out, cap, &n, l->deadline_ms);
        fs_put(out, cap, &n, "}");
    }
    fs_put(out, cap, &n, "]}");
    // The n >= cap arm is unreachable: fs_put/fs_put_u32 only ever advance n while n + 1 < cap, so n
    // can never reach cap by the time we get here (cap > 0 was already established above).
    out[n < cap ? n : cap - 1] = '\0';
    ctx->ns->n = (int)n;
}

// Designated, so a member's position in the struct does not decide what it binds to.
FailsafeNs Failsafe = {.reset = failsafe_reset,
                       .add = failsafe_add,
                       .feed = failsafe_feed,
                       .on_breach = failsafe_on_breach,
                       .check = failsafe_check,
                       .json = failsafe_json,
                       .internal = &s_fs_ctx};

#endif // PROTOCORE_ENABLE_FAILSAFE
