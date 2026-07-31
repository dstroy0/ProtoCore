// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file diffserv.h
 * @brief Layer 4 (Transport) - DiffServ QoS marking (RFC 2474) for outbound traffic.
 *
 * Stamps the 6-bit DSCP into the DS field (the high 6 bits of the IPv4 TOS / IPv6 Traffic-Class byte) of
 * outbound TCP connections and UDP datagrams so a QoS-aware network - and the Wi-Fi driver's 802.11e WMM
 * access-category mapping - can prioritize real-time / safety packets over best-effort. The marking is applied
 * on tcpip_thread where the pcb is created (accept / connect / udp create), so nothing is added to the send
 * hot path. This module owns the two server-wide DSCP defaults; the per-listener and per-connection overrides
 * live with their pcb (listener.cpp / tcp.cpp) but read the defaults through here.
 *
 * Three levels of control, coarse to fine:
 *   - pc_set_default_dscp(): every outbound TCP connection (accepted + client) gets this DSCP.
 *   - pc_listen_set_dscp(): all connections accepted on one port get a DSCP (overrides the default).
 *   - pc_conn_set_dscp(): tag one live connection with any DSCP - real QoS, or arbitrary network testing.
 * Plus pc_udp_set_dscp() for outbound datagrams. A DSCP of 0 means best-effort (no marking / TOS left 0).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DIFFSERV_H
#define PROTOCORE_DIFFSERV_H

#include "protocore_config.h"

#if PC_ENABLE_DIFFSERV

#include <stdint.h>

// Common RFC 2474 / RFC 4594 code points for convenience; any 0-63 value is accepted.
#define PC_DSCP_CS0 0      ///< default / best effort
#define PC_DSCP_CS6 48     ///< network control
#define PC_DSCP_EF 46      ///< expedited forwarding (low-latency real-time)
#define PC_DSCP_AF41 34    ///< assured forwarding, class 4 low drop (interactive)
#define PC_DSCP_AF31 26    ///< assured forwarding, class 3 low drop (multimedia streaming)
#define PC_DSCP_UNSET 0xFF ///< per-listener sentinel: fall back to the server-wide default

/** @brief DSCP (0-63) -> the 8-bit DS field. The low 2 bits are ECN (left 0); TOS = DSCP << 2. */
static inline uint8_t pc_dscp_to_tos(uint8_t dscp)
{
    return (uint8_t)((dscp & 0x3F) << 2);
}

/**
 * @brief Set the server-wide default DSCP for every outbound TCP connection (accepted + client).
 * @param dscp 0-63; 0 means best-effort (no marking). Takes effect for connections opened after the call.
 */
void pc_set_default_dscp(uint8_t dscp);

/** @brief The current server-wide TCP default DSCP (read by the accept / connect paths). */
uint8_t pc_diffserv_default_dscp(void);

/**
 * @brief Set the default DSCP for outbound UDP datagrams.
 * @param dscp 0-63; 0 means best-effort. Applied to each UDP pcb as it is created.
 */
void pc_udp_set_dscp(uint8_t dscp);

/** @brief The current UDP default DSCP (read when a UDP pcb is created). */
uint8_t pc_diffserv_udp_dscp(void);

/**
 * @brief Tag one accepted server connection with a DSCP (per-connection override).
 *
 * Marshalled to tcpip_thread. Lets an individual flow carry any DSCP - real per-flow QoS, or arbitrary QoS
 * tagging for network testing. Also works mid-connection (lwIP reads pcb->tos per outbound segment).
 * @param slot connection-pool slot.
 * @param dscp 0-63.
 * @return false if @p slot is out of range or no longer holds a live pcb.
 */
bool pc_conn_set_dscp(uint8_t slot, uint8_t dscp);

/**
 * @brief Set the DSCP applied to every connection accepted on @p port (per-listener override).
 * @param port the listening port.
 * @param dscp 0-63, or PC_DSCP_UNSET to clear the override (fall back to the server default).
 * @return false if no active listener binds @p port.
 */
bool pc_listen_set_dscp(uint16_t port, uint8_t dscp);

#endif // PC_ENABLE_DIFFSERV
#endif // PROTOCORE_DIFFSERV_H
