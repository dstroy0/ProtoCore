// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file diffserv.c
 * @brief DiffServ QoS marking (RFC 2474) - the two server-wide DSCP defaults.
 *
 * Owns the default DSCP applied to outbound TCP connections and the default for UDP datagrams. A
 * listener's override and a connection's override are set through those objects, each next to the
 * pcb pool it touches, and both read these defaults.
 */

#include "diffserv.h"

#if PROTOCORE_ENABLE_DIFFSERV

/**
 * @brief The two code points this server marks with, held where only this file describes them.
 *
 * RFC 2474 sec 3 names the six-bit selector in the DS field the DSCP; these are the defaults each
 * outbound object starts from. Both start at 0: RFC 2474 sec 4.1 makes the all-zero DSCP the
 * default PHB, best effort.
 */
struct DiffServStorage
{
    uint8_t tcp_dscp; ///< server-wide default DSCP for outbound TCP connections (0 = best-effort)
    uint8_t udp_dscp; ///< default DSCP for outbound UDP datagrams (0 = best-effort)
};

/**
 * @brief The marks and the calls that reach them - what DiffServNs points at.
 *
 * @var DiffServInternal::store  the two code points
 * @var DiffServInternal::ns     the handle a caller sets a call's mark on
 */
struct DiffServInternal
{
    struct DiffServStorage *store;
    DiffServNs *ns;
};

static struct DiffServStorage s_store;

static struct DiffServInternal s_diffserv = {.store = &s_store, .ns = &DiffServ};

// Masked to six bits on write, so a caller cannot spill into the two currently-unused bits.
static void set_default(struct DiffServInternal *restrict ctx)
{
    ctx->store->tcp_dscp = (uint8_t)(ctx->ns->dscp & 0x3F);
}

static void default_dscp(struct DiffServInternal *restrict ctx)
{
    ctx->ns->u8 = ctx->store->tcp_dscp;
}

static void set_udp(struct DiffServInternal *restrict ctx)
{
    ctx->store->udp_dscp = (uint8_t)(ctx->ns->dscp & 0x3F);
}

static void udp_dscp(struct DiffServInternal *restrict ctx)
{
    ctx->ns->u8 = ctx->store->udp_dscp;
}

uint8_t protocore_diffserv_default_dscp(void)
{
    return s_store.tcp_dscp;
}

uint8_t protocore_diffserv_udp_dscp(void)
{
    return s_store.udp_dscp;
}

// Designated, so a member's position in the struct does not decide what it binds to.
DiffServNs DiffServ = {.set_default = set_default,
                       .default_dscp = default_dscp,
                       .set_udp = set_udp,
                       .udp_dscp = udp_dscp,
                       .internal = &s_diffserv};

#endif // PROTOCORE_ENABLE_DIFFSERV
