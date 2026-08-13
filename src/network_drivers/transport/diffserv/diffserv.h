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
 * the send hot path. This module owns the two server-wide DSCP defaults; the per-listener override lives with
 * its control block (tcp/server/server.c) but reads the defaults through here.
 *
 * Two levels of control, coarse to fine, each set through the object it marks:
 *   - DiffServ.set_default: every outbound TCP connection, accepted and client, starts here.
 *   - TcpListener.set_dscp: every connection accepted on one port, overriding that default.
 * A live connection has no third level. RFC 9293 sec 3.9.2 SHLD-23: an application should not change
 * the Diffserv field during a connection, so the port is the finest granularity.
 * DiffServ.set_udp is the same default for outbound datagrams. A DSCP of 0 is best-effort: no
 * marking, TOS left 0.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DIFFSERV_H
#define PROTOCORE_DIFFSERV_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_DIFFSERV

// Common RFC 2474 / RFC 4594 code points for convenience; any 0-63 value is accepted.
#define PROTOCORE_DSCP_CS0 0      ///< default / best effort
#define PROTOCORE_DSCP_CS6 48     ///< network control
#define PROTOCORE_DSCP_EF 46      ///< expedited forwarding (low-latency real-time)
#define PROTOCORE_DSCP_AF41 34    ///< assured forwarding, class 4 low drop (interactive)
#define PROTOCORE_DSCP_AF31 26    ///< assured forwarding, class 3 low drop (multimedia streaming)
#define PROTOCORE_DSCP_UNSET 0xFF ///< per-listener sentinel: fall back to the server-wide default

/** @brief DSCP (0-63) -> the 8-bit DS field. The low 2 bits are ECN (left 0); TOS = DSCP << 2. */
static inline uint8_t protocore_dscp_to_tos(uint8_t dscp)
{
    return (uint8_t)((dscp & 0x3F) << 2);
}

#endif // PROTOCORE_ENABLE_DIFFSERV

/** @brief The two marks and the calls that reach them, described only in diffserv.c. */
struct DiffServInternal;

/**
 * @brief The DSCP marks this server sets on what it sends.
 *
 * A caller sets the mark it wants, invokes the call through ::DiffServ, and reads a mark back off
 * the same handle.
 *
 * @var DiffServNs::dscp          the code point a setter installs
 * @var DiffServNs::u8            the code point a getter reports
 * @var DiffServNs::set_default   the mark new TCP connections take
 * @var DiffServNs::default_dscp  that mark
 * @var DiffServNs::set_udp       the mark UDP sends take
 * @var DiffServNs::udp_dscp      that mark
 * @var DiffServNs::internal      the two marks and the calls that reach them
 *
 * Per-connection and per-listener marks are set through the connection and the listener, which own
 * those objects; this table holds only the defaults they start from.
 */
typedef struct
{
    uint8_t dscp;

    uint8_t u8;

    void (*set_default)(struct DiffServInternal *ctx);
    void (*default_dscp)(struct DiffServInternal *ctx);
    void (*set_udp)(struct DiffServInternal *ctx);
    void (*udp_dscp)(struct DiffServInternal *ctx);

    struct DiffServInternal *internal;
} DiffServNs;

/** @brief The one symbol this module exports. */
extern DiffServNs DiffServ;

#if PROTOCORE_ENABLE_DIFFSERV
/** @brief The server-wide default DSCP outbound TCP connections start from. */
uint8_t protocore_diffserv_default_dscp(void);

/** @brief The default DSCP outbound UDP datagrams are stamped with. */
uint8_t protocore_diffserv_udp_dscp(void);
#endif

PROTOCORE_END_DECLS

#endif // PROTOCORE_DIFFSERV_H
