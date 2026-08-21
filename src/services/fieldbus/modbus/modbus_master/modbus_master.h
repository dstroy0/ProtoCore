// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_MODBUS_MASTER_H
#define PROTOCORE_MODBUS_MASTER_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file modbus_master.h
 * @brief Modbus TCP master codec + register scanner (PROTOCORE_ENABLE_MODBUS_MASTER).
 *
 * The master/client side of Modbus: build a read-request ADU (MBAP header + PDU)
 * and parse the slave's response into register values, so an application can poll
 * or auto-discover a slave's registers. Pure - no sockets, no heap - so it is
 * host-tested as a full round-trip against the slave codec (Modbus.process_adu).
 * The app supplies the transport (send the ADU, receive the reply).
 *
 * Auto-discovery pattern: walk the address space one read at a time; a register
 * exists where the response parses without a Modbus exception.
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_MODBUS_MASTER_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    size_t (*build_read)(uint8_t *, uint8_t, uint16_t, uint8_t, uint16_t, uint16_t, uint8_t *, size_t);
    int (*parse_response)(uint8_t *, const uint8_t *, size_t, uint16_t *, size_t, uint8_t *);
    size_t (*build_read_bits)(uint8_t *, uint8_t, uint16_t, uint8_t, uint16_t, uint16_t, uint8_t *, size_t);
    int (*parse_read_bits_response)(uint8_t *, const uint8_t *, size_t, uint16_t, uint8_t *, size_t, uint8_t *);
    size_t (*build_write_single_coil)(uint8_t *, uint16_t, uint8_t, uint16_t, proto_bool, uint8_t *, size_t);
    size_t (*build_write_multiple_coils)(uint8_t *, uint16_t, uint8_t, uint16_t, const uint8_t *, uint16_t, uint8_t *,
                                         size_t);
    size_t (*build_write_single)(uint8_t *, uint16_t, uint8_t, uint16_t, uint16_t, uint8_t *, size_t);
    size_t (*build_write_multiple)(uint8_t *, uint16_t, uint8_t, uint16_t, const uint16_t *, uint16_t, uint8_t *,
                                   size_t);
    int (*parse_write_response)(uint8_t *, const uint8_t *, size_t, uint16_t *, uint8_t *);
    size_t (*build_mask_write)(uint8_t *, uint16_t, uint8_t, uint16_t, uint16_t, uint16_t, uint8_t *, size_t);
    size_t (*build_read_write_multiple)(uint8_t *, uint16_t, uint8_t, uint16_t, uint16_t, uint16_t, const uint16_t *,
                                        uint16_t, uint8_t *, size_t);
    int (*parse_mask_write_response)(uint8_t *, const uint8_t *, size_t, uint16_t *, uint16_t *, uint16_t *, uint8_t *);
} ModbusMasterNs;
PROTOCORE_NS_LAYOUT(ModbusMasterNs, build_read, parse_response, build_read_bits, parse_read_bits_response,
                    build_write_single_coil, build_write_multiple_coils, build_write_single, build_write_multiple,
                    parse_write_response, build_mask_write, build_read_write_multiple, parse_mask_write_response);

/**
 * @brief Build a read-request ADU (FC 0x03 holding or 0x04 input registers).
 * @param work PROTOCORE_MODBUS_MASTER_BORROW bytes the caller took. Not held past the call.
 * @param fc MODBUS_FC_READ_HOLDING_REGS (0x03) or MODBUS_FC_READ_INPUT_REGS (0x04)
 * @param txid transaction id echoed by the slave (caller's correlation token)
 * @param unit unit / slave id
 * @param start first register address
 * @param count number of registers (1..125)
 * @param out destination buffer
 * @param cap destination capacity (>= 12)
 * @return The size_t.
 */
size_t protocore_modbus_master_build_read(uint8_t *work, uint8_t fc, uint16_t txid, uint8_t unit, uint16_t start,
                                          uint16_t count, uint8_t *out, size_t cap);
/**
 * @brief Parse a read-response ADU into register values.
 * @param work PROTOCORE_MODBUS_MASTER_BORROW bytes the caller took. Not held past the call.
 * @param adu response bytes (MBAP + PDU)
 * @param len response length
 * @param regs_out destination for parsed 16-bit register values
 * @param max_regs capacity of regs_out
 * @param exception_out set to the Modbus exception code if the slave returned one (then the function returns 0
 * @return The int.
 */
int protocore_modbus_master_parse_response(uint8_t *work, const uint8_t *adu, size_t len, uint16_t *regs_out,
                                           size_t max_regs, uint8_t *exception_out);
/**
 * @brief Build a read-bits request ADU (FC 0x01 coils or 0x02 discrete .
 * @param work PROTOCORE_MODBUS_MASTER_BORROW bytes the caller took. Not held past the call.
 * @param fc MODBUS_FC_READ_COILS (0x01) or ::MODBUS_FC_READ_DISCRETE_INPUTS (0x02)
 * @param txid transaction id echoed by the slave
 * @param unit unit / slave id
 * @param start first bit address
 * @param count number of bits (1..2000)
 * @param out destination buffer
 * @param cap destination capacity (>= 12)
 * @return The size_t.
 */
size_t protocore_modbus_master_build_read_bits(uint8_t *work, uint8_t fc, uint16_t txid, uint8_t unit, uint16_t start,
                                               uint16_t count, uint8_t *out, size_t cap);
/**
 * @brief Parse a read-bits response ADU (FC 0x01 / 0x02) into one byte (0/1).
 * @param work PROTOCORE_MODBUS_MASTER_BORROW bytes the caller took. Not held past the call.
 * @param adu Adu
 * @param len Len
 * @param count the number of bits requested (1..2000)
 * @param bits_out destination for count unpacked bits (nullable to just validate)
 * @param max_bits capacity of bits_out
 * @param exception_out set to the Modbus exception code if the slave returned one (then 0 is returned)
 * @return The int.
 */
int protocore_modbus_master_parse_read_bits_response(uint8_t *work, const uint8_t *adu, size_t len, uint16_t count,
                                                     uint8_t *bits_out, size_t max_bits, uint8_t *exception_out);
/**
 * @brief Build a Write Single Coil request ADU (FC 0x05).
 * @param work PROTOCORE_MODBUS_MASTER_BORROW bytes the caller took. Not held past the call.
 * @param txid Txid
 * @param unit Unit
 * @param addr Addr
 * @param on the coil value; encoded on the wire as 0xFF00 (on) or 0x0000 (off) per the Modbus spec
 * @param out Out
 * @param cap destination capacity (>= 12)
 * @return The size_t.
 */
size_t protocore_modbus_master_build_write_single_coil(uint8_t *work, uint16_t txid, uint8_t unit, uint16_t addr,
                                                       proto_bool on, uint8_t *out, size_t cap);
/**
 * @brief Build a Write Multiple Coils request ADU (FC 0x0F).
 * @param work PROTOCORE_MODBUS_MASTER_BORROW bytes the caller took. Not held past the call.
 * @param txid Txid
 * @param unit Unit
 * @param start Start
 * @param bits one byte (0/1) per coil to write; packed LSB-first into the wire bytes
 * @param count number of coils (1..1968)
 * @param out Out
 * @param cap destination capacity (>= 14 + ceil(count/8))
 * @return The size_t.
 */
size_t protocore_modbus_master_build_write_multiple_coils(uint8_t *work, uint16_t txid, uint8_t unit, uint16_t start,
                                                          const uint8_t *bits, uint16_t count, uint8_t *out,
                                                          size_t cap);
/**
 * @brief Build a Write Single Register request ADU (FC 0x06).
 * @param work PROTOCORE_MODBUS_MASTER_BORROW bytes the caller took. Not held past the call.
 * @param txid transaction id echoed by the slave
 * @param unit unit / slave id
 * @param addr register address
 * @param value 16-bit value to write
 * @param out destination buffer
 * @param cap destination capacity (>= 12)
 * @return The size_t.
 */
size_t protocore_modbus_master_build_write_single(uint8_t *work, uint16_t txid, uint8_t unit, uint16_t addr,
                                                  uint16_t value, uint8_t *out, size_t cap);
/**
 * @brief Build a Write Multiple Registers request ADU (FC 0x10).
 * @param work PROTOCORE_MODBUS_MASTER_BORROW bytes the caller took. Not held past the call.
 * @param txid transaction id echoed by the slave
 * @param unit unit / slave id
 * @param start first register address
 * @param values the count register values
 * @param count number of registers (1..123)
 * @param out destination buffer
 * @param cap destination capacity (>= 13 + 2*count)
 * @return The size_t.
 */
size_t protocore_modbus_master_build_write_multiple(uint8_t *work, uint16_t txid, uint8_t unit, uint16_t start,
                                                    const uint16_t *values, uint16_t count, uint8_t *out, size_t cap);
/**
 * @brief Parse a write-response ADU (FC 0x05, 0x06, 0x0F, or 0x10). A normal .
 * @param work PROTOCORE_MODBUS_MASTER_BORROW bytes the caller took. Not held past the call.
 * @param adu response bytes (MBAP + PDU)
 * @param len response length
 * @param addr_out set to the echoed address / start (nullable)
 * @param exception_out set to the Modbus exception code if the slave returned one (then 0 is returned)
 * @return The int.
 */
int protocore_modbus_master_parse_write_response(uint8_t *work, const uint8_t *adu, size_t len, uint16_t *addr_out,
                                                 uint8_t *exception_out);
/**
 * @brief Build a Mask Write Register request ADU (FC 0x16). The slave .
 * @param work PROTOCORE_MODBUS_MASTER_BORROW bytes the caller took. Not held past the call.
 * @param txid Txid
 * @param unit Unit
 * @param addr Addr
 * @param and_mask And mask
 * @param or_mask Or mask
 * @param out Out
 * @param cap destination capacity (>= 14)
 * @return The size_t.
 */
size_t protocore_modbus_master_build_mask_write(uint8_t *work, uint16_t txid, uint8_t unit, uint16_t addr,
                                                uint16_t and_mask, uint16_t or_mask, uint8_t *out, size_t cap);
/**
 * @brief Build a Read/Write Multiple Registers request ADU (FC 0x17): write.
 * @param work PROTOCORE_MODBUS_MASTER_BORROW bytes the caller took. Not held past the call.
 * @param txid Txid
 * @param unit Unit
 * @param read_start / read_count the registers to read back (1..125)
 * @param read_count Read count
 * @param write_start / write_count the registers to write (1..121); values holds write_count words
 * @param values Values
 * @param write_count Write count
 * @param out Out
 * @param cap destination capacity (>= 17 + 2*write_count)
 * @return The size_t.
 */
size_t protocore_modbus_master_build_read_write_multiple(uint8_t *work, uint16_t txid, uint8_t unit,
                                                         uint16_t read_start, uint16_t read_count, uint16_t write_start,
                                                         const uint16_t *values, uint16_t write_count, uint8_t *out,
                                                         size_t cap);
/**
 * @brief Parse a Mask Write Register response (FC 0x16), which echoes the .
 * @param work PROTOCORE_MODBUS_MASTER_BORROW bytes the caller took. Not held past the call.
 * @param adu Adu
 * @param len Len
 * @param addr_out / and_out / or_out receive the echoed fields (each nullable)
 * @param and_out And out
 * @param or_out Or out
 * @param exception_out set to the Modbus exception code if the slave returned one
 * @return The int.
 */
int protocore_modbus_master_parse_mask_write_response(uint8_t *work, const uint8_t *adu, size_t len, uint16_t *addr_out,
                                                      uint16_t *and_out, uint16_t *or_out, uint8_t *exception_out);

/** @brief Module namespace. */
PROTOCORE_NS ModbusMasterNs ModbusMaster PROTOCORE_UNUSED = {
    .build_read = protocore_modbus_master_build_read,
    .parse_response = protocore_modbus_master_parse_response,
    .build_read_bits = protocore_modbus_master_build_read_bits,
    .parse_read_bits_response = protocore_modbus_master_parse_read_bits_response,
    .build_write_single_coil = protocore_modbus_master_build_write_single_coil,
    .build_write_multiple_coils = protocore_modbus_master_build_write_multiple_coils,
    .build_write_single = protocore_modbus_master_build_write_single,
    .build_write_multiple = protocore_modbus_master_build_write_multiple,
    .parse_write_response = protocore_modbus_master_parse_write_response,
    .build_mask_write = protocore_modbus_master_build_mask_write,
    .build_read_write_multiple = protocore_modbus_master_build_read_write_multiple,
    .parse_mask_write_response = protocore_modbus_master_parse_mask_write_response};

PROTOCORE_END_DECLS

#endif // PROTOCORE_MODBUS_MASTER_H
