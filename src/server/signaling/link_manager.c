// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file link_manager.c
 * @brief Multi-interface egress selection + graceful escalation/failover (see link_manager.h).
 */

#include "server/signaling/link_manager.h"

#if PROTOCORE_ENABLE_LINK_MANAGER

/**
 * @brief The manager's calls - what LinkManagerNs points at.
 *
 * @var LinkManagerInternal::ns  the handle a caller sets a call's members on
 */
struct LinkManagerInternal
{
    LinkManagerNs *ns;
};

static struct LinkManagerInternal s_link = {.ns = &Link};

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

static void link_select(struct LinkManagerInternal *restrict ctx)
{
    ctx->ns->i32 = select_best(ctx->ns->args.m_ro);
}

static void link_init(struct LinkManagerInternal *restrict ctx)
{
    LinkManager *m = ctx->ns->args.m;
    if (!m)
    {
        return;
    }
    m->ifaces = ctx->ns->args.ifaces;
    m->n = ctx->ns->args.ifaces ? ctx->ns->args.n : 0;
    m->active = select_best(m);
}

static void link_active(struct LinkManagerInternal *restrict ctx)
{
    const LinkManager *m = ctx->ns->args.m_ro;
    ctx->ns->i32 = m ? m->active : -1;
}

static void link_set(struct LinkManagerInternal *restrict ctx)
{
    LinkManager *m = ctx->ns->args.m;
    const size_t idx = ctx->ns->args.idx;

    if (!m || !m->ifaces || idx >= m->n)
    {
        ctx->ns->from = m ? m->active : -1;
        ctx->ns->to = ctx->ns->from;
        ctx->ns->changed = PROTO_FALSE;
        return;
    }
    int prev = m->active;
    m->ifaces[idx].up = ctx->ns->args.up;
    m->active = select_best(m);
    ctx->ns->from = prev;
    ctx->ns->to = m->active;
    ctx->ns->changed = (m->active != prev);
}

// Designated, so a member's position in the struct does not decide what it binds to.
LinkManagerNs Link = {
    .init = link_init, .select = link_select, .active = link_active, .set = link_set, .internal = &s_link};

#endif // PROTOCORE_ENABLE_LINK_MANAGER
