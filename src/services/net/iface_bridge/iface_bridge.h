// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file iface_bridge.h
 * @brief User-defined address:port -> hardware-bus translation (PC_ENABLE_IFACE_BRIDGE).
 *
 * A configurable "device server": the application registers rules mapping a listen address:port (plus
 * TCP/UDP) to a hardware endpoint - a UART, an SPI chip-select, or an I2C address - so a network client
 * talking to `x.x.x.x:nnnn` is transparently bridged to that bus. Two payload models:
 *
 *   - STREAM (UART): raw bidirectional passthrough. Socket bytes are written to the UART and UART bytes
 *     flow back to the socket, with no framing (a classic serial-device server / ser2net).
 *   - TRANSACTION (SPI / I2C, also usable for UART): the socket carries framed write-then-read
 *     transactions, which is what master-initiated buses need. Each request frame is
 *         uint16 write_len (big-endian) || uint16 read_len (big-endian) || write_bytes[write_len]
 *     and the reply is the read_len bytes clocked/read back. The bus address (I2C 7-bit addr) or
 *     chip-select (SPI CS gpio) + clock/mode come from the rule's target, so the frame stays generic.
 *
 * This header is the pure, host-tested core: the fixed-capacity rule table (zero heap) and the
 * transaction frame codec. The actual bus I/O (uart.h / spi.h / i2c.h) and the PROTO_BRIDGE listener are
 * the bus step (iface_bridge_hw.*), kept separate exactly like the peripheral services.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_IFACE_BRIDGE_H
#define PROTOCORE_IFACE_BRIDGE_H

#include "protocore_config.h"

#if PC_ENABLE_IFACE_BRIDGE

#include "shared_primitives/ip.h" // pc_ip (carry the full bind address, never a flattened one)

// PC_BRIDGE_MAX_RULES is defined in protocore_config.h (the config owner).

#define PC_BRIDGE_TXN_HDR 4 ///< transaction frame header: write_len(2) + read_len(2), big-endian

/// Which hardware bus a rule targets.
typedef enum PROTO_ENUM_PACKED
{
    BRIDGE_BUS_UART = 0,
    BRIDGE_BUS_SPI = 1,
    BRIDGE_BUS_I2C = 2
} BridgeBus;

/// How socket bytes map to bus activity.
typedef enum PROTO_ENUM_PACKED
{
    BRIDGE_MODE_STREAM = 0,     ///< raw bidirectional passthrough (UART)
    BRIDGE_MODE_TRANSACTION = 1 ///< framed write-then-read (SPI / I2C; also usable for UART)
} BridgeMode;

/// The transport a rule listens on (matches the value stored on the wire; kept generic to avoid a hard
/// dependency on ConnProto here in the pure core).
typedef enum PROTO_ENUM_PACKED
{
    BRIDGE_PROTO_TCP = 0,
    BRIDGE_PROTO_UDP = 1
} BridgeProto;

/// One hardware endpoint a network port is bridged to.
typedef struct
{
    BridgeBus bus;
    BridgeMode mode;
    uint8_t unit;      ///< UART port # / SPI host # / I2C bus #
    uint16_t addr_cs;  ///< I2C 7-bit address, or SPI chip-select GPIO
    uint32_t rate;     ///< UART baud, or SPI/I2C clock (Hz)
    uint8_t spi_mode;  ///< SPI mode 0..3 (SPI only)
    uint8_t bit_order; ///< 0 = MSB-first, 1 = LSB-first (SPI only)
} BridgeTarget;

/// A single address:port -> bus mapping.
typedef struct
{
    pc_ip listen_ip;      ///< bind address (x.x.x.x / [v6]); family PC_IP_NONE = any interface
    uint16_t listen_port; ///< nnnn
    BridgeProto proto;    ///< TCP or UDP
    BridgeTarget target;
    proto_bool used;
} BridgeRule;

// ---------------------------------------------------------------------------------------------
// Rule table (zero heap; register before begin()). Pure - host-testable.
// ---------------------------------------------------------------------------------------------

/// Remove all rules.
void pc_iface_bridge_clear();

/// Register a rule. Returns false if the table is full or a rule already binds the same port+proto.
proto_bool pc_iface_bridge_add(const BridgeRule *rule);

/// Convenience: build + add a rule in one call. @p ip may be NULL for "any interface". Returns false on
/// a bad address, a full table, or a duplicate port+proto.
proto_bool pc_iface_bridge_map(const char *ip, uint16_t port, BridgeProto proto, const BridgeTarget *target);

/// Find the rule bound to @p port + @p proto, or NULL. This is the listener-dispatch lookup.
const BridgeRule *pc_iface_bridge_find(uint16_t port, BridgeProto proto);

/// Number of registered rules.
uint8_t pc_iface_bridge_count();

// ---------------------------------------------------------------------------------------------
// Transaction frame codec (pure). write_len / read_len are big-endian.
// ---------------------------------------------------------------------------------------------

/**
 * @brief Parse a transaction request from a socket buffer.
 *
 * On a complete frame, sets @p write_len / @p read_len, points @p write_data at the write payload inside
 * @p buf, and returns the total bytes the frame occupies (header + write payload). Returns 0 when @p buf
 * does not yet hold the whole frame (the caller should read more), so a partial header or a partial write
 * payload both yield 0.
 */
size_t pc_iface_bridge_txn_parse(const uint8_t *buf, size_t len, uint16_t *write_len, uint16_t *read_len,
                                 const uint8_t **write_data);

/**
 * @brief Build a transaction request frame (header + write payload) into @p out.
 * @return bytes written, or 0 if @p out is too small.
 */
size_t pc_iface_bridge_txn_build(uint8_t *out, size_t cap, const uint8_t *write_data, uint16_t write_len,
                                 uint16_t read_len);

#endif // PC_ENABLE_IFACE_BRIDGE

#endif // PROTOCORE_IFACE_BRIDGE_H
