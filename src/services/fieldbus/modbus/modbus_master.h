// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_modbus_master.h
 * @brief Modbus TCP master codec + register scanner (PC_ENABLE_MODBUS_MASTER).
 *
 * The master/client side of Modbus: build a read-request ADU (MBAP header + PDU)
 * and parse the slave's response into register values, so an application can poll
 * or auto-discover a slave's registers. Pure - no sockets, no heap - so it is
 * host-tested as a full round-trip against the slave codec (pc_modbus_process_adu).
 * The app supplies the transport (send the ADU, receive the reply).
 *
 * Auto-discovery pattern: walk the address space one read at a time; a register
 * exists where the response parses without a Modbus exception.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MODBUS_MASTER_H
#define PROTOCORE_MODBUS_MASTER_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_MODBUS_MASTER

/**
 * @brief Build a read-request ADU (FC 0x03 holding or 0x04 input registers).
 *
 * @param fc     MODBUS_FC_READ_HOLDING_REGS (0x03) or MODBUS_FC_READ_INPUT_REGS (0x04).
 * @param txid   transaction id echoed by the slave (caller's correlation token).
 * @param unit   unit / slave id.
 * @param start  first register address.
 * @param count  number of registers (1..125).
 * @param out    destination buffer.
 * @param cap    destination capacity (>= 12).
 * @return bytes written (12), or 0 on a bad argument / too-small buffer.
 */
size_t pc_modbus_build_read(uint8_t fc, uint16_t txid, uint8_t unit, uint16_t start, uint16_t count, uint8_t *out,
                            size_t cap);

/**
 * @brief Parse a read-response ADU into register values.
 *
 * @param adu       response bytes (MBAP + PDU).
 * @param len       response length.
 * @param regs_out  destination for parsed 16-bit register values.
 * @param max_regs  capacity of @p regs_out.
 * @param exception_out  set to the Modbus exception code if the slave returned one
 *                       (then the function returns 0 registers); 0 otherwise.
 * @return number of registers parsed (>= 0), or -1 on a malformed/short frame.
 */
int pc_modbus_parse_response(const uint8_t *adu, size_t len, uint16_t *regs_out, size_t max_regs,
                             uint8_t *exception_out);

/**
 * @brief Build a read-bits request ADU (FC 0x01 coils or 0x02 discrete inputs).
 *
 * @param fc     MODBUS_FC_READ_COILS (0x01) or ::MODBUS_FC_READ_DISCRETE_INPUTS (0x02).
 * @param txid   transaction id echoed by the slave.
 * @param unit   unit / slave id.
 * @param start  first bit address.
 * @param count  number of bits (1..2000).
 * @param out    destination buffer.
 * @param cap    destination capacity (>= 12).
 * @return bytes written (12), or 0 on a bad argument / too-small buffer.
 */
size_t pc_modbus_build_read_bits(uint8_t fc, uint16_t txid, uint8_t unit, uint16_t start, uint16_t count, uint8_t *out,
                                 size_t cap);

/**
 * @brief Parse a read-bits response ADU (FC 0x01 / 0x02) into one byte (0/1) per bit.
 *
 * The response carries only a byte count, so the caller passes the @p count it requested; the byte count
 * must equal ceil(count/8). Each requested bit is unpacked LSB-first into @p bits_out as 0 or 1.
 * @param count      the number of bits requested (1..2000).
 * @param bits_out   destination for @p count unpacked bits (nullable to just validate).
 * @param max_bits   capacity of @p bits_out.
 * @param exception_out  set to the Modbus exception code if the slave returned one (then 0 is returned).
 * @return the number of bits unpacked (@p count, capped by @p max_bits), 0 on an exception, or -1 on a
 *         malformed / short frame or a byte count that disagrees with @p count.
 */
int pc_modbus_parse_read_bits_response(const uint8_t *adu, size_t len, uint16_t count, uint8_t *bits_out,
                                       size_t max_bits, uint8_t *exception_out);

/**
 * @brief Build a Write Single Coil request ADU (FC 0x05).
 *
 * @param on   the coil value; encoded on the wire as 0xFF00 (on) or 0x0000 (off) per the Modbus spec.
 * @param cap  destination capacity (>= 12).
 * @return bytes written (12), or 0 on a null / too-small buffer.
 */
size_t pc_modbus_build_write_single_coil(uint16_t txid, uint8_t unit, uint16_t addr, proto_bool on, uint8_t *out,
                                         size_t cap);

/**
 * @brief Build a Write Multiple Coils request ADU (FC 0x0F).
 *
 * @param bits   one byte (0/1) per coil to write; packed LSB-first into the wire bytes.
 * @param count  number of coils (1..1968).
 * @param cap    destination capacity (>= 14 + ceil(count/8)).
 * @return bytes written, or 0 on a bad argument / too-small buffer.
 */
size_t pc_modbus_build_write_multiple_coils(uint16_t txid, uint8_t unit, uint16_t start, const uint8_t *bits,
                                            uint16_t count, uint8_t *out, size_t cap);

/**
 * @brief Build a Write Single Register request ADU (FC 0x06).
 *
 * @param txid   transaction id echoed by the slave.
 * @param unit   unit / slave id.
 * @param addr   register address.
 * @param value  16-bit value to write.
 * @param out    destination buffer.
 * @param cap    destination capacity (>= 12).
 * @return bytes written (12), or 0 on a null / too-small buffer.
 */
size_t pc_modbus_build_write_single(uint16_t txid, uint8_t unit, uint16_t addr, uint16_t value, uint8_t *out,
                                    size_t cap);

/**
 * @brief Build a Write Multiple Registers request ADU (FC 0x10).
 *
 * @param txid    transaction id echoed by the slave.
 * @param unit    unit / slave id.
 * @param start   first register address.
 * @param values  the @p count register values.
 * @param count   number of registers (1..123).
 * @param out     destination buffer.
 * @param cap     destination capacity (>= 13 + 2*count).
 * @return bytes written (13 + 2*count), or 0 on a bad argument / too-small buffer.
 */
size_t pc_modbus_build_write_multiple(uint16_t txid, uint8_t unit, uint16_t start, const uint16_t *values,
                                      uint16_t count, uint8_t *out, size_t cap);

/**
 * @brief Parse a write-response ADU (FC 0x05, 0x06, 0x0F, or 0x10).
 *
 * A normal reply echoes the address and the written value (single: 0x05 / 0x06) or the start address and
 * the quantity written (multiple: 0x0F / 0x10) - the coil and register replies share a wire format.
 * @param adu       response bytes (MBAP + PDU).
 * @param len       response length.
 * @param addr_out  set to the echoed address / start (nullable).
 * @param exception_out  set to the Modbus exception code if the slave returned one (then 0 is returned).
 * @return number written (1 for a single 0x05 / 0x06, the count for a multiple 0x0F / 0x10), 0 on an
 *         exception, or -1 on a malformed / short frame.
 */
int pc_modbus_parse_write_response(const uint8_t *adu, size_t len, uint16_t *addr_out, uint8_t *exception_out);

/**
 * @brief Build a Mask Write Register request ADU (FC 0x16).
 *
 * The slave computes reg = (reg AND @p and_mask) OR (@p or_mask AND NOT @p and_mask), so a client can set
 * or clear individual bits of a register without a read-modify-write race.
 * @param cap  destination capacity (>= 14).
 * @return bytes written (14), or 0 on a null / too-small buffer.
 */
size_t pc_modbus_build_mask_write(uint16_t txid, uint8_t unit, uint16_t addr, uint16_t and_mask, uint16_t or_mask,
                                  uint8_t *out, size_t cap);

/**
 * @brief Build a Read/Write Multiple Registers request ADU (FC 0x17): write a span, then read a span, in
 *        one transaction (the write is applied before the read).
 * @param read_start / @p read_count   the registers to read back (1..125).
 * @param write_start / @p write_count the registers to write (1..121); @p values holds @p write_count words.
 * @param cap  destination capacity (>= 17 + 2*write_count).
 * @return bytes written, or 0 on a bad argument / too-small buffer.
 */
size_t pc_modbus_build_read_write_multiple(uint16_t txid, uint8_t unit, uint16_t read_start, uint16_t read_count,
                                           uint16_t write_start, const uint16_t *values, uint16_t write_count,
                                           uint8_t *out, size_t cap);

/**
 * @brief Parse a Mask Write Register response (FC 0x16), which echoes the address and both masks.
 * @param addr_out / @p and_out / @p or_out  receive the echoed fields (each nullable).
 * @param exception_out  set to the Modbus exception code if the slave returned one.
 * @return 1 on a normal echo, 0 on an exception, or -1 on a malformed / short frame.
 */
int pc_modbus_parse_mask_write_response(const uint8_t *adu, size_t len, uint16_t *addr_out, uint16_t *and_out,
                                        uint16_t *or_out, uint8_t *exception_out);

#endif // PC_ENABLE_MODBUS_MASTER

PROTO_END_DECLS

#endif // PROTOCORE_MODBUS_MASTER_H
