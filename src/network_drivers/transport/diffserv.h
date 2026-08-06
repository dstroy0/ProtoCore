// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file diffserv.h
 * @brief Layer 4 (Transport) - DiffServ QoS marking (RFC 2474) for outbound traffic.
 *
 * Stamps the 6-bit DSCP into the DS field (the high 6 bits of the IPv4 TOS / IPv6 Traffic-Class byte) of
 * outbound TCP connections and UDP datagrams so a QoS-aware network - and the Wi-Fi driver's 802.11e WMM
 * access-category mapping - can prioritize real-time / safety packets over best-effort. The marking is applied
 * on the stack's own thread where the pcb is created (accept / connect / udp create), so nothing is added to
 * the send hot path. This module owns the two server-wide DSCP defaults; the per-listener and per-connection
 * overrides live with their pcb (listener.c / tcp.c) but read the defaults through here.
 *
 * Three levels of control, coarse to fine, each set through the object it marks:
 *   - DiffServ.set_default(): every outbound TCP connection, accepted and client, starts here.
 *   - Tcp.listener->set_dscp(): every connection accepted on one port, overriding that default.
 *   - Tcp.conn->set_dscp(): one live connection, any DSCP, at any time.
 * DiffServ.set_udp() is the same default for outbound datagrams. A DSCP of 0 is best-effort: no
 * marking, TOS left 0.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DIFFSERV_H
#define PROTOCORE_DIFFSERV_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_DIFFSERV

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

#endif // PC_ENABLE_DIFFSERV

/**
 * @brief The DSCP marks this server sets on what it sends.
 *
 * @var DiffServNs::set_default   the mark new TCP connections take
 * @var DiffServNs::default_dscp  that mark
 * @var DiffServNs::set_udp       the mark UDP sends take
 * @var DiffServNs::udp_dscp      that mark
 *
 * Per-connection and per-listener marks are set through the connection and the listener, which own
 * those objects; this table holds only the defaults they start from.
 *
 * No storage member: the two marks belong to diffserv.c.
 */
typedef struct
{
    void (*set_default)(uint8_t dscp);
    uint8_t (*default_dscp)(void);
    void (*set_udp)(uint8_t dscp);
    uint8_t (*udp_dscp)(void);
} DiffServNs;

/** @brief The one symbol this module exports. */
extern const DiffServNs DiffServ;

PROTO_END_DECLS

#endif // PROTOCORE_DIFFSERV_H
