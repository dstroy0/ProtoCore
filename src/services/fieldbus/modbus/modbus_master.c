// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_modbus_master.c
 * @brief Modbus TCP master codec - build read requests, parse responses (pure).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_MODBUS_MASTER

#include "services/fieldbus/modbus/modbus_master.h"

PROTOCORE_BEGIN_DECLS

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void modbus_master_build_read(uint8_t *restrict work)
{
    (void)work;
    uint8_t fc = ModbusMaster.build_read_args.fc;
    uint16_t txid = ModbusMaster.build_read_args.txid;
    uint8_t unit = ModbusMaster.build_read_args.unit;
    uint16_t start = ModbusMaster.build_read_args.start;
    uint16_t count = ModbusMaster.build_read_args.count;
    uint8_t *out = ModbusMaster.build_read_args.out;
    size_t cap = ModbusMaster.build_read_args.cap;

    if (!out || cap < 12)
    {
        ModbusMaster.n = 0;
        return;
    }
    if (fc != 0x03 && fc != 0x04) // read holding / input registers only
    {
        ModbusMaster.n = 0;
        return;
    }
    if (count < 1 || count > 125)
    {
        ModbusMaster.n = 0;
        return;
    }

    // MBAP header
    out[0] = (uint8_t)(txid >> 8);
    out[1] = (uint8_t)(txid & 0xFF);
    out[2] = 0; // protocol id (Modbus) = 0
    out[3] = 0;
    out[4] = 0; // length = unit(1) + PDU(5) = 6
    out[5] = 6;
    out[6] = unit;
    // PDU
    out[7] = fc;
    out[8] = (uint8_t)(start >> 8);
    out[9] = (uint8_t)(start & 0xFF);
    out[10] = (uint8_t)(count >> 8);
    out[11] = (uint8_t)(count & 0xFF);
    ModbusMaster.n = 12;
}

static void modbus_master_parse_response(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *adu = ModbusMaster.parse_response_args.adu;
    size_t len = ModbusMaster.parse_response_args.len;
    uint16_t *regs_out = ModbusMaster.parse_response_args.regs_out;
    size_t max_regs = ModbusMaster.parse_response_args.max_regs;
    uint8_t *exception_out = ModbusMaster.parse_response_args.exception_out;

    if (exception_out)
    {
        *exception_out = 0;
    }
    if (!adu || len < 9) // MBAP(7) + FC(1) + at least one more byte
    {
        ModbusMaster.i32 = -1;
        return;
    }
    if (adu[2] != 0 || adu[3] != 0) // protocol id must be 0
    {
        ModbusMaster.i32 = -1;
        return;
    }

    uint8_t fc = adu[7];
    if (fc & 0x80) // exception response: FC | 0x80, then the exception code
    {
        if (exception_out)
        {
            *exception_out = adu[8];
        }
        ModbusMaster.i32 = 0;
        return;
    }
    if (fc != 0x03 && fc != 0x04 && fc != 0x17) // read holding / input / read-write-multiple all reply the same
    {
        ModbusMaster.i32 = -1;
        return;
    }

    uint8_t byte_count = adu[8];
    if ((byte_count & 1) || len < (size_t)(9 + byte_count)) // must be even and present
    {
        ModbusMaster.i32 = -1;
        return;
    }

    int nregs = byte_count / 2;
    int copied = 0;
    for (int i = 0; i < nregs && (size_t)i < max_regs; i++)
    {
        if (regs_out)
        {
            regs_out[i] = (uint16_t)((adu[9 + i * 2] << 8) | adu[9 + i * 2 + 1]);
        }
        copied++;
    }
    ModbusMaster.i32 = copied;
}

static void modbus_master_build_read_bits(uint8_t *restrict work)
{
    (void)work;
    uint8_t fc = ModbusMaster.build_read_bits_args.fc;
    uint16_t txid = ModbusMaster.build_read_bits_args.txid;
    uint8_t unit = ModbusMaster.build_read_bits_args.unit;
    uint16_t start = ModbusMaster.build_read_bits_args.start;
    uint16_t count = ModbusMaster.build_read_bits_args.count;
    uint8_t *out = ModbusMaster.build_read_bits_args.out;
    size_t cap = ModbusMaster.build_read_bits_args.cap;

    if (!out || cap < 12)
    {
        ModbusMaster.n = 0;
        return;
    }
    if (fc != 0x01 && fc != 0x02) // read coils / discrete inputs only
    {
        ModbusMaster.n = 0;
        return;
    }
    if (count < 1 || count > 2000) // FC 0x01/0x02 cap
    {
        ModbusMaster.n = 0;
        return;
    }

    // The read PDU is identical for bits and registers: fc | start(2) | count(2).
    out[0] = (uint8_t)(txid >> 8);
    out[1] = (uint8_t)(txid & 0xFF);
    out[2] = 0; // protocol id (Modbus) = 0
    out[3] = 0;
    out[4] = 0; // length = unit(1) + PDU(5) = 6
    out[5] = 6;
    out[6] = unit;
    out[7] = fc;
    out[8] = (uint8_t)(start >> 8);
    out[9] = (uint8_t)(start & 0xFF);
    out[10] = (uint8_t)(count >> 8);
    out[11] = (uint8_t)(count & 0xFF);
    ModbusMaster.n = 12;
}

static void modbus_master_parse_read_bits_response(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *adu = ModbusMaster.parse_read_bits_response_args.adu;
    size_t len = ModbusMaster.parse_read_bits_response_args.len;
    uint16_t count = ModbusMaster.parse_read_bits_response_args.count;
    uint8_t *bits_out = ModbusMaster.parse_read_bits_response_args.bits_out;
    size_t max_bits = ModbusMaster.parse_read_bits_response_args.max_bits;
    uint8_t *exception_out = ModbusMaster.parse_read_bits_response_args.exception_out;

    if (exception_out)
    {
        *exception_out = 0;
    }
    if (!adu || len < 9) // MBAP(7) + FC(1) + byte count(1)
    {
        ModbusMaster.i32 = -1;
        return;
    }
    if (adu[2] != 0 || adu[3] != 0) // protocol id must be 0
    {
        ModbusMaster.i32 = -1;
        return;
    }

    uint8_t fc = adu[7];
    if (fc & 0x80) // exception response: FC | 0x80, then the exception code
    {
        if (exception_out)
        {
            *exception_out = adu[8];
        }
        ModbusMaster.i32 = 0;
        return;
    }
    if (fc != 0x01 && fc != 0x02)
    {
        ModbusMaster.i32 = -1;
        return;
    }
    if (count < 1 || count > 2000)
    {
        ModbusMaster.i32 = -1;
        return;
    }

    uint8_t byte_count = adu[8];
    uint16_t expect_bytes = (uint16_t)((count + 7) / 8);
    if (byte_count != expect_bytes || len < (size_t)(9 + byte_count)) // must match the request and be present
    {
        ModbusMaster.i32 = -1;
        return;
    }

    int copied = 0;
    for (uint16_t i = 0; i < count && (size_t)i < max_bits; i++)
    {
        uint8_t byte = adu[9 + (i / 8)];
        if (bits_out)
        {
            bits_out[i] = (uint8_t)((byte >> (i % 8)) & 1); // LSB-first packing (Modbus)
        }
        copied++;
    }
    ModbusMaster.i32 = copied;
}

static void modbus_master_build_write_single_coil(uint8_t *restrict work)
{
    (void)work;
    uint16_t txid = ModbusMaster.build_write_single_coil_args.txid;
    uint8_t unit = ModbusMaster.build_write_single_coil_args.unit;
    uint16_t addr = ModbusMaster.build_write_single_coil_args.addr;
    proto_bool on = ModbusMaster.build_write_single_coil_args.on;
    uint8_t *out = ModbusMaster.build_write_single_coil_args.out;
    size_t cap = ModbusMaster.build_write_single_coil_args.cap;

    if (!out || cap < 12)
    {
        ModbusMaster.n = 0;
        return;
    }

    out[0] = (uint8_t)(txid >> 8);
    out[1] = (uint8_t)(txid & 0xFF);
    out[2] = 0;
    out[3] = 0;
    out[4] = 0; // length = unit(1) + PDU(5) = 6
    out[5] = 6;
    out[6] = unit;
    // PDU (FC 0x05: address + value, where 0xFF00 = on and 0x0000 = off)
    out[7] = 0x05;
    out[8] = (uint8_t)(addr >> 8);
    out[9] = (uint8_t)(addr & 0xFF);
    out[10] = on ? 0xFF : 0x00;
    out[11] = 0x00;
    ModbusMaster.n = 12;
}

static void modbus_master_build_write_multiple_coils(uint8_t *restrict work)
{
    (void)work;
    uint16_t txid = ModbusMaster.build_write_multiple_coils_args.txid;
    uint8_t unit = ModbusMaster.build_write_multiple_coils_args.unit;
    uint16_t start = ModbusMaster.build_write_multiple_coils_args.start;
    const uint8_t *bits = ModbusMaster.build_write_multiple_coils_args.bits;
    uint16_t count = ModbusMaster.build_write_multiple_coils_args.count;
    uint8_t *out = ModbusMaster.build_write_multiple_coils_args.out;
    size_t cap = ModbusMaster.build_write_multiple_coils_args.cap;

    if (!out || !bits)
    {
        ModbusMaster.n = 0;
        return;
    }
    if (count < 1 || count > 1968) // FC 0x0F cap (0x07B0)
    {
        ModbusMaster.n = 0;
        return;
    }

    uint8_t byte_count = (uint8_t)((count + 7) / 8);
    size_t pdu_len = 6u + (size_t)byte_count; // fc(1) + start(2) + count(2) + byte_count(1) + data
    size_t total = 7u + pdu_len;              // MBAP(7) + PDU
    if (cap < total)
    {
        ModbusMaster.n = 0;
        return;
    }

    uint16_t mbap_len = (uint16_t)(1u + pdu_len); // unit + PDU
    out[0] = (uint8_t)(txid >> 8);
    out[1] = (uint8_t)(txid & 0xFF);
    out[2] = 0;
    out[3] = 0;
    out[4] = (uint8_t)(mbap_len >> 8);
    out[5] = (uint8_t)(mbap_len & 0xFF);
    out[6] = unit;
    // PDU (FC 0x0F: start + count + byte count + packed bits)
    out[7] = 0x0F;
    out[8] = (uint8_t)(start >> 8);
    out[9] = (uint8_t)(start & 0xFF);
    out[10] = (uint8_t)(count >> 8);
    out[11] = (uint8_t)(count & 0xFF);
    out[12] = byte_count;
    for (uint16_t i = 0; i < byte_count; i++)
    {
        out[13 + i] = 0;
    }
    for (uint16_t i = 0; i < count; i++)
    {
        if (bits[i])
        {
            out[13 + (i / 8)] |= (uint8_t)(1u << (i % 8)); // LSB-first packing (Modbus)
        }
    }
    ModbusMaster.n = total;
}

static void modbus_master_build_write_single(uint8_t *restrict work)
{
    (void)work;
    uint16_t txid = ModbusMaster.build_write_single_args.txid;
    uint8_t unit = ModbusMaster.build_write_single_args.unit;
    uint16_t addr = ModbusMaster.build_write_single_args.addr;
    uint16_t value = ModbusMaster.build_write_single_args.value;
    uint8_t *out = ModbusMaster.build_write_single_args.out;
    size_t cap = ModbusMaster.build_write_single_args.cap;

    if (!out || cap < 12)
    {
        ModbusMaster.n = 0;
        return;
    }

    // MBAP header
    out[0] = (uint8_t)(txid >> 8);
    out[1] = (uint8_t)(txid & 0xFF);
    out[2] = 0; // protocol id (Modbus) = 0
    out[3] = 0;
    out[4] = 0; // length = unit(1) + PDU(5) = 6
    out[5] = 6;
    out[6] = unit;
    // PDU (FC 0x06: address + value)
    out[7] = 0x06;
    out[8] = (uint8_t)(addr >> 8);
    out[9] = (uint8_t)(addr & 0xFF);
    out[10] = (uint8_t)(value >> 8);
    out[11] = (uint8_t)(value & 0xFF);
    ModbusMaster.n = 12;
}

static void modbus_master_build_write_multiple(uint8_t *restrict work)
{
    (void)work;
    uint16_t txid = ModbusMaster.build_write_multiple_args.txid;
    uint8_t unit = ModbusMaster.build_write_multiple_args.unit;
    uint16_t start = ModbusMaster.build_write_multiple_args.start;
    const uint16_t *values = ModbusMaster.build_write_multiple_args.values;
    uint16_t count = ModbusMaster.build_write_multiple_args.count;
    uint8_t *out = ModbusMaster.build_write_multiple_args.out;
    size_t cap = ModbusMaster.build_write_multiple_args.cap;

    if (!out || !values)
    {
        ModbusMaster.n = 0;
        return;
    }
    if (count < 1 || count > 123) // FC 0x10 caps at 123 registers (PDU fits 253 bytes)
    {
        ModbusMaster.n = 0;
        return;
    }

    uint8_t byte_count = (uint8_t)(count * 2);
    size_t pdu_len = 6u + (size_t)byte_count; // fc(1) + start(2) + count(2) + byte_count(1) + data
    size_t total = 7u + pdu_len;              // MBAP(7) + PDU
    if (cap < total)
    {
        ModbusMaster.n = 0;
        return;
    }

    // MBAP header
    uint16_t mbap_len = (uint16_t)(1u + pdu_len); // unit + PDU
    out[0] = (uint8_t)(txid >> 8);
    out[1] = (uint8_t)(txid & 0xFF);
    out[2] = 0;
    out[3] = 0;
    out[4] = (uint8_t)(mbap_len >> 8);
    out[5] = (uint8_t)(mbap_len & 0xFF);
    out[6] = unit;
    // PDU (FC 0x10: start + count + byte count + data)
    out[7] = 0x10;
    out[8] = (uint8_t)(start >> 8);
    out[9] = (uint8_t)(start & 0xFF);
    out[10] = (uint8_t)(count >> 8);
    out[11] = (uint8_t)(count & 0xFF);
    out[12] = byte_count;
    for (uint16_t i = 0; i < count; i++)
    {
        out[13 + i * 2] = (uint8_t)(values[i] >> 8);
        out[13 + i * 2 + 1] = (uint8_t)(values[i] & 0xFF);
    }
    ModbusMaster.n = total;
}

static void modbus_master_parse_write_response(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *adu = ModbusMaster.parse_write_response_args.adu;
    size_t len = ModbusMaster.parse_write_response_args.len;
    uint16_t *addr_out = ModbusMaster.parse_write_response_args.addr_out;
    uint8_t *exception_out = ModbusMaster.parse_write_response_args.exception_out;

    if (exception_out)
    {
        *exception_out = 0;
    }
    if (addr_out)
    {
        *addr_out = 0;
    }
    if (!adu || len < 9) // MBAP(7) + FC(1) + at least one more byte
    {
        ModbusMaster.i32 = -1;
        return;
    }
    if (adu[2] != 0 || adu[3] != 0) // protocol id must be 0
    {
        ModbusMaster.i32 = -1;
        return;
    }

    uint8_t fc = adu[7];
    if (fc & 0x80) // exception response: FC | 0x80, then the exception code
    {
        if (exception_out)
        {
            *exception_out = adu[8];
        }
        ModbusMaster.i32 = 0;
        return;
    }
    if (fc != 0x05 && fc != 0x06 && fc != 0x0F && fc != 0x10)
    {
        ModbusMaster.i32 = -1;
        return;
    }
    if (len < 12) // every reply is MBAP(7) + FC(1) + addr(2) + value-or-count(2) = 12 bytes
    {
        ModbusMaster.i32 = -1;
        return;
    }

    if (addr_out)
    {
        *addr_out = (uint16_t)((adu[8] << 8) | adu[9]);
    }
    uint16_t tail = (uint16_t)((adu[10] << 8) | adu[11]); // value (0x05/0x06) or quantity (0x0F/0x10)
    proto_bool single = (fc == 0x05 || fc == 0x06);       // single-write echoes a value; multi echoes a count
    ModbusMaster.i32 = single ? 1 : (int)tail;
}

static void modbus_master_build_mask_write(uint8_t *restrict work)
{
    (void)work;
    uint16_t txid = ModbusMaster.build_mask_write_args.txid;
    uint8_t unit = ModbusMaster.build_mask_write_args.unit;
    uint16_t addr = ModbusMaster.build_mask_write_args.addr;
    uint16_t and_mask = ModbusMaster.build_mask_write_args.and_mask;
    uint16_t or_mask = ModbusMaster.build_mask_write_args.or_mask;
    uint8_t *out = ModbusMaster.build_mask_write_args.out;
    size_t cap = ModbusMaster.build_mask_write_args.cap;

    if (!out || cap < 14) // MBAP(7) + FC(1) + addr(2) + And_Mask(2) + Or_Mask(2)
    {
        ModbusMaster.n = 0;
        return;
    }
    // MBAP header: length = unit(1) + PDU(7) = 8.
    out[0] = (uint8_t)(txid >> 8);
    out[1] = (uint8_t)(txid & 0xFF);
    out[2] = 0;
    out[3] = 0;
    out[4] = 0;
    out[5] = 8;
    out[6] = unit;
    // PDU (FC 0x16: address + And_Mask + Or_Mask)
    out[7] = 0x16;
    out[8] = (uint8_t)(addr >> 8);
    out[9] = (uint8_t)(addr & 0xFF);
    out[10] = (uint8_t)(and_mask >> 8);
    out[11] = (uint8_t)(and_mask & 0xFF);
    out[12] = (uint8_t)(or_mask >> 8);
    out[13] = (uint8_t)(or_mask & 0xFF);
    ModbusMaster.n = 14;
}

static void modbus_master_build_read_write_multiple(uint8_t *restrict work)
{
    (void)work;
    uint16_t txid = ModbusMaster.build_read_write_multiple_args.txid;
    uint8_t unit = ModbusMaster.build_read_write_multiple_args.unit;
    uint16_t read_start = ModbusMaster.build_read_write_multiple_args.read_start;
    uint16_t read_count = ModbusMaster.build_read_write_multiple_args.read_count;
    uint16_t write_start = ModbusMaster.build_read_write_multiple_args.write_start;
    const uint16_t *values = ModbusMaster.build_read_write_multiple_args.values;
    uint16_t write_count = ModbusMaster.build_read_write_multiple_args.write_count;
    uint8_t *out = ModbusMaster.build_read_write_multiple_args.out;
    size_t cap = ModbusMaster.build_read_write_multiple_args.cap;

    if (!out || !values)
    {
        ModbusMaster.n = 0;
        return;
    }
    if (read_count < 1 || read_count > 125 || write_count < 1 || write_count > 121)
    {
        ModbusMaster.n = 0;
        return;
    }

    uint8_t byte_count = (uint8_t)(write_count * 2);
    size_t pdu_len = 10u + (size_t)byte_count; // fc + rStart(2) + rQty(2) + wStart(2) + wQty(2) + wBC(1) + data
    size_t total = 7u + pdu_len;               // MBAP(7) + PDU
    if (cap < total)
    {
        ModbusMaster.n = 0;
        return;
    }

    uint16_t mbap_len = (uint16_t)(1u + pdu_len); // unit + PDU
    out[0] = (uint8_t)(txid >> 8);
    out[1] = (uint8_t)(txid & 0xFF);
    out[2] = 0;
    out[3] = 0;
    out[4] = (uint8_t)(mbap_len >> 8);
    out[5] = (uint8_t)(mbap_len & 0xFF);
    out[6] = unit;
    // PDU (FC 0x17: read span, write span, then the write data)
    out[7] = 0x17;
    out[8] = (uint8_t)(read_start >> 8);
    out[9] = (uint8_t)(read_start & 0xFF);
    out[10] = (uint8_t)(read_count >> 8);
    out[11] = (uint8_t)(read_count & 0xFF);
    out[12] = (uint8_t)(write_start >> 8);
    out[13] = (uint8_t)(write_start & 0xFF);
    out[14] = (uint8_t)(write_count >> 8);
    out[15] = (uint8_t)(write_count & 0xFF);
    out[16] = byte_count;
    for (uint16_t i = 0; i < write_count; i++)
    {
        out[17 + i * 2] = (uint8_t)(values[i] >> 8);
        out[17 + i * 2 + 1] = (uint8_t)(values[i] & 0xFF);
    }
    ModbusMaster.n = total;
}

static void modbus_master_parse_mask_write_response(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *adu = ModbusMaster.parse_mask_write_response_args.adu;
    size_t len = ModbusMaster.parse_mask_write_response_args.len;
    uint16_t *addr_out = ModbusMaster.parse_mask_write_response_args.addr_out;
    uint16_t *and_out = ModbusMaster.parse_mask_write_response_args.and_out;
    uint16_t *or_out = ModbusMaster.parse_mask_write_response_args.or_out;
    uint8_t *exception_out = ModbusMaster.parse_mask_write_response_args.exception_out;

    if (exception_out)
    {
        *exception_out = 0;
    }
    if (!adu || len < 9)
    {
        ModbusMaster.i32 = -1;
        return;
    }
    if (adu[2] != 0 || adu[3] != 0) // protocol id must be 0
    {
        ModbusMaster.i32 = -1;
        return;
    }
    uint8_t fc = adu[7];
    if (fc & 0x80)
    {
        if (exception_out)
        {
            *exception_out = adu[8];
        }
        ModbusMaster.i32 = 0;
        return;
    }
    if (fc != 0x16 || len < 14) // MBAP(7) + FC(1) + addr(2) + And_Mask(2) + Or_Mask(2)
    {
        ModbusMaster.i32 = -1;
        return;
    }
    if (addr_out)
    {
        *addr_out = (uint16_t)((adu[8] << 8) | adu[9]);
    }
    if (and_out)
    {
        *and_out = (uint16_t)((adu[10] << 8) | adu[11]);
    }
    if (or_out)
    {
        *or_out = (uint16_t)((adu[12] << 8) | adu[13]);
    }
    ModbusMaster.i32 = 1;
}

ModbusMasterNs ModbusMaster = {.build_read = modbus_master_build_read,
                               .parse_response = modbus_master_parse_response,
                               .build_read_bits = modbus_master_build_read_bits,
                               .parse_read_bits_response = modbus_master_parse_read_bits_response,
                               .build_write_single_coil = modbus_master_build_write_single_coil,
                               .build_write_multiple_coils = modbus_master_build_write_multiple_coils,
                               .build_write_single = modbus_master_build_write_single,
                               .build_write_multiple = modbus_master_build_write_multiple,
                               .parse_write_response = modbus_master_parse_write_response,
                               .build_mask_write = modbus_master_build_mask_write,
                               .build_read_write_multiple = modbus_master_build_read_write_multiple,
                               .parse_mask_write_response = modbus_master_parse_mask_write_response};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_MODBUS_MASTER
