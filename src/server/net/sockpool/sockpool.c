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

void protocore_sockpool_init(uint8_t *restrict work)
{
    (void)work;
    SockPool *p = SockpoolV.init_args.p;
    SockSlot *slots = SockpoolV.init_args.slots;
    size_t n = SockpoolV.init_args.n;

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

void protocore_sockpool_acquire(uint8_t *restrict work)
{
    (void)work;
    SockPool *p = SockpoolV.acquire_args.p;
    uint32_t id = SockpoolV.acquire_args.id;
    uint32_t now = SockpoolV.acquire_args.now;
    size_t *idx = SockpoolV.acquire_args.idx;
    uint32_t *evicted_id = SockpoolV.acquire_args.evicted_id;

    if (!p || !p->slots || p->n == 0)
    {
        SockpoolV.acq = SOCK_ACQ_FAIL;
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
            SockpoolV.acq = SOCK_ACQ_FREE;
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
    SockpoolV.acq = SOCK_ACQ_RECYCLED;
}

void protocore_sockpool_touch(uint8_t *restrict work)
{
    (void)work;
    SockPool *p = SockpoolV.touch_args.p;
    size_t idx = SockpoolV.touch_args.idx;
    uint32_t now = SockpoolV.touch_args.now;

    if (!p || !p->slots || idx >= p->n)
    {
        return;
    }
    if (p->slots[idx].in_use)
    {
        p->slots[idx].last_used = now;
    }
}

void protocore_sockpool_release(uint8_t *restrict work)
{
    (void)work;
    SockPool *p = SockpoolV.release_args.p;
    size_t idx = SockpoolV.release_args.idx;

    if (!p || !p->slots || idx >= p->n || !p->slots[idx].in_use)
    {
        SockpoolV.ok = PROTO_FALSE;
        return;
    }
    p->slots[idx].in_use = PROTO_FALSE;
    SockpoolV.ok = PROTO_TRUE;
}

void protocore_sockpool_find(uint8_t *restrict work)
{
    (void)work;
    const SockPool *p = SockpoolV.find_args.p;
    uint32_t id = SockpoolV.find_args.id;
    size_t *idx = SockpoolV.find_args.idx;

    if (!p || !p->slots)
    {
        SockpoolV.ok = PROTO_FALSE;
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
            SockpoolV.ok = PROTO_TRUE;
            return;
        }
    }
    SockpoolV.ok = PROTO_FALSE;
}

void protocore_sockpool_in_use(uint8_t *restrict work)
{
    (void)work;
    const SockPool *p = SockpoolV.in_use_args.p;

    if (!p || !p->slots)
    {
        SockpoolV.n = 0;
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
    SockpoolV.n = c;
}

/** @brief The operands and the outcome. */
SockpoolVars SockpoolV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SOCKPOOL
