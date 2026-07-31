// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the Modbus TCP slave core (services/fieldbus/modbus): the data model and
// the MBAP/PDU codec (pc_modbus_process_adu). No sockets - pure host tests.

#include "services/fieldbus/modbus/modbus.h"
#include <string.h>
#include <unity.h>

// Last write reported via pc_modbus_on_write().
static uint8_t g_wfc;
static uint16_t g_wstart, g_wcount;
static int g_wcalls;
static void on_write(uint8_t fc, uint16_t start, uint16_t count)
{
    g_wfc = fc;
    g_wstart = start;
    g_wcount = count;
    g_wcalls++;
}

void setUp()
{
    pc_modbus_server_init();
    pc_modbus_on_write(on_write);
    g_wfc = 0;
    g_wstart = g_wcount = 0;
    g_wcalls = 0;
}
void tearDown()
{
}

// Build a Modbus TCP ADU: MBAP(tid, pid=0, len, uid) + pdu.
static size_t build_adu(uint8_t *buf, uint16_t tid, uint8_t uid, const uint8_t *pdu, size_t pdu_len)
{
    uint16_t len = (uint16_t)(1 + pdu_len); // uid + pdu
    buf[0] = (uint8_t)(tid >> 8);
    buf[1] = (uint8_t)(tid & 0xFF);
    buf[2] = 0;
    buf[3] = 0; // protocol id
    buf[4] = (uint8_t)(len >> 8);
    buf[5] = (uint8_t)(len & 0xFF);
    buf[6] = uid;
    memcpy(buf + 7, pdu, pdu_len);
    return 7 + pdu_len;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

// ---------------------------------------------------------------------------

void test_read_holding_registers()
{
    pc_modbus_set_holding_reg(0, 0x1234);
    pc_modbus_set_holding_reg(1, 0xABCD);
    pc_modbus_set_holding_reg(2, 0x0001);

    uint8_t pdu[] = {0x03, 0x00, 0x00, 0x00, 0x03}; // FC3, start 0, qty 3
    uint8_t req[260], resp[260];
    size_t rl = build_adu(req, 0x0001, 0x11, pdu, sizeof(pdu));
    size_t n = pc_modbus_process_adu(req, rl, resp, sizeof(resp));

    TEST_ASSERT_EQUAL_size_t(7 + 2 + 6, n);           // MBAP + fc + bytecount + 3*2
    TEST_ASSERT_EQUAL_UINT16(0x0001, rd16(resp));     // tid echoed
    TEST_ASSERT_EQUAL_UINT16(0x0000, rd16(resp + 2)); // pid 0
    TEST_ASSERT_EQUAL_UINT16(1 + 8, rd16(resp + 4));  // len = uid + 8-byte pdu (fc + count + 3*2)
    TEST_ASSERT_EQUAL_UINT8(0x11, resp[6]);           // uid echoed
    TEST_ASSERT_EQUAL_UINT8(0x03, resp[7]);           // fc
    TEST_ASSERT_EQUAL_UINT8(6, resp[8]);              // byte count
    TEST_ASSERT_EQUAL_UINT16(0x1234, rd16(resp + 9));
    TEST_ASSERT_EQUAL_UINT16(0xABCD, rd16(resp + 11));
    TEST_ASSERT_EQUAL_UINT16(0x0001, rd16(resp + 13));
}

void test_read_input_registers()
{
    pc_modbus_set_input_reg(5, 0xBEEF);
    uint8_t pdu[] = {0x04, 0x00, 0x05, 0x00, 0x01};
    uint8_t req[260], resp[260];
    size_t rl = build_adu(req, 0x0002, 0x01, pdu, sizeof(pdu));
    size_t n = pc_modbus_process_adu(req, rl, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_size_t(7 + 2 + 2, n);
    TEST_ASSERT_EQUAL_UINT8(0x04, resp[7]);
    TEST_ASSERT_EQUAL_UINT16(0xBEEF, rd16(resp + 9));
}

void test_read_coils_packs_bits()
{
    pc_modbus_set_coil(0, true);
    pc_modbus_set_coil(1, false);
    pc_modbus_set_coil(2, true);
    pc_modbus_set_coil(9, true); // second byte, bit 1

    uint8_t pdu[] = {0x01, 0x00, 0x00, 0x00, 0x0A}; // FC1, start 0, qty 10
    uint8_t req[260], resp[260];
    size_t rl = build_adu(req, 1, 1, pdu, sizeof(pdu));
    size_t n = pc_modbus_process_adu(req, rl, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_size_t(7 + 2 + 2, n); // 10 bits -> 2 bytes
    TEST_ASSERT_EQUAL_UINT8(0x01, resp[7]);
    TEST_ASSERT_EQUAL_UINT8(2, resp[8]);
    TEST_ASSERT_EQUAL_UINT8(0x05, resp[9]);  // bits 0 and 2 set
    TEST_ASSERT_EQUAL_UINT8(0x02, resp[10]); // bit 9 -> second byte bit 1
}

void test_write_single_coil()
{
    uint8_t pdu[] = {0x05, 0x00, 0x03, 0xFF, 0x00}; // set coil 3 ON
    uint8_t req[260], resp[260];
    size_t rl = build_adu(req, 7, 1, pdu, sizeof(pdu));
    size_t n = pc_modbus_process_adu(req, rl, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_size_t(7 + 5, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(pdu, resp + 7, 5); // echo
    TEST_ASSERT_TRUE(pc_modbus_get_coil(3));
    TEST_ASSERT_EQUAL_INT(1, g_wcalls);
    TEST_ASSERT_EQUAL_UINT8(0x05, g_wfc);
    TEST_ASSERT_EQUAL_UINT16(3, g_wstart);
}

void test_write_single_register()
{
    uint8_t pdu[] = {0x06, 0x00, 0x0A, 0x12, 0x34};
    uint8_t req[260], resp[260];
    size_t rl = build_adu(req, 7, 1, pdu, sizeof(pdu));
    size_t n = pc_modbus_process_adu(req, rl, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_size_t(7 + 5, n);
    TEST_ASSERT_EQUAL_UINT16(0x1234, pc_modbus_get_holding_reg(10));
    TEST_ASSERT_EQUAL_INT(1, g_wcalls);
}

void test_write_multiple_registers()
{
    uint8_t pdu[] = {0x10, 0x00, 0x02, 0x00, 0x02, 0x04, 0xAA, 0xBB, 0xCC, 0xDD};
    uint8_t req[260], resp[260];
    size_t rl = build_adu(req, 7, 1, pdu, sizeof(pdu));
    size_t n = pc_modbus_process_adu(req, rl, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_size_t(7 + 5, n);
    TEST_ASSERT_EQUAL_UINT8(0x10, resp[7]);
    TEST_ASSERT_EQUAL_UINT16(2, rd16(resp + 8));  // start
    TEST_ASSERT_EQUAL_UINT16(2, rd16(resp + 10)); // qty
    TEST_ASSERT_EQUAL_UINT16(0xAABB, pc_modbus_get_holding_reg(2));
    TEST_ASSERT_EQUAL_UINT16(0xCCDD, pc_modbus_get_holding_reg(3));
    TEST_ASSERT_EQUAL_UINT16(2, g_wcount);
}

void test_write_multiple_coils()
{
    // qty 5, 1 byte of data: bits 0..4 = 1,0,1,1,0 -> 0x0D
    uint8_t pdu[] = {0x0F, 0x00, 0x00, 0x00, 0x05, 0x01, 0x0D};
    uint8_t req[260], resp[260];
    size_t rl = build_adu(req, 7, 1, pdu, sizeof(pdu));
    size_t n = pc_modbus_process_adu(req, rl, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_size_t(7 + 5, n);
    TEST_ASSERT_TRUE(pc_modbus_get_coil(0));
    TEST_ASSERT_FALSE(pc_modbus_get_coil(1));
    TEST_ASSERT_TRUE(pc_modbus_get_coil(2));
    TEST_ASSERT_TRUE(pc_modbus_get_coil(3));
    TEST_ASSERT_FALSE(pc_modbus_get_coil(4));
    TEST_ASSERT_EQUAL_UINT16(5, g_wcount);
}

void test_exception_illegal_function()
{
    uint8_t pdu[] = {0x7F, 0x00, 0x00}; // unsupported FC
    uint8_t req[260], resp[260];
    size_t rl = build_adu(req, 7, 1, pdu, sizeof(pdu));
    size_t n = pc_modbus_process_adu(req, rl, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_size_t(7 + 2, n);
    TEST_ASSERT_EQUAL_UINT8(0x7F | 0x80, resp[7]);
    TEST_ASSERT_EQUAL_UINT8(ModbusException::MODBUS_EX_ILLEGAL_FUNCTION, resp[8]);
}

void test_exception_illegal_address()
{
    // Read holding regs beyond the 64-register table.
    uint8_t pdu[] = {0x03, 0x00, 0x3C, 0x00, 0x0A}; // start 60, qty 10 -> 70 > 64
    uint8_t req[260], resp[260];
    size_t rl = build_adu(req, 7, 1, pdu, sizeof(pdu));
    size_t n = pc_modbus_process_adu(req, rl, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_size_t(7 + 2, n);
    TEST_ASSERT_EQUAL_UINT8(0x03 | 0x80, resp[7]);
    TEST_ASSERT_EQUAL_UINT8(ModbusException::MODBUS_EX_ILLEGAL_DATA_ADDRESS, resp[8]);
}

void test_exception_illegal_value()
{
    uint8_t pdu[] = {0x03, 0x00, 0x00, 0x00, 0x00}; // qty 0
    uint8_t req[260], resp[260];
    size_t rl = build_adu(req, 7, 1, pdu, sizeof(pdu));
    size_t n = pc_modbus_process_adu(req, rl, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_size_t(7 + 2, n);
    TEST_ASSERT_EQUAL_UINT8(ModbusException::MODBUS_EX_ILLEGAL_DATA_VALUE, resp[8]);
}

void test_write_single_coil_bad_value()
{
    uint8_t pdu[] = {0x05, 0x00, 0x00, 0x12, 0x34}; // not 0x0000/0xFF00
    uint8_t req[260], resp[260];
    size_t rl = build_adu(req, 7, 1, pdu, sizeof(pdu));
    pc_modbus_process_adu(req, rl, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_UINT8(0x05 | 0x80, resp[7]);
    TEST_ASSERT_EQUAL_UINT8(ModbusException::MODBUS_EX_ILLEGAL_DATA_VALUE, resp[8]);
    TEST_ASSERT_EQUAL_INT(0, g_wcalls); // not applied
}

void test_non_modbus_protocol_id_ignored()
{
    uint8_t pdu[] = {0x03, 0x00, 0x00, 0x00, 0x01};
    uint8_t req[260], resp[260];
    size_t rl = build_adu(req, 7, 1, pdu, sizeof(pdu));
    req[3] = 0x01; // corrupt the protocol id (must be 0)
    size_t n = pc_modbus_process_adu(req, rl, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_size_t(0, n); // not a Modbus frame -> no response
}

void test_truncated_frame_ignored()
{
    uint8_t pdu[] = {0x03, 0x00, 0x00, 0x00, 0x03};
    uint8_t req[260], resp[260];
    size_t rl = build_adu(req, 7, 1, pdu, sizeof(pdu));
    size_t n = pc_modbus_process_adu(req, rl - 2, resp, sizeof(resp)); // drop 2 bytes
    TEST_ASSERT_EQUAL_size_t(0, n);                                    // length field disagrees -> wait/ignore
}

// Run a PDU through the TCP ADU path and return the response length.
static size_t run_pdu(const uint8_t *pdu, size_t plen, uint8_t *resp, size_t pc_resp_cap)
{
    uint8_t req[300];
    size_t rl = build_adu(req, 1, 1, pdu, plen);
    return pc_modbus_process_adu(req, rl, resp, pc_resp_cap);
}

// Assert a PDU yields a Modbus exception (fc|0x80, code).
static void assert_exception(const uint8_t *pdu, size_t plen, uint8_t fc, uint8_t code)
{
    uint8_t resp[64];
    size_t n = run_pdu(pdu, plen, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_size_t(7 + 2, n);
    TEST_ASSERT_EQUAL_UINT8((uint8_t)(fc | 0x80), resp[7]);
    TEST_ASSERT_EQUAL_UINT8(code, resp[8]);
}

// Discrete inputs, out-of-range accessors, and an FC2 read (never exercised elsewhere).
void test_discrete_and_input_accessors()
{
    pc_modbus_set_discrete_input(3, true);
    pc_modbus_set_discrete_input(0xFFFF, true); // out of range -> ignored
    TEST_ASSERT_TRUE(pc_modbus_get_discrete_input(3));
    TEST_ASSERT_FALSE(pc_modbus_get_discrete_input(2));
    TEST_ASSERT_FALSE(pc_modbus_get_discrete_input(0xFFFF)); // out of range -> false
    TEST_ASSERT_EQUAL_UINT16(0, pc_modbus_get_input_reg(0xFFFF));
    pc_modbus_set_coil(0xFFFF, true); // out-of-range coil / holding ignored
    TEST_ASSERT_FALSE(pc_modbus_get_coil(0xFFFF));
    pc_modbus_set_holding_reg(0xFFFF, 1);
    TEST_ASSERT_EQUAL_UINT16(0, pc_modbus_get_holding_reg(0xFFFF));

    const uint8_t pdu[] = {0x02, 0x00, 0x03, 0x00, 0x01}; // FC2 read 1 discrete input @3
    uint8_t resp[64];
    size_t n = run_pdu(pdu, sizeof(pdu), resp, sizeof(resp));
    TEST_ASSERT_EQUAL_size_t(7 + 3, n);
    TEST_ASSERT_EQUAL_UINT8(0x02, resp[7]);
    TEST_ASSERT_EQUAL_UINT8(0x01, resp[9]); // input 3 -> bit 0 of the data byte
}

// Each function code returns the right exception for a short PDU, bad quantity,
// and an out-of-range address.
void test_exceptions_per_function()
{
    const uint8_t EV = (uint8_t)ModbusException::MODBUS_EX_ILLEGAL_DATA_VALUE,
                  EA = (uint8_t)ModbusException::MODBUS_EX_ILLEGAL_DATA_ADDRESS;
    // FC1/FC2 read coils/discrete.
    const uint8_t fc1_short[] = {0x01, 0x00, 0x00};
    assert_exception(fc1_short, 3, 0x01, EV); // < 5
    const uint8_t fc1_qty[] = {0x01, 0x00, 0x00, 0x08, 0x00};
    assert_exception(fc1_qty, 5, 0x01, EV); // qty 2048 > 2000
    const uint8_t fc1_addr[] = {0x01, 0x00, 0x3C, 0x00, 0x0A};
    assert_exception(fc1_addr, 5, 0x01, EA); // start 60 + 10 > 64
    // FC3/FC4 read registers: short (value/address already covered).
    const uint8_t fc3_short[] = {0x03, 0x00, 0x00};
    assert_exception(fc3_short, 3, 0x03, EV);
    // FC5 write single coil: short + out-of-range address.
    const uint8_t fc5_short[] = {0x05, 0x00, 0x00};
    assert_exception(fc5_short, 3, 0x05, EV);
    const uint8_t fc5_addr[] = {0x05, 0x00, 0x40, 0xFF, 0x00}; // addr 64 (>= 64)
    assert_exception(fc5_addr, 5, 0x05, EA);
    // FC6 write single register: short + out-of-range address.
    const uint8_t fc6_short[] = {0x06, 0x00, 0x00};
    assert_exception(fc6_short, 3, 0x06, EV);
    const uint8_t fc6_addr[] = {0x06, 0x00, 0x40, 0x12, 0x34};
    assert_exception(fc6_addr, 5, 0x06, EA);
    // FC15 write multiple coils: short, byte-count mismatch, out-of-range.
    const uint8_t fc15_short[] = {0x0F, 0x00, 0x00, 0x00, 0x05};
    assert_exception(fc15_short, 5, 0x0F, EV);                                  // < 6
    const uint8_t fc15_bc[] = {0x0F, 0x00, 0x00, 0x00, 0x05, 0x02, 0x00, 0x00}; // qty 5 wants bc 1, got 2
    assert_exception(fc15_bc, 8, 0x0F, EV);
    const uint8_t fc15_addr[] = {0x0F, 0x00, 0x3C, 0x00, 0x08, 0x01, 0xFF}; // start 60 + 8 > 64
    assert_exception(fc15_addr, 7, 0x0F, EA);
    // FC16 write multiple registers: short, byte-count mismatch, out-of-range.
    const uint8_t fc16_short[] = {0x10, 0x00, 0x00, 0x00, 0x02};
    assert_exception(fc16_short, 5, 0x10, EV);
    const uint8_t fc16_bc[] = {0x10, 0x00, 0x00, 0x00, 0x02, 0x02, 0, 0}; // qty 2 wants bc 4, got 2
    assert_exception(fc16_bc, 8, 0x10, EV);
    const uint8_t fc16_addr[] = {0x10, 0x00, 0x3C, 0x00, 0x08, 0x10, 0, 0, 0, 0, 0,
                                 0,    0,    0,    0,    0,    0,    0, 0, 0, 0, 0}; // start 60 + 8 > 64
    assert_exception(fc16_addr, 22, 0x10, EA);
}

// A response buffer too small for the reply makes each handler send nothing (0).
void test_small_response_buffer()
{
    uint8_t small[8]; // ADU path leaves 1 octet for the PDU response
    const uint8_t rd_coils[] = {0x01, 0x00, 0x00, 0x00, 0x01};
    TEST_ASSERT_EQUAL_size_t(0, run_pdu(rd_coils, 5, small, 8));
    const uint8_t rd_regs[] = {0x03, 0x00, 0x00, 0x00, 0x01};
    TEST_ASSERT_EQUAL_size_t(0, run_pdu(rd_regs, 5, small, 8));
    const uint8_t wr_coil[] = {0x05, 0x00, 0x00, 0xFF, 0x00};
    TEST_ASSERT_EQUAL_size_t(0, run_pdu(wr_coil, 5, small, 8));
    const uint8_t wr_reg[] = {0x06, 0x00, 0x00, 0x12, 0x34};
    TEST_ASSERT_EQUAL_size_t(0, run_pdu(wr_reg, 5, small, 8));
    const uint8_t wr_coils[] = {0x0F, 0x00, 0x00, 0x00, 0x05, 0x01, 0x0D};
    TEST_ASSERT_EQUAL_size_t(0, run_pdu(wr_coils, 7, small, 8));
    const uint8_t wr_regs[] = {0x10, 0x00, 0x00, 0x00, 0x01, 0x02, 0x12, 0x34};
    TEST_ASSERT_EQUAL_size_t(0, run_pdu(wr_regs, 8, small, 8));
}

#if PC_ENABLE_MODBUS_RTU
// Independent CRC16-Modbus (init 0xFFFF, reflected poly 0xA001) for building RTU
// frames + verifying response CRCs. Anchored to a known vector below.
static uint16_t t_crc16(const uint8_t *d, size_t n)
{
    uint16_t c = 0xFFFF;
    for (size_t i = 0; i < n; i++)
    {
        c ^= d[i];
        for (int b = 0; b < 8; b++)
        {
            c = (c & 1u) ? (uint16_t)((c >> 1) ^ 0xA001u) : (uint16_t)(c >> 1);
        }
    }
    return c;
}

// Build an RTU ADU: [addr][pdu][crc_lo][crc_hi].
static size_t build_rtu(uint8_t *buf, uint8_t addr, const uint8_t *pdu, size_t pdu_len)
{
    buf[0] = addr;
    memcpy(buf + 1, pdu, pdu_len);
    uint16_t c = t_crc16(buf, 1 + pdu_len);
    buf[1 + pdu_len] = (uint8_t)(c & 0xFF);
    buf[2 + pdu_len] = (uint8_t)(c >> 8);
    return 1 + pdu_len + 2;
}

void test_rtu_crc16_known_vector()
{
    const uint8_t f[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x0A};
    TEST_ASSERT_EQUAL_HEX16(0xCDC5, t_crc16(f, sizeof(f))); // wire bytes C5 CD
}

void test_rtu_read_holding_roundtrip()
{
    pc_modbus_set_holding_reg(0, 0x1234);
    pc_modbus_set_holding_reg(1, 0xABCD);
    const uint8_t pdu[] = {0x03, 0x00, 0x00, 0x00, 0x02}; // read 2 holding regs @0
    uint8_t req[16], resp[64];
    size_t rl = build_rtu(req, 0x11, pdu, sizeof(pdu));
    size_t n = pc_modbus_rtu_process_adu(req, rl, resp, sizeof(resp), 0x11);
    TEST_ASSERT_GREATER_THAN(0, n);
    TEST_ASSERT_EQUAL_HEX8(0x11, resp[0]); // echoed slave address
    TEST_ASSERT_EQUAL_HEX8(0x03, resp[1]); // function code
    TEST_ASSERT_EQUAL_HEX8(0x04, resp[2]); // byte count
    TEST_ASSERT_EQUAL_HEX16(0x1234, rd16(resp + 3));
    TEST_ASSERT_EQUAL_HEX16(0xABCD, rd16(resp + 5));
    // Response CRC must validate.
    uint16_t c = t_crc16(resp, n - 2);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(c & 0xFF), resp[n - 2]);
    TEST_ASSERT_EQUAL_HEX8((uint8_t)(c >> 8), resp[n - 1]);
}

void test_rtu_bad_crc_dropped()
{
    const uint8_t pdu[] = {0x03, 0x00, 0x00, 0x00, 0x01};
    uint8_t req[16], resp[64];
    size_t rl = build_rtu(req, 0x11, pdu, sizeof(pdu));
    req[rl - 1] ^= 0xFF; // corrupt the CRC
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_rtu_process_adu(req, rl, resp, sizeof(resp), 0x11));
}

void test_rtu_wrong_address_dropped()
{
    const uint8_t pdu[] = {0x03, 0x00, 0x00, 0x00, 0x01};
    uint8_t req[16], resp[64];
    size_t rl = build_rtu(req, 0x05, pdu, sizeof(pdu));
    TEST_ASSERT_EQUAL_size_t(0,
                             pc_modbus_rtu_process_adu(req, rl, resp, sizeof(resp), 0x11)); // addressed to 0x05, not us
}

void test_rtu_broadcast_executes_without_reply()
{
    const uint8_t pdu[] = {0x06, 0x00, 0x00, 0xBE, 0xEF}; // write single reg @0 = 0xBEEF
    uint8_t req[16], resp[64];
    size_t rl = build_rtu(req, 0x00, pdu, sizeof(pdu));                                        // broadcast address 0
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_rtu_process_adu(req, rl, resp, sizeof(resp), 0x11)); // no reply
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, pc_modbus_get_holding_reg(0));                             // but executed
}

void test_rtu_edge_cases()
{
    uint8_t resp[64];
    const uint8_t tiny[3] = {0x11, 0x03, 0x00};
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_rtu_process_adu(tiny, sizeof(tiny), resp, sizeof(resp), 0x11)); // < 4

    // A valid frame whose reply cannot fit sends nothing (PDU handler returns 0).
    const uint8_t pdu[] = {0x03, 0x00, 0x00, 0x00, 0x01};
    uint8_t req[16];
    size_t rl = build_rtu(req, 0x11, pdu, sizeof(pdu));
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_rtu_process_adu(req, rl, resp, 4, 0x11)); // out_cap tiny
}
#endif // PC_ENABLE_MODBUS_RTU

void test_server_init_bounds_and_handler()
{
    pc_modbus_server_init();
    pc_modbus_set_coil(0xFFFF, true);
    TEST_ASSERT_FALSE(pc_modbus_get_coil(0xFFFF));                  // out of range -> false
    TEST_ASSERT_EQUAL_UINT16(0, pc_modbus_get_holding_reg(0xFFFF)); // out of range -> 0
    (void)pc_modbus_proto_handler();
}

// Input registers round-trip in range and are ignored out of range.
void test_input_register_accessor_bounds()
{
    pc_modbus_set_input_reg(7, 0xCAFE);
    TEST_ASSERT_EQUAL_UINT16(0xCAFE, pc_modbus_get_input_reg(7));
    pc_modbus_set_input_reg(0xFFFF, 0x1111); // out of range -> dropped
    TEST_ASSERT_EQUAL_UINT16(0, pc_modbus_get_input_reg(0xFFFF));
    TEST_ASSERT_EQUAL_UINT16(0xCAFE, pc_modbus_get_input_reg(7)); // neighbours untouched
}

// A zero quantity is rejected for bit reads, and an oversized one for register reads
// (the mirror cases of the checks already covered).
void test_read_quantity_bounds()
{
    const uint8_t EV = (uint8_t)ModbusException::MODBUS_EX_ILLEGAL_DATA_VALUE;
    const uint8_t fc1_zero[] = {0x01, 0x00, 0x00, 0x00, 0x00}; // FC1 qty 0
    assert_exception(fc1_zero, 5, 0x01, EV);
    const uint8_t fc3_big[] = {0x03, 0x00, 0x00, 0x00, 0xC8}; // FC3 qty 200 > 125
    assert_exception(fc3_big, 5, 0x03, EV);
}

// Writing a single coil OFF (value 0x0000) is the other legal value.
void test_write_single_coil_off()
{
    pc_modbus_set_coil(3, true);
    uint8_t pdu[] = {0x05, 0x00, 0x03, 0x00, 0x00}; // clear coil 3
    uint8_t req[260], resp[260];
    size_t rl = build_adu(req, 7, 1, pdu, sizeof(pdu));
    size_t n = pc_modbus_process_adu(req, rl, resp, sizeof(resp));
    TEST_ASSERT_EQUAL_size_t(7 + 5, n);
    TEST_ASSERT_FALSE(pc_modbus_get_coil(3));
    TEST_ASSERT_EQUAL_INT(1, g_wcalls);
}

// With no write callback registered every write path still applies and replies.
void test_writes_without_callback()
{
    pc_modbus_on_write(nullptr);
    uint8_t req[260], resp[260];

    uint8_t fc5[] = {0x05, 0x00, 0x01, 0xFF, 0x00};
    size_t rl = build_adu(req, 1, 1, fc5, sizeof(fc5));
    TEST_ASSERT_EQUAL_size_t(7 + 5, pc_modbus_process_adu(req, rl, resp, sizeof(resp)));
    TEST_ASSERT_TRUE(pc_modbus_get_coil(1));

    uint8_t fc6[] = {0x06, 0x00, 0x02, 0x11, 0x22};
    rl = build_adu(req, 1, 1, fc6, sizeof(fc6));
    TEST_ASSERT_EQUAL_size_t(7 + 5, pc_modbus_process_adu(req, rl, resp, sizeof(resp)));
    TEST_ASSERT_EQUAL_UINT16(0x1122, pc_modbus_get_holding_reg(2));

    uint8_t fc15[] = {0x0F, 0x00, 0x08, 0x00, 0x02, 0x01, 0x03};
    rl = build_adu(req, 1, 1, fc15, sizeof(fc15));
    TEST_ASSERT_EQUAL_size_t(7 + 5, pc_modbus_process_adu(req, rl, resp, sizeof(resp)));
    TEST_ASSERT_TRUE(pc_modbus_get_coil(8));
    TEST_ASSERT_TRUE(pc_modbus_get_coil(9));

    uint8_t fc16[] = {0x10, 0x00, 0x06, 0x00, 0x01, 0x02, 0x33, 0x44};
    rl = build_adu(req, 1, 1, fc16, sizeof(fc16));
    TEST_ASSERT_EQUAL_size_t(7 + 5, pc_modbus_process_adu(req, rl, resp, sizeof(resp)));
    TEST_ASSERT_EQUAL_UINT16(0x3344, pc_modbus_get_holding_reg(6));

    TEST_ASSERT_EQUAL_INT(0, g_wcalls); // nothing was reported: no callback installed
}

// The multi-write requests validate quantity bounds and that the declared byte
// count is actually present in the PDU.
void test_multi_write_field_validation()
{
    const uint8_t EV = (uint8_t)ModbusException::MODBUS_EX_ILLEGAL_DATA_VALUE;
    // FC15: qty 0, qty above the 1968 limit, and a byte count that runs past the PDU.
    const uint8_t fc15_zero[] = {0x0F, 0x00, 0x00, 0x00, 0x00, 0x00};
    assert_exception(fc15_zero, 6, 0x0F, EV);
    const uint8_t fc15_huge[] = {0x0F, 0x00, 0x00, 0x07, 0xD0, 0x00}; // qty 2000 > 1968
    assert_exception(fc15_huge, 6, 0x0F, EV);
    const uint8_t fc15_cut[] = {0x0F, 0x00, 0x00, 0x00, 0x05, 0x01}; // bc 1 promised, absent
    assert_exception(fc15_cut, 6, 0x0F, EV);
    // FC16: the same three shapes.
    const uint8_t fc16_zero[] = {0x10, 0x00, 0x00, 0x00, 0x00, 0x00};
    assert_exception(fc16_zero, 6, 0x10, EV);
    const uint8_t fc16_huge[] = {0x10, 0x00, 0x00, 0x00, 0xC8, 0x00}; // qty 200 > 123
    assert_exception(fc16_huge, 6, 0x10, EV);
    const uint8_t fc16_cut[] = {0x10, 0x00, 0x00, 0x00, 0x01, 0x02}; // bc 2 promised, absent
    assert_exception(fc16_cut, 6, 0x10, EV);
}

// The MBAP framing guards: a runt frame, a response buffer with no room for the
// MBAP header, and a length field below the one-byte-PDU minimum.
void test_adu_framing_guards()
{
    uint8_t pdu[] = {0x03, 0x00, 0x00, 0x00, 0x01};
    uint8_t req[260], resp[260];
    size_t rl = build_adu(req, 7, 1, pdu, sizeof(pdu));

    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_process_adu(req, 7, resp, sizeof(resp))); // req_len < 8
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_process_adu(req, rl, resp, 7));           // resp cap < 8

    // len field = 1 (unit id only, no PDU) with a full-size frame on the wire.
    uint8_t shortlen[] = {0x00, 0x07, 0x00, 0x00, 0x00, 0x01, 0x01, 0x03, 0x00};
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_process_adu(shortlen, sizeof(shortlen), resp, sizeof(resp)));
}

#if PC_ENABLE_MODBUS_RTU
// An RTU response buffer too small even for addr + PDU + CRC is refused up front.
void test_rtu_response_buffer_too_small()
{
    const uint8_t pdu[] = {0x03, 0x00, 0x00, 0x00, 0x01};
    uint8_t req[16], resp[64];
    size_t rl = build_rtu(req, 0x11, pdu, sizeof(pdu));
    TEST_ASSERT_EQUAL_size_t(0, pc_modbus_rtu_process_adu(req, rl, resp, 3, 0x11));
}
#endif // PC_ENABLE_MODBUS_RTU

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_read_holding_registers);
    RUN_TEST(test_read_input_registers);
    RUN_TEST(test_read_coils_packs_bits);
    RUN_TEST(test_write_single_coil);
    RUN_TEST(test_write_single_register);
    RUN_TEST(test_write_multiple_registers);
    RUN_TEST(test_write_multiple_coils);
    RUN_TEST(test_exception_illegal_function);
    RUN_TEST(test_exception_illegal_address);
    RUN_TEST(test_exception_illegal_value);
    RUN_TEST(test_write_single_coil_bad_value);
    RUN_TEST(test_non_modbus_protocol_id_ignored);
    RUN_TEST(test_truncated_frame_ignored);
    RUN_TEST(test_discrete_and_input_accessors);
    RUN_TEST(test_exceptions_per_function);
    RUN_TEST(test_small_response_buffer);
#if PC_ENABLE_MODBUS_RTU
    RUN_TEST(test_rtu_crc16_known_vector);
    RUN_TEST(test_rtu_read_holding_roundtrip);
    RUN_TEST(test_rtu_bad_crc_dropped);
    RUN_TEST(test_rtu_wrong_address_dropped);
    RUN_TEST(test_rtu_broadcast_executes_without_reply);
    RUN_TEST(test_rtu_edge_cases);
#endif
    RUN_TEST(test_server_init_bounds_and_handler);
    RUN_TEST(test_input_register_accessor_bounds);
    RUN_TEST(test_read_quantity_bounds);
    RUN_TEST(test_write_single_coil_off);
    RUN_TEST(test_writes_without_callback);
    RUN_TEST(test_multi_write_field_validation);
    RUN_TEST(test_adu_framing_guards);
#if PC_ENABLE_MODBUS_RTU
    RUN_TEST(test_rtu_response_buffer_too_small);
#endif
    return UNITY_END();
}
