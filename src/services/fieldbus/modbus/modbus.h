// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file modbus.h
 * @brief Zero-heap Modbus TCP slave/server (Modbus Application Protocol v1.1b3).
 *
 * Split like the CoAP/SNMP services into a pure, host-testable core and an
 * ESP32-only TCP transport:
 *
 *  - pc_modbus_process_adu() takes a complete Modbus TCP ADU (MBAP header + PDU) and
 *    produces the response ADU in a caller buffer - no sockets, no heap. It is
 *    unit-tested on the host (env:native_modbus).
 *  - pc_modbus_rx() is the ConnProto::PROTO_MODBUS data handler dispatched by the session layer;
 *    it frames ADUs out of the rx ring and feeds them through
 *    pc_modbus_process_adu(). The slave keeps no per-connection state (a partial
 *    frame waits in the rx ring), so no accept/close hooks are needed. Open the
 *    port with listen(502, ConnProto::PROTO_MODBUS).
 *
 * The data model is four fixed BSS tables (coils, discrete inputs, holding
 * registers, input registers). The application reads and writes them with the
 * accessors below; a write arriving from a client also fires pc_modbus_on_write().
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

#include "protocore_config.h"

#if PC_NEED_MODBUS

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

/** @brief Largest Modbus TCP ADU (7-byte MBAP + 253-byte PDU). */
#define MODBUS_ADU_MAX 260

/**
 * @brief Notified after a client write is applied to the data model.
 *
 * @param fc    the write function code (5, 6, 0x0F, or 0x10).
 * @param start first coil/register address written.
 * @param count number of coils/registers written.
 */
typedef void (*ModbusWriteCb)(uint8_t fc, uint16_t start, uint16_t count);

// ---------------------------------------------------------------------------
// Data model
// ---------------------------------------------------------------------------

/** @brief Zero the entire data model and clear the write callback. */
void pc_modbus_server_init();

/** @brief Register a callback invoked after each client write (nullable). */
void pc_modbus_on_write(ModbusWriteCb cb);

proto_bool pc_modbus_get_coil(uint16_t addr);                    ///< Read a coil (false if out of range).
void pc_modbus_set_coil(uint16_t addr, proto_bool on);           ///< Set a coil (no-op if out of range).
proto_bool pc_modbus_get_discrete_input(uint16_t addr);          ///< Read a discrete input.
void pc_modbus_set_discrete_input(uint16_t addr, proto_bool on); ///< Set a discrete input (application side).
uint16_t pc_modbus_get_holding_reg(uint16_t addr);               ///< Read a holding register (0 if out of range).
void pc_modbus_set_holding_reg(uint16_t addr, uint16_t value);   ///< Set a holding register.
uint16_t pc_modbus_get_input_reg(uint16_t addr);                 ///< Read an input register.
void pc_modbus_set_input_reg(uint16_t addr, uint16_t value);     ///< Set an input register (application side).

// ---------------------------------------------------------------------------
// Core processing (host-testable; no sockets, no heap)
// ---------------------------------------------------------------------------

/**
 * @brief Process one Modbus TCP ADU and build the response ADU.
 *
 * Parses the MBAP header (Transaction/Protocol Id, Length, Unit Id), dispatches
 * the PDU against the data model, and writes the response ADU (echoing the
 * Transaction/Unit Id). A non-zero Protocol Id, a truncated/oversized frame, or a
 * length mismatch yields 0 (send nothing). An unsupported function or an invalid
 * address/value yields a Modbus exception response.
 *
 * @return number of response bytes written, or 0 to send nothing.
 */
size_t pc_modbus_process_adu(const uint8_t *req, size_t req_len, uint8_t *resp, size_t pc_resp_cap);

#if PC_ENABLE_MODBUS_RTU
/**
 * @brief Process one complete Modbus RTU ADU (`[addr][PDU][CRC16]`) for slave
 *        @p my_addr against the data model, writing the RTU response ADU.
 *
 * Validates the trailing CRC16-Modbus (drops the frame silently on mismatch) and
 * the unit address (frames for another slave are dropped); a broadcast
 * (address 0) is executed but draws no reply. Pure / host-tested; feed it a
 * complete frame from a UART/RS-485 driver (delimited by the 3.5-char idle gap).
 *
 * @return number of RTU response bytes written, or 0 to send nothing.
 */
size_t pc_modbus_rtu_process_adu(const uint8_t *req, size_t req_len, uint8_t *resp, size_t pc_resp_cap,
                                 uint8_t my_addr);
#endif

// ---------------------------------------------------------------------------
// TCP transport (ConnProto::PROTO_MODBUS data handler; ESP32-only)
// ---------------------------------------------------------------------------

/** @brief Frame and process received Modbus ADUs for the connection on @p slot. */
void pc_modbus_rx(uint8_t slot);

/** @brief The Modbus ProtoHandler (accessor; nullptr on host builds; installed by the builtins list). */
struct ProtoHandler;
const struct ProtoHandler *pc_modbus_proto_handler(void);

#endif // PC_NEED_MODBUS

#endif // PROTOCORE_MODBUS_H
