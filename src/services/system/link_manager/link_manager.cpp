// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file link_manager.cpp
 * @brief Multi-interface egress selection + graceful escalation/failover (see link_manager.h).
 */

#include "services/system/link_manager/link_manager.h"

#if PC_ENABLE_LINK_MANAGER

int pc_link_select(const LinkManager *m)
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
        // Higher priority wins; the lower index breaks a tie (best is the first at that priority).
        if (best < 0 || m->ifaces[i].priority > m->ifaces[best].priority)
        {
            best = (int)i;
        }
    }
    return best;
}

void pc_link_init(LinkManager *m, LinkIface *ifaces, size_t n)
{
    if (!m)
    {
        return;
    }
    m->ifaces = ifaces;
    m->n = ifaces ? n : 0;
    m->active = pc_link_select(m);
}

int pc_link_active(const LinkManager *m)
{
    return m ? m->active : -1;
}

bool pc_link_set(LinkManager *m, size_t idx, bool up, int *from, int *to)
{
    if (!m || !m->ifaces || idx >= m->n)
    {
        if (from)
        {
            *from = m ? m->active : -1;
        }
        if (to)
        {
            *to = m ? m->active : -1;
        }
        return false;
    }
    int prev = m->active;
    m->ifaces[idx].up = up;
    m->active = pc_link_select(m);
    if (from)
    {
        *from = prev;
    }
    if (to)
    {
        *to = m->active;
    }
    return m->active != prev;
}

#endif // PC_ENABLE_LINK_MANAGER
