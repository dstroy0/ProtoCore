// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file failsafe.c
 * @brief Software watchdog / deadlock detection + safe-state (see failsafe.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_FAILSAFE

#include "mmgr/plaintext/plaintext.h" // the persistent end this module's state is taken from
#include "server/core/failsafe/failsafe.h"

/**
 * @brief The lifelines' compile-time storage: the table, and what a breach fires.
 */
struct FailsafeStorage
{
    protocore_lifeline lines[PROTOCORE_FAILSAFE_MAX_LIFELINES];
    protocore_failsafe_cb cb;
    void *cb_arg;
};

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define FAILSAFE_OFF_CTX 0u
static_assert(FAILSAFE_OFF_CTX + sizeof(struct FailsafeStorage) <= PROTOCORE_FAILSAFE_BORROW,
              "PROTOCORE_FAILSAFE_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define FAILSAFE_CTX(w) ((struct FailsafeStorage *)(void *)((w) + FAILSAFE_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_FAILSAFE_BORROW persistent bytes
} FailsafeOwnCtx;
static FailsafeOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_failsafe_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_plaintext_persist_span(PROTOCORE_FAILSAFE_BORROW).buf;
    }
    return s_own.span;
}

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

void protocore_failsafe_reset(uint8_t *restrict work)
{
    const protocore_lifeline blank = {0};
    for (int i = 0; i < PROTOCORE_FAILSAFE_MAX_LIFELINES; i++)
    {
        FAILSAFE_CTX(work)->lines[i] = blank;
    }
    FAILSAFE_CTX(work)->cb = NULL;
    FAILSAFE_CTX(work)->cb_arg = NULL;
}

void protocore_failsafe_add(uint8_t *restrict work)
{
    const char *name = FailsafeV.args.name;
    const uint32_t deadline_ms = FailsafeV.args.deadline_ms;
    const uint32_t now = FailsafeV.args.now;

    for (int i = 0; i < PROTOCORE_FAILSAFE_MAX_LIFELINES; i++)
    {
        if (!FAILSAFE_CTX(work)->lines[i].armed)
        {
            FAILSAFE_CTX(work)->lines[i].name = name;
            FAILSAFE_CTX(work)->lines[i].deadline_ms = deadline_ms;
            FAILSAFE_CTX(work)->lines[i].last_feed_ms = now; // starts fed, so it is not instantly overdue
            FAILSAFE_CTX(work)->lines[i].armed = PROTO_TRUE;
            FAILSAFE_CTX(work)->lines[i].breached = PROTO_FALSE;
            FailsafeV.i32 = i;
            return;
        }
    }
    FailsafeV.i32 = -1;
}

void protocore_failsafe_feed(uint8_t *restrict work)
{
    const int id = FailsafeV.args.id;
    const uint32_t now = FailsafeV.args.now;

    if (id < 0 || id >= PROTOCORE_FAILSAFE_MAX_LIFELINES || !FAILSAFE_CTX(work)->lines[id].armed)
    {
        FailsafeV.ok = PROTO_FALSE;
        return;
    }
    FAILSAFE_CTX(work)->lines[id].last_feed_ms = now;
    FAILSAFE_CTX(work)->lines[id].breached =
        PROTO_FALSE; // a fresh check-in clears the breach so it can fire again next time
    FailsafeV.ok = PROTO_TRUE;
}

void protocore_failsafe_on_breach(uint8_t *restrict work)
{
    FAILSAFE_CTX(work)->cb = FailsafeV.out_args.cb;
    FAILSAFE_CTX(work)->cb_arg = FailsafeV.out_args.arg;
}

void protocore_failsafe_check(uint8_t *restrict work)
{
    const uint32_t now = FailsafeV.args.now;

    uint32_t mask = 0;
    for (int i = 0; i < PROTOCORE_FAILSAFE_MAX_LIFELINES; i++)
    {
        protocore_lifeline *l = &FAILSAFE_CTX(work)->lines[i];
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
        if (FAILSAFE_CTX(work)->cb)
        {
            FAILSAFE_CTX(work)->cb(i, l->name, FAILSAFE_CTX(work)->cb_arg);
        }
    }
    FailsafeV.breached = mask;
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

void protocore_failsafe_json(uint8_t *restrict work)
{
    const uint32_t now = FailsafeV.args.now;
    char *out = FailsafeV.out_args.out;
    const size_t cap = FailsafeV.out_args.cap;

    // {"lifelines":[{"name":"...","overdue":false,"age_ms":N,"deadline_ms":N},...]}
    FailsafeV.n = 0;
    if (!out || cap == 0)
    {
        return;
    }
    size_t n = 0;
    fs_put(out, cap, &n, "{\"lifelines\":[");
    proto_bool first = PROTO_TRUE;
    for (int i = 0; i < PROTOCORE_FAILSAFE_MAX_LIFELINES; i++)
    {
        const protocore_lifeline *l = &FAILSAFE_CTX(work)->lines[i];
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
    FailsafeV.n = (int)n;
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
FailsafeVars FailsafeV;

#endif // PROTOCORE_ENABLE_FAILSAFE
