// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file diffserv.cpp
 * @brief DiffServ QoS marking (RFC 2474) - the two server-wide DSCP defaults.
 *
 * Owns the default DSCP applied to outbound TCP connections and the default for UDP datagrams. The
 * per-listener override (pc_listen_set_dscp) lives in listener.cpp and the per-connection setter
 * (pc_conn_set_dscp) in tcp.cpp - each next to the pcb pool it touches - but both read these defaults.
 */

#include "diffserv.h"

#if PC_ENABLE_DIFFSERV

namespace
{
/// The single owner of all DiffServ file-scope state.
struct DiffServCtx
{
    uint8_t tcp_dscp; ///< server-wide default DSCP for outbound TCP connections (0 = best-effort)
    uint8_t udp_dscp; ///< default DSCP for outbound UDP datagrams (0 = best-effort)
};
DiffServCtx s_diffserv = {0, 0};
} // namespace

void pc_set_default_dscp(uint8_t dscp)
{
    s_diffserv.tcp_dscp = (uint8_t)(dscp & 0x3F);
}

uint8_t pc_diffserv_default_dscp(void)
{
    return s_diffserv.tcp_dscp;
}

void pc_udp_set_dscp(uint8_t dscp)
{
    s_diffserv.udp_dscp = (uint8_t)(dscp & 0x3F);
}

uint8_t pc_diffserv_udp_dscp(void)
{
    return s_diffserv.udp_dscp;
}

#endif // PC_ENABLE_DIFFSERV
