// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file iface_bridge_hw.h
 * @brief Bus glue for the interface bridge (PROTOCORE_ENABLE_IFACE_BRIDGE): the PROTO_BRIDGE listener that
 *        wires an accepted connection to a UART / SPI / I2C endpoint, plus the bus I/O.
 *
 * The pure core (iface_bridge.h) owns the rule table and the transaction frame codec; this file owns the
 * side that reaches the seam: a ProtoConn::PROTO_BRIDGE connection handler and the uart.h / spi.h / i2c.h
 * transfers. Layered exactly like server/net/relay - the app opens the listener, then publishes a target:
 *
 * @code
 *   int32_t li = server.listen(2323, ProtoConn::PROTO_BRIDGE);          // front port 2323
 *   BridgeTarget uart = {BRIDGE_BUS_UART, BRIDGE_MODE_STREAM, 1, 0, 115200, 0, 0};
 *   protocore_iface_bridge_publish((uint8_t)li, 2323, BRIDGE_PROTO_TCP, &uart);     // -> UART1 raw passthrough
 *
 *   int32_t ls = server.listen(2324, ProtoConn::PROTO_BRIDGE);
 *   BridgeTarget spi = {BRIDGE_BUS_SPI, BRIDGE_MODE_TRANSACTION, 0, 5, 1000000, 0, 0}; // 5 = CS gpio
 *   protocore_iface_bridge_publish((uint8_t)ls, 2324, BRIDGE_PROTO_TCP, &spi);      // -> SPI write-then-read frames
 * @endcode
 *
 * Security: a published port is a direct pipe to the bus. Only expose it on a trusted interface / behind
 * an upstream ACL; there is no authentication at this layer.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_IFACE_BRIDGE_HW_H
#define PROTOCORE_IFACE_BRIDGE_HW_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_IFACE_BRIDGE

PROTOCORE_BEGIN_DECLS

// PROTOCORE_IFACE_BRIDGE_HW_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

/** @brief BridgeTarget, as the caller already knows it. */
struct BridgeTarget;

/** @brief What publish takes: listener_id, port, proto, target. */
typedef struct
{
    uint8_t listener_id;               ///< the id returned by `server.listen(port, ProtoConn::PROTO_BRIDGE)`
    uint16_t port;                     ///< the same listen port (the dispatch key into the rule table)
    BridgeProto proto;                 ///< TCP or UDP (matches how the listener was opened)
    const struct BridgeTarget *target; ///< the UART / SPI / I2C endpoint (copied into the rule)
} IfaceBridgeHwPublishArgs;

/**
 * @brief Bus glue for the interface bridge (PROTOCORE_ENABLE_IFACE_BRIDGE): the PROTO_BRIDGE listener that wires an ...
 *
 * A caller sets the members a call takes, invokes it through ::IfaceBridgeHw with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   IfaceBridgeHw.publish_args.listener_id = ...;
 *   IfaceBridgeHw.publish_args.port = ...;
 *   IfaceBridgeHw.publish_args.proto = ...;
 *   IfaceBridgeHw.publish_args.target = ...;
 *   IfaceBridgeHw.publish(work);
 *   // IfaceBridgeHw.ok is what the call reports
 *
 * @var IfaceBridgeHwNs::publish_args  what publish takes: listener_id, port, proto, target
 * @var IfaceBridgeHwNs::ok  true; false if target is null, the rule table is full, or the ...
 * @var IfaceBridgeHwNs::publish  bind a PROTO_BRIDGE listener to a hardware target and install the ...
 * @var IfaceBridgeHwNs::reset  clear all listener bindings and rules (start from empty)
 *
 * @c work is PROTOCORE_IFACE_BRIDGE_HW_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    IfaceBridgeHwPublishArgs publish_args;

    proto_bool ok;

    void (*const publish)(uint8_t *restrict work);
    void (*const reset)(uint8_t *restrict work);
} IfaceBridgeHwNs;

/** @brief The one symbol this module exports. */
extern IfaceBridgeHwNs IfaceBridgeHw;

/**
 * @brief The PROTOCORE_IFACE_BRIDGE_HW_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_iface_bridge_hw_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_IFACE_BRIDGE

#endif // PROTOCORE_IFACE_BRIDGE_HW_H
