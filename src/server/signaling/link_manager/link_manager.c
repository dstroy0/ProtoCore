// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file link_manager.c
 * @brief Multi-interface egress selection + graceful escalation/failover (see link_manager.h).
 */

#include "server/signaling/link_manager/link_manager.h"

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

void protocore_link_select(uint8_t *restrict work)
{
    (void)work;
    LinkV.i32 = select_best(LinkV.args.m_ro);
}

void protocore_link_init(uint8_t *restrict work)
{
    (void)work;
    LinkManager *m = LinkV.args.m;
    if (!m)
    {
        return;
    }
    m->ifaces = LinkV.args.ifaces;
    m->n = LinkV.args.ifaces ? LinkV.args.n : 0;
    m->active = select_best(m);
}

void protocore_link_active(uint8_t *restrict work)
{
    (void)work;
    const LinkManager *m = LinkV.args.m_ro;
    LinkV.i32 = m ? m->active : -1;
}

void protocore_link_set(uint8_t *restrict work)
{
    (void)work;
    LinkManager *m = LinkV.args.m;
    const size_t idx = LinkV.args.idx;

    if (!m || !m->ifaces || idx >= m->n)
    {
        LinkV.from = m ? m->active : -1;
        LinkV.to = LinkV.from;
        LinkV.changed = PROTO_FALSE;
        return;
    }
    int prev = m->active;
    m->ifaces[idx].up = LinkV.args.up;
    m->active = select_best(m);
    LinkV.from = prev;
    LinkV.to = m->active;
    LinkV.changed = (m->active != prev);
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
LinkVars LinkV;

#endif // PROTOCORE_ENABLE_LINK_MANAGER
