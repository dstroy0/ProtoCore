// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sockpool.cpp
 * @brief Dynamic socket recycling: an LRU connection-slot pool (see sockpool.h).
 */

#include "services/net/sockpool/sockpool.h"

#if PC_ENABLE_SOCKPOOL

void pc_sockpool_init(SockPool *p, SockSlot *slots, size_t n)
{
    if (!p)
    {
        return;
    }
    p->slots = slots;
    p->n = slots ? n : 0;
    for (size_t i = 0; i < p->n; i++)
    {
        p->slots[i].in_use = false;
        p->slots[i].id = 0;
        p->slots[i].last_used = 0;
    }
}

SockAcq pc_sockpool_acquire(SockPool *p, uint32_t id, uint32_t now, size_t *idx, uint32_t *evicted_id)
{
    if (!p || !p->slots || p->n == 0)
    {
        return SockAcq::SOCK_ACQ_FAIL;
    }

    // Prefer a free slot.
    for (size_t i = 0; i < p->n; i++)
    {
        if (!p->slots[i].in_use)
        {
            p->slots[i].in_use = true;
            p->slots[i].id = id;
            p->slots[i].last_used = now;
            if (idx)
            {
                *idx = i;
            }
            return SockAcq::SOCK_ACQ_FREE;
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
    return SockAcq::SOCK_ACQ_RECYCLED;
}

void pc_sockpool_touch(SockPool *p, size_t idx, uint32_t now)
{
    if (!p || !p->slots || idx >= p->n)
    {
        return;
    }
    if (p->slots[idx].in_use)
    {
        p->slots[idx].last_used = now;
    }
}

bool pc_sockpool_release(SockPool *p, size_t idx)
{
    if (!p || !p->slots || idx >= p->n || !p->slots[idx].in_use)
    {
        return false;
    }
    p->slots[idx].in_use = false;
    return true;
}

bool pc_sockpool_find(const SockPool *p, uint32_t id, size_t *idx)
{
    if (!p || !p->slots)
    {
        return false;
    }
    for (size_t i = 0; i < p->n; i++)
    {
        if (p->slots[i].in_use && p->slots[i].id == id)
        {
            if (idx)
            {
                *idx = i;
            }
            return true;
        }
    }
    return false;
}

size_t pc_sockpool_in_use(const SockPool *p)
{
    if (!p || !p->slots)
    {
        return 0;
    }
    size_t c = 0;
    for (size_t i = 0; i < p->n; i++)
    {
        if (p->slots[i].in_use)
        {
            c++;
        }
    }
    return c;
}

#endif // PC_ENABLE_SOCKPOOL
