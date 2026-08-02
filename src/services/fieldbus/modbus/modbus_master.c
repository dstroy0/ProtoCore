// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_modbus_master.cpp
 * @brief Modbus TCP master codec - build read requests, parse responses (pure).
 */

#include "services/fieldbus/modbus/modbus_master.h"

#if PC_ENABLE_MODBUS_MASTER

size_t pc_modbus_build_read(uint8_t fc, uint16_t txid, uint8_t unit, uint16_t start, uint16_t count, uint8_t *out,
                            size_t cap)
{
    if (!out || cap < 12)
    {
        return 0;
    }
    if (fc != 0x03 && fc != 0x04) // read holding / input registers only
    {
        return 0;
    }
    if (count < 1 || count > 125)
    {
        return 0;
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
    return 12;
}

int pc_modbus_parse_response(const uint8_t *adu, size_t len, uint16_t *regs_out, size_t max_regs,
                             uint8_t *exception_out)
{
    if (exception_out)
    {
        *exception_out = 0;
    }
    if (!adu || len < 9) // MBAP(7) + FC(1) + at least one more byte
    {
        return -1;
    }
    if (adu[2] != 0 || adu[3] != 0) // protocol id must be 0
    {
        return -1;
    }

    uint8_t fc = adu[7];
    if (fc & 0x80) // exception response: FC | 0x80, then the exception code
    {
        if (exception_out)
        {
            *exception_out = adu[8];
        }
        return 0;
    }
    if (fc != 0x03 && fc != 0x04 && fc != 0x17) // read holding / input / read-write-multiple all reply the same
    {
        return -1;
    }

    uint8_t byte_count = adu[8];
    if ((byte_count & 1) || len < (size_t)(9 + byte_count)) // must be even and present
    {
        return -1;
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
    return copied;
}

size_t pc_modbus_build_read_bits(uint8_t fc, uint16_t txid, uint8_t unit, uint16_t start, uint16_t count, uint8_t *out,
                                 size_t cap)
{
    if (!out || cap < 12)
    {
        return 0;
    }
    if (fc != 0x01 && fc != 0x02) // read coils / discrete inputs only
    {
        return 0;
    }
    if (count < 1 || count > 2000) // FC 0x01/0x02 cap
    {
        return 0;
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
    return 12;
}

int pc_modbus_parse_read_bits_response(const uint8_t *adu, size_t len, uint16_t count, uint8_t *bits_out,
                                       size_t max_bits, uint8_t *exception_out)
{
    if (exception_out)
    {
        *exception_out = 0;
    }
    if (!adu || len < 9) // MBAP(7) + FC(1) + byte count(1)
    {
        return -1;
    }
    if (adu[2] != 0 || adu[3] != 0) // protocol id must be 0
    {
        return -1;
    }

    uint8_t fc = adu[7];
    if (fc & 0x80) // exception response: FC | 0x80, then the exception code
    {
        if (exception_out)
        {
            *exception_out = adu[8];
        }
        return 0;
    }
    if (fc != 0x01 && fc != 0x02)
    {
        return -1;
    }
    if (count < 1 || count > 2000)
    {
        return -1;
    }

    uint8_t byte_count = adu[8];
    uint16_t expect_bytes = (uint16_t)((count + 7) / 8);
    if (byte_count != expect_bytes || len < (size_t)(9 + byte_count)) // must match the request and be present
    {
        return -1;
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
    return copied;
}

size_t pc_modbus_build_write_single_coil(uint16_t txid, uint8_t unit, uint16_t addr, proto_bool on, uint8_t *out,
                                         size_t cap)
{
    if (!out || cap < 12)
    {
        return 0;
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
    return 12;
}

size_t pc_modbus_build_write_multiple_coils(uint16_t txid, uint8_t unit, uint16_t start, const uint8_t *bits,
                                            uint16_t count, uint8_t *out, size_t cap)
{
    if (!out || !bits)
    {
        return 0;
    }
    if (count < 1 || count > 1968) // FC 0x0F cap (0x07B0)
    {
        return 0;
    }

    uint8_t byte_count = (uint8_t)((count + 7) / 8);
    size_t pdu_len = 6u + (size_t)byte_count; // fc(1) + start(2) + count(2) + byte_count(1) + data
    size_t total = 7u + pdu_len;              // MBAP(7) + PDU
    if (cap < total)
    {
        return 0;
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
    return total;
}

size_t pc_modbus_build_write_single(uint16_t txid, uint8_t unit, uint16_t addr, uint16_t value, uint8_t *out,
                                    size_t cap)
{
    if (!out || cap < 12)
    {
        return 0;
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
    return 12;
}

size_t pc_modbus_build_write_multiple(uint16_t txid, uint8_t unit, uint16_t start, const uint16_t *values,
                                      uint16_t count, uint8_t *out, size_t cap)
{
    if (!out || !values)
    {
        return 0;
    }
    if (count < 1 || count > 123) // FC 0x10 caps at 123 registers (PDU fits 253 bytes)
    {
        return 0;
    }

    uint8_t byte_count = (uint8_t)(count * 2);
    size_t pdu_len = 6u + (size_t)byte_count; // fc(1) + start(2) + count(2) + byte_count(1) + data
    size_t total = 7u + pdu_len;              // MBAP(7) + PDU
    if (cap < total)
    {
        return 0;
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
    return total;
}

int pc_modbus_parse_write_response(const uint8_t *adu, size_t len, uint16_t *addr_out, uint8_t *exception_out)
{
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
        return -1;
    }
    if (adu[2] != 0 || adu[3] != 0) // protocol id must be 0
    {
        return -1;
    }

    uint8_t fc = adu[7];
    if (fc & 0x80) // exception response: FC | 0x80, then the exception code
    {
        if (exception_out)
        {
            *exception_out = adu[8];
        }
        return 0;
    }
    if (fc != 0x05 && fc != 0x06 && fc != 0x0F && fc != 0x10)
    {
        return -1;
    }
    if (len < 12) // every reply is MBAP(7) + FC(1) + addr(2) + value-or-count(2) = 12 bytes
    {
        return -1;
    }

    if (addr_out)
    {
        *addr_out = (uint16_t)((adu[8] << 8) | adu[9]);
    }
    uint16_t tail = (uint16_t)((adu[10] << 8) | adu[11]); // value (0x05/0x06) or quantity (0x0F/0x10)
    proto_bool single = (fc == 0x05 || fc == 0x06);       // single-write echoes a value; multi echoes a count
    return single ? 1 : (int)tail;
}

size_t pc_modbus_build_mask_write(uint16_t txid, uint8_t unit, uint16_t addr, uint16_t and_mask, uint16_t or_mask,
                                  uint8_t *out, size_t cap)
{
    if (!out || cap < 14) // MBAP(7) + FC(1) + addr(2) + And_Mask(2) + Or_Mask(2)
    {
        return 0;
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
    return 14;
}

size_t pc_modbus_build_read_write_multiple(uint16_t txid, uint8_t unit, uint16_t read_start, uint16_t read_count,
                                           uint16_t write_start, const uint16_t *values, uint16_t write_count,
                                           uint8_t *out, size_t cap)
{
    if (!out || !values)
    {
        return 0;
    }
    if (read_count < 1 || read_count > 125 || write_count < 1 || write_count > 121)
    {
        return 0;
    }

    uint8_t byte_count = (uint8_t)(write_count * 2);
    size_t pdu_len = 10u + (size_t)byte_count; // fc + rStart(2) + rQty(2) + wStart(2) + wQty(2) + wBC(1) + data
    size_t total = 7u + pdu_len;               // MBAP(7) + PDU
    if (cap < total)
    {
        return 0;
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
    return total;
}

int pc_modbus_parse_mask_write_response(const uint8_t *adu, size_t len, uint16_t *addr_out, uint16_t *and_out,
                                        uint16_t *or_out, uint8_t *exception_out)
{
    if (exception_out)
    {
        *exception_out = 0;
    }
    if (!adu || len < 9)
    {
        return -1;
    }
    if (adu[2] != 0 || adu[3] != 0) // protocol id must be 0
    {
        return -1;
    }
    uint8_t fc = adu[7];
    if (fc & 0x80)
    {
        if (exception_out)
        {
            *exception_out = adu[8];
        }
        return 0;
    }
    if (fc != 0x16 || len < 14) // MBAP(7) + FC(1) + addr(2) + And_Mask(2) + Or_Mask(2)
    {
        return -1;
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
    return 1;
}

#endif // PC_ENABLE_MODBUS_MASTER
