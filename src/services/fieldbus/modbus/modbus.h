// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file modbus.h
 * @brief Zero-heap Modbus TCP slave/server (Modbus Application Protocol v1.1b3).
 *
 * Split like the CoAP/SNMP services into a pure, host-testable core and an
 * ESP32-only TCP transport:
 *
 *  - protocore_modbus_process_adu() takes a complete Modbus TCP ADU (MBAP header + PDU) and
 *    produces the response ADU in a caller buffer - no sockets, no heap. It is
 *    unit-tested on the host (env:native_modbus).
 *  - protocore_modbus_rx() is the ProtoConn::PROTO_MODBUS data handler dispatched by the session layer;
 *    it frames ADUs out of the rx ring and feeds them through
 *    protocore_modbus_process_adu(). The slave keeps no per-connection state (a partial
 *    frame waits in the rx ring), so no accept/close hooks are needed. Open the
 *    port with listen(502, ProtoConn::PROTO_MODBUS).
 *
 * The data model is four fixed BSS tables (coils, discrete inputs, holding
 * registers, input registers). The application reads and writes them with the
 * accessors below; a write arriving from a client also fires protocore_modbus_on_write().
 *
 * Supported function codes: 0x01 Read Coils, 0x02 Read Discrete Inputs,
 * 0x03 Read Holding Registers, 0x04 Read Input Registers, 0x05 Write Single Coil,
 * 0x06 Write Single Register, 0x0F Write Multiple Coils, 0x10 Write Multiple
 * Registers. Any other function code returns exception 0x01 (Illegal Function).
 *
 * Modbus has no authentication or encryption - run it only on a trusted network.
 */

#ifndef PROTOCORE_MODBUS_H
#define PROTOCORE_MODBUS_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_NEED_MODBUS

PROTOCORE_BEGIN_DECLS

// PROTOCORE_MODBUS_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

/** @brief Largest Modbus TCP ADU (7-byte MBAP + 253-byte PDU). */
#define MODBUS_ADU_MAX 260

/** @brief Modbus function codes (Modbus Application Protocol §6). */
typedef enum PROTO_ENUM_PACKED
{
    MODBUS_FC_READ_COILS = 0x01,
    MODBUS_FC_READ_DISCRETE_INPUTS = 0x02,
    MODBUS_FC_READ_HOLDING_REGS = 0x03,
    MODBUS_FC_READ_INPUT_REGS = 0x04,
    MODBUS_FC_WRITE_SINGLE_COIL = 0x05,
    MODBUS_FC_WRITE_SINGLE_REG = 0x06,
    MODBUS_FC_WRITE_MULTIPLE_COILS = 0x0F,
    MODBUS_FC_WRITE_MULTIPLE_REGS = 0x10,
    MODBUS_FC_MASK_WRITE_REG = 0x16,
    MODBUS_FC_READ_WRITE_MULTIPLE_REGS = 0x17,
} ModbusFunction;

/** @brief Modbus exception codes (Modbus Application Protocol §7). */
typedef enum PROTO_ENUM_PACKED
{
    MODBUS_EX_ILLEGAL_FUNCTION = 0x01,
    MODBUS_EX_ILLEGAL_DATA_ADDRESS = 0x02,
    MODBUS_EX_ILLEGAL_DATA_VALUE = 0x03,
    MODBUS_EX_SERVER_FAILURE = 0x04,
} ModbusException;

/**
 * @brief Notified after a client write is applied to the data model.
 *
 * @param fc    the write function code (5, 6, 0x0F, or 0x10).
 * @param start first coil/register address written.
 * @param count number of coils/registers written.
 */
typedef void (*ModbusWriteCb)(uint8_t fc, uint16_t start, uint16_t count);

/** @brief What on_write takes: cb. */
typedef struct
{
    ModbusWriteCb cb;
} ModbusOnWriteArgs;

/** @brief What get_coil takes: addr. */
typedef struct
{
    uint16_t addr;
} ModbusGetCoilArgs;

/** @brief What set_coil takes: addr, on. */
typedef struct
{
    uint16_t addr;
    proto_bool on;
} ModbusSetCoilArgs;

/** @brief What get_discrete_input takes: addr. */
typedef struct
{
    uint16_t addr;
} ModbusGetDiscreteInputArgs;

/** @brief What set_discrete_input takes: addr, on. */
typedef struct
{
    uint16_t addr;
    proto_bool on;
} ModbusSetDiscreteInputArgs;

/** @brief What get_holding_reg takes: addr. */
typedef struct
{
    uint16_t addr;
} ModbusGetHoldingRegArgs;

/** @brief What set_holding_reg takes: addr, value. */
typedef struct
{
    uint16_t addr;
    uint16_t value;
} ModbusSetHoldingRegArgs;

/** @brief What get_input_reg takes: addr. */
typedef struct
{
    uint16_t addr;
} ModbusGetInputRegArgs;

/** @brief What set_input_reg takes: addr, value. */
typedef struct
{
    uint16_t addr;
    uint16_t value;
} ModbusSetInputRegArgs;

/** @brief What process_adu takes: req, req_len, resp, ... */
typedef struct
{
    const uint8_t *req;
    size_t req_len;
    uint8_t *resp;
    size_t protocore_resp_cap;
} ModbusProcessAduArgs;

#if PROTOCORE_ENABLE_MODBUS_RTU
/** @brief What rtu_process_adu takes: req, req_len, resp, ... */
typedef struct
{
    const uint8_t *req;
    size_t req_len;
    uint8_t *resp;
    size_t protocore_resp_cap;
    uint8_t my_addr;
} ModbusRtuProcessAduArgs;
#endif

#if PROTOCORE_HAS_NET_STACK
/** @brief What rx takes: slot. */
typedef struct
{
    uint8_t slot;
} ModbusRxArgs;
#endif

/** @brief The Layer 5 dispatch record; server/core/proto_handler.h defines it. */
struct ProtoHandler;

/**
 * @brief Zero-heap Modbus TCP slave/server (Modbus Application Protocol v1.1b3).
 *
 * A caller sets the members a call takes, invokes it through ::Modbus with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Modbus.server_init(work);
 *
 * @var ModbusNs::on_write_args  what on_write takes: cb
 * @var ModbusNs::get_coil_args  what get_coil takes: addr
 * @var ModbusNs::set_coil_args  what set_coil takes: addr, on
 * @var ModbusNs::get_discrete_input_args  what get_discrete_input takes: addr
 * @var ModbusNs::set_discrete_input_args  what set_discrete_input takes: addr, on
 * @var ModbusNs::get_holding_reg_args  what get_holding_reg takes: addr
 * @var ModbusNs::set_holding_reg_args  what set_holding_reg takes: addr, value
 * @var ModbusNs::get_input_reg_args  what get_input_reg takes: addr
 * @var ModbusNs::set_input_reg_args  what set_input_reg takes: addr, value
 * @var ModbusNs::process_adu_args  what process_adu takes: req, req_len, resp,
 * @var ModbusNs::rtu_process_adu_args  what rtu_process_adu takes: req, req_len, resp,
 * @var ModbusNs::rx_args  what rx takes: slot
 * @var ModbusNs::ok  a call's true/false outcome
 * @var ModbusNs::value  the value a call reports
 * @var ModbusNs::n  number of response bytes written, or 0 to send nothing
 * @var ModbusNs::ptr  the pointer a call reports
 * @var ModbusNs::server_init  zero the entire data model and clear the write callback
 * @var ModbusNs::on_write  register a callback invoked after each client write (nullable)
 * @var ModbusNs::get_coil  get_coil
 * @var ModbusNs::set_coil  set_coil
 * @var ModbusNs::get_discrete_input  get_discrete_input
 * @var ModbusNs::set_discrete_input  set_discrete_input
 * @var ModbusNs::get_holding_reg  get_holding_reg
 * @var ModbusNs::set_holding_reg  set_holding_reg
 * @var ModbusNs::get_input_reg  get_input_reg
 * @var ModbusNs::set_input_reg  set_input_reg
 * @var ModbusNs::process_adu  process one Modbus TCP ADU and build the response ADU. Parses the ...
 * @var ModbusNs::rtu_process_adu  process one complete Modbus RTU ADU (`[addr][PDU][CRC16]`) for slave
 * @var ModbusNs::rx  frame and process received Modbus ADUs for the connection on slot
 * @var ModbusNs::handler  handler
 *
 * @c work is PROTOCORE_MODBUS_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    ModbusOnWriteArgs on_write_args;
    ModbusGetCoilArgs get_coil_args;
    ModbusSetCoilArgs set_coil_args;
    ModbusGetDiscreteInputArgs get_discrete_input_args;
    ModbusSetDiscreteInputArgs set_discrete_input_args;
    ModbusGetHoldingRegArgs get_holding_reg_args;
    ModbusSetHoldingRegArgs set_holding_reg_args;
    ModbusGetInputRegArgs get_input_reg_args;
    ModbusSetInputRegArgs set_input_reg_args;
    ModbusProcessAduArgs process_adu_args;
#if PROTOCORE_ENABLE_MODBUS_RTU
    ModbusRtuProcessAduArgs rtu_process_adu_args;
#endif
#if PROTOCORE_HAS_NET_STACK
    ModbusRxArgs rx_args;
#endif

    proto_bool ok;
    uint16_t value;
    size_t n;
    const struct ProtoHandler *ptr;

    void (*const server_init)(uint8_t *restrict work);
    void (*const on_write)(uint8_t *restrict work);
    void (*const get_coil)(uint8_t *restrict work);
    void (*const set_coil)(uint8_t *restrict work);
    void (*const get_discrete_input)(uint8_t *restrict work);
    void (*const set_discrete_input)(uint8_t *restrict work);
    void (*const get_holding_reg)(uint8_t *restrict work);
    void (*const set_holding_reg)(uint8_t *restrict work);
    void (*const get_input_reg)(uint8_t *restrict work);
    void (*const set_input_reg)(uint8_t *restrict work);
    void (*const process_adu)(uint8_t *restrict work);
#if PROTOCORE_ENABLE_MODBUS_RTU
    void (*const rtu_process_adu)(uint8_t *restrict work);
#endif
#if PROTOCORE_HAS_NET_STACK
    void (*const rx)(uint8_t *restrict work);
#endif
    void (*const handler)(uint8_t *restrict work);
} ModbusNs;

/** @brief The one symbol this module exports. */
extern ModbusNs Modbus;

/**
 * @brief The PROTOCORE_MODBUS_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span, or NULL while the pool was short - which every entry refuses.
 */
uint8_t *protocore_modbus_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_NEED_MODBUS

#endif // PROTOCORE_MODBUS_H
