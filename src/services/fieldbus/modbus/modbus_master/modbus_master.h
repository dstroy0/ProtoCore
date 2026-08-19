// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_modbus_master.h
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
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MODBUS_MASTER_H
#define PROTOCORE_MODBUS_MASTER_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_MODBUS_MASTER

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief What build_read takes: fc, txid, unit, start, count, out, ... */
typedef struct
{
    uint8_t fc;     ///< MODBUS_FC_READ_HOLDING_REGS (0x03) or MODBUS_FC_READ_INPUT_REGS (0x04)
    uint16_t txid;  ///< transaction id echoed by the slave (caller's correlation token)
    uint8_t unit;   ///< unit / slave id
    uint16_t start; ///< first register address
    uint16_t count; ///< number of registers (1..125)
    uint8_t *out;   ///< destination buffer
    size_t cap;     ///< destination capacity (>= 12)
} ModbusMasterBuildReadArgs;

/** @brief What parse_response takes: adu, len, regs_out, max_regs, ... */
typedef struct
{
    const uint8_t *adu; ///< response bytes (MBAP + PDU)
    size_t len;         ///< response length
    uint16_t *regs_out; ///< destination for parsed 16-bit register values
    size_t max_regs;    ///< capacity of regs_out
    uint8_t
        *exception_out; ///< set to the Modbus exception code if the slave returned one (then the function returns 0 ...
} ModbusMasterParseResponseArgs;

/** @brief What build_read_bits takes: fc, txid, unit, start, count, ... */
typedef struct
{
    uint8_t fc;     ///< MODBUS_FC_READ_COILS (0x01) or ::MODBUS_FC_READ_DISCRETE_INPUTS (0x02)
    uint16_t txid;  ///< transaction id echoed by the slave
    uint8_t unit;   ///< unit / slave id
    uint16_t start; ///< first bit address
    uint16_t count; ///< number of bits (1..2000)
    uint8_t *out;   ///< destination buffer
    size_t cap;     ///< destination capacity (>= 12)
} ModbusMasterBuildReadBitsArgs;

/** @brief What parse_read_bits_response takes: adu, len, count, ... */
typedef struct
{
    const uint8_t *adu;
    size_t len;
    uint16_t count;         ///< the number of bits requested (1..2000)
    uint8_t *bits_out;      ///< destination for count unpacked bits (nullable to just validate)
    size_t max_bits;        ///< capacity of bits_out
    uint8_t *exception_out; ///< set to the Modbus exception code if the slave returned one (then 0 is returned)
} ModbusMasterParseReadBitsResponseArgs;

/** @brief What build_write_single_coil takes: txid, unit, addr, on, ... */
typedef struct
{
    uint16_t txid;
    uint8_t unit;
    uint16_t addr;
    proto_bool on; ///< the coil value; encoded on the wire as 0xFF00 (on) or 0x0000 (off) per the Modbus spec
    uint8_t *out;
    size_t cap; ///< destination capacity (>= 12)
} ModbusMasterBuildWriteSingleCoilArgs;

/** @brief What build_write_multiple_coils takes: txid, unit, start, ... */
typedef struct
{
    uint16_t txid;
    uint8_t unit;
    uint16_t start;
    const uint8_t *bits; ///< one byte (0/1) per coil to write; packed LSB-first into the wire bytes
    uint16_t count;      ///< number of coils (1..1968)
    uint8_t *out;
    size_t cap; ///< destination capacity (>= 14 + ceil(count/8))
} ModbusMasterBuildWriteMultipleCoilsArgs;

/** @brief What build_write_single takes: txid, unit, addr, value, ... */
typedef struct
{
    uint16_t txid;  ///< transaction id echoed by the slave
    uint8_t unit;   ///< unit / slave id
    uint16_t addr;  ///< register address
    uint16_t value; ///< 16-bit value to write
    uint8_t *out;   ///< destination buffer
    size_t cap;     ///< destination capacity (>= 12)
} ModbusMasterBuildWriteSingleArgs;

/** @brief What build_write_multiple takes: txid, unit, start, values, ... */
typedef struct
{
    uint16_t txid;          ///< transaction id echoed by the slave
    uint8_t unit;           ///< unit / slave id
    uint16_t start;         ///< first register address
    const uint16_t *values; ///< the count register values
    uint16_t count;         ///< number of registers (1..123)
    uint8_t *out;           ///< destination buffer
    size_t cap;             ///< destination capacity (>= 13 + 2*count)
} ModbusMasterBuildWriteMultipleArgs;

/** @brief What parse_write_response takes: adu, len, addr_out, ... */
typedef struct
{
    const uint8_t *adu;     ///< response bytes (MBAP + PDU)
    size_t len;             ///< response length
    uint16_t *addr_out;     ///< set to the echoed address / start (nullable)
    uint8_t *exception_out; ///< set to the Modbus exception code if the slave returned one (then 0 is returned)
} ModbusMasterParseWriteResponseArgs;

/** @brief What build_mask_write takes: txid, unit, addr, and_mask, ... */
typedef struct
{
    uint16_t txid;
    uint8_t unit;
    uint16_t addr;
    uint16_t and_mask;
    uint16_t or_mask;
    uint8_t *out;
    size_t cap; ///< destination capacity (>= 14)
} ModbusMasterBuildMaskWriteArgs;

/** @brief What build_read_write_multiple takes: txid, unit, ... */
typedef struct
{
    uint16_t txid;
    uint8_t unit;
    uint16_t read_start; ///< / read_count the registers to read back (1..125)
    uint16_t read_count;
    uint16_t write_start; ///< / write_count the registers to write (1..121); values holds write_count words
    const uint16_t *values;
    uint16_t write_count;
    uint8_t *out;
    size_t cap; ///< destination capacity (>= 17 + 2*write_count)
} ModbusMasterBuildReadWriteMultipleArgs;

/** @brief What parse_mask_write_response takes: adu, len, addr_out, ... */
typedef struct
{
    const uint8_t *adu;
    size_t len;
    uint16_t *addr_out; ///< / and_out / or_out receive the echoed fields (each nullable)
    uint16_t *and_out;
    uint16_t *or_out;
    uint8_t *exception_out; ///< set to the Modbus exception code if the slave returned one
} ModbusMasterParseMaskWriteResponseArgs;

/**
 * @brief Modbus TCP master codec + register scanner (PROTOCORE_ENABLE_MODBUS_MASTER).
 *
 * A caller sets the members a call takes, invokes it through ::ModbusMaster with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   ModbusMaster.build_read_args.fc = ...;
 *   ModbusMaster.build_read_args.txid = ...;
 *   ModbusMaster.build_read_args.unit = ...;
 *   ModbusMaster.build_read_args.start = ...;
 *   ModbusMaster.build_read_args.count = ...;
 *   ModbusMaster.build_read_args.out = ...;
 *   ModbusMaster.build_read_args.cap = ...;
 *   ModbusMaster.build_read(work);
 *   // ModbusMaster.n is what the call reports
 *
 * @var ModbusMasterNs::build_read_args  what build_read takes: fc, txid, unit, start, count, out,
 * @var ModbusMasterNs::parse_response_args  what parse_response takes: adu, len, regs_out, max_regs,
 * @var ModbusMasterNs::build_read_bits_args  what build_read_bits takes: fc, txid, unit, start, count,
 * @var ModbusMasterNs::parse_read_bits_response_args  what parse_read_bits_response takes: adu, len, count,
 * @var ModbusMasterNs::build_write_single_coil_args  what build_write_single_coil takes: txid, unit, addr, on,
 * @var ModbusMasterNs::build_write_multiple_coils_args  what build_write_multiple_coils takes: txid, unit, start,
 * @var ModbusMasterNs::build_write_single_args  what build_write_single takes: txid, unit, addr, value,
 * @var ModbusMasterNs::build_write_multiple_args  what build_write_multiple takes: txid, unit, start, values,
 * @var ModbusMasterNs::parse_write_response_args  what parse_write_response takes: adu, len, addr_out,
 * @var ModbusMasterNs::build_mask_write_args  what build_mask_write takes: txid, unit, addr, and_mask,
 * @var ModbusMasterNs::build_read_write_multiple_args  what build_read_write_multiple takes: txid, unit,
 * @var ModbusMasterNs::parse_mask_write_response_args  what parse_mask_write_response takes: adu, len, addr_out,
 * @var ModbusMasterNs::ok  a call's true/false outcome
 * @var ModbusMasterNs::n  bytes written (12), or 0 on a bad argument / too-small buffer
 * @var ModbusMasterNs::i32  number of registers parsed (>= 0), or -1 on a malformed/short frame
 * @var ModbusMasterNs::build_read  build a read-request ADU (FC 0x03 holding or 0x04 input registers)
 * @var ModbusMasterNs::parse_response  parse a read-response ADU into register values
 * @var ModbusMasterNs::build_read_bits  build a read-bits request ADU (FC 0x01 coils or 0x02 discrete ...
 * @var ModbusMasterNs::parse_read_bits_response  parse a read-bits response ADU (FC 0x01 / 0x02) into one byte (0/1)
 * ...
 * @var ModbusMasterNs::build_write_single_coil  build a Write Single Coil request ADU (FC 0x05)
 * @var ModbusMasterNs::build_write_multiple_coils  build a Write Multiple Coils request ADU (FC 0x0F)
 * @var ModbusMasterNs::build_write_single  build a Write Single Register request ADU (FC 0x06)
 * @var ModbusMasterNs::build_write_multiple  build a Write Multiple Registers request ADU (FC 0x10)
 * @var ModbusMasterNs::parse_write_response  parse a write-response ADU (FC 0x05, 0x06, 0x0F, or 0x10). A normal ...
 * @var ModbusMasterNs::build_mask_write  build a Mask Write Register request ADU (FC 0x16). The slave ...
 * @var ModbusMasterNs::build_read_write_multiple  build a Read/Write Multiple Registers request ADU (FC 0x17): write
 * ...
 * @var ModbusMasterNs::parse_mask_write_response  parse a Mask Write Register response (FC 0x16), which echoes the ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    ModbusMasterBuildReadArgs build_read_args;
    ModbusMasterParseResponseArgs parse_response_args;
    ModbusMasterBuildReadBitsArgs build_read_bits_args;
    ModbusMasterParseReadBitsResponseArgs parse_read_bits_response_args;
    ModbusMasterBuildWriteSingleCoilArgs build_write_single_coil_args;
    ModbusMasterBuildWriteMultipleCoilsArgs build_write_multiple_coils_args;
    ModbusMasterBuildWriteSingleArgs build_write_single_args;
    ModbusMasterBuildWriteMultipleArgs build_write_multiple_args;
    ModbusMasterParseWriteResponseArgs parse_write_response_args;
    ModbusMasterBuildMaskWriteArgs build_mask_write_args;
    ModbusMasterBuildReadWriteMultipleArgs build_read_write_multiple_args;
    ModbusMasterParseMaskWriteResponseArgs parse_mask_write_response_args;

    proto_bool ok;
    size_t n;
    int i32;

    void (*const build_read)(uint8_t *restrict work);
    void (*const parse_response)(uint8_t *restrict work);
    void (*const build_read_bits)(uint8_t *restrict work);
    void (*const parse_read_bits_response)(uint8_t *restrict work);
    void (*const build_write_single_coil)(uint8_t *restrict work);
    void (*const build_write_multiple_coils)(uint8_t *restrict work);
    void (*const build_write_single)(uint8_t *restrict work);
    void (*const build_write_multiple)(uint8_t *restrict work);
    void (*const parse_write_response)(uint8_t *restrict work);
    void (*const build_mask_write)(uint8_t *restrict work);
    void (*const build_read_write_multiple)(uint8_t *restrict work);
    void (*const parse_mask_write_response)(uint8_t *restrict work);
} ModbusMasterNs;

/** @brief The one symbol this module exports. */
extern ModbusMasterNs ModbusMaster;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_MODBUS_MASTER

#endif // PROTOCORE_MODBUS_MASTER_H
