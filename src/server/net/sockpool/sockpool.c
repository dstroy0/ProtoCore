// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sockpool.c
 * @brief Dynamic socket recycling: an LRU connection-slot pool (see sockpool.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SOCKPOOL

#include "server/net/sockpool/sockpool.h"

PROTOCORE_BEGIN_DECLS

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void sockpool_init(uint8_t *restrict work)
{
    (void)work;
    SockPool *p = Sockpool.init_args.p;
    SockSlot *slots = Sockpool.init_args.slots;
    size_t n = Sockpool.init_args.n;

    if (!p)
    {
        return;
    }
    p->slots = slots;
    p->n = slots ? n : 0;
    for (size_t i = 0; i < p->n; i++)
    {
        p->slots[i].in_use = PROTO_FALSE;
        p->slots[i].id = 0;
        p->slots[i].last_used = 0;
    }
}

static void sockpool_acquire(uint8_t *restrict work)
{
    (void)work;
    SockPool *p = Sockpool.acquire_args.p;
    uint32_t id = Sockpool.acquire_args.id;
    uint32_t now = Sockpool.acquire_args.now;
    size_t *idx = Sockpool.acquire_args.idx;
    uint32_t *evicted_id = Sockpool.acquire_args.evicted_id;

    if (!p || !p->slots || p->n == 0)
    {
        Sockpool.acq = SOCK_ACQ_FAIL;
        return;
    }

    // Prefer a free slot.
    for (size_t i = 0; i < p->n; i++)
    {
        if (!p->slots[i].in_use)
        {
            p->slots[i].in_use = PROTO_TRUE;
            p->slots[i].id = id;
            p->slots[i].last_used = now;
            if (idx)
            {
                *idx = i;
            }
            Sockpool.acq = SOCK_ACQ_FREE;
            return;
        }
    }

    // Full: recycle the least-recently-used slot.
    size_t lru = 0;
    for (size_t i = 1; i < p->n; i++)
    {
        if (p->slots[i].last_used < p->slots[lru].last_used)
        {
            lru = i;
        }
    }
    if (evicted_id)
    {
        *evicted_id = p->slots[lru].id;
    }
    p->slots[lru].id = id;
    p->slots[lru].last_used = now;
    if (idx)
    {
        *idx = lru;
    }
    Sockpool.acq = SOCK_ACQ_RECYCLED;
}

static void sockpool_touch(uint8_t *restrict work)
{
    (void)work;
    SockPool *p = Sockpool.touch_args.p;
    size_t idx = Sockpool.touch_args.idx;
    uint32_t now = Sockpool.touch_args.now;

    if (!p || !p->slots || idx >= p->n)
    {
        return;
    }
    if (p->slots[idx].in_use)
    {
        p->slots[idx].last_used = now;
    }
}

static void sockpool_release(uint8_t *restrict work)
{
    (void)work;
    SockPool *p = Sockpool.release_args.p;
    size_t idx = Sockpool.release_args.idx;

    if (!p || !p->slots || idx >= p->n || !p->slots[idx].in_use)
    {
        Sockpool.ok = PROTO_FALSE;
        return;
    }
    p->slots[idx].in_use = PROTO_FALSE;
    Sockpool.ok = PROTO_TRUE;
}

static void sockpool_find(uint8_t *restrict work)
{
    (void)work;
    const SockPool *p = Sockpool.find_args.p;
    uint32_t id = Sockpool.find_args.id;
    size_t *idx = Sockpool.find_args.idx;

    if (!p || !p->slots)
    {
        Sockpool.ok = PROTO_FALSE;
        return;
    }
    for (size_t i = 0; i < p->n; i++)
    {
        if (p->slots[i].in_use && p->slots[i].id == id)
        {
            if (idx)
            {
                *idx = i;
            }
            Sockpool.ok = PROTO_TRUE;
            return;
        }
    }
    Sockpool.ok = PROTO_FALSE;
}

static void sockpool_in_use(uint8_t *restrict work)
{
    (void)work;
    const SockPool *p = Sockpool.in_use_args.p;

    if (!p || !p->slots)
    {
        Sockpool.n = 0;
        return;
    }
    size_t c = 0;
    for (size_t i = 0; i < p->n; i++)
    {
        if (p->slots[i].in_use)
        {
            c++;
        }
    }
    Sockpool.n = c;
}

SockpoolNs Sockpool = {.init = sockpool_init,
                       .acquire = sockpool_acquire,
                       .touch = sockpool_touch,
                       .release = sockpool_release,
                       .find = sockpool_find,
                       .in_use = sockpool_in_use};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SOCKPOOL
