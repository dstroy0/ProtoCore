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

#if PC_ENABLE_DIFFSERV

/// The single owner of all DiffServ file-scope state.
typedef struct
{
    uint8_t tcp_dscp; ///< server-wide default DSCP for outbound TCP connections (0 = best-effort)
    uint8_t udp_dscp; ///< default DSCP for outbound UDP datagrams (0 = best-effort)
} DiffServCtx;
static DiffServCtx s_diffserv = {0, 0};

static void set_default(uint8_t dscp)
{
    s_diffserv.tcp_dscp = (uint8_t)(dscp & 0x3F);
}

static uint8_t default_dscp(void)
{
    return s_diffserv.tcp_dscp;
}

static void set_udp(uint8_t dscp)
{
    s_diffserv.udp_dscp = (uint8_t)(dscp & 0x3F);
}

static uint8_t udp_dscp(void)
{
    return s_diffserv.udp_dscp;
}

const DiffServNs DiffServ = {set_default, default_dscp, set_udp, udp_dscp};

#endif // PC_ENABLE_DIFFSERV
