// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file link_manager.c
 * @brief Multi-interface egress selection + graceful escalation/failover (see link_manager.h).
 */

#include "server/signaling/link_manager.h"

#if PROTOCORE_ENABLE_LINK_MANAGER

// The highest-priority interface that is up, or -1. Higher priority wins; the lower index breaks a
// tie, because best is the first entry seen at that priority.
static int select_best(const LinkManager *m)
{
    if (!m || !m->ifaces)
    {
        return -1;
    }
    int best = -1;
    for (size_t i = 0; i < m->n; i++)
    {
        if (!m->ifaces[i].up)
        {
            continue;
        }
        if (best < 0 || m->ifaces[i].priority > m->ifaces[best].priority)
        {
            best = (int)i;
        }
    }
    return best;
}

static void link_select(uint8_t *restrict work)
{
    (void)work;
    Link.i32 = select_best(Link.args.m_ro);
}

static void link_init(uint8_t *restrict work)
{
    (void)work;
    LinkManager *m = Link.args.m;
    if (!m)
    {
        return;
    }
    m->ifaces = Link.args.ifaces;
    m->n = Link.args.ifaces ? Link.args.n : 0;
    m->active = select_best(m);
}

static void link_active(uint8_t *restrict work)
{
    (void)work;
    const LinkManager *m = Link.args.m_ro;
    Link.i32 = m ? m->active : -1;
}

static void link_set(uint8_t *restrict work)
{
    (void)work;
    LinkManager *m = Link.args.m;
    const size_t idx = Link.args.idx;

    if (!m || !m->ifaces || idx >= m->n)
    {
        Link.from = m ? m->active : -1;
        Link.to = Link.from;
        Link.changed = PROTO_FALSE;
        return;
    }
    int prev = m->active;
    m->ifaces[idx].up = Link.args.up;
    m->active = select_best(m);
    Link.from = prev;
    Link.to = m->active;
    Link.changed = (m->active != prev);
}

// Designated, so a member's position in the struct does not decide what it binds to.
LinkManagerNs Link = {.init = link_init, .select = link_select, .active = link_active, .set = link_set};

#endif // PROTOCORE_ENABLE_LINK_MANAGER
