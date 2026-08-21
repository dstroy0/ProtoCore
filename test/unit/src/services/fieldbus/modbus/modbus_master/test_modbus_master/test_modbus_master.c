// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "services/fieldbus/modbus/modbus/modbus.h"
#include "services/fieldbus/modbus/modbus_master/modbus_master.h"
#include <unity.h>

static uint8_t modbus_master_work[16]; // the borrow an entry takes; ModbusMaster never reads it

void setUp()
{
    Modbus.server_init(protocore_modbus_span());
}
void tearDown()
{
}

void test_build_read_bytes()
{
    uint8_t adu[16];
    size_t modbus_master_n =
        ModbusMaster.build_read(modbus_master_work, (uint8_t)MODBUS_FC_READ_HOLDING_REGS, 1, 1, 0, 2, adu, sizeof(adu));
    size_t n = modbus_master_n;
    TEST_ASSERT_EQUAL_size_t(12, n);
    const uint8_t expect[12] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00, 0x00, 0x00, 0x02};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, adu, 12);
}

void test_build_rejects_bad_args()
{
    uint8_t adu[16];
    size_t modbus_master_n = ModbusMaster.build_read(modbus_master_work, 0x06, 1, 1, 0, 2, adu, sizeof(adu));
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);
    modbus_master_n = ModbusMaster.build_read(modbus_master_work, 0x03, 1, 1, 0, 0, adu, sizeof(adu));
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);
    modbus_master_n = ModbusMaster.build_read(modbus_master_work, 0x03, 1, 1, 0, 200, adu, sizeof(adu));
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);
    modbus_master_n = ModbusMaster.build_read(modbus_master_work, 0x03, 1, 1, 0, 2, adu, 4);
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);
}

void test_round_trip_holding_regs()
{
    ModbusV.set_holding_reg_args.addr = 0;
    ModbusV.set_holding_reg_args.value = 0x1234;
    Modbus.set_holding_reg(protocore_modbus_span());
    ModbusV.set_holding_reg_args.addr = 1;
    ModbusV.set_holding_reg_args.value = 0xABCD;
    Modbus.set_holding_reg(protocore_modbus_span());

    uint8_t req[16];
    size_t modbus_master_n =
        ModbusMaster.build_read(modbus_master_work, (uint8_t)MODBUS_FC_READ_HOLDING_REGS, 7, 1, 0, 2, req, sizeof(req));
    size_t rn = modbus_master_n;
    TEST_ASSERT_EQUAL_size_t(12, rn);

    uint8_t resp[MODBUS_ADU_MAX];
    ModbusV.process_adu_args.req = req;
    ModbusV.process_adu_args.req_len = rn;
    ModbusV.process_adu_args.resp = resp;
    ModbusV.process_adu_args.protocore_resp_cap = sizeof(resp);
    Modbus.process_adu(protocore_modbus_span());
    size_t pn = ModbusV.n;
    TEST_ASSERT_TRUE(pn > 0);

    uint16_t regs[4];
    uint8_t ex = 0xFF;
    int modbus_master_i32 = ModbusMaster.parse_response(modbus_master_work, resp, pn, regs, 4, &ex);
    int got = modbus_master_i32;
    TEST_ASSERT_EQUAL_INT(2, got);
    TEST_ASSERT_EQUAL_UINT8(0, ex);
    TEST_ASSERT_EQUAL_HEX16(0x1234, regs[0]);
    TEST_ASSERT_EQUAL_HEX16(0xABCD, regs[1]);
}

void test_round_trip_exception()
{

    uint8_t req[16];
    size_t modbus_master_n = ModbusMaster.build_read(modbus_master_work, (uint8_t)MODBUS_FC_READ_HOLDING_REGS, 9, 1,
                                                     60000, 1, req, sizeof(req));
    size_t rn = modbus_master_n;
    uint8_t resp[MODBUS_ADU_MAX];
    ModbusV.process_adu_args.req = req;
    ModbusV.process_adu_args.req_len = rn;
    ModbusV.process_adu_args.resp = resp;
    ModbusV.process_adu_args.protocore_resp_cap = sizeof(resp);
    Modbus.process_adu(protocore_modbus_span());
    size_t pn = ModbusV.n;
    TEST_ASSERT_TRUE(pn > 0);

    uint16_t regs[4];
    uint8_t ex = 0;
    int modbus_master_i32 = ModbusMaster.parse_response(modbus_master_work, resp, pn, regs, 4, &ex);
    int got = modbus_master_i32;
    TEST_ASSERT_EQUAL_INT(0, got);
    TEST_ASSERT_EQUAL_UINT8(MODBUS_EX_ILLEGAL_DATA_ADDRESS, ex);
}

void test_parse_short_frame_fails()
{
    uint8_t buf[4] = {0, 1, 0, 0};
    int modbus_master_i32 = ModbusMaster.parse_response(modbus_master_work, buf, sizeof(buf), NULL, 0, NULL);
    TEST_ASSERT_EQUAL_INT(-1, modbus_master_i32);
}

void test_build_null_out_and_input_fc()
{
    uint8_t adu[16];
    size_t modbus_master_n = ModbusMaster.build_read(modbus_master_work, 0x03, 1, 1, 0, 2, NULL, 16);
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);
    modbus_master_n = ModbusMaster.build_read(modbus_master_work, 0x04, 1, 1, 0, 2, adu, sizeof(adu));
    size_t n = modbus_master_n;
    TEST_ASSERT_EQUAL_size_t(12, n);
    TEST_ASSERT_EQUAL_HEX8(0x04, adu[7]);
}

void test_parse_null_adu()
{
    uint16_t regs[4];
    uint8_t ex = 0xFF;
    int modbus_master_i32 = ModbusMaster.parse_response(modbus_master_work, NULL, 12, regs, 4, &ex);
    TEST_ASSERT_EQUAL_INT(-1, modbus_master_i32);
}

void test_parse_bad_protocol_id()
{
    uint16_t regs[4];
    uint8_t ex = 0xFF;
    uint8_t adu[13] = {0, 7, 0, 1, 0, 7, 1, 3, 4, 0, 0, 0, 0};
    int modbus_master_i32 = ModbusMaster.parse_response(modbus_master_work, adu, sizeof(adu), regs, 4, &ex);
    TEST_ASSERT_EQUAL_INT(-1, modbus_master_i32);
    adu[2] = 1;
    adu[3] = 0;
    modbus_master_i32 = ModbusMaster.parse_response(modbus_master_work, adu, sizeof(adu), regs, 4, &ex);
    TEST_ASSERT_EQUAL_INT(-1, modbus_master_i32);
}

void test_parse_unexpected_function()
{
    uint16_t regs[4];
    uint8_t ex = 0xFF;
    uint8_t adu[13] = {0, 7, 0, 0, 0, 7, 1, 0x06, 4, 0, 0, 0, 0};
    int modbus_master_i32 = ModbusMaster.parse_response(modbus_master_work, adu, sizeof(adu), regs, 4, &ex);
    TEST_ASSERT_EQUAL_INT(-1, modbus_master_i32);
}

void test_parse_exception_null_out()
{
    uint16_t regs[4];
    uint8_t adu[9] = {0, 9, 0, 0, 0, 3, 1, 0x83, 0x02};
    int modbus_master_i32 = ModbusMaster.parse_response(modbus_master_work, adu, sizeof(adu), regs, 4, NULL);
    TEST_ASSERT_EQUAL_INT(0, modbus_master_i32);
}

void test_parse_bad_byte_count()
{
    uint16_t regs[4];
    uint8_t ex = 0xFF;
    uint8_t odd[13] = {0, 7, 0, 0, 0, 7, 1, 3, 3, 0, 0, 0, 0};
    int modbus_master_i32 = ModbusMaster.parse_response(modbus_master_work, odd, sizeof(odd), regs, 4, &ex);
    TEST_ASSERT_EQUAL_INT(-1, modbus_master_i32);
    uint8_t truncated[11] = {0, 7, 0, 0, 0, 7, 1, 3, 4, 0, 0};
    modbus_master_i32 = ModbusMaster.parse_response(modbus_master_work, truncated, sizeof(truncated), regs, 4, &ex);
    TEST_ASSERT_EQUAL_INT(-1, modbus_master_i32);
}

void test_parse_max_regs_and_null_out()
{
    uint8_t ex = 0xFF;

    uint8_t adu[17] = {0, 7, 0, 0, 0, 11, 1, 3, 8, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    uint16_t regs[2];
    int modbus_master_i32 = ModbusMaster.parse_response(modbus_master_work, adu, sizeof(adu), regs, 2, &ex);
    int got = modbus_master_i32;
    TEST_ASSERT_EQUAL_INT(2, got);
    TEST_ASSERT_EQUAL_HEX16(0x1122, regs[0]);
    TEST_ASSERT_EQUAL_HEX16(0x3344, regs[1]);

    modbus_master_i32 = ModbusMaster.parse_response(modbus_master_work, adu, sizeof(adu), NULL, 8, &ex);
    int got2 = modbus_master_i32;
    TEST_ASSERT_EQUAL_INT(4, got2);
}

void test_parse_accepts_input_regs_function()
{
    uint16_t regs[4];
    uint8_t ex = 0xFF;
    uint8_t adu[11] = {0, 7, 0, 0, 0, 5, 1, 0x04, 2, 0x12, 0x34};
    int modbus_master_i32 = ModbusMaster.parse_response(modbus_master_work, adu, sizeof(adu), regs, 4, &ex);
    int got = modbus_master_i32;
    TEST_ASSERT_EQUAL_INT(1, got);
    TEST_ASSERT_EQUAL_UINT8(0, ex);
    TEST_ASSERT_EQUAL_HEX16(0x1234, regs[0]);
}

void test_build_write_single_bytes()
{
    uint8_t adu[16];
    size_t modbus_master_n =
        ModbusMaster.build_write_single(modbus_master_work, 0x0102, 1, 0x0013, 0xABCD, adu, sizeof(adu));
    size_t n = modbus_master_n;
    TEST_ASSERT_EQUAL_size_t(12, n);
    const uint8_t expect[12] = {0x01, 0x02, 0x00, 0x00, 0x00, 0x06, 0x01, 0x06, 0x00, 0x13, 0xAB, 0xCD};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, adu, 12);
}

void test_round_trip_write_single()
{
    uint8_t req[16];
    size_t modbus_master_n = ModbusMaster.build_write_single(modbus_master_work, 3, 1, 7, 0x5A5A, req, sizeof(req));
    size_t rn = modbus_master_n;
    uint8_t resp[MODBUS_ADU_MAX];
    ModbusV.process_adu_args.req = req;
    ModbusV.process_adu_args.req_len = rn;
    ModbusV.process_adu_args.resp = resp;
    ModbusV.process_adu_args.protocore_resp_cap = sizeof(resp);
    Modbus.process_adu(protocore_modbus_span());
    size_t pn = ModbusV.n;
    TEST_ASSERT_TRUE(pn > 0);

    uint16_t addr = 0;
    uint8_t ex = 0xFF;
    int modbus_master_i32 = ModbusMaster.parse_write_response(modbus_master_work, resp, pn, &addr, &ex);
    int w = modbus_master_i32;
    TEST_ASSERT_EQUAL_INT(1, w);
    TEST_ASSERT_EQUAL_UINT8(0, ex);
    TEST_ASSERT_EQUAL_HEX16(7, addr);
    ModbusV.get_holding_reg_args.addr = 7;
    Modbus.get_holding_reg(protocore_modbus_span());
    TEST_ASSERT_EQUAL_HEX16(0x5A5A, ModbusV.value);
}

void test_build_write_multiple_bytes()
{
    uint8_t adu[32];
    const uint16_t vals[2] = {0x1111, 0x2222};
    size_t modbus_master_n =
        ModbusMaster.build_write_multiple(modbus_master_work, 0x0102, 1, 0x0000, vals, 2, adu, sizeof(adu));
    size_t n = modbus_master_n;
    TEST_ASSERT_EQUAL_size_t(17, n);

    const uint8_t expect[17] = {0x01, 0x02, 0x00, 0x00, 0x00, 0x0B, 0x01, 0x10, 0x00,
                                0x00, 0x00, 0x02, 0x04, 0x11, 0x11, 0x22, 0x22};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, adu, 17);
}

void test_round_trip_write_multiple()
{
    const uint16_t vals[3] = {0xDEAD, 0xBEEF, 0xF00D};
    uint8_t req[32];
    size_t modbus_master_n = ModbusMaster.build_write_multiple(modbus_master_work, 5, 1, 30, vals, 3, req, sizeof(req));
    size_t rn = modbus_master_n;
    uint8_t resp[MODBUS_ADU_MAX];
    ModbusV.process_adu_args.req = req;
    ModbusV.process_adu_args.req_len = rn;
    ModbusV.process_adu_args.resp = resp;
    ModbusV.process_adu_args.protocore_resp_cap = sizeof(resp);
    Modbus.process_adu(protocore_modbus_span());
    size_t pn = ModbusV.n;
    TEST_ASSERT_TRUE(pn > 0);

    uint16_t start = 0;
    uint8_t ex = 0xFF;
    int modbus_master_i32 = ModbusMaster.parse_write_response(modbus_master_work, resp, pn, &start, &ex);
    int w = modbus_master_i32;
    TEST_ASSERT_EQUAL_INT(3, w);
    TEST_ASSERT_EQUAL_UINT8(0, ex);
    TEST_ASSERT_EQUAL_HEX16(30, start);
    ModbusV.get_holding_reg_args.addr = 30;
    Modbus.get_holding_reg(protocore_modbus_span());
    TEST_ASSERT_EQUAL_HEX16(0xDEAD, ModbusV.value);
    ModbusV.get_holding_reg_args.addr = 31;
    Modbus.get_holding_reg(protocore_modbus_span());
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, ModbusV.value);
    ModbusV.get_holding_reg_args.addr = 32;
    Modbus.get_holding_reg(protocore_modbus_span());
    TEST_ASSERT_EQUAL_HEX16(0xF00D, ModbusV.value);
}

void test_build_write_rejects_bad_args()
{
    uint8_t adu[300];
    const uint16_t vals[2] = {1, 2};
    size_t modbus_master_n = ModbusMaster.build_write_single(modbus_master_work, 1, 1, 0, 5, NULL, 16);
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);
    modbus_master_n = ModbusMaster.build_write_single(modbus_master_work, 1, 1, 0, 5, adu, 4);
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);
    modbus_master_n = ModbusMaster.build_write_multiple(modbus_master_work, 1, 1, 0, vals, 2, NULL, 32);
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);
    modbus_master_n = ModbusMaster.build_write_multiple(modbus_master_work, 1, 1, 0, NULL, 2, adu, 32);
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);
    modbus_master_n = ModbusMaster.build_write_multiple(modbus_master_work, 1, 1, 0, vals, 0, adu, 32);
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);
    modbus_master_n = ModbusMaster.build_write_multiple(modbus_master_work, 1, 1, 0, vals, 124, adu, 300);
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);
    modbus_master_n = ModbusMaster.build_write_multiple(modbus_master_work, 1, 1, 0, vals, 2, adu, 16);
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);
}

void test_parse_write_response_edges()
{
    uint16_t addr = 0xFFFF;
    uint8_t ex = 0;

    uint8_t exc[9] = {0, 3, 0, 0, 0, 3, 1, 0x86, 0x02};
    int modbus_master_i32 = ModbusMaster.parse_write_response(modbus_master_work, exc, sizeof(exc), &addr, &ex);
    TEST_ASSERT_EQUAL_INT(0, modbus_master_i32);
    TEST_ASSERT_EQUAL_UINT8(0x02, ex);
    TEST_ASSERT_EQUAL_HEX16(0, addr);

    uint8_t shortf[11] = {0, 3, 0, 0, 0, 5, 1, 0x06, 0, 7, 0};
    modbus_master_i32 = ModbusMaster.parse_write_response(modbus_master_work, shortf, sizeof(shortf), &addr, &ex);
    TEST_ASSERT_EQUAL_INT(-1, modbus_master_i32);
    uint8_t badfc[12] = {0, 3, 0, 0, 0, 6, 1, 0x03, 0, 7, 0, 1};
    modbus_master_i32 = ModbusMaster.parse_write_response(modbus_master_work, badfc, sizeof(badfc), &addr, &ex);
    TEST_ASSERT_EQUAL_INT(-1, modbus_master_i32);

    uint8_t badproto[12] = {0, 3, 0, 1, 0, 6, 1, 0x06, 0, 7, 0xAB, 0xCD};
    modbus_master_i32 = ModbusMaster.parse_write_response(modbus_master_work, badproto, sizeof(badproto), &addr, &ex);
    TEST_ASSERT_EQUAL_INT(-1, modbus_master_i32);
    modbus_master_i32 = ModbusMaster.parse_write_response(modbus_master_work, NULL, 12, &addr, &ex);
    TEST_ASSERT_EQUAL_INT(-1, modbus_master_i32);
}

void test_round_trip_read_coils()
{
    ModbusV.set_coil_args.addr = 0;
    ModbusV.set_coil_args.on = PROTO_TRUE;
    Modbus.set_coil(protocore_modbus_span());
    ModbusV.set_coil_args.addr = 1;
    ModbusV.set_coil_args.on = PROTO_FALSE;
    Modbus.set_coil(protocore_modbus_span());
    ModbusV.set_coil_args.addr = 2;
    ModbusV.set_coil_args.on = PROTO_TRUE;
    Modbus.set_coil(protocore_modbus_span());
    ModbusV.set_coil_args.addr = 9;
    ModbusV.set_coil_args.on = PROTO_TRUE;
    Modbus.set_coil(protocore_modbus_span());

    uint8_t req[16];
    size_t modbus_master_n =
        ModbusMaster.build_read_bits(modbus_master_work, (uint8_t)MODBUS_FC_READ_COILS, 7, 1, 0, 10, req, sizeof(req));
    size_t rn = modbus_master_n;
    TEST_ASSERT_EQUAL_size_t(12, rn);
    TEST_ASSERT_EQUAL_HEX8(0x01, req[7]);

    uint8_t resp[MODBUS_ADU_MAX];
    ModbusV.process_adu_args.req = req;
    ModbusV.process_adu_args.req_len = rn;
    ModbusV.process_adu_args.resp = resp;
    ModbusV.process_adu_args.protocore_resp_cap = sizeof(resp);
    Modbus.process_adu(protocore_modbus_span());
    size_t pn = ModbusV.n;
    TEST_ASSERT_TRUE(pn > 0);

    uint8_t bits[10];
    uint8_t ex = 0xFF;
    int modbus_master_i32 =
        ModbusMaster.parse_read_bits_response(modbus_master_work, resp, pn, 10, bits, sizeof(bits), &ex);
    int got = modbus_master_i32;
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
    ModbusV.set_discrete_input_args.addr = 3;
    ModbusV.set_discrete_input_args.on = PROTO_TRUE;
    Modbus.set_discrete_input(protocore_modbus_span());
    ModbusV.set_discrete_input_args.addr = 4;
    ModbusV.set_discrete_input_args.on = PROTO_TRUE;
    Modbus.set_discrete_input(protocore_modbus_span());

    uint8_t req[16];
    size_t modbus_master_n = ModbusMaster.build_read_bits(modbus_master_work, (uint8_t)MODBUS_FC_READ_DISCRETE_INPUTS,
                                                          8, 1, 0, 6, req, sizeof(req));
    size_t rn = modbus_master_n;
    TEST_ASSERT_EQUAL_HEX8(0x02, req[7]);
    uint8_t resp[MODBUS_ADU_MAX];
    ModbusV.process_adu_args.req = req;
    ModbusV.process_adu_args.req_len = rn;
    ModbusV.process_adu_args.resp = resp;
    ModbusV.process_adu_args.protocore_resp_cap = sizeof(resp);
    Modbus.process_adu(protocore_modbus_span());
    size_t pn = ModbusV.n;
    uint8_t bits[6];
    uint8_t ex = 0xFF;
    int modbus_master_i32 =
        ModbusMaster.parse_read_bits_response(modbus_master_work, resp, pn, 6, bits, sizeof(bits), &ex);
    int got = modbus_master_i32;
    TEST_ASSERT_EQUAL_INT(6, got);
    TEST_ASSERT_EQUAL_UINT8(1, bits[3]);
    TEST_ASSERT_EQUAL_UINT8(1, bits[4]);
    TEST_ASSERT_EQUAL_UINT8(0, bits[5]);
}

void test_round_trip_write_single_coil()
{
    ModbusV.set_coil_args.addr = 5;
    ModbusV.set_coil_args.on = PROTO_FALSE;
    Modbus.set_coil(protocore_modbus_span());
    uint8_t req[16];
    size_t modbus_master_n =
        ModbusMaster.build_write_single_coil(modbus_master_work, 11, 1, 5, PROTO_TRUE, req, sizeof(req));
    size_t rn = modbus_master_n;
    TEST_ASSERT_EQUAL_size_t(12, rn);
    TEST_ASSERT_EQUAL_HEX8(0x05, req[7]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, req[10]);
    TEST_ASSERT_EQUAL_HEX8(0x00, req[11]);

    uint8_t resp[MODBUS_ADU_MAX];
    ModbusV.process_adu_args.req = req;
    ModbusV.process_adu_args.req_len = rn;
    ModbusV.process_adu_args.resp = resp;
    ModbusV.process_adu_args.protocore_resp_cap = sizeof(resp);
    Modbus.process_adu(protocore_modbus_span());
    size_t pn = ModbusV.n;
    uint16_t addr = 0xFFFF;
    uint8_t ex = 0xFF;
    int modbus_master_i32 = ModbusMaster.parse_write_response(modbus_master_work, resp, pn, &addr, &ex);
    int wrote = modbus_master_i32;
    TEST_ASSERT_EQUAL_INT(1, wrote);
    TEST_ASSERT_EQUAL_UINT8(0, ex);
    TEST_ASSERT_EQUAL_HEX16(5, addr);
    ModbusV.get_coil_args.addr = 5;
    Modbus.get_coil(protocore_modbus_span());
    TEST_ASSERT_TRUE(ModbusV.ok);
}

void test_round_trip_write_multiple_coils()
{

    for (uint16_t a = 0; a < 12; a++)
    {
        ModbusV.set_coil_args.addr = a;
        ModbusV.set_coil_args.on = PROTO_FALSE;
        Modbus.set_coil(protocore_modbus_span());
    }
    const uint8_t pattern[12] = {1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1};

    uint8_t req[24];
    size_t modbus_master_n =
        ModbusMaster.build_write_multiple_coils(modbus_master_work, 12, 1, 0, pattern, 12, req, sizeof(req));
    size_t rn = modbus_master_n;
    TEST_ASSERT_EQUAL_size_t(7 + 6 + 2, rn);
    TEST_ASSERT_EQUAL_HEX8(0x0F, req[7]);
    TEST_ASSERT_EQUAL_HEX8(2, req[12]);
    TEST_ASSERT_EQUAL_HEX8(0x55, req[13]);
    TEST_ASSERT_EQUAL_HEX8(0x0B, req[14]);

    uint8_t resp[MODBUS_ADU_MAX];
    ModbusV.process_adu_args.req = req;
    ModbusV.process_adu_args.req_len = rn;
    ModbusV.process_adu_args.resp = resp;
    ModbusV.process_adu_args.protocore_resp_cap = sizeof(resp);
    Modbus.process_adu(protocore_modbus_span());
    size_t pn = ModbusV.n;
    uint16_t addr = 0xFFFF;
    uint8_t ex = 0xFF;
    int modbus_master_i32 = ModbusMaster.parse_write_response(modbus_master_work, resp, pn, &addr, &ex);
    int wrote = modbus_master_i32;
    TEST_ASSERT_EQUAL_INT(12, wrote);
    TEST_ASSERT_EQUAL_HEX16(0, addr);
    for (uint16_t a = 0; a < 12; a++)
    {
        ModbusV.get_coil_args.addr = a;
        Modbus.get_coil(protocore_modbus_span());
        TEST_ASSERT_EQUAL_UINT8(pattern[a], ModbusV.ok ? 1 : 0);
    }
}

void test_bit_build_and_parse_guards()
{
    uint8_t adu[16];

    size_t modbus_master_n = ModbusMaster.build_read_bits(modbus_master_work, 0x03, 1, 1, 0, 8, adu, sizeof(adu));
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);
    modbus_master_n = ModbusMaster.build_read_bits(modbus_master_work, 0x01, 1, 1, 0, 0, adu, sizeof(adu));
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);
    modbus_master_n = ModbusMaster.build_read_bits(modbus_master_work, 0x01, 1, 1, 0, 2001, adu, sizeof(adu));
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);
    modbus_master_n = ModbusMaster.build_read_bits(modbus_master_work, 0x01, 1, 1, 0, 8, NULL, 16);
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);

    modbus_master_n = ModbusMaster.build_write_single_coil(modbus_master_work, 1, 1, 0, PROTO_TRUE, NULL, 16);
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);
    const uint8_t bits[4] = {1, 0, 1, 1};
    modbus_master_n = ModbusMaster.build_write_multiple_coils(modbus_master_work, 1, 1, 0, NULL, 4, adu, sizeof(adu));
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);
    modbus_master_n = ModbusMaster.build_write_multiple_coils(modbus_master_work, 1, 1, 0, bits, 0, adu, sizeof(adu));
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);
    modbus_master_n =
        ModbusMaster.build_write_multiple_coils(modbus_master_work, 1, 1, 0, bits, 1969, adu, sizeof(adu));
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);

    uint8_t resp[16] = {0, 7, 0, 0, 0, 4, 1, 0x01, 2, 0x05, 0x00};
    uint8_t out[8];
    uint8_t ex = 0;
    int modbus_master_i32 =
        ModbusMaster.parse_read_bits_response(modbus_master_work, resp, 11, 4, out, sizeof(out), &ex);
    TEST_ASSERT_EQUAL_INT(-1, modbus_master_i32);

    uint8_t exc[9] = {0, 7, 0, 0, 0, 3, 1, 0x81, 0x02};
    modbus_master_i32 =
        ModbusMaster.parse_read_bits_response(modbus_master_work, exc, sizeof(exc), 4, out, sizeof(out), &ex);
    TEST_ASSERT_EQUAL_INT(0, modbus_master_i32);
    TEST_ASSERT_EQUAL_UINT8(0x02, ex);
}

void test_round_trip_mask_write()
{
    ModbusV.set_holding_reg_args.addr = 10;
    ModbusV.set_holding_reg_args.value = 0x1234;
    Modbus.set_holding_reg(protocore_modbus_span());

    uint8_t req[16];
    size_t modbus_master_n =
        ModbusMaster.build_mask_write(modbus_master_work, 5, 1, 10, 0xF0FF, 0x0500, req, sizeof(req));
    size_t rn = modbus_master_n;
    TEST_ASSERT_EQUAL_size_t(14, rn);
    TEST_ASSERT_EQUAL_HEX8(0x16, req[7]);

    uint8_t resp[MODBUS_ADU_MAX];
    ModbusV.process_adu_args.req = req;
    ModbusV.process_adu_args.req_len = rn;
    ModbusV.process_adu_args.resp = resp;
    ModbusV.process_adu_args.protocore_resp_cap = sizeof(resp);
    Modbus.process_adu(protocore_modbus_span());
    size_t pn = ModbusV.n;
    uint16_t addr = 0, andm = 0, orm = 0;
    uint8_t ex = 0xFF;
    int modbus_master_i32 =
        ModbusMaster.parse_mask_write_response(modbus_master_work, resp, pn, &addr, &andm, &orm, &ex);
    int r = modbus_master_i32;
    TEST_ASSERT_EQUAL_INT(1, r);
    TEST_ASSERT_EQUAL_UINT8(0, ex);
    TEST_ASSERT_EQUAL_HEX16(10, addr);
    TEST_ASSERT_EQUAL_HEX16(0xF0FF, andm);
    TEST_ASSERT_EQUAL_HEX16(0x0500, orm);
    ModbusV.get_holding_reg_args.addr = 10;
    Modbus.get_holding_reg(protocore_modbus_span());
    TEST_ASSERT_EQUAL_HEX16(0x1534, ModbusV.value);
}

void test_round_trip_read_write_multiple()
{
    ModbusV.set_holding_reg_args.addr = 20;
    ModbusV.set_holding_reg_args.value = 0x1111;
    Modbus.set_holding_reg(protocore_modbus_span());
    ModbusV.set_holding_reg_args.addr = 21;
    ModbusV.set_holding_reg_args.value = 0x2222;
    Modbus.set_holding_reg(protocore_modbus_span());

    const uint16_t wvals[2] = {0xAAAA, 0xBBBB};
    uint8_t req[32];
    size_t modbus_master_n =
        ModbusMaster.build_read_write_multiple(modbus_master_work, 9, 1, 20, 2, 20, wvals, 2, req, sizeof(req));
    size_t rn = modbus_master_n;
    TEST_ASSERT_EQUAL_size_t(7 + 10 + 4, rn);
    TEST_ASSERT_EQUAL_HEX8(0x17, req[7]);

    uint8_t resp[MODBUS_ADU_MAX];
    ModbusV.process_adu_args.req = req;
    ModbusV.process_adu_args.req_len = rn;
    ModbusV.process_adu_args.resp = resp;
    ModbusV.process_adu_args.protocore_resp_cap = sizeof(resp);
    Modbus.process_adu(protocore_modbus_span());
    size_t pn = ModbusV.n;
    uint16_t regs[2];
    uint8_t ex = 0xFF;
    int modbus_master_i32 = ModbusMaster.parse_response(modbus_master_work, resp, pn, regs, 2, &ex);
    int got = modbus_master_i32;
    TEST_ASSERT_EQUAL_INT(2, got);
    TEST_ASSERT_EQUAL_UINT8(0, ex);
    TEST_ASSERT_EQUAL_HEX16(0xAAAA, regs[0]);
    TEST_ASSERT_EQUAL_HEX16(0xBBBB, regs[1]);
    ModbusV.get_holding_reg_args.addr = 21;
    Modbus.get_holding_reg(protocore_modbus_span());
    TEST_ASSERT_EQUAL_HEX16(0xBBBB, ModbusV.value);
}

void test_fc16_17_guards()
{
    uint8_t adu[32];
    const uint16_t vals[2] = {1, 2};

    size_t modbus_master_n = ModbusMaster.build_mask_write(modbus_master_work, 1, 1, 0, 0, 0, NULL, 16);
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);
    modbus_master_n = ModbusMaster.build_mask_write(modbus_master_work, 1, 1, 0, 0, 0, adu, 8);
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);

    modbus_master_n =
        ModbusMaster.build_read_write_multiple(modbus_master_work, 1, 1, 0, 2, 0, NULL, 2, adu, sizeof(adu));
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);
    modbus_master_n =
        ModbusMaster.build_read_write_multiple(modbus_master_work, 1, 1, 0, 0, 0, vals, 2, adu, sizeof(adu));
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);
    modbus_master_n =
        ModbusMaster.build_read_write_multiple(modbus_master_work, 1, 1, 0, 126, 0, vals, 2, adu, sizeof(adu));
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);
    modbus_master_n =
        ModbusMaster.build_read_write_multiple(modbus_master_work, 1, 1, 0, 2, 0, vals, 0, adu, sizeof(adu));
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);
    modbus_master_n =
        ModbusMaster.build_read_write_multiple(modbus_master_work, 1, 1, 0, 2, 0, vals, 122, adu, sizeof(adu));
    TEST_ASSERT_EQUAL_size_t(0, modbus_master_n);

    uint8_t req[16];
    modbus_master_n = ModbusMaster.build_mask_write(modbus_master_work, 1, 1, 60000, 0xFFFF, 0, req, sizeof(req));
    size_t rn = modbus_master_n;
    uint8_t resp[MODBUS_ADU_MAX];
    ModbusV.process_adu_args.req = req;
    ModbusV.process_adu_args.req_len = rn;
    ModbusV.process_adu_args.resp = resp;
    ModbusV.process_adu_args.protocore_resp_cap = sizeof(resp);
    Modbus.process_adu(protocore_modbus_span());
    size_t pn = ModbusV.n;
    uint8_t ex = 0;
    int modbus_master_i32 = ModbusMaster.parse_mask_write_response(modbus_master_work, resp, pn, NULL, NULL, NULL, &ex);
    TEST_ASSERT_EQUAL_INT(0, modbus_master_i32);
    TEST_ASSERT_EQUAL_UINT8(MODBUS_EX_ILLEGAL_DATA_ADDRESS, ex);

    uint8_t shortf[10] = {0, 1, 0, 0, 0, 8, 1, 0x16, 0, 0};
    modbus_master_i32 =
        ModbusMaster.parse_mask_write_response(modbus_master_work, shortf, sizeof(shortf), NULL, NULL, NULL, &ex);
    TEST_ASSERT_EQUAL_INT(-1, modbus_master_i32);
    uint8_t badfc[14] = {0, 1, 0, 0, 0, 8, 1, 0x06, 0, 0, 0, 0, 0, 0};
    modbus_master_i32 =
        ModbusMaster.parse_mask_write_response(modbus_master_work, badfc, sizeof(badfc), NULL, NULL, NULL, &ex);
    TEST_ASSERT_EQUAL_INT(-1, modbus_master_i32);
}
