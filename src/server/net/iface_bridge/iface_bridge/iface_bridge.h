// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file iface_bridge.h
 * @brief User-defined address:port -> hardware-bus translation (PROTOCORE_ENABLE_IFACE_BRIDGE).
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

#include "shared/ip/ip.h" // the complete type a public struct below holds by value

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_IFACE_BRIDGE

PROTOCORE_BEGIN_DECLS

// PROTOCORE_IFACE_BRIDGE_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

#define PROTOCORE_BRIDGE_TXN_HDR 4 ///< transaction frame header: write_len(2) + read_len(2), big-endian

typedef enum PROTO_ENUM_PACKED
{
    BRIDGE_BUS_UART = 0,
    BRIDGE_BUS_SPI = 1,
    BRIDGE_BUS_I2C = 2
} BridgeBus;

typedef enum PROTO_ENUM_PACKED
{
    BRIDGE_MODE_STREAM = 0,     ///< raw bidirectional passthrough (UART)
    BRIDGE_MODE_TRANSACTION = 1 ///< framed write-then-read (SPI / I2C; also usable for UART)
} BridgeMode;

typedef enum PROTO_ENUM_PACKED
{
    BRIDGE_PROTO_TCP = 0,
    BRIDGE_PROTO_UDP = 1
} BridgeProto;

typedef struct BridgeTarget
{
    BridgeBus bus;
    BridgeMode mode;
    uint8_t unit;      ///< UART port # / SPI host # / I2C bus #
    uint16_t addr_cs;  ///< I2C 7-bit address, or SPI chip-select GPIO
    uint32_t rate;     ///< UART baud, or SPI/I2C clock (Hz)
    uint8_t spi_mode;  ///< SPI mode 0..3 (SPI only)
    uint8_t bit_order; ///< 0 = MSB-first, 1 = LSB-first (SPI only)
} BridgeTarget;

typedef struct
{
    protocore_ip listen_ip; ///< bind address (x.x.x.x / [v6]); family PROTOCORE_IP_NONE = any interface
    uint16_t listen_port;   ///< nnnn
    BridgeProto proto;      ///< TCP or UDP
    BridgeTarget target;
    proto_bool used;
} BridgeRule;
/** @brief What add takes: rule. */
typedef struct
{
    const BridgeRule *rule;
} IfaceBridgeAddArgs;
/** @brief What map takes: ip, port, proto, target. */
typedef struct
{
    const char *ip;
    uint16_t port;
    BridgeProto proto;
    const BridgeTarget *target;
} IfaceBridgeMapArgs;
/** @brief What find takes: port, proto. */
typedef struct
{
    uint16_t port;
    BridgeProto proto;
} IfaceBridgeFindArgs;
/** @brief What txn_parse takes: buf, len, write_len, read_len, ... */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    uint16_t *write_len;
    uint16_t *read_len;
    const uint8_t **write_data;
} IfaceBridgeTxnParseArgs;
/** @brief What txn_build takes: out, cap, write_data, write_len, ... */
typedef struct
{
    uint8_t *out;
    size_t cap;
    const uint8_t *write_data;
    uint16_t write_len;
    uint16_t read_len;
} IfaceBridgeTxnBuildArgs;
/**
 * @brief User-defined address:port -> hardware-bus translation (PROTOCORE_ENABLE_IFACE_BRIDGE). A configurable "device
 * ...
 *
 * A caller sets the members a call takes, invokes it through ::IfaceBridge with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   IfaceBridge.clear(work);
 *
 * @var IfaceBridgeNs::add_args  what add takes: rule
 * @var IfaceBridgeNs::map_args  what map takes: ip, port, proto, target
 * @var IfaceBridgeNs::find_args  what find takes: port, proto
 * @var IfaceBridgeNs::txn_parse_args  what txn_parse takes: buf, len, write_len, read_len,
 * @var IfaceBridgeNs::txn_build_args  what txn_build takes: out, cap, write_data, write_len,
 * @var IfaceBridgeNs::ok  a call's true/false outcome
 * @var IfaceBridgeNs::rule  what a call reports
 * @var IfaceBridgeNs::u8  what a call reports
 * @var IfaceBridgeNs::n  bytes written, or 0 if out is too small
 * @var IfaceBridgeNs::clear  clear
 * @var IfaceBridgeNs::add  add
 * @var IfaceBridgeNs::map  map
 * @var IfaceBridgeNs::find  find
 * @var IfaceBridgeNs::count  count
 * @var IfaceBridgeNs::txn_parse  parse a transaction request from a socket buffer. On a complete ...
 * @var IfaceBridgeNs::txn_build  build a transaction request frame (header + write payload) into out
 *
 * @c work is PROTOCORE_IFACE_BRIDGE_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    IfaceBridgeAddArgs add_args;
    IfaceBridgeMapArgs map_args;
    IfaceBridgeFindArgs find_args;
    IfaceBridgeTxnParseArgs txn_parse_args;
    IfaceBridgeTxnBuildArgs txn_build_args;
    proto_bool ok;
    const BridgeRule *rule;
    uint8_t u8;
    size_t n;
} IfaceBridgeVars;

/** @brief The operands and the outcome. */
extern IfaceBridgeVars IfaceBridgeV;

/** @brief The entries. */
typedef struct
{
    void (*const clear)(uint8_t *restrict work);
    void (*const add)(uint8_t *restrict work);
    void (*const map)(uint8_t *restrict work);
    void (*const find)(uint8_t *restrict work);
    void (*const count)(uint8_t *restrict work);
    void (*const txn_parse)(uint8_t *restrict work);
    void (*const txn_build)(uint8_t *restrict work);
} IfaceBridgeNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in IfaceBridgeV or a region of the borrow at a fixed offset.
void protocore_iface_bridge_clear(uint8_t *restrict work);
void protocore_iface_bridge_add(uint8_t *restrict work);
void protocore_iface_bridge_map(uint8_t *restrict work);
void protocore_iface_bridge_find(uint8_t *restrict work);
void protocore_iface_bridge_count(uint8_t *restrict work);
void protocore_iface_bridge_txn_parse(uint8_t *restrict work);
void protocore_iface_bridge_txn_build(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `IfaceBridge.clear(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const IfaceBridgeNs IfaceBridge __attribute__((unused)) = {
    .clear = protocore_iface_bridge_clear,
    .add = protocore_iface_bridge_add,
    .map = protocore_iface_bridge_map,
    .find = protocore_iface_bridge_find,
    .count = protocore_iface_bridge_count,
    .txn_parse = protocore_iface_bridge_txn_parse,
    .txn_build = protocore_iface_bridge_txn_build,
};

/**
 * @brief The PROTOCORE_IFACE_BRIDGE_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_iface_bridge_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_IFACE_BRIDGE

#endif // PROTOCORE_IFACE_BRIDGE_H
