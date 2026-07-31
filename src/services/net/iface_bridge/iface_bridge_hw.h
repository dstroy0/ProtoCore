// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file iface_bridge_hw.h
 * @brief ESP32 glue for the interface bridge (PC_ENABLE_IFACE_BRIDGE): the PROTO_BRIDGE listener that
 *        wires an accepted connection to a UART / SPI / I2C endpoint, plus the bus I/O.
 *
 * The pure core (iface_bridge.h) owns the rule table and the transaction frame codec; this file owns the
 * side that touches hardware: a ConnProto::PROTO_BRIDGE connection handler and the Serial / SPI / Wire
 * transfers. Layered exactly like services/net/relay - the app opens the listener, then publishes a target:
 *
 * @code
 *   int32_t li = server.listen(2323, ConnProto::PROTO_BRIDGE);          // front port 2323
 *   BridgeTarget uart = {BridgeBus::uart, BridgeMode::stream, 1, 0, 115200, 0, 0};
 *   pc_iface_bridge_publish((uint8_t)li, 2323, BridgeProto::tcp, &uart);     // -> UART1 raw passthrough
 *
 *   int32_t ls = server.listen(2324, ConnProto::PROTO_BRIDGE);
 *   BridgeTarget spi = {BridgeBus::spi, BridgeMode::transaction, 0, 5, 1000000, 0, 0}; // 5 = CS gpio
 *   pc_iface_bridge_publish((uint8_t)ls, 2324, BridgeProto::tcp, &spi);      // -> SPI write-then-read frames
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

#include "protocore_config.h"

#if PC_ENABLE_IFACE_BRIDGE

#include "services/net/iface_bridge/iface_bridge.h"
#include <stdint.h>

/**
 * @brief Bind a PROTO_BRIDGE listener to a hardware target and install the handler (first call).
 *
 * Registers the rule in the pure table (pc_iface_bridge_map), records the listener_id -> rule binding used to
 * dispatch accepted connections, and brings up the bus (Serial.begin / SPI CS pin / Wire).
 *
 * @param listener_id  the id returned by `server.listen(port, ConnProto::PROTO_BRIDGE)`.
 * @param port         the same listen port (the dispatch key into the rule table).
 * @param proto        TCP or UDP (matches how the listener was opened).
 * @param target       the UART / SPI / I2C endpoint (copied into the rule).
 * @return true; false if @p target is null, the rule table is full, or the port+proto is already bound.
 */
bool pc_iface_bridge_publish(uint8_t listener_id, uint16_t port, BridgeProto proto, const BridgeTarget *target);

/** @brief Clear all listener bindings and rules (start from empty). */
void pc_iface_bridge_listener_reset(void);

#endif // PC_ENABLE_IFACE_BRIDGE

#endif // PROTOCORE_IFACE_BRIDGE_HW_H
