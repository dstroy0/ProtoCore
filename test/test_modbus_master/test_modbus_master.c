// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the Modbus master codec (services/fieldbus/modbus/pc_modbus_master): request
// framing, plus a full master<->slave round-trip against the slave codec
// (pc_modbus_process_adu) - no hardware needed.

#include "services/fieldbus/modbus/modbus.h"
#include "services/fieldbus/modbus/modbus_master.h"
#include <unity.h>

void setUp()
{
    pc_modbus_server_init();
}
void tearDown()
{
}

void test_build_read_bytes()
{
    uint8_t adu[16];
    size_t n = pc_modbus_build_read((uint8_t)MODBUS_FC_READ_HOLDING_REGS, 1, 1, 0, 2, adu, sizeof(adu));
    TEST_ASSERT_EQUAL_size_t(12, n);
    const uint8_t expect[12] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00, 0x00, 0x00, 0x02};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, adu, 12);
}

void test_build_rejects_bad_args()
{
    uint8_t adu[16];
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_build_read(0x06, 1, 1, 0, 2, adu, sizeof(adu)));   // not a read FC
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_build_read(0x03, 1, 1, 0, 0, adu, sizeof(adu)));   // count 0
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_build_read(0x03, 1, 1, 0, 200, adu, sizeof(adu))); // count > 125
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_build_read(0x03, 1, 1, 0, 2, adu, 4));             // buffer too small
}

void test_round_trip_holding_regs()
{
    pc_modbus_set_holding_reg(0, 0x1234);
    pc_modbus_set_holding_reg(1, 0xABCD);

    uint8_t req[16];
    size_t rn =
        pc_modbus_build_read((uint8_t)MODBUS_FC_READ_HOLDING_REGS, 7, 1, 0, 2, req, sizeof(req));
    TEST_ASSERT_EQUAL_size_t(12, rn);

    uint8_t resp[MODBUS_ADU_MAX];
    size_t pn = pc_modbus_process_adu(req, rn, resp, sizeof(resp)); // the slave answers
    TEST_ASSERT_TRUE(pn > 0);

    uint16_t regs[4];
    uint8_t ex = 0xFF;
    int got = pc_modbus_parse_response(resp, pn, regs, 4, &ex);
    TEST_ASSERT_EQUAL_INT(2, got);
    TEST_ASSERT_EQUAL_UINT8(0, ex);
    TEST_ASSERT_EQUAL_HEX16(0x1234, regs[0]);
    TEST_ASSERT_EQUAL_HEX16(0xABCD, regs[1]);
}

void test_round_trip_exception()
{
    // Read a wildly out-of-range address: the slave returns an exception ADU.
    uint8_t req[16];
    size_t rn =
        pc_modbus_build_read((uint8_t)MODBUS_FC_READ_HOLDING_REGS, 9, 1, 60000, 1, req, sizeof(req));
    uint8_t resp[MODBUS_ADU_MAX];
    size_t pn = pc_modbus_process_adu(req, rn, resp, sizeof(resp));
    TEST_ASSERT_TRUE(pn > 0);

    uint16_t regs[4];
    uint8_t ex = 0;
    int got = pc_modbus_parse_response(resp, pn, regs, 4, &ex);
    TEST_ASSERT_EQUAL_INT(0, got);
    TEST_ASSERT_EQUAL_UINT8(MODBUS_EX_ILLEGAL_DATA_ADDRESS, ex);
}

void test_parse_short_frame_fails()
{
    uint8_t buf[4] = {0, 1, 0, 0};
    TEST_ASSERT_EQUAL_INT(-1, pc_modbus_parse_response(buf, sizeof(buf), NULL, 0, NULL));
}

// build rejects a null output buffer, and accepts FC 0x04 (read input registers).
void test_build_null_out_and_input_fc()
{
    uint8_t adu[16];
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_build_read(0x03, 1, 1, 0, 2, NULL, 16)); // null out
    size_t n = pc_modbus_build_read(0x04, 1, 1, 0, 2, adu, sizeof(adu));              // FC 0x04 is valid
    TEST_ASSERT_EQUAL_size_t(12, n);
    TEST_ASSERT_EQUAL_HEX8(0x04, adu[7]);
}

// parse rejects a null ADU pointer.
void test_parse_null_adu()
{
    uint16_t regs[4];
    uint8_t ex = 0xFF;
    TEST_ASSERT_EQUAL_INT(-1, pc_modbus_parse_response(NULL, 12, regs, 4, &ex));
}

// parse rejects a response whose MBAP protocol id is not 0 (either high or low byte non-zero).
void test_parse_bad_protocol_id()
{
    uint16_t regs[4];
    uint8_t ex = 0xFF;
    uint8_t adu[13] = {0, 7, 0, 1, 0, 7, 1, 3, 4, 0, 0, 0, 0}; // proto id low byte = 1
    TEST_ASSERT_EQUAL_INT(-1, pc_modbus_parse_response(adu, sizeof(adu), regs, 4, &ex));
    adu[2] = 1; // proto id high byte = 1
    adu[3] = 0;
    TEST_ASSERT_EQUAL_INT(-1, pc_modbus_parse_response(adu, sizeof(adu), regs, 4, &ex));
}

// parse rejects a non-exception response function that is neither read-holding nor read-input.
void test_parse_unexpected_function()
{
    uint16_t regs[4];
    uint8_t ex = 0xFF;
    uint8_t adu[13] = {0, 7, 0, 0, 0, 7, 1, 0x06, 4, 0, 0, 0, 0}; // FC 0x06 (write single), not a read
    TEST_ASSERT_EQUAL_INT(-1, pc_modbus_parse_response(adu, sizeof(adu), regs, 4, &ex));
}

// An exception response parsed with a null exception_out still returns 0 registers (no write attempted).
void test_parse_exception_null_out()
{
    uint16_t regs[4];
    uint8_t adu[9] = {0, 9, 0, 0, 0, 3, 1, 0x83, 0x02}; // FC 0x03|0x80 exception, code 2
    TEST_ASSERT_EQUAL_INT(0, pc_modbus_parse_response(adu, sizeof(adu), regs, 4, NULL));
}

// parse rejects an odd byte count and a byte count that runs past the frame.
void test_parse_bad_byte_count()
{
    uint16_t regs[4];
    uint8_t ex = 0xFF;
    uint8_t odd[13] = {0, 7, 0, 0, 0, 7, 1, 3, 3, 0, 0, 0, 0}; // byte count 3 is odd
    TEST_ASSERT_EQUAL_INT(-1, pc_modbus_parse_response(odd, sizeof(odd), regs, 4, &ex));
    uint8_t truncated[11] = {0, 7, 0, 0, 0, 7, 1, 3, 4, 0, 0}; // byte count 4 but only 2 present
    TEST_ASSERT_EQUAL_INT(-1, pc_modbus_parse_response(truncated, sizeof(truncated), regs, 4, &ex));
}

// parse stops at max_regs (extra registers dropped), and tolerates a null regs_out (counts without writing).
void test_parse_max_regs_and_null_out()
{
    uint8_t ex = 0xFF;
    // A 4-register response (byte count 8), len = 9 + 8 = 17.
    uint8_t adu[17] = {0, 7, 0, 0, 0, 11, 1, 3, 8, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    uint16_t regs[2];
    int got = pc_modbus_parse_response(adu, sizeof(adu), regs, 2, &ex); // only room for 2
    TEST_ASSERT_EQUAL_INT(2, got);
    TEST_ASSERT_EQUAL_HEX16(0x1122, regs[0]);
    TEST_ASSERT_EQUAL_HEX16(0x3344, regs[1]);
    // Null regs_out: still counts every register present.
    int got2 = pc_modbus_parse_response(adu, sizeof(adu), NULL, 8, &ex);
    TEST_ASSERT_EQUAL_INT(4, got2);
}

// parse accepts FC 0x04 (read input registers) as a valid non-exception response function.
void test_parse_accepts_input_regs_function()
{
    uint16_t regs[4];
    uint8_t ex = 0xFF;
    uint8_t adu[11] = {0, 7, 0, 0, 0, 5, 1, 0x04, 2, 0x12, 0x34}; // FC 0x04, byte count 2, one reg
    int got = pc_modbus_parse_response(adu, sizeof(adu), regs, 4, &ex);
    TEST_ASSERT_EQUAL_INT(1, got);
    TEST_ASSERT_EQUAL_UINT8(0, ex);
    TEST_ASSERT_EQUAL_HEX16(0x1234, regs[0]);
}

// --- Write requests (FC 0x06 / 0x10) ---

void test_build_write_single_bytes()
{
    uint8_t adu[16];
    size_t n = pc_modbus_build_write_single(0x0102, 1, 0x0013, 0xABCD, adu, sizeof(adu));
    TEST_ASSERT_EQUAL_size_t(12, n);
    const uint8_t expect[12] = {0x01, 0x02, 0x00, 0x00, 0x00, 0x06, 0x01, 0x06, 0x00, 0x13, 0xAB, 0xCD};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, adu, 12);
}

void test_round_trip_write_single()
{
    uint8_t req[16];
    size_t rn = pc_modbus_build_write_single(3, 1, 7, 0x5A5A, req, sizeof(req));
    uint8_t resp[MODBUS_ADU_MAX];
    size_t pn = pc_modbus_process_adu(req, rn, resp, sizeof(resp)); // the slave applies + echoes
    TEST_ASSERT_TRUE(pn > 0);

    uint16_t addr = 0;
    uint8_t ex = 0xFF;
    int w = pc_modbus_parse_write_response(resp, pn, &addr, &ex);
    TEST_ASSERT_EQUAL_INT(1, w);
    TEST_ASSERT_EQUAL_UINT8(0, ex);
    TEST_ASSERT_EQUAL_HEX16(7, addr);
    TEST_ASSERT_EQUAL_HEX16(0x5A5A, pc_modbus_get_holding_reg(7)); // the slave actually stored it
}

void test_build_write_multiple_bytes()
{
    uint8_t adu[32];
    const uint16_t vals[2] = {0x1111, 0x2222};
    size_t n = pc_modbus_build_write_multiple(0x0102, 1, 0x0000, vals, 2, adu, sizeof(adu));
    TEST_ASSERT_EQUAL_size_t(17, n); // 13 + 2*2
    // MBAP length = unit(1) + PDU(6 + 4) = 11; byte count = 4.
    const uint8_t expect[17] = {0x01, 0x02, 0x00, 0x00, 0x00, 0x0B, 0x01, 0x10, 0x00,
                                0x00, 0x00, 0x02, 0x04, 0x11, 0x11, 0x22, 0x22};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, adu, 17);
}

void test_round_trip_write_multiple()
{
    const uint16_t vals[3] = {0xDEAD, 0xBEEF, 0xF00D};
    uint8_t req[32];
    size_t rn = pc_modbus_build_write_multiple(5, 1, 30, vals, 3, req, sizeof(req));
    uint8_t resp[MODBUS_ADU_MAX];
    size_t pn = pc_modbus_process_adu(req, rn, resp, sizeof(resp));
    TEST_ASSERT_TRUE(pn > 0);

    uint16_t start = 0;
    uint8_t ex = 0xFF;
    int w = pc_modbus_parse_write_response(resp, pn, &start, &ex);
    TEST_ASSERT_EQUAL_INT(3, w); // three registers written
    TEST_ASSERT_EQUAL_UINT8(0, ex);
    TEST_ASSERT_EQUAL_HEX16(30, start);
    TEST_ASSERT_EQUAL_HEX16(0xDEAD, pc_modbus_get_holding_reg(30));
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, pc_modbus_get_holding_reg(31));
    TEST_ASSERT_EQUAL_HEX16(0xF00D, pc_modbus_get_holding_reg(32));
}

void test_build_write_rejects_bad_args()
{
    uint8_t adu[300];
    const uint16_t vals[2] = {1, 2};
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_build_write_single(1, 1, 0, 5, NULL, 16));         // null out
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_build_write_single(1, 1, 0, 5, adu, 4));              // buffer too small
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_build_write_multiple(1, 1, 0, vals, 2, NULL, 32)); // null out
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_build_write_multiple(1, 1, 0, NULL, 2, adu, 32));  // null values
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_build_write_multiple(1, 1, 0, vals, 0, adu, 32));     // count 0
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_build_write_multiple(1, 1, 0, vals, 124, adu, 300));  // count > 123
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_build_write_multiple(1, 1, 0, vals, 2, adu, 16));     // buffer too small
}

void test_parse_write_response_edges()
{
    uint16_t addr = 0xFFFF;
    uint8_t ex = 0;
    // Exception reply (FC 0x06 | 0x80, code 2) -> 0 written, exception set.
    uint8_t exc[9] = {0, 3, 0, 0, 0, 3, 1, 0x86, 0x02};
    TEST_ASSERT_EQUAL_INT(0, pc_modbus_parse_write_response(exc, sizeof(exc), &addr, &ex));
    TEST_ASSERT_EQUAL_UINT8(0x02, ex);
    TEST_ASSERT_EQUAL_HEX16(0, addr); // addr_out zeroed on the exception path
    // A short frame and a non-write function code both fail closed.
    uint8_t shortf[11] = {0, 3, 0, 0, 0, 5, 1, 0x06, 0, 7, 0}; // only 11 bytes, needs 12
    TEST_ASSERT_EQUAL_INT(-1, pc_modbus_parse_write_response(shortf, sizeof(shortf), &addr, &ex));
    uint8_t badfc[12] = {0, 3, 0, 0, 0, 6, 1, 0x03, 0, 7, 0, 1}; // FC 0x03 is a read, not a write
    TEST_ASSERT_EQUAL_INT(-1, pc_modbus_parse_write_response(badfc, sizeof(badfc), &addr, &ex));
    // A bad protocol id fails closed.
    uint8_t badproto[12] = {0, 3, 0, 1, 0, 6, 1, 0x06, 0, 7, 0xAB, 0xCD};
    TEST_ASSERT_EQUAL_INT(-1, pc_modbus_parse_write_response(badproto, sizeof(badproto), &addr, &ex));
    TEST_ASSERT_EQUAL_INT(-1, pc_modbus_parse_write_response(NULL, 12, &addr, &ex)); // null adu
}

// ── bit access: coils (FC 0x01 / 0x05 / 0x0F) and discrete inputs (FC 0x02) ──────────────────────
void test_round_trip_read_coils()
{
    pc_modbus_set_coil(0, true);
    pc_modbus_set_coil(1, false);
    pc_modbus_set_coil(2, true);
    pc_modbus_set_coil(9, true); // spans a byte boundary within the 10-bit read

    uint8_t req[16];
    size_t rn = pc_modbus_build_read_bits((uint8_t)MODBUS_FC_READ_COILS, 7, 1, 0, 10, req, sizeof(req));
    TEST_ASSERT_EQUAL_size_t(12, rn);
    TEST_ASSERT_EQUAL_HEX8(0x01, req[7]);

    uint8_t resp[MODBUS_ADU_MAX];
    size_t pn = pc_modbus_process_adu(req, rn, resp, sizeof(resp)); // the slave answers
    TEST_ASSERT_TRUE(pn > 0);

    uint8_t bits[10];
    uint8_t ex = 0xFF;
    int got = pc_modbus_parse_read_bits_response(resp, pn, 10, bits, sizeof(bits), &ex);
    TEST_ASSERT_EQUAL_INT(10, got);
    TEST_ASSERT_EQUAL_UINT8(0, ex);
    TEST_ASSERT_EQUAL_UINT8(1, bits[0]);
    TEST_ASSERT_EQUAL_UINT8(0, bits[1]);
    TEST_ASSERT_EQUAL_UINT8(1, bits[2]);
    TEST_ASSERT_EQUAL_UINT8(1, bits[9]);
    TEST_ASSERT_EQUAL_UINT8(0, bits[8]);
}

void test_round_trip_read_discrete_inputs()
{
    pc_modbus_set_discrete_input(3, true);
    pc_modbus_set_discrete_input(4, true);

    uint8_t req[16];
    size_t rn = pc_modbus_build_read_bits((uint8_t)MODBUS_FC_READ_DISCRETE_INPUTS, 8, 1, 0, 6, req,
                                          sizeof(req));
    TEST_ASSERT_EQUAL_HEX8(0x02, req[7]);
    uint8_t resp[MODBUS_ADU_MAX];
    size_t pn = pc_modbus_process_adu(req, rn, resp, sizeof(resp));
    uint8_t bits[6];
    uint8_t ex = 0xFF;
    int got = pc_modbus_parse_read_bits_response(resp, pn, 6, bits, sizeof(bits), &ex);
    TEST_ASSERT_EQUAL_INT(6, got);
    TEST_ASSERT_EQUAL_UINT8(1, bits[3]);
    TEST_ASSERT_EQUAL_UINT8(1, bits[4]);
    TEST_ASSERT_EQUAL_UINT8(0, bits[5]);
}

void test_round_trip_write_single_coil()
{
    pc_modbus_set_coil(5, false);
    uint8_t req[16];
    size_t rn = pc_modbus_build_write_single_coil(11, 1, 5, true, req, sizeof(req));
    TEST_ASSERT_EQUAL_size_t(12, rn);
    TEST_ASSERT_EQUAL_HEX8(0x05, req[7]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, req[10]); // on encodes as 0xFF00
    TEST_ASSERT_EQUAL_HEX8(0x00, req[11]);

    uint8_t resp[MODBUS_ADU_MAX];
    size_t pn = pc_modbus_process_adu(req, rn, resp, sizeof(resp)); // slave applies + echoes
    uint16_t addr = 0xFFFF;
    uint8_t ex = 0xFF;
    int wrote = pc_modbus_parse_write_response(resp, pn, &addr, &ex);
    TEST_ASSERT_EQUAL_INT(1, wrote);
    TEST_ASSERT_EQUAL_UINT8(0, ex);
    TEST_ASSERT_EQUAL_HEX16(5, addr);
    TEST_ASSERT_TRUE(pc_modbus_get_coil(5)); // the write actually took effect
}

void test_round_trip_write_multiple_coils()
{
    // Clear then write an alternating pattern across a byte boundary.
    for (uint16_t a = 0; a < 12; a++)
    {
        pc_modbus_set_coil(a, false);
    }
    const uint8_t pattern[12] = {1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1};

    uint8_t req[24];
    size_t rn = pc_modbus_build_write_multiple_coils(12, 1, 0, pattern, 12, req, sizeof(req));
    TEST_ASSERT_EQUAL_size_t(7 + 6 + 2, rn); // MBAP + fc/start/count/bytecount + ceil(12/8)=2 data bytes
    TEST_ASSERT_EQUAL_HEX8(0x0F, req[7]);
    TEST_ASSERT_EQUAL_HEX8(2, req[12]);    // byte count
    TEST_ASSERT_EQUAL_HEX8(0x55, req[13]); // bits 0..7 = 1,0,1,0,1,0,1,0 -> 0x55 LSB-first
    TEST_ASSERT_EQUAL_HEX8(0x0B, req[14]); // bits 8..11 = 1,1,0,1 -> 0x0B

    uint8_t resp[MODBUS_ADU_MAX];
    size_t pn = pc_modbus_process_adu(req, rn, resp, sizeof(resp));
    uint16_t addr = 0xFFFF;
    uint8_t ex = 0xFF;
    int wrote = pc_modbus_parse_write_response(resp, pn, &addr, &ex);
    TEST_ASSERT_EQUAL_INT(12, wrote); // the quantity written is echoed
    TEST_ASSERT_EQUAL_HEX16(0, addr);
    for (uint16_t a = 0; a < 12; a++)
    {
        TEST_ASSERT_EQUAL_UINT8(pattern[a], pc_modbus_get_coil(a) ? 1 : 0);
    }
}

void test_bit_build_and_parse_guards()
{
    uint8_t adu[16];
    // build_read_bits rejects a non-bit FC, an out-of-range count, and a null buffer.
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_build_read_bits(0x03, 1, 1, 0, 8, adu, sizeof(adu))); // FC 0x03 is a read reg
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_build_read_bits(0x01, 1, 1, 0, 0, adu, sizeof(adu))); // count 0
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_build_read_bits(0x01, 1, 1, 0, 2001, adu, sizeof(adu))); // count > 2000
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_build_read_bits(0x01, 1, 1, 0, 8, NULL, 16));
    // write coil builders reject bad args.
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_build_write_single_coil(1, 1, 0, true, NULL, 16));
    const uint8_t bits[4] = {1, 0, 1, 1};
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_build_write_multiple_coils(1, 1, 0, NULL, 4, adu, sizeof(adu)));
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_build_write_multiple_coils(1, 1, 0, bits, 0, adu, sizeof(adu)));    // count 0
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_build_write_multiple_coils(1, 1, 0, bits, 1969, adu, sizeof(adu))); // > 1968

    // parse_read_bits_response: a byte count that disagrees with the requested count fails closed.
    uint8_t resp[16] = {0, 7, 0, 0, 0, 4, 1, 0x01, 2, 0x05, 0x00}; // byte count 2 but only 4 bits requested (needs 1)
    uint8_t out[8];
    uint8_t ex = 0;
    TEST_ASSERT_EQUAL_INT(-1, pc_modbus_parse_read_bits_response(resp, 11, 4, out, sizeof(out), &ex));
    // An exception response sets the code and returns 0.
    uint8_t exc[9] = {0, 7, 0, 0, 0, 3, 1, 0x81, 0x02};
    TEST_ASSERT_EQUAL_INT(0, pc_modbus_parse_read_bits_response(exc, sizeof(exc), 4, out, sizeof(out), &ex));
    TEST_ASSERT_EQUAL_UINT8(0x02, ex);
}

// ── FC 0x16 Mask Write Register, FC 0x17 Read/Write Multiple Registers ───────────────────────────
void test_round_trip_mask_write()
{
    pc_modbus_set_holding_reg(10, 0x1234);
    // reg = (0x1234 & 0xF0FF) | (0x0500 & ~0xF0FF) = 0x1034 | 0x0500 = 0x1534.
    uint8_t req[16];
    size_t rn = pc_modbus_build_mask_write(5, 1, 10, 0xF0FF, 0x0500, req, sizeof(req));
    TEST_ASSERT_EQUAL_size_t(14, rn); // MBAP(7) + FC + addr(2) + and(2) + or(2)
    TEST_ASSERT_EQUAL_HEX8(0x16, req[7]);

    uint8_t resp[MODBUS_ADU_MAX];
    size_t pn = pc_modbus_process_adu(req, rn, resp, sizeof(resp));
    uint16_t addr = 0, andm = 0, orm = 0;
    uint8_t ex = 0xFF;
    int r = pc_modbus_parse_mask_write_response(resp, pn, &addr, &andm, &orm, &ex);
    TEST_ASSERT_EQUAL_INT(1, r);
    TEST_ASSERT_EQUAL_UINT8(0, ex);
    TEST_ASSERT_EQUAL_HEX16(10, addr);
    TEST_ASSERT_EQUAL_HEX16(0xF0FF, andm);
    TEST_ASSERT_EQUAL_HEX16(0x0500, orm);
    TEST_ASSERT_EQUAL_HEX16(0x1534, pc_modbus_get_holding_reg(10)); // the mask took effect
}

void test_round_trip_read_write_multiple()
{
    pc_modbus_set_holding_reg(20, 0x1111);
    pc_modbus_set_holding_reg(21, 0x2222);
    // Write 0xAAAA/0xBBBB to 20,21 and read the same span back: the write is applied first (§6.17), so the
    // read reflects the new values.
    const uint16_t wvals[2] = {0xAAAA, 0xBBBB};
    uint8_t req[32];
    size_t rn = pc_modbus_build_read_write_multiple(9, 1, /*read*/ 20, 2, /*write*/ 20, wvals, 2, req, sizeof(req));
    TEST_ASSERT_EQUAL_size_t(7 + 10 + 4, rn); // MBAP + 10-byte fixed PDU head + 2*2 write data
    TEST_ASSERT_EQUAL_HEX8(0x17, req[7]);

    uint8_t resp[MODBUS_ADU_MAX];
    size_t pn = pc_modbus_process_adu(req, rn, resp, sizeof(resp));
    uint16_t regs[2];
    uint8_t ex = 0xFF;
    int got = pc_modbus_parse_response(resp, pn, regs, 2, &ex);
    TEST_ASSERT_EQUAL_INT(2, got);
    TEST_ASSERT_EQUAL_UINT8(0, ex);
    TEST_ASSERT_EQUAL_HEX16(0xAAAA, regs[0]);
    TEST_ASSERT_EQUAL_HEX16(0xBBBB, regs[1]);
    TEST_ASSERT_EQUAL_HEX16(0xBBBB, pc_modbus_get_holding_reg(21)); // and the write persisted
}

void test_fc16_17_guards()
{
    uint8_t adu[32];
    const uint16_t vals[2] = {1, 2};
    // Mask-write build guards.
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_build_mask_write(1, 1, 0, 0, 0, NULL, 16));
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_build_mask_write(1, 1, 0, 0, 0, adu, 8)); // too small
    // Read/write build guards.
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_build_read_write_multiple(1, 1, 0, 2, 0, NULL, 2, adu, sizeof(adu)));
    TEST_ASSERT_EQUAL_size_t(0,
                             pc_modbus_build_read_write_multiple(1, 1, 0, 0, 0, vals, 2, adu, sizeof(adu))); // read 0
    TEST_ASSERT_EQUAL_size_t(
        0, pc_modbus_build_read_write_multiple(1, 1, 0, 126, 0, vals, 2, adu, sizeof(adu))); // read>125
    TEST_ASSERT_EQUAL_size_t(0,
                             pc_modbus_build_read_write_multiple(1, 1, 0, 2, 0, vals, 0, adu, sizeof(adu))); // write 0
    TEST_ASSERT_EQUAL_size_t(
        0, pc_modbus_build_read_write_multiple(1, 1, 0, 2, 0, vals, 122, adu, sizeof(adu))); // write>121

    // An out-of-range mask-write address surfaces as an exception.
    uint8_t req[16];
    size_t rn = pc_modbus_build_mask_write(1, 1, 60000, 0xFFFF, 0, req, sizeof(req));
    uint8_t resp[MODBUS_ADU_MAX];
    size_t pn = pc_modbus_process_adu(req, rn, resp, sizeof(resp));
    uint8_t ex = 0;
    TEST_ASSERT_EQUAL_INT(0, pc_modbus_parse_mask_write_response(resp, pn, NULL, NULL, NULL, &ex));
    TEST_ASSERT_EQUAL_UINT8(MODBUS_EX_ILLEGAL_DATA_ADDRESS, ex);
    // parse_mask_write_response rejects a normal (non-exception) frame that is too short, and a wrong FC.
    uint8_t shortf[10] = {0, 1, 0, 0, 0, 8, 1, 0x16, 0, 0}; // FC 0x16 but only 10 bytes, needs 14
    TEST_ASSERT_EQUAL_INT(-1,
                          pc_modbus_parse_mask_write_response(shortf, sizeof(shortf), NULL, NULL, NULL, &ex));
    uint8_t badfc[14] = {0, 1, 0, 0, 0, 8, 1, 0x06, 0, 0, 0, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(-1,
                          pc_modbus_parse_mask_write_response(badfc, sizeof(badfc), NULL, NULL, NULL, &ex));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_build_read_bytes);
    RUN_TEST(test_build_rejects_bad_args);
    RUN_TEST(test_round_trip_holding_regs);
    RUN_TEST(test_round_trip_exception);
    RUN_TEST(test_parse_short_frame_fails);
    RUN_TEST(test_build_null_out_and_input_fc);
    RUN_TEST(test_parse_null_adu);
    RUN_TEST(test_parse_bad_protocol_id);
    RUN_TEST(test_parse_unexpected_function);
    RUN_TEST(test_parse_exception_null_out);
    RUN_TEST(test_parse_bad_byte_count);
    RUN_TEST(test_parse_max_regs_and_null_out);
    RUN_TEST(test_parse_accepts_input_regs_function);
    RUN_TEST(test_build_write_single_bytes);
    RUN_TEST(test_round_trip_write_single);
    RUN_TEST(test_build_write_multiple_bytes);
    RUN_TEST(test_round_trip_write_multiple);
    RUN_TEST(test_build_write_rejects_bad_args);
    RUN_TEST(test_parse_write_response_edges);
    RUN_TEST(test_round_trip_read_coils);
    RUN_TEST(test_round_trip_read_discrete_inputs);
    RUN_TEST(test_round_trip_write_single_coil);
    RUN_TEST(test_round_trip_write_multiple_coils);
    RUN_TEST(test_bit_build_and_parse_guards);
    RUN_TEST(test_round_trip_mask_write);
    RUN_TEST(test_round_trip_read_write_multiple);
    RUN_TEST(test_fc16_17_guards);
    return UNITY_END();
}
