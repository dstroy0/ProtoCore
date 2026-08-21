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
    ModbusMasterV.build_read_args.fc = (uint8_t)MODBUS_FC_READ_HOLDING_REGS;
    ModbusMasterV.build_read_args.txid = 1;
    ModbusMasterV.build_read_args.unit = 1;
    ModbusMasterV.build_read_args.start = 0;
    ModbusMasterV.build_read_args.count = 2;
    ModbusMasterV.build_read_args.out = adu;
    ModbusMasterV.build_read_args.cap = sizeof(adu);
    ModbusMaster.build_read(modbus_master_work);
    size_t n = ModbusMasterV.n;
    TEST_ASSERT_EQUAL_size_t(12, n);
    const uint8_t expect[12] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00, 0x00, 0x00, 0x02};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, adu, 12);
}

void test_build_rejects_bad_args()
{
    uint8_t adu[16];
    ModbusMasterV.build_read_args.fc = 0x06;
    ModbusMasterV.build_read_args.txid = 1;
    ModbusMasterV.build_read_args.unit = 1;
    ModbusMasterV.build_read_args.start = 0;
    ModbusMasterV.build_read_args.count = 2;
    ModbusMasterV.build_read_args.out = adu;
    ModbusMasterV.build_read_args.cap = sizeof(adu);
    ModbusMaster.build_read(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);
    ModbusMasterV.build_read_args.fc = 0x03;
    ModbusMasterV.build_read_args.txid = 1;
    ModbusMasterV.build_read_args.unit = 1;
    ModbusMasterV.build_read_args.start = 0;
    ModbusMasterV.build_read_args.count = 0;
    ModbusMasterV.build_read_args.out = adu;
    ModbusMasterV.build_read_args.cap = sizeof(adu);
    ModbusMaster.build_read(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);
    ModbusMasterV.build_read_args.fc = 0x03;
    ModbusMasterV.build_read_args.txid = 1;
    ModbusMasterV.build_read_args.unit = 1;
    ModbusMasterV.build_read_args.start = 0;
    ModbusMasterV.build_read_args.count = 200;
    ModbusMasterV.build_read_args.out = adu;
    ModbusMasterV.build_read_args.cap = sizeof(adu);
    ModbusMaster.build_read(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);
    ModbusMasterV.build_read_args.fc = 0x03;
    ModbusMasterV.build_read_args.txid = 1;
    ModbusMasterV.build_read_args.unit = 1;
    ModbusMasterV.build_read_args.start = 0;
    ModbusMasterV.build_read_args.count = 2;
    ModbusMasterV.build_read_args.out = adu;
    ModbusMasterV.build_read_args.cap = 4;
    ModbusMaster.build_read(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);
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
    ModbusMasterV.build_read_args.fc = (uint8_t)MODBUS_FC_READ_HOLDING_REGS;
    ModbusMasterV.build_read_args.txid = 7;
    ModbusMasterV.build_read_args.unit = 1;
    ModbusMasterV.build_read_args.start = 0;
    ModbusMasterV.build_read_args.count = 2;
    ModbusMasterV.build_read_args.out = req;
    ModbusMasterV.build_read_args.cap = sizeof(req);
    ModbusMaster.build_read(modbus_master_work);
    size_t rn = ModbusMasterV.n;
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
    ModbusMasterV.parse_response_args.adu = resp;
    ModbusMasterV.parse_response_args.len = pn;
    ModbusMasterV.parse_response_args.regs_out = regs;
    ModbusMasterV.parse_response_args.max_regs = 4;
    ModbusMasterV.parse_response_args.exception_out = &ex;
    ModbusMaster.parse_response(modbus_master_work);
    int got = ModbusMasterV.i32;
    TEST_ASSERT_EQUAL_INT(2, got);
    TEST_ASSERT_EQUAL_UINT8(0, ex);
    TEST_ASSERT_EQUAL_HEX16(0x1234, regs[0]);
    TEST_ASSERT_EQUAL_HEX16(0xABCD, regs[1]);
}

void test_round_trip_exception()
{

    uint8_t req[16];
    ModbusMasterV.build_read_args.fc = (uint8_t)MODBUS_FC_READ_HOLDING_REGS;
    ModbusMasterV.build_read_args.txid = 9;
    ModbusMasterV.build_read_args.unit = 1;
    ModbusMasterV.build_read_args.start = 60000;
    ModbusMasterV.build_read_args.count = 1;
    ModbusMasterV.build_read_args.out = req;
    ModbusMasterV.build_read_args.cap = sizeof(req);
    ModbusMaster.build_read(modbus_master_work);
    size_t rn = ModbusMasterV.n;
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
    ModbusMasterV.parse_response_args.adu = resp;
    ModbusMasterV.parse_response_args.len = pn;
    ModbusMasterV.parse_response_args.regs_out = regs;
    ModbusMasterV.parse_response_args.max_regs = 4;
    ModbusMasterV.parse_response_args.exception_out = &ex;
    ModbusMaster.parse_response(modbus_master_work);
    int got = ModbusMasterV.i32;
    TEST_ASSERT_EQUAL_INT(0, got);
    TEST_ASSERT_EQUAL_UINT8(MODBUS_EX_ILLEGAL_DATA_ADDRESS, ex);
}

void test_parse_short_frame_fails()
{
    uint8_t buf[4] = {0, 1, 0, 0};
    ModbusMasterV.parse_response_args.adu = buf;
    ModbusMasterV.parse_response_args.len = sizeof(buf);
    ModbusMasterV.parse_response_args.regs_out = NULL;
    ModbusMasterV.parse_response_args.max_regs = 0;
    ModbusMasterV.parse_response_args.exception_out = NULL;
    ModbusMaster.parse_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMasterV.i32);
}

void test_build_null_out_and_input_fc()
{
    uint8_t adu[16];
    ModbusMasterV.build_read_args.fc = 0x03;
    ModbusMasterV.build_read_args.txid = 1;
    ModbusMasterV.build_read_args.unit = 1;
    ModbusMasterV.build_read_args.start = 0;
    ModbusMasterV.build_read_args.count = 2;
    ModbusMasterV.build_read_args.out = NULL;
    ModbusMasterV.build_read_args.cap = 16;
    ModbusMaster.build_read(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);
    ModbusMasterV.build_read_args.fc = 0x04;
    ModbusMasterV.build_read_args.txid = 1;
    ModbusMasterV.build_read_args.unit = 1;
    ModbusMasterV.build_read_args.start = 0;
    ModbusMasterV.build_read_args.count = 2;
    ModbusMasterV.build_read_args.out = adu;
    ModbusMasterV.build_read_args.cap = sizeof(adu);
    ModbusMaster.build_read(modbus_master_work);
    size_t n = ModbusMasterV.n;
    TEST_ASSERT_EQUAL_size_t(12, n);
    TEST_ASSERT_EQUAL_HEX8(0x04, adu[7]);
}

void test_parse_null_adu()
{
    uint16_t regs[4];
    uint8_t ex = 0xFF;
    ModbusMasterV.parse_response_args.adu = NULL;
    ModbusMasterV.parse_response_args.len = 12;
    ModbusMasterV.parse_response_args.regs_out = regs;
    ModbusMasterV.parse_response_args.max_regs = 4;
    ModbusMasterV.parse_response_args.exception_out = &ex;
    ModbusMaster.parse_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMasterV.i32);
}

void test_parse_bad_protocol_id()
{
    uint16_t regs[4];
    uint8_t ex = 0xFF;
    uint8_t adu[13] = {0, 7, 0, 1, 0, 7, 1, 3, 4, 0, 0, 0, 0};
    ModbusMasterV.parse_response_args.adu = adu;
    ModbusMasterV.parse_response_args.len = sizeof(adu);
    ModbusMasterV.parse_response_args.regs_out = regs;
    ModbusMasterV.parse_response_args.max_regs = 4;
    ModbusMasterV.parse_response_args.exception_out = &ex;
    ModbusMaster.parse_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMasterV.i32);
    adu[2] = 1;
    adu[3] = 0;
    ModbusMasterV.parse_response_args.adu = adu;
    ModbusMasterV.parse_response_args.len = sizeof(adu);
    ModbusMasterV.parse_response_args.regs_out = regs;
    ModbusMasterV.parse_response_args.max_regs = 4;
    ModbusMasterV.parse_response_args.exception_out = &ex;
    ModbusMaster.parse_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMasterV.i32);
}

void test_parse_unexpected_function()
{
    uint16_t regs[4];
    uint8_t ex = 0xFF;
    uint8_t adu[13] = {0, 7, 0, 0, 0, 7, 1, 0x06, 4, 0, 0, 0, 0};
    ModbusMasterV.parse_response_args.adu = adu;
    ModbusMasterV.parse_response_args.len = sizeof(adu);
    ModbusMasterV.parse_response_args.regs_out = regs;
    ModbusMasterV.parse_response_args.max_regs = 4;
    ModbusMasterV.parse_response_args.exception_out = &ex;
    ModbusMaster.parse_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMasterV.i32);
}

void test_parse_exception_null_out()
{
    uint16_t regs[4];
    uint8_t adu[9] = {0, 9, 0, 0, 0, 3, 1, 0x83, 0x02};
    ModbusMasterV.parse_response_args.adu = adu;
    ModbusMasterV.parse_response_args.len = sizeof(adu);
    ModbusMasterV.parse_response_args.regs_out = regs;
    ModbusMasterV.parse_response_args.max_regs = 4;
    ModbusMasterV.parse_response_args.exception_out = NULL;
    ModbusMaster.parse_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(0, ModbusMasterV.i32);
}

void test_parse_bad_byte_count()
{
    uint16_t regs[4];
    uint8_t ex = 0xFF;
    uint8_t odd[13] = {0, 7, 0, 0, 0, 7, 1, 3, 3, 0, 0, 0, 0};
    ModbusMasterV.parse_response_args.adu = odd;
    ModbusMasterV.parse_response_args.len = sizeof(odd);
    ModbusMasterV.parse_response_args.regs_out = regs;
    ModbusMasterV.parse_response_args.max_regs = 4;
    ModbusMasterV.parse_response_args.exception_out = &ex;
    ModbusMaster.parse_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMasterV.i32);
    uint8_t truncated[11] = {0, 7, 0, 0, 0, 7, 1, 3, 4, 0, 0};
    ModbusMasterV.parse_response_args.adu = truncated;
    ModbusMasterV.parse_response_args.len = sizeof(truncated);
    ModbusMasterV.parse_response_args.regs_out = regs;
    ModbusMasterV.parse_response_args.max_regs = 4;
    ModbusMasterV.parse_response_args.exception_out = &ex;
    ModbusMaster.parse_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMasterV.i32);
}

void test_parse_max_regs_and_null_out()
{
    uint8_t ex = 0xFF;

    uint8_t adu[17] = {0, 7, 0, 0, 0, 11, 1, 3, 8, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    uint16_t regs[2];
    ModbusMasterV.parse_response_args.adu = adu;
    ModbusMasterV.parse_response_args.len = sizeof(adu);
    ModbusMasterV.parse_response_args.regs_out = regs;
    ModbusMasterV.parse_response_args.max_regs = 2;
    ModbusMasterV.parse_response_args.exception_out = &ex;
    ModbusMaster.parse_response(modbus_master_work);
    int got = ModbusMasterV.i32;
    TEST_ASSERT_EQUAL_INT(2, got);
    TEST_ASSERT_EQUAL_HEX16(0x1122, regs[0]);
    TEST_ASSERT_EQUAL_HEX16(0x3344, regs[1]);

    ModbusMasterV.parse_response_args.adu = adu;
    ModbusMasterV.parse_response_args.len = sizeof(adu);
    ModbusMasterV.parse_response_args.regs_out = NULL;
    ModbusMasterV.parse_response_args.max_regs = 8;
    ModbusMasterV.parse_response_args.exception_out = &ex;
    ModbusMaster.parse_response(modbus_master_work);
    int got2 = ModbusMasterV.i32;
    TEST_ASSERT_EQUAL_INT(4, got2);
}

void test_parse_accepts_input_regs_function()
{
    uint16_t regs[4];
    uint8_t ex = 0xFF;
    uint8_t adu[11] = {0, 7, 0, 0, 0, 5, 1, 0x04, 2, 0x12, 0x34};
    ModbusMasterV.parse_response_args.adu = adu;
    ModbusMasterV.parse_response_args.len = sizeof(adu);
    ModbusMasterV.parse_response_args.regs_out = regs;
    ModbusMasterV.parse_response_args.max_regs = 4;
    ModbusMasterV.parse_response_args.exception_out = &ex;
    ModbusMaster.parse_response(modbus_master_work);
    int got = ModbusMasterV.i32;
    TEST_ASSERT_EQUAL_INT(1, got);
    TEST_ASSERT_EQUAL_UINT8(0, ex);
    TEST_ASSERT_EQUAL_HEX16(0x1234, regs[0]);
}

void test_build_write_single_bytes()
{
    uint8_t adu[16];
    ModbusMasterV.build_write_single_args.txid = 0x0102;
    ModbusMasterV.build_write_single_args.unit = 1;
    ModbusMasterV.build_write_single_args.addr = 0x0013;
    ModbusMasterV.build_write_single_args.value = 0xABCD;
    ModbusMasterV.build_write_single_args.out = adu;
    ModbusMasterV.build_write_single_args.cap = sizeof(adu);
    ModbusMaster.build_write_single(modbus_master_work);
    size_t n = ModbusMasterV.n;
    TEST_ASSERT_EQUAL_size_t(12, n);
    const uint8_t expect[12] = {0x01, 0x02, 0x00, 0x00, 0x00, 0x06, 0x01, 0x06, 0x00, 0x13, 0xAB, 0xCD};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, adu, 12);
}

void test_round_trip_write_single()
{
    uint8_t req[16];
    ModbusMasterV.build_write_single_args.txid = 3;
    ModbusMasterV.build_write_single_args.unit = 1;
    ModbusMasterV.build_write_single_args.addr = 7;
    ModbusMasterV.build_write_single_args.value = 0x5A5A;
    ModbusMasterV.build_write_single_args.out = req;
    ModbusMasterV.build_write_single_args.cap = sizeof(req);
    ModbusMaster.build_write_single(modbus_master_work);
    size_t rn = ModbusMasterV.n;
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
    ModbusMasterV.parse_write_response_args.adu = resp;
    ModbusMasterV.parse_write_response_args.len = pn;
    ModbusMasterV.parse_write_response_args.addr_out = &addr;
    ModbusMasterV.parse_write_response_args.exception_out = &ex;
    ModbusMaster.parse_write_response(modbus_master_work);
    int w = ModbusMasterV.i32;
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
    ModbusMasterV.build_write_multiple_args.txid = 0x0102;
    ModbusMasterV.build_write_multiple_args.unit = 1;
    ModbusMasterV.build_write_multiple_args.start = 0x0000;
    ModbusMasterV.build_write_multiple_args.values = vals;
    ModbusMasterV.build_write_multiple_args.count = 2;
    ModbusMasterV.build_write_multiple_args.out = adu;
    ModbusMasterV.build_write_multiple_args.cap = sizeof(adu);
    ModbusMaster.build_write_multiple(modbus_master_work);
    size_t n = ModbusMasterV.n;
    TEST_ASSERT_EQUAL_size_t(17, n);

    const uint8_t expect[17] = {0x01, 0x02, 0x00, 0x00, 0x00, 0x0B, 0x01, 0x10, 0x00,
                                0x00, 0x00, 0x02, 0x04, 0x11, 0x11, 0x22, 0x22};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, adu, 17);
}

void test_round_trip_write_multiple()
{
    const uint16_t vals[3] = {0xDEAD, 0xBEEF, 0xF00D};
    uint8_t req[32];
    ModbusMasterV.build_write_multiple_args.txid = 5;
    ModbusMasterV.build_write_multiple_args.unit = 1;
    ModbusMasterV.build_write_multiple_args.start = 30;
    ModbusMasterV.build_write_multiple_args.values = vals;
    ModbusMasterV.build_write_multiple_args.count = 3;
    ModbusMasterV.build_write_multiple_args.out = req;
    ModbusMasterV.build_write_multiple_args.cap = sizeof(req);
    ModbusMaster.build_write_multiple(modbus_master_work);
    size_t rn = ModbusMasterV.n;
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
    ModbusMasterV.parse_write_response_args.adu = resp;
    ModbusMasterV.parse_write_response_args.len = pn;
    ModbusMasterV.parse_write_response_args.addr_out = &start;
    ModbusMasterV.parse_write_response_args.exception_out = &ex;
    ModbusMaster.parse_write_response(modbus_master_work);
    int w = ModbusMasterV.i32;
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
    ModbusMasterV.build_write_single_args.txid = 1;
    ModbusMasterV.build_write_single_args.unit = 1;
    ModbusMasterV.build_write_single_args.addr = 0;
    ModbusMasterV.build_write_single_args.value = 5;
    ModbusMasterV.build_write_single_args.out = NULL;
    ModbusMasterV.build_write_single_args.cap = 16;
    ModbusMaster.build_write_single(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);
    ModbusMasterV.build_write_single_args.txid = 1;
    ModbusMasterV.build_write_single_args.unit = 1;
    ModbusMasterV.build_write_single_args.addr = 0;
    ModbusMasterV.build_write_single_args.value = 5;
    ModbusMasterV.build_write_single_args.out = adu;
    ModbusMasterV.build_write_single_args.cap = 4;
    ModbusMaster.build_write_single(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);
    ModbusMasterV.build_write_multiple_args.txid = 1;
    ModbusMasterV.build_write_multiple_args.unit = 1;
    ModbusMasterV.build_write_multiple_args.start = 0;
    ModbusMasterV.build_write_multiple_args.values = vals;
    ModbusMasterV.build_write_multiple_args.count = 2;
    ModbusMasterV.build_write_multiple_args.out = NULL;
    ModbusMasterV.build_write_multiple_args.cap = 32;
    ModbusMaster.build_write_multiple(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);
    ModbusMasterV.build_write_multiple_args.txid = 1;
    ModbusMasterV.build_write_multiple_args.unit = 1;
    ModbusMasterV.build_write_multiple_args.start = 0;
    ModbusMasterV.build_write_multiple_args.values = NULL;
    ModbusMasterV.build_write_multiple_args.count = 2;
    ModbusMasterV.build_write_multiple_args.out = adu;
    ModbusMasterV.build_write_multiple_args.cap = 32;
    ModbusMaster.build_write_multiple(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);
    ModbusMasterV.build_write_multiple_args.txid = 1;
    ModbusMasterV.build_write_multiple_args.unit = 1;
    ModbusMasterV.build_write_multiple_args.start = 0;
    ModbusMasterV.build_write_multiple_args.values = vals;
    ModbusMasterV.build_write_multiple_args.count = 0;
    ModbusMasterV.build_write_multiple_args.out = adu;
    ModbusMasterV.build_write_multiple_args.cap = 32;
    ModbusMaster.build_write_multiple(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);
    ModbusMasterV.build_write_multiple_args.txid = 1;
    ModbusMasterV.build_write_multiple_args.unit = 1;
    ModbusMasterV.build_write_multiple_args.start = 0;
    ModbusMasterV.build_write_multiple_args.values = vals;
    ModbusMasterV.build_write_multiple_args.count = 124;
    ModbusMasterV.build_write_multiple_args.out = adu;
    ModbusMasterV.build_write_multiple_args.cap = 300;
    ModbusMaster.build_write_multiple(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);
    ModbusMasterV.build_write_multiple_args.txid = 1;
    ModbusMasterV.build_write_multiple_args.unit = 1;
    ModbusMasterV.build_write_multiple_args.start = 0;
    ModbusMasterV.build_write_multiple_args.values = vals;
    ModbusMasterV.build_write_multiple_args.count = 2;
    ModbusMasterV.build_write_multiple_args.out = adu;
    ModbusMasterV.build_write_multiple_args.cap = 16;
    ModbusMaster.build_write_multiple(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);
}

void test_parse_write_response_edges()
{
    uint16_t addr = 0xFFFF;
    uint8_t ex = 0;

    uint8_t exc[9] = {0, 3, 0, 0, 0, 3, 1, 0x86, 0x02};
    ModbusMasterV.parse_write_response_args.adu = exc;
    ModbusMasterV.parse_write_response_args.len = sizeof(exc);
    ModbusMasterV.parse_write_response_args.addr_out = &addr;
    ModbusMasterV.parse_write_response_args.exception_out = &ex;
    ModbusMaster.parse_write_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(0, ModbusMasterV.i32);
    TEST_ASSERT_EQUAL_UINT8(0x02, ex);
    TEST_ASSERT_EQUAL_HEX16(0, addr);

    uint8_t shortf[11] = {0, 3, 0, 0, 0, 5, 1, 0x06, 0, 7, 0};
    ModbusMasterV.parse_write_response_args.adu = shortf;
    ModbusMasterV.parse_write_response_args.len = sizeof(shortf);
    ModbusMasterV.parse_write_response_args.addr_out = &addr;
    ModbusMasterV.parse_write_response_args.exception_out = &ex;
    ModbusMaster.parse_write_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMasterV.i32);
    uint8_t badfc[12] = {0, 3, 0, 0, 0, 6, 1, 0x03, 0, 7, 0, 1};
    ModbusMasterV.parse_write_response_args.adu = badfc;
    ModbusMasterV.parse_write_response_args.len = sizeof(badfc);
    ModbusMasterV.parse_write_response_args.addr_out = &addr;
    ModbusMasterV.parse_write_response_args.exception_out = &ex;
    ModbusMaster.parse_write_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMasterV.i32);

    uint8_t badproto[12] = {0, 3, 0, 1, 0, 6, 1, 0x06, 0, 7, 0xAB, 0xCD};
    ModbusMasterV.parse_write_response_args.adu = badproto;
    ModbusMasterV.parse_write_response_args.len = sizeof(badproto);
    ModbusMasterV.parse_write_response_args.addr_out = &addr;
    ModbusMasterV.parse_write_response_args.exception_out = &ex;
    ModbusMaster.parse_write_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMasterV.i32);
    ModbusMasterV.parse_write_response_args.adu = NULL;
    ModbusMasterV.parse_write_response_args.len = 12;
    ModbusMasterV.parse_write_response_args.addr_out = &addr;
    ModbusMasterV.parse_write_response_args.exception_out = &ex;
    ModbusMaster.parse_write_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMasterV.i32);
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
    ModbusMasterV.build_read_bits_args.fc = (uint8_t)MODBUS_FC_READ_COILS;
    ModbusMasterV.build_read_bits_args.txid = 7;
    ModbusMasterV.build_read_bits_args.unit = 1;
    ModbusMasterV.build_read_bits_args.start = 0;
    ModbusMasterV.build_read_bits_args.count = 10;
    ModbusMasterV.build_read_bits_args.out = req;
    ModbusMasterV.build_read_bits_args.cap = sizeof(req);
    ModbusMaster.build_read_bits(modbus_master_work);
    size_t rn = ModbusMasterV.n;
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
    ModbusMasterV.parse_read_bits_response_args.adu = resp;
    ModbusMasterV.parse_read_bits_response_args.len = pn;
    ModbusMasterV.parse_read_bits_response_args.count = 10;
    ModbusMasterV.parse_read_bits_response_args.bits_out = bits;
    ModbusMasterV.parse_read_bits_response_args.max_bits = sizeof(bits);
    ModbusMasterV.parse_read_bits_response_args.exception_out = &ex;
    ModbusMaster.parse_read_bits_response(modbus_master_work);
    int got = ModbusMasterV.i32;
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
    ModbusMasterV.build_read_bits_args.fc = (uint8_t)MODBUS_FC_READ_DISCRETE_INPUTS;
    ModbusMasterV.build_read_bits_args.txid = 8;
    ModbusMasterV.build_read_bits_args.unit = 1;
    ModbusMasterV.build_read_bits_args.start = 0;
    ModbusMasterV.build_read_bits_args.count = 6;
    ModbusMasterV.build_read_bits_args.out = req;
    ModbusMasterV.build_read_bits_args.cap = sizeof(req);
    ModbusMaster.build_read_bits(modbus_master_work);
    size_t rn = ModbusMasterV.n;
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
    ModbusMasterV.parse_read_bits_response_args.adu = resp;
    ModbusMasterV.parse_read_bits_response_args.len = pn;
    ModbusMasterV.parse_read_bits_response_args.count = 6;
    ModbusMasterV.parse_read_bits_response_args.bits_out = bits;
    ModbusMasterV.parse_read_bits_response_args.max_bits = sizeof(bits);
    ModbusMasterV.parse_read_bits_response_args.exception_out = &ex;
    ModbusMaster.parse_read_bits_response(modbus_master_work);
    int got = ModbusMasterV.i32;
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
    ModbusMasterV.build_write_single_coil_args.txid = 11;
    ModbusMasterV.build_write_single_coil_args.unit = 1;
    ModbusMasterV.build_write_single_coil_args.addr = 5;
    ModbusMasterV.build_write_single_coil_args.on = PROTO_TRUE;
    ModbusMasterV.build_write_single_coil_args.out = req;
    ModbusMasterV.build_write_single_coil_args.cap = sizeof(req);
    ModbusMaster.build_write_single_coil(modbus_master_work);
    size_t rn = ModbusMasterV.n;
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
    ModbusMasterV.parse_write_response_args.adu = resp;
    ModbusMasterV.parse_write_response_args.len = pn;
    ModbusMasterV.parse_write_response_args.addr_out = &addr;
    ModbusMasterV.parse_write_response_args.exception_out = &ex;
    ModbusMaster.parse_write_response(modbus_master_work);
    int wrote = ModbusMasterV.i32;
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
    ModbusMasterV.build_write_multiple_coils_args.txid = 12;
    ModbusMasterV.build_write_multiple_coils_args.unit = 1;
    ModbusMasterV.build_write_multiple_coils_args.start = 0;
    ModbusMasterV.build_write_multiple_coils_args.bits = pattern;
    ModbusMasterV.build_write_multiple_coils_args.count = 12;
    ModbusMasterV.build_write_multiple_coils_args.out = req;
    ModbusMasterV.build_write_multiple_coils_args.cap = sizeof(req);
    ModbusMaster.build_write_multiple_coils(modbus_master_work);
    size_t rn = ModbusMasterV.n;
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
    ModbusMasterV.parse_write_response_args.adu = resp;
    ModbusMasterV.parse_write_response_args.len = pn;
    ModbusMasterV.parse_write_response_args.addr_out = &addr;
    ModbusMasterV.parse_write_response_args.exception_out = &ex;
    ModbusMaster.parse_write_response(modbus_master_work);
    int wrote = ModbusMasterV.i32;
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

    ModbusMasterV.build_read_bits_args.fc = 0x03;
    ModbusMasterV.build_read_bits_args.txid = 1;
    ModbusMasterV.build_read_bits_args.unit = 1;
    ModbusMasterV.build_read_bits_args.start = 0;
    ModbusMasterV.build_read_bits_args.count = 8;
    ModbusMasterV.build_read_bits_args.out = adu;
    ModbusMasterV.build_read_bits_args.cap = sizeof(adu);
    ModbusMaster.build_read_bits(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);
    ModbusMasterV.build_read_bits_args.fc = 0x01;
    ModbusMasterV.build_read_bits_args.txid = 1;
    ModbusMasterV.build_read_bits_args.unit = 1;
    ModbusMasterV.build_read_bits_args.start = 0;
    ModbusMasterV.build_read_bits_args.count = 0;
    ModbusMasterV.build_read_bits_args.out = adu;
    ModbusMasterV.build_read_bits_args.cap = sizeof(adu);
    ModbusMaster.build_read_bits(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);
    ModbusMasterV.build_read_bits_args.fc = 0x01;
    ModbusMasterV.build_read_bits_args.txid = 1;
    ModbusMasterV.build_read_bits_args.unit = 1;
    ModbusMasterV.build_read_bits_args.start = 0;
    ModbusMasterV.build_read_bits_args.count = 2001;
    ModbusMasterV.build_read_bits_args.out = adu;
    ModbusMasterV.build_read_bits_args.cap = sizeof(adu);
    ModbusMaster.build_read_bits(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);
    ModbusMasterV.build_read_bits_args.fc = 0x01;
    ModbusMasterV.build_read_bits_args.txid = 1;
    ModbusMasterV.build_read_bits_args.unit = 1;
    ModbusMasterV.build_read_bits_args.start = 0;
    ModbusMasterV.build_read_bits_args.count = 8;
    ModbusMasterV.build_read_bits_args.out = NULL;
    ModbusMasterV.build_read_bits_args.cap = 16;
    ModbusMaster.build_read_bits(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);

    ModbusMasterV.build_write_single_coil_args.txid = 1;
    ModbusMasterV.build_write_single_coil_args.unit = 1;
    ModbusMasterV.build_write_single_coil_args.addr = 0;
    ModbusMasterV.build_write_single_coil_args.on = PROTO_TRUE;
    ModbusMasterV.build_write_single_coil_args.out = NULL;
    ModbusMasterV.build_write_single_coil_args.cap = 16;
    ModbusMaster.build_write_single_coil(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);
    const uint8_t bits[4] = {1, 0, 1, 1};
    ModbusMasterV.build_write_multiple_coils_args.txid = 1;
    ModbusMasterV.build_write_multiple_coils_args.unit = 1;
    ModbusMasterV.build_write_multiple_coils_args.start = 0;
    ModbusMasterV.build_write_multiple_coils_args.bits = NULL;
    ModbusMasterV.build_write_multiple_coils_args.count = 4;
    ModbusMasterV.build_write_multiple_coils_args.out = adu;
    ModbusMasterV.build_write_multiple_coils_args.cap = sizeof(adu);
    ModbusMaster.build_write_multiple_coils(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);
    ModbusMasterV.build_write_multiple_coils_args.txid = 1;
    ModbusMasterV.build_write_multiple_coils_args.unit = 1;
    ModbusMasterV.build_write_multiple_coils_args.start = 0;
    ModbusMasterV.build_write_multiple_coils_args.bits = bits;
    ModbusMasterV.build_write_multiple_coils_args.count = 0;
    ModbusMasterV.build_write_multiple_coils_args.out = adu;
    ModbusMasterV.build_write_multiple_coils_args.cap = sizeof(adu);
    ModbusMaster.build_write_multiple_coils(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);
    ModbusMasterV.build_write_multiple_coils_args.txid = 1;
    ModbusMasterV.build_write_multiple_coils_args.unit = 1;
    ModbusMasterV.build_write_multiple_coils_args.start = 0;
    ModbusMasterV.build_write_multiple_coils_args.bits = bits;
    ModbusMasterV.build_write_multiple_coils_args.count = 1969;
    ModbusMasterV.build_write_multiple_coils_args.out = adu;
    ModbusMasterV.build_write_multiple_coils_args.cap = sizeof(adu);
    ModbusMaster.build_write_multiple_coils(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);

    uint8_t resp[16] = {0, 7, 0, 0, 0, 4, 1, 0x01, 2, 0x05, 0x00};
    uint8_t out[8];
    uint8_t ex = 0;
    ModbusMasterV.parse_read_bits_response_args.adu = resp;
    ModbusMasterV.parse_read_bits_response_args.len = 11;
    ModbusMasterV.parse_read_bits_response_args.count = 4;
    ModbusMasterV.parse_read_bits_response_args.bits_out = out;
    ModbusMasterV.parse_read_bits_response_args.max_bits = sizeof(out);
    ModbusMasterV.parse_read_bits_response_args.exception_out = &ex;
    ModbusMaster.parse_read_bits_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMasterV.i32);

    uint8_t exc[9] = {0, 7, 0, 0, 0, 3, 1, 0x81, 0x02};
    ModbusMasterV.parse_read_bits_response_args.adu = exc;
    ModbusMasterV.parse_read_bits_response_args.len = sizeof(exc);
    ModbusMasterV.parse_read_bits_response_args.count = 4;
    ModbusMasterV.parse_read_bits_response_args.bits_out = out;
    ModbusMasterV.parse_read_bits_response_args.max_bits = sizeof(out);
    ModbusMasterV.parse_read_bits_response_args.exception_out = &ex;
    ModbusMaster.parse_read_bits_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(0, ModbusMasterV.i32);
    TEST_ASSERT_EQUAL_UINT8(0x02, ex);
}

void test_round_trip_mask_write()
{
    ModbusV.set_holding_reg_args.addr = 10;
    ModbusV.set_holding_reg_args.value = 0x1234;
    Modbus.set_holding_reg(protocore_modbus_span());

    uint8_t req[16];
    ModbusMasterV.build_mask_write_args.txid = 5;
    ModbusMasterV.build_mask_write_args.unit = 1;
    ModbusMasterV.build_mask_write_args.addr = 10;
    ModbusMasterV.build_mask_write_args.and_mask = 0xF0FF;
    ModbusMasterV.build_mask_write_args.or_mask = 0x0500;
    ModbusMasterV.build_mask_write_args.out = req;
    ModbusMasterV.build_mask_write_args.cap = sizeof(req);
    ModbusMaster.build_mask_write(modbus_master_work);
    size_t rn = ModbusMasterV.n;
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
    ModbusMasterV.parse_mask_write_response_args.adu = resp;
    ModbusMasterV.parse_mask_write_response_args.len = pn;
    ModbusMasterV.parse_mask_write_response_args.addr_out = &addr;
    ModbusMasterV.parse_mask_write_response_args.and_out = &andm;
    ModbusMasterV.parse_mask_write_response_args.or_out = &orm;
    ModbusMasterV.parse_mask_write_response_args.exception_out = &ex;
    ModbusMaster.parse_mask_write_response(modbus_master_work);
    int r = ModbusMasterV.i32;
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
    ModbusMasterV.build_read_write_multiple_args.txid = 9;
    ModbusMasterV.build_read_write_multiple_args.unit = 1;
    ModbusMasterV.build_read_write_multiple_args.read_start = 20;
    ModbusMasterV.build_read_write_multiple_args.read_count = 2;
    ModbusMasterV.build_read_write_multiple_args.write_start = 20;
    ModbusMasterV.build_read_write_multiple_args.values = wvals;
    ModbusMasterV.build_read_write_multiple_args.write_count = 2;
    ModbusMasterV.build_read_write_multiple_args.out = req;
    ModbusMasterV.build_read_write_multiple_args.cap = sizeof(req);
    ModbusMaster.build_read_write_multiple(modbus_master_work);
    size_t rn = ModbusMasterV.n;
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
    ModbusMasterV.parse_response_args.adu = resp;
    ModbusMasterV.parse_response_args.len = pn;
    ModbusMasterV.parse_response_args.regs_out = regs;
    ModbusMasterV.parse_response_args.max_regs = 2;
    ModbusMasterV.parse_response_args.exception_out = &ex;
    ModbusMaster.parse_response(modbus_master_work);
    int got = ModbusMasterV.i32;
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

    ModbusMasterV.build_mask_write_args.txid = 1;
    ModbusMasterV.build_mask_write_args.unit = 1;
    ModbusMasterV.build_mask_write_args.addr = 0;
    ModbusMasterV.build_mask_write_args.and_mask = 0;
    ModbusMasterV.build_mask_write_args.or_mask = 0;
    ModbusMasterV.build_mask_write_args.out = NULL;
    ModbusMasterV.build_mask_write_args.cap = 16;
    ModbusMaster.build_mask_write(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);
    ModbusMasterV.build_mask_write_args.txid = 1;
    ModbusMasterV.build_mask_write_args.unit = 1;
    ModbusMasterV.build_mask_write_args.addr = 0;
    ModbusMasterV.build_mask_write_args.and_mask = 0;
    ModbusMasterV.build_mask_write_args.or_mask = 0;
    ModbusMasterV.build_mask_write_args.out = adu;
    ModbusMasterV.build_mask_write_args.cap = 8;
    ModbusMaster.build_mask_write(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);

    ModbusMasterV.build_read_write_multiple_args.txid = 1;
    ModbusMasterV.build_read_write_multiple_args.unit = 1;
    ModbusMasterV.build_read_write_multiple_args.read_start = 0;
    ModbusMasterV.build_read_write_multiple_args.read_count = 2;
    ModbusMasterV.build_read_write_multiple_args.write_start = 0;
    ModbusMasterV.build_read_write_multiple_args.values = NULL;
    ModbusMasterV.build_read_write_multiple_args.write_count = 2;
    ModbusMasterV.build_read_write_multiple_args.out = adu;
    ModbusMasterV.build_read_write_multiple_args.cap = sizeof(adu);
    ModbusMaster.build_read_write_multiple(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);
    ModbusMasterV.build_read_write_multiple_args.txid = 1;
    ModbusMasterV.build_read_write_multiple_args.unit = 1;
    ModbusMasterV.build_read_write_multiple_args.read_start = 0;
    ModbusMasterV.build_read_write_multiple_args.read_count = 0;
    ModbusMasterV.build_read_write_multiple_args.write_start = 0;
    ModbusMasterV.build_read_write_multiple_args.values = vals;
    ModbusMasterV.build_read_write_multiple_args.write_count = 2;
    ModbusMasterV.build_read_write_multiple_args.out = adu;
    ModbusMasterV.build_read_write_multiple_args.cap = sizeof(adu);
    ModbusMaster.build_read_write_multiple(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);
    ModbusMasterV.build_read_write_multiple_args.txid = 1;
    ModbusMasterV.build_read_write_multiple_args.unit = 1;
    ModbusMasterV.build_read_write_multiple_args.read_start = 0;
    ModbusMasterV.build_read_write_multiple_args.read_count = 126;
    ModbusMasterV.build_read_write_multiple_args.write_start = 0;
    ModbusMasterV.build_read_write_multiple_args.values = vals;
    ModbusMasterV.build_read_write_multiple_args.write_count = 2;
    ModbusMasterV.build_read_write_multiple_args.out = adu;
    ModbusMasterV.build_read_write_multiple_args.cap = sizeof(adu);
    ModbusMaster.build_read_write_multiple(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);
    ModbusMasterV.build_read_write_multiple_args.txid = 1;
    ModbusMasterV.build_read_write_multiple_args.unit = 1;
    ModbusMasterV.build_read_write_multiple_args.read_start = 0;
    ModbusMasterV.build_read_write_multiple_args.read_count = 2;
    ModbusMasterV.build_read_write_multiple_args.write_start = 0;
    ModbusMasterV.build_read_write_multiple_args.values = vals;
    ModbusMasterV.build_read_write_multiple_args.write_count = 0;
    ModbusMasterV.build_read_write_multiple_args.out = adu;
    ModbusMasterV.build_read_write_multiple_args.cap = sizeof(adu);
    ModbusMaster.build_read_write_multiple(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);
    ModbusMasterV.build_read_write_multiple_args.txid = 1;
    ModbusMasterV.build_read_write_multiple_args.unit = 1;
    ModbusMasterV.build_read_write_multiple_args.read_start = 0;
    ModbusMasterV.build_read_write_multiple_args.read_count = 2;
    ModbusMasterV.build_read_write_multiple_args.write_start = 0;
    ModbusMasterV.build_read_write_multiple_args.values = vals;
    ModbusMasterV.build_read_write_multiple_args.write_count = 122;
    ModbusMasterV.build_read_write_multiple_args.out = adu;
    ModbusMasterV.build_read_write_multiple_args.cap = sizeof(adu);
    ModbusMaster.build_read_write_multiple(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMasterV.n);

    uint8_t req[16];
    ModbusMasterV.build_mask_write_args.txid = 1;
    ModbusMasterV.build_mask_write_args.unit = 1;
    ModbusMasterV.build_mask_write_args.addr = 60000;
    ModbusMasterV.build_mask_write_args.and_mask = 0xFFFF;
    ModbusMasterV.build_mask_write_args.or_mask = 0;
    ModbusMasterV.build_mask_write_args.out = req;
    ModbusMasterV.build_mask_write_args.cap = sizeof(req);
    ModbusMaster.build_mask_write(modbus_master_work);
    size_t rn = ModbusMasterV.n;
    uint8_t resp[MODBUS_ADU_MAX];
    ModbusV.process_adu_args.req = req;
    ModbusV.process_adu_args.req_len = rn;
    ModbusV.process_adu_args.resp = resp;
    ModbusV.process_adu_args.protocore_resp_cap = sizeof(resp);
    Modbus.process_adu(protocore_modbus_span());
    size_t pn = ModbusV.n;
    uint8_t ex = 0;
    ModbusMasterV.parse_mask_write_response_args.adu = resp;
    ModbusMasterV.parse_mask_write_response_args.len = pn;
    ModbusMasterV.parse_mask_write_response_args.addr_out = NULL;
    ModbusMasterV.parse_mask_write_response_args.and_out = NULL;
    ModbusMasterV.parse_mask_write_response_args.or_out = NULL;
    ModbusMasterV.parse_mask_write_response_args.exception_out = &ex;
    ModbusMaster.parse_mask_write_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(0, ModbusMasterV.i32);
    TEST_ASSERT_EQUAL_UINT8(MODBUS_EX_ILLEGAL_DATA_ADDRESS, ex);

    uint8_t shortf[10] = {0, 1, 0, 0, 0, 8, 1, 0x16, 0, 0};
    ModbusMasterV.parse_mask_write_response_args.adu = shortf;
    ModbusMasterV.parse_mask_write_response_args.len = sizeof(shortf);
    ModbusMasterV.parse_mask_write_response_args.addr_out = NULL;
    ModbusMasterV.parse_mask_write_response_args.and_out = NULL;
    ModbusMasterV.parse_mask_write_response_args.or_out = NULL;
    ModbusMasterV.parse_mask_write_response_args.exception_out = &ex;
    ModbusMaster.parse_mask_write_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMasterV.i32);
    uint8_t badfc[14] = {0, 1, 0, 0, 0, 8, 1, 0x06, 0, 0, 0, 0, 0, 0};
    ModbusMasterV.parse_mask_write_response_args.adu = badfc;
    ModbusMasterV.parse_mask_write_response_args.len = sizeof(badfc);
    ModbusMasterV.parse_mask_write_response_args.addr_out = NULL;
    ModbusMasterV.parse_mask_write_response_args.and_out = NULL;
    ModbusMasterV.parse_mask_write_response_args.or_out = NULL;
    ModbusMasterV.parse_mask_write_response_args.exception_out = &ex;
    ModbusMaster.parse_mask_write_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMasterV.i32);
}
