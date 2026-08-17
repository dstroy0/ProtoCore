// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "services/fieldbus/modbus/modbus.h"
#include "services/fieldbus/modbus/modbus_master.h"
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
    ModbusMaster.build_read_args.fc = (uint8_t)MODBUS_FC_READ_HOLDING_REGS;
    ModbusMaster.build_read_args.txid = 1;
    ModbusMaster.build_read_args.unit = 1;
    ModbusMaster.build_read_args.start = 0;
    ModbusMaster.build_read_args.count = 2;
    ModbusMaster.build_read_args.out = adu;
    ModbusMaster.build_read_args.cap = sizeof(adu);
    ModbusMaster.build_read(modbus_master_work);
    size_t n = ModbusMaster.n;
    TEST_ASSERT_EQUAL_size_t(12, n);
    const uint8_t expect[12] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x06, 0x01, 0x03, 0x00, 0x00, 0x00, 0x02};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, adu, 12);
}

void test_build_rejects_bad_args()
{
    uint8_t adu[16];
    ModbusMaster.build_read_args.fc = 0x06;
    ModbusMaster.build_read_args.txid = 1;
    ModbusMaster.build_read_args.unit = 1;
    ModbusMaster.build_read_args.start = 0;
    ModbusMaster.build_read_args.count = 2;
    ModbusMaster.build_read_args.out = adu;
    ModbusMaster.build_read_args.cap = sizeof(adu);
    ModbusMaster.build_read(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);
    ModbusMaster.build_read_args.fc = 0x03;
    ModbusMaster.build_read_args.txid = 1;
    ModbusMaster.build_read_args.unit = 1;
    ModbusMaster.build_read_args.start = 0;
    ModbusMaster.build_read_args.count = 0;
    ModbusMaster.build_read_args.out = adu;
    ModbusMaster.build_read_args.cap = sizeof(adu);
    ModbusMaster.build_read(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);
    ModbusMaster.build_read_args.fc = 0x03;
    ModbusMaster.build_read_args.txid = 1;
    ModbusMaster.build_read_args.unit = 1;
    ModbusMaster.build_read_args.start = 0;
    ModbusMaster.build_read_args.count = 200;
    ModbusMaster.build_read_args.out = adu;
    ModbusMaster.build_read_args.cap = sizeof(adu);
    ModbusMaster.build_read(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);
    ModbusMaster.build_read_args.fc = 0x03;
    ModbusMaster.build_read_args.txid = 1;
    ModbusMaster.build_read_args.unit = 1;
    ModbusMaster.build_read_args.start = 0;
    ModbusMaster.build_read_args.count = 2;
    ModbusMaster.build_read_args.out = adu;
    ModbusMaster.build_read_args.cap = 4;
    ModbusMaster.build_read(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);
}

void test_round_trip_holding_regs()
{
    Modbus.set_holding_reg_args.addr = 0;
    Modbus.set_holding_reg_args.value = 0x1234;
    Modbus.set_holding_reg(protocore_modbus_span());
    Modbus.set_holding_reg_args.addr = 1;
    Modbus.set_holding_reg_args.value = 0xABCD;
    Modbus.set_holding_reg(protocore_modbus_span());

    uint8_t req[16];
    ModbusMaster.build_read_args.fc = (uint8_t)MODBUS_FC_READ_HOLDING_REGS;
    ModbusMaster.build_read_args.txid = 7;
    ModbusMaster.build_read_args.unit = 1;
    ModbusMaster.build_read_args.start = 0;
    ModbusMaster.build_read_args.count = 2;
    ModbusMaster.build_read_args.out = req;
    ModbusMaster.build_read_args.cap = sizeof(req);
    ModbusMaster.build_read(modbus_master_work);
    size_t rn = ModbusMaster.n;
    TEST_ASSERT_EQUAL_size_t(12, rn);

    uint8_t resp[MODBUS_ADU_MAX];
    Modbus.process_adu_args.req = req;
    Modbus.process_adu_args.req_len = rn;
    Modbus.process_adu_args.resp = resp;
    Modbus.process_adu_args.protocore_resp_cap = sizeof(resp);
    Modbus.process_adu(protocore_modbus_span());
    size_t pn = Modbus.n;
    TEST_ASSERT_TRUE(pn > 0);

    uint16_t regs[4];
    uint8_t ex = 0xFF;
    ModbusMaster.parse_response_args.adu = resp;
    ModbusMaster.parse_response_args.len = pn;
    ModbusMaster.parse_response_args.regs_out = regs;
    ModbusMaster.parse_response_args.max_regs = 4;
    ModbusMaster.parse_response_args.exception_out = &ex;
    ModbusMaster.parse_response(modbus_master_work);
    int got = ModbusMaster.i32;
    TEST_ASSERT_EQUAL_INT(2, got);
    TEST_ASSERT_EQUAL_UINT8(0, ex);
    TEST_ASSERT_EQUAL_HEX16(0x1234, regs[0]);
    TEST_ASSERT_EQUAL_HEX16(0xABCD, regs[1]);
}

void test_round_trip_exception()
{

    uint8_t req[16];
    ModbusMaster.build_read_args.fc = (uint8_t)MODBUS_FC_READ_HOLDING_REGS;
    ModbusMaster.build_read_args.txid = 9;
    ModbusMaster.build_read_args.unit = 1;
    ModbusMaster.build_read_args.start = 60000;
    ModbusMaster.build_read_args.count = 1;
    ModbusMaster.build_read_args.out = req;
    ModbusMaster.build_read_args.cap = sizeof(req);
    ModbusMaster.build_read(modbus_master_work);
    size_t rn = ModbusMaster.n;
    uint8_t resp[MODBUS_ADU_MAX];
    Modbus.process_adu_args.req = req;
    Modbus.process_adu_args.req_len = rn;
    Modbus.process_adu_args.resp = resp;
    Modbus.process_adu_args.protocore_resp_cap = sizeof(resp);
    Modbus.process_adu(protocore_modbus_span());
    size_t pn = Modbus.n;
    TEST_ASSERT_TRUE(pn > 0);

    uint16_t regs[4];
    uint8_t ex = 0;
    ModbusMaster.parse_response_args.adu = resp;
    ModbusMaster.parse_response_args.len = pn;
    ModbusMaster.parse_response_args.regs_out = regs;
    ModbusMaster.parse_response_args.max_regs = 4;
    ModbusMaster.parse_response_args.exception_out = &ex;
    ModbusMaster.parse_response(modbus_master_work);
    int got = ModbusMaster.i32;
    TEST_ASSERT_EQUAL_INT(0, got);
    TEST_ASSERT_EQUAL_UINT8(MODBUS_EX_ILLEGAL_DATA_ADDRESS, ex);
}

void test_parse_short_frame_fails()
{
    uint8_t buf[4] = {0, 1, 0, 0};
    ModbusMaster.parse_response_args.adu = buf;
    ModbusMaster.parse_response_args.len = sizeof(buf);
    ModbusMaster.parse_response_args.regs_out = NULL;
    ModbusMaster.parse_response_args.max_regs = 0;
    ModbusMaster.parse_response_args.exception_out = NULL;
    ModbusMaster.parse_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMaster.i32);
}

void test_build_null_out_and_input_fc()
{
    uint8_t adu[16];
    ModbusMaster.build_read_args.fc = 0x03;
    ModbusMaster.build_read_args.txid = 1;
    ModbusMaster.build_read_args.unit = 1;
    ModbusMaster.build_read_args.start = 0;
    ModbusMaster.build_read_args.count = 2;
    ModbusMaster.build_read_args.out = NULL;
    ModbusMaster.build_read_args.cap = 16;
    ModbusMaster.build_read(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);
    ModbusMaster.build_read_args.fc = 0x04;
    ModbusMaster.build_read_args.txid = 1;
    ModbusMaster.build_read_args.unit = 1;
    ModbusMaster.build_read_args.start = 0;
    ModbusMaster.build_read_args.count = 2;
    ModbusMaster.build_read_args.out = adu;
    ModbusMaster.build_read_args.cap = sizeof(adu);
    ModbusMaster.build_read(modbus_master_work);
    size_t n = ModbusMaster.n;
    TEST_ASSERT_EQUAL_size_t(12, n);
    TEST_ASSERT_EQUAL_HEX8(0x04, adu[7]);
}

void test_parse_null_adu()
{
    uint16_t regs[4];
    uint8_t ex = 0xFF;
    ModbusMaster.parse_response_args.adu = NULL;
    ModbusMaster.parse_response_args.len = 12;
    ModbusMaster.parse_response_args.regs_out = regs;
    ModbusMaster.parse_response_args.max_regs = 4;
    ModbusMaster.parse_response_args.exception_out = &ex;
    ModbusMaster.parse_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMaster.i32);
}

void test_parse_bad_protocol_id()
{
    uint16_t regs[4];
    uint8_t ex = 0xFF;
    uint8_t adu[13] = {0, 7, 0, 1, 0, 7, 1, 3, 4, 0, 0, 0, 0};
    ModbusMaster.parse_response_args.adu = adu;
    ModbusMaster.parse_response_args.len = sizeof(adu);
    ModbusMaster.parse_response_args.regs_out = regs;
    ModbusMaster.parse_response_args.max_regs = 4;
    ModbusMaster.parse_response_args.exception_out = &ex;
    ModbusMaster.parse_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMaster.i32);
    adu[2] = 1;
    adu[3] = 0;
    ModbusMaster.parse_response_args.adu = adu;
    ModbusMaster.parse_response_args.len = sizeof(adu);
    ModbusMaster.parse_response_args.regs_out = regs;
    ModbusMaster.parse_response_args.max_regs = 4;
    ModbusMaster.parse_response_args.exception_out = &ex;
    ModbusMaster.parse_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMaster.i32);
}

void test_parse_unexpected_function()
{
    uint16_t regs[4];
    uint8_t ex = 0xFF;
    uint8_t adu[13] = {0, 7, 0, 0, 0, 7, 1, 0x06, 4, 0, 0, 0, 0};
    ModbusMaster.parse_response_args.adu = adu;
    ModbusMaster.parse_response_args.len = sizeof(adu);
    ModbusMaster.parse_response_args.regs_out = regs;
    ModbusMaster.parse_response_args.max_regs = 4;
    ModbusMaster.parse_response_args.exception_out = &ex;
    ModbusMaster.parse_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMaster.i32);
}

void test_parse_exception_null_out()
{
    uint16_t regs[4];
    uint8_t adu[9] = {0, 9, 0, 0, 0, 3, 1, 0x83, 0x02};
    ModbusMaster.parse_response_args.adu = adu;
    ModbusMaster.parse_response_args.len = sizeof(adu);
    ModbusMaster.parse_response_args.regs_out = regs;
    ModbusMaster.parse_response_args.max_regs = 4;
    ModbusMaster.parse_response_args.exception_out = NULL;
    ModbusMaster.parse_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(0, ModbusMaster.i32);
}

void test_parse_bad_byte_count()
{
    uint16_t regs[4];
    uint8_t ex = 0xFF;
    uint8_t odd[13] = {0, 7, 0, 0, 0, 7, 1, 3, 3, 0, 0, 0, 0};
    ModbusMaster.parse_response_args.adu = odd;
    ModbusMaster.parse_response_args.len = sizeof(odd);
    ModbusMaster.parse_response_args.regs_out = regs;
    ModbusMaster.parse_response_args.max_regs = 4;
    ModbusMaster.parse_response_args.exception_out = &ex;
    ModbusMaster.parse_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMaster.i32);
    uint8_t truncated[11] = {0, 7, 0, 0, 0, 7, 1, 3, 4, 0, 0};
    ModbusMaster.parse_response_args.adu = truncated;
    ModbusMaster.parse_response_args.len = sizeof(truncated);
    ModbusMaster.parse_response_args.regs_out = regs;
    ModbusMaster.parse_response_args.max_regs = 4;
    ModbusMaster.parse_response_args.exception_out = &ex;
    ModbusMaster.parse_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMaster.i32);
}

void test_parse_max_regs_and_null_out()
{
    uint8_t ex = 0xFF;

    uint8_t adu[17] = {0, 7, 0, 0, 0, 11, 1, 3, 8, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88};
    uint16_t regs[2];
    ModbusMaster.parse_response_args.adu = adu;
    ModbusMaster.parse_response_args.len = sizeof(adu);
    ModbusMaster.parse_response_args.regs_out = regs;
    ModbusMaster.parse_response_args.max_regs = 2;
    ModbusMaster.parse_response_args.exception_out = &ex;
    ModbusMaster.parse_response(modbus_master_work);
    int got = ModbusMaster.i32;
    TEST_ASSERT_EQUAL_INT(2, got);
    TEST_ASSERT_EQUAL_HEX16(0x1122, regs[0]);
    TEST_ASSERT_EQUAL_HEX16(0x3344, regs[1]);

    ModbusMaster.parse_response_args.adu = adu;
    ModbusMaster.parse_response_args.len = sizeof(adu);
    ModbusMaster.parse_response_args.regs_out = NULL;
    ModbusMaster.parse_response_args.max_regs = 8;
    ModbusMaster.parse_response_args.exception_out = &ex;
    ModbusMaster.parse_response(modbus_master_work);
    int got2 = ModbusMaster.i32;
    TEST_ASSERT_EQUAL_INT(4, got2);
}

void test_parse_accepts_input_regs_function()
{
    uint16_t regs[4];
    uint8_t ex = 0xFF;
    uint8_t adu[11] = {0, 7, 0, 0, 0, 5, 1, 0x04, 2, 0x12, 0x34};
    ModbusMaster.parse_response_args.adu = adu;
    ModbusMaster.parse_response_args.len = sizeof(adu);
    ModbusMaster.parse_response_args.regs_out = regs;
    ModbusMaster.parse_response_args.max_regs = 4;
    ModbusMaster.parse_response_args.exception_out = &ex;
    ModbusMaster.parse_response(modbus_master_work);
    int got = ModbusMaster.i32;
    TEST_ASSERT_EQUAL_INT(1, got);
    TEST_ASSERT_EQUAL_UINT8(0, ex);
    TEST_ASSERT_EQUAL_HEX16(0x1234, regs[0]);
}

void test_build_write_single_bytes()
{
    uint8_t adu[16];
    ModbusMaster.build_write_single_args.txid = 0x0102;
    ModbusMaster.build_write_single_args.unit = 1;
    ModbusMaster.build_write_single_args.addr = 0x0013;
    ModbusMaster.build_write_single_args.value = 0xABCD;
    ModbusMaster.build_write_single_args.out = adu;
    ModbusMaster.build_write_single_args.cap = sizeof(adu);
    ModbusMaster.build_write_single(modbus_master_work);
    size_t n = ModbusMaster.n;
    TEST_ASSERT_EQUAL_size_t(12, n);
    const uint8_t expect[12] = {0x01, 0x02, 0x00, 0x00, 0x00, 0x06, 0x01, 0x06, 0x00, 0x13, 0xAB, 0xCD};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, adu, 12);
}

void test_round_trip_write_single()
{
    uint8_t req[16];
    ModbusMaster.build_write_single_args.txid = 3;
    ModbusMaster.build_write_single_args.unit = 1;
    ModbusMaster.build_write_single_args.addr = 7;
    ModbusMaster.build_write_single_args.value = 0x5A5A;
    ModbusMaster.build_write_single_args.out = req;
    ModbusMaster.build_write_single_args.cap = sizeof(req);
    ModbusMaster.build_write_single(modbus_master_work);
    size_t rn = ModbusMaster.n;
    uint8_t resp[MODBUS_ADU_MAX];
    Modbus.process_adu_args.req = req;
    Modbus.process_adu_args.req_len = rn;
    Modbus.process_adu_args.resp = resp;
    Modbus.process_adu_args.protocore_resp_cap = sizeof(resp);
    Modbus.process_adu(protocore_modbus_span());
    size_t pn = Modbus.n;
    TEST_ASSERT_TRUE(pn > 0);

    uint16_t addr = 0;
    uint8_t ex = 0xFF;
    ModbusMaster.parse_write_response_args.adu = resp;
    ModbusMaster.parse_write_response_args.len = pn;
    ModbusMaster.parse_write_response_args.addr_out = &addr;
    ModbusMaster.parse_write_response_args.exception_out = &ex;
    ModbusMaster.parse_write_response(modbus_master_work);
    int w = ModbusMaster.i32;
    TEST_ASSERT_EQUAL_INT(1, w);
    TEST_ASSERT_EQUAL_UINT8(0, ex);
    TEST_ASSERT_EQUAL_HEX16(7, addr);
    Modbus.get_holding_reg_args.addr = 7;
    Modbus.get_holding_reg(protocore_modbus_span());
    TEST_ASSERT_EQUAL_HEX16(0x5A5A, Modbus.value);
}

void test_build_write_multiple_bytes()
{
    uint8_t adu[32];
    const uint16_t vals[2] = {0x1111, 0x2222};
    ModbusMaster.build_write_multiple_args.txid = 0x0102;
    ModbusMaster.build_write_multiple_args.unit = 1;
    ModbusMaster.build_write_multiple_args.start = 0x0000;
    ModbusMaster.build_write_multiple_args.values = vals;
    ModbusMaster.build_write_multiple_args.count = 2;
    ModbusMaster.build_write_multiple_args.out = adu;
    ModbusMaster.build_write_multiple_args.cap = sizeof(adu);
    ModbusMaster.build_write_multiple(modbus_master_work);
    size_t n = ModbusMaster.n;
    TEST_ASSERT_EQUAL_size_t(17, n);

    const uint8_t expect[17] = {0x01, 0x02, 0x00, 0x00, 0x00, 0x0B, 0x01, 0x10, 0x00,
                                0x00, 0x00, 0x02, 0x04, 0x11, 0x11, 0x22, 0x22};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expect, adu, 17);
}

void test_round_trip_write_multiple()
{
    const uint16_t vals[3] = {0xDEAD, 0xBEEF, 0xF00D};
    uint8_t req[32];
    ModbusMaster.build_write_multiple_args.txid = 5;
    ModbusMaster.build_write_multiple_args.unit = 1;
    ModbusMaster.build_write_multiple_args.start = 30;
    ModbusMaster.build_write_multiple_args.values = vals;
    ModbusMaster.build_write_multiple_args.count = 3;
    ModbusMaster.build_write_multiple_args.out = req;
    ModbusMaster.build_write_multiple_args.cap = sizeof(req);
    ModbusMaster.build_write_multiple(modbus_master_work);
    size_t rn = ModbusMaster.n;
    uint8_t resp[MODBUS_ADU_MAX];
    Modbus.process_adu_args.req = req;
    Modbus.process_adu_args.req_len = rn;
    Modbus.process_adu_args.resp = resp;
    Modbus.process_adu_args.protocore_resp_cap = sizeof(resp);
    Modbus.process_adu(protocore_modbus_span());
    size_t pn = Modbus.n;
    TEST_ASSERT_TRUE(pn > 0);

    uint16_t start = 0;
    uint8_t ex = 0xFF;
    ModbusMaster.parse_write_response_args.adu = resp;
    ModbusMaster.parse_write_response_args.len = pn;
    ModbusMaster.parse_write_response_args.addr_out = &start;
    ModbusMaster.parse_write_response_args.exception_out = &ex;
    ModbusMaster.parse_write_response(modbus_master_work);
    int w = ModbusMaster.i32;
    TEST_ASSERT_EQUAL_INT(3, w);
    TEST_ASSERT_EQUAL_UINT8(0, ex);
    TEST_ASSERT_EQUAL_HEX16(30, start);
    Modbus.get_holding_reg_args.addr = 30;
    Modbus.get_holding_reg(protocore_modbus_span());
    TEST_ASSERT_EQUAL_HEX16(0xDEAD, Modbus.value);
    Modbus.get_holding_reg_args.addr = 31;
    Modbus.get_holding_reg(protocore_modbus_span());
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, Modbus.value);
    Modbus.get_holding_reg_args.addr = 32;
    Modbus.get_holding_reg(protocore_modbus_span());
    TEST_ASSERT_EQUAL_HEX16(0xF00D, Modbus.value);
}

void test_build_write_rejects_bad_args()
{
    uint8_t adu[300];
    const uint16_t vals[2] = {1, 2};
    ModbusMaster.build_write_single_args.txid = 1;
    ModbusMaster.build_write_single_args.unit = 1;
    ModbusMaster.build_write_single_args.addr = 0;
    ModbusMaster.build_write_single_args.value = 5;
    ModbusMaster.build_write_single_args.out = NULL;
    ModbusMaster.build_write_single_args.cap = 16;
    ModbusMaster.build_write_single(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);
    ModbusMaster.build_write_single_args.txid = 1;
    ModbusMaster.build_write_single_args.unit = 1;
    ModbusMaster.build_write_single_args.addr = 0;
    ModbusMaster.build_write_single_args.value = 5;
    ModbusMaster.build_write_single_args.out = adu;
    ModbusMaster.build_write_single_args.cap = 4;
    ModbusMaster.build_write_single(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);
    ModbusMaster.build_write_multiple_args.txid = 1;
    ModbusMaster.build_write_multiple_args.unit = 1;
    ModbusMaster.build_write_multiple_args.start = 0;
    ModbusMaster.build_write_multiple_args.values = vals;
    ModbusMaster.build_write_multiple_args.count = 2;
    ModbusMaster.build_write_multiple_args.out = NULL;
    ModbusMaster.build_write_multiple_args.cap = 32;
    ModbusMaster.build_write_multiple(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);
    ModbusMaster.build_write_multiple_args.txid = 1;
    ModbusMaster.build_write_multiple_args.unit = 1;
    ModbusMaster.build_write_multiple_args.start = 0;
    ModbusMaster.build_write_multiple_args.values = NULL;
    ModbusMaster.build_write_multiple_args.count = 2;
    ModbusMaster.build_write_multiple_args.out = adu;
    ModbusMaster.build_write_multiple_args.cap = 32;
    ModbusMaster.build_write_multiple(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);
    ModbusMaster.build_write_multiple_args.txid = 1;
    ModbusMaster.build_write_multiple_args.unit = 1;
    ModbusMaster.build_write_multiple_args.start = 0;
    ModbusMaster.build_write_multiple_args.values = vals;
    ModbusMaster.build_write_multiple_args.count = 0;
    ModbusMaster.build_write_multiple_args.out = adu;
    ModbusMaster.build_write_multiple_args.cap = 32;
    ModbusMaster.build_write_multiple(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);
    ModbusMaster.build_write_multiple_args.txid = 1;
    ModbusMaster.build_write_multiple_args.unit = 1;
    ModbusMaster.build_write_multiple_args.start = 0;
    ModbusMaster.build_write_multiple_args.values = vals;
    ModbusMaster.build_write_multiple_args.count = 124;
    ModbusMaster.build_write_multiple_args.out = adu;
    ModbusMaster.build_write_multiple_args.cap = 300;
    ModbusMaster.build_write_multiple(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);
    ModbusMaster.build_write_multiple_args.txid = 1;
    ModbusMaster.build_write_multiple_args.unit = 1;
    ModbusMaster.build_write_multiple_args.start = 0;
    ModbusMaster.build_write_multiple_args.values = vals;
    ModbusMaster.build_write_multiple_args.count = 2;
    ModbusMaster.build_write_multiple_args.out = adu;
    ModbusMaster.build_write_multiple_args.cap = 16;
    ModbusMaster.build_write_multiple(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);
}

void test_parse_write_response_edges()
{
    uint16_t addr = 0xFFFF;
    uint8_t ex = 0;

    uint8_t exc[9] = {0, 3, 0, 0, 0, 3, 1, 0x86, 0x02};
    ModbusMaster.parse_write_response_args.adu = exc;
    ModbusMaster.parse_write_response_args.len = sizeof(exc);
    ModbusMaster.parse_write_response_args.addr_out = &addr;
    ModbusMaster.parse_write_response_args.exception_out = &ex;
    ModbusMaster.parse_write_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(0, ModbusMaster.i32);
    TEST_ASSERT_EQUAL_UINT8(0x02, ex);
    TEST_ASSERT_EQUAL_HEX16(0, addr);

    uint8_t shortf[11] = {0, 3, 0, 0, 0, 5, 1, 0x06, 0, 7, 0};
    ModbusMaster.parse_write_response_args.adu = shortf;
    ModbusMaster.parse_write_response_args.len = sizeof(shortf);
    ModbusMaster.parse_write_response_args.addr_out = &addr;
    ModbusMaster.parse_write_response_args.exception_out = &ex;
    ModbusMaster.parse_write_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMaster.i32);
    uint8_t badfc[12] = {0, 3, 0, 0, 0, 6, 1, 0x03, 0, 7, 0, 1};
    ModbusMaster.parse_write_response_args.adu = badfc;
    ModbusMaster.parse_write_response_args.len = sizeof(badfc);
    ModbusMaster.parse_write_response_args.addr_out = &addr;
    ModbusMaster.parse_write_response_args.exception_out = &ex;
    ModbusMaster.parse_write_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMaster.i32);

    uint8_t badproto[12] = {0, 3, 0, 1, 0, 6, 1, 0x06, 0, 7, 0xAB, 0xCD};
    ModbusMaster.parse_write_response_args.adu = badproto;
    ModbusMaster.parse_write_response_args.len = sizeof(badproto);
    ModbusMaster.parse_write_response_args.addr_out = &addr;
    ModbusMaster.parse_write_response_args.exception_out = &ex;
    ModbusMaster.parse_write_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMaster.i32);
    ModbusMaster.parse_write_response_args.adu = NULL;
    ModbusMaster.parse_write_response_args.len = 12;
    ModbusMaster.parse_write_response_args.addr_out = &addr;
    ModbusMaster.parse_write_response_args.exception_out = &ex;
    ModbusMaster.parse_write_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMaster.i32);
}

void test_round_trip_read_coils()
{
    Modbus.set_coil_args.addr = 0;
    Modbus.set_coil_args.on = PROTO_TRUE;
    Modbus.set_coil(protocore_modbus_span());
    Modbus.set_coil_args.addr = 1;
    Modbus.set_coil_args.on = PROTO_FALSE;
    Modbus.set_coil(protocore_modbus_span());
    Modbus.set_coil_args.addr = 2;
    Modbus.set_coil_args.on = PROTO_TRUE;
    Modbus.set_coil(protocore_modbus_span());
    Modbus.set_coil_args.addr = 9;
    Modbus.set_coil_args.on = PROTO_TRUE;
    Modbus.set_coil(protocore_modbus_span());

    uint8_t req[16];
    ModbusMaster.build_read_bits_args.fc = (uint8_t)MODBUS_FC_READ_COILS;
    ModbusMaster.build_read_bits_args.txid = 7;
    ModbusMaster.build_read_bits_args.unit = 1;
    ModbusMaster.build_read_bits_args.start = 0;
    ModbusMaster.build_read_bits_args.count = 10;
    ModbusMaster.build_read_bits_args.out = req;
    ModbusMaster.build_read_bits_args.cap = sizeof(req);
    ModbusMaster.build_read_bits(modbus_master_work);
    size_t rn = ModbusMaster.n;
    TEST_ASSERT_EQUAL_size_t(12, rn);
    TEST_ASSERT_EQUAL_HEX8(0x01, req[7]);

    uint8_t resp[MODBUS_ADU_MAX];
    Modbus.process_adu_args.req = req;
    Modbus.process_adu_args.req_len = rn;
    Modbus.process_adu_args.resp = resp;
    Modbus.process_adu_args.protocore_resp_cap = sizeof(resp);
    Modbus.process_adu(protocore_modbus_span());
    size_t pn = Modbus.n;
    TEST_ASSERT_TRUE(pn > 0);

    uint8_t bits[10];
    uint8_t ex = 0xFF;
    ModbusMaster.parse_read_bits_response_args.adu = resp;
    ModbusMaster.parse_read_bits_response_args.len = pn;
    ModbusMaster.parse_read_bits_response_args.count = 10;
    ModbusMaster.parse_read_bits_response_args.bits_out = bits;
    ModbusMaster.parse_read_bits_response_args.max_bits = sizeof(bits);
    ModbusMaster.parse_read_bits_response_args.exception_out = &ex;
    ModbusMaster.parse_read_bits_response(modbus_master_work);
    int got = ModbusMaster.i32;
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
    Modbus.set_discrete_input_args.addr = 3;
    Modbus.set_discrete_input_args.on = PROTO_TRUE;
    Modbus.set_discrete_input(protocore_modbus_span());
    Modbus.set_discrete_input_args.addr = 4;
    Modbus.set_discrete_input_args.on = PROTO_TRUE;
    Modbus.set_discrete_input(protocore_modbus_span());

    uint8_t req[16];
    ModbusMaster.build_read_bits_args.fc = (uint8_t)MODBUS_FC_READ_DISCRETE_INPUTS;
    ModbusMaster.build_read_bits_args.txid = 8;
    ModbusMaster.build_read_bits_args.unit = 1;
    ModbusMaster.build_read_bits_args.start = 0;
    ModbusMaster.build_read_bits_args.count = 6;
    ModbusMaster.build_read_bits_args.out = req;
    ModbusMaster.build_read_bits_args.cap = sizeof(req);
    ModbusMaster.build_read_bits(modbus_master_work);
    size_t rn = ModbusMaster.n;
    TEST_ASSERT_EQUAL_HEX8(0x02, req[7]);
    uint8_t resp[MODBUS_ADU_MAX];
    Modbus.process_adu_args.req = req;
    Modbus.process_adu_args.req_len = rn;
    Modbus.process_adu_args.resp = resp;
    Modbus.process_adu_args.protocore_resp_cap = sizeof(resp);
    Modbus.process_adu(protocore_modbus_span());
    size_t pn = Modbus.n;
    uint8_t bits[6];
    uint8_t ex = 0xFF;
    ModbusMaster.parse_read_bits_response_args.adu = resp;
    ModbusMaster.parse_read_bits_response_args.len = pn;
    ModbusMaster.parse_read_bits_response_args.count = 6;
    ModbusMaster.parse_read_bits_response_args.bits_out = bits;
    ModbusMaster.parse_read_bits_response_args.max_bits = sizeof(bits);
    ModbusMaster.parse_read_bits_response_args.exception_out = &ex;
    ModbusMaster.parse_read_bits_response(modbus_master_work);
    int got = ModbusMaster.i32;
    TEST_ASSERT_EQUAL_INT(6, got);
    TEST_ASSERT_EQUAL_UINT8(1, bits[3]);
    TEST_ASSERT_EQUAL_UINT8(1, bits[4]);
    TEST_ASSERT_EQUAL_UINT8(0, bits[5]);
}

void test_round_trip_write_single_coil()
{
    Modbus.set_coil_args.addr = 5;
    Modbus.set_coil_args.on = PROTO_FALSE;
    Modbus.set_coil(protocore_modbus_span());
    uint8_t req[16];
    ModbusMaster.build_write_single_coil_args.txid = 11;
    ModbusMaster.build_write_single_coil_args.unit = 1;
    ModbusMaster.build_write_single_coil_args.addr = 5;
    ModbusMaster.build_write_single_coil_args.on = PROTO_TRUE;
    ModbusMaster.build_write_single_coil_args.out = req;
    ModbusMaster.build_write_single_coil_args.cap = sizeof(req);
    ModbusMaster.build_write_single_coil(modbus_master_work);
    size_t rn = ModbusMaster.n;
    TEST_ASSERT_EQUAL_size_t(12, rn);
    TEST_ASSERT_EQUAL_HEX8(0x05, req[7]);
    TEST_ASSERT_EQUAL_HEX8(0xFF, req[10]);
    TEST_ASSERT_EQUAL_HEX8(0x00, req[11]);

    uint8_t resp[MODBUS_ADU_MAX];
    Modbus.process_adu_args.req = req;
    Modbus.process_adu_args.req_len = rn;
    Modbus.process_adu_args.resp = resp;
    Modbus.process_adu_args.protocore_resp_cap = sizeof(resp);
    Modbus.process_adu(protocore_modbus_span());
    size_t pn = Modbus.n;
    uint16_t addr = 0xFFFF;
    uint8_t ex = 0xFF;
    ModbusMaster.parse_write_response_args.adu = resp;
    ModbusMaster.parse_write_response_args.len = pn;
    ModbusMaster.parse_write_response_args.addr_out = &addr;
    ModbusMaster.parse_write_response_args.exception_out = &ex;
    ModbusMaster.parse_write_response(modbus_master_work);
    int wrote = ModbusMaster.i32;
    TEST_ASSERT_EQUAL_INT(1, wrote);
    TEST_ASSERT_EQUAL_UINT8(0, ex);
    TEST_ASSERT_EQUAL_HEX16(5, addr);
    Modbus.get_coil_args.addr = 5;
    Modbus.get_coil(protocore_modbus_span());
    TEST_ASSERT_TRUE(Modbus.ok);
}

void test_round_trip_write_multiple_coils()
{

    for (uint16_t a = 0; a < 12; a++)
    {
        Modbus.set_coil_args.addr = a;
        Modbus.set_coil_args.on = PROTO_FALSE;
        Modbus.set_coil(protocore_modbus_span());
    }
    const uint8_t pattern[12] = {1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 0, 1};

    uint8_t req[24];
    ModbusMaster.build_write_multiple_coils_args.txid = 12;
    ModbusMaster.build_write_multiple_coils_args.unit = 1;
    ModbusMaster.build_write_multiple_coils_args.start = 0;
    ModbusMaster.build_write_multiple_coils_args.bits = pattern;
    ModbusMaster.build_write_multiple_coils_args.count = 12;
    ModbusMaster.build_write_multiple_coils_args.out = req;
    ModbusMaster.build_write_multiple_coils_args.cap = sizeof(req);
    ModbusMaster.build_write_multiple_coils(modbus_master_work);
    size_t rn = ModbusMaster.n;
    TEST_ASSERT_EQUAL_size_t(7 + 6 + 2, rn);
    TEST_ASSERT_EQUAL_HEX8(0x0F, req[7]);
    TEST_ASSERT_EQUAL_HEX8(2, req[12]);
    TEST_ASSERT_EQUAL_HEX8(0x55, req[13]);
    TEST_ASSERT_EQUAL_HEX8(0x0B, req[14]);

    uint8_t resp[MODBUS_ADU_MAX];
    Modbus.process_adu_args.req = req;
    Modbus.process_adu_args.req_len = rn;
    Modbus.process_adu_args.resp = resp;
    Modbus.process_adu_args.protocore_resp_cap = sizeof(resp);
    Modbus.process_adu(protocore_modbus_span());
    size_t pn = Modbus.n;
    uint16_t addr = 0xFFFF;
    uint8_t ex = 0xFF;
    ModbusMaster.parse_write_response_args.adu = resp;
    ModbusMaster.parse_write_response_args.len = pn;
    ModbusMaster.parse_write_response_args.addr_out = &addr;
    ModbusMaster.parse_write_response_args.exception_out = &ex;
    ModbusMaster.parse_write_response(modbus_master_work);
    int wrote = ModbusMaster.i32;
    TEST_ASSERT_EQUAL_INT(12, wrote);
    TEST_ASSERT_EQUAL_HEX16(0, addr);
    for (uint16_t a = 0; a < 12; a++)
    {
        Modbus.get_coil_args.addr = a;
        Modbus.get_coil(protocore_modbus_span());
        TEST_ASSERT_EQUAL_UINT8(pattern[a], Modbus.ok ? 1 : 0);
    }
}

void test_bit_build_and_parse_guards()
{
    uint8_t adu[16];

    ModbusMaster.build_read_bits_args.fc = 0x03;
    ModbusMaster.build_read_bits_args.txid = 1;
    ModbusMaster.build_read_bits_args.unit = 1;
    ModbusMaster.build_read_bits_args.start = 0;
    ModbusMaster.build_read_bits_args.count = 8;
    ModbusMaster.build_read_bits_args.out = adu;
    ModbusMaster.build_read_bits_args.cap = sizeof(adu);
    ModbusMaster.build_read_bits(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);
    ModbusMaster.build_read_bits_args.fc = 0x01;
    ModbusMaster.build_read_bits_args.txid = 1;
    ModbusMaster.build_read_bits_args.unit = 1;
    ModbusMaster.build_read_bits_args.start = 0;
    ModbusMaster.build_read_bits_args.count = 0;
    ModbusMaster.build_read_bits_args.out = adu;
    ModbusMaster.build_read_bits_args.cap = sizeof(adu);
    ModbusMaster.build_read_bits(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);
    ModbusMaster.build_read_bits_args.fc = 0x01;
    ModbusMaster.build_read_bits_args.txid = 1;
    ModbusMaster.build_read_bits_args.unit = 1;
    ModbusMaster.build_read_bits_args.start = 0;
    ModbusMaster.build_read_bits_args.count = 2001;
    ModbusMaster.build_read_bits_args.out = adu;
    ModbusMaster.build_read_bits_args.cap = sizeof(adu);
    ModbusMaster.build_read_bits(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);
    ModbusMaster.build_read_bits_args.fc = 0x01;
    ModbusMaster.build_read_bits_args.txid = 1;
    ModbusMaster.build_read_bits_args.unit = 1;
    ModbusMaster.build_read_bits_args.start = 0;
    ModbusMaster.build_read_bits_args.count = 8;
    ModbusMaster.build_read_bits_args.out = NULL;
    ModbusMaster.build_read_bits_args.cap = 16;
    ModbusMaster.build_read_bits(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);

    ModbusMaster.build_write_single_coil_args.txid = 1;
    ModbusMaster.build_write_single_coil_args.unit = 1;
    ModbusMaster.build_write_single_coil_args.addr = 0;
    ModbusMaster.build_write_single_coil_args.on = PROTO_TRUE;
    ModbusMaster.build_write_single_coil_args.out = NULL;
    ModbusMaster.build_write_single_coil_args.cap = 16;
    ModbusMaster.build_write_single_coil(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);
    const uint8_t bits[4] = {1, 0, 1, 1};
    ModbusMaster.build_write_multiple_coils_args.txid = 1;
    ModbusMaster.build_write_multiple_coils_args.unit = 1;
    ModbusMaster.build_write_multiple_coils_args.start = 0;
    ModbusMaster.build_write_multiple_coils_args.bits = NULL;
    ModbusMaster.build_write_multiple_coils_args.count = 4;
    ModbusMaster.build_write_multiple_coils_args.out = adu;
    ModbusMaster.build_write_multiple_coils_args.cap = sizeof(adu);
    ModbusMaster.build_write_multiple_coils(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);
    ModbusMaster.build_write_multiple_coils_args.txid = 1;
    ModbusMaster.build_write_multiple_coils_args.unit = 1;
    ModbusMaster.build_write_multiple_coils_args.start = 0;
    ModbusMaster.build_write_multiple_coils_args.bits = bits;
    ModbusMaster.build_write_multiple_coils_args.count = 0;
    ModbusMaster.build_write_multiple_coils_args.out = adu;
    ModbusMaster.build_write_multiple_coils_args.cap = sizeof(adu);
    ModbusMaster.build_write_multiple_coils(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);
    ModbusMaster.build_write_multiple_coils_args.txid = 1;
    ModbusMaster.build_write_multiple_coils_args.unit = 1;
    ModbusMaster.build_write_multiple_coils_args.start = 0;
    ModbusMaster.build_write_multiple_coils_args.bits = bits;
    ModbusMaster.build_write_multiple_coils_args.count = 1969;
    ModbusMaster.build_write_multiple_coils_args.out = adu;
    ModbusMaster.build_write_multiple_coils_args.cap = sizeof(adu);
    ModbusMaster.build_write_multiple_coils(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);

    uint8_t resp[16] = {0, 7, 0, 0, 0, 4, 1, 0x01, 2, 0x05, 0x00};
    uint8_t out[8];
    uint8_t ex = 0;
    ModbusMaster.parse_read_bits_response_args.adu = resp;
    ModbusMaster.parse_read_bits_response_args.len = 11;
    ModbusMaster.parse_read_bits_response_args.count = 4;
    ModbusMaster.parse_read_bits_response_args.bits_out = out;
    ModbusMaster.parse_read_bits_response_args.max_bits = sizeof(out);
    ModbusMaster.parse_read_bits_response_args.exception_out = &ex;
    ModbusMaster.parse_read_bits_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMaster.i32);

    uint8_t exc[9] = {0, 7, 0, 0, 0, 3, 1, 0x81, 0x02};
    ModbusMaster.parse_read_bits_response_args.adu = exc;
    ModbusMaster.parse_read_bits_response_args.len = sizeof(exc);
    ModbusMaster.parse_read_bits_response_args.count = 4;
    ModbusMaster.parse_read_bits_response_args.bits_out = out;
    ModbusMaster.parse_read_bits_response_args.max_bits = sizeof(out);
    ModbusMaster.parse_read_bits_response_args.exception_out = &ex;
    ModbusMaster.parse_read_bits_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(0, ModbusMaster.i32);
    TEST_ASSERT_EQUAL_UINT8(0x02, ex);
}

void test_round_trip_mask_write()
{
    Modbus.set_holding_reg_args.addr = 10;
    Modbus.set_holding_reg_args.value = 0x1234;
    Modbus.set_holding_reg(protocore_modbus_span());

    uint8_t req[16];
    ModbusMaster.build_mask_write_args.txid = 5;
    ModbusMaster.build_mask_write_args.unit = 1;
    ModbusMaster.build_mask_write_args.addr = 10;
    ModbusMaster.build_mask_write_args.and_mask = 0xF0FF;
    ModbusMaster.build_mask_write_args.or_mask = 0x0500;
    ModbusMaster.build_mask_write_args.out = req;
    ModbusMaster.build_mask_write_args.cap = sizeof(req);
    ModbusMaster.build_mask_write(modbus_master_work);
    size_t rn = ModbusMaster.n;
    TEST_ASSERT_EQUAL_size_t(14, rn);
    TEST_ASSERT_EQUAL_HEX8(0x16, req[7]);

    uint8_t resp[MODBUS_ADU_MAX];
    Modbus.process_adu_args.req = req;
    Modbus.process_adu_args.req_len = rn;
    Modbus.process_adu_args.resp = resp;
    Modbus.process_adu_args.protocore_resp_cap = sizeof(resp);
    Modbus.process_adu(protocore_modbus_span());
    size_t pn = Modbus.n;
    uint16_t addr = 0, andm = 0, orm = 0;
    uint8_t ex = 0xFF;
    ModbusMaster.parse_mask_write_response_args.adu = resp;
    ModbusMaster.parse_mask_write_response_args.len = pn;
    ModbusMaster.parse_mask_write_response_args.addr_out = &addr;
    ModbusMaster.parse_mask_write_response_args.and_out = &andm;
    ModbusMaster.parse_mask_write_response_args.or_out = &orm;
    ModbusMaster.parse_mask_write_response_args.exception_out = &ex;
    ModbusMaster.parse_mask_write_response(modbus_master_work);
    int r = ModbusMaster.i32;
    TEST_ASSERT_EQUAL_INT(1, r);
    TEST_ASSERT_EQUAL_UINT8(0, ex);
    TEST_ASSERT_EQUAL_HEX16(10, addr);
    TEST_ASSERT_EQUAL_HEX16(0xF0FF, andm);
    TEST_ASSERT_EQUAL_HEX16(0x0500, orm);
    Modbus.get_holding_reg_args.addr = 10;
    Modbus.get_holding_reg(protocore_modbus_span());
    TEST_ASSERT_EQUAL_HEX16(0x1534, Modbus.value);
}

void test_round_trip_read_write_multiple()
{
    Modbus.set_holding_reg_args.addr = 20;
    Modbus.set_holding_reg_args.value = 0x1111;
    Modbus.set_holding_reg(protocore_modbus_span());
    Modbus.set_holding_reg_args.addr = 21;
    Modbus.set_holding_reg_args.value = 0x2222;
    Modbus.set_holding_reg(protocore_modbus_span());

    const uint16_t wvals[2] = {0xAAAA, 0xBBBB};
    uint8_t req[32];
    ModbusMaster.build_read_write_multiple_args.txid = 9;
    ModbusMaster.build_read_write_multiple_args.unit = 1;
    ModbusMaster.build_read_write_multiple_args.read_start = 20;
    ModbusMaster.build_read_write_multiple_args.read_count = 2;
    ModbusMaster.build_read_write_multiple_args.write_start = 20;
    ModbusMaster.build_read_write_multiple_args.values = wvals;
    ModbusMaster.build_read_write_multiple_args.write_count = 2;
    ModbusMaster.build_read_write_multiple_args.out = req;
    ModbusMaster.build_read_write_multiple_args.cap = sizeof(req);
    ModbusMaster.build_read_write_multiple(modbus_master_work);
    size_t rn = ModbusMaster.n;
    TEST_ASSERT_EQUAL_size_t(7 + 10 + 4, rn);
    TEST_ASSERT_EQUAL_HEX8(0x17, req[7]);

    uint8_t resp[MODBUS_ADU_MAX];
    Modbus.process_adu_args.req = req;
    Modbus.process_adu_args.req_len = rn;
    Modbus.process_adu_args.resp = resp;
    Modbus.process_adu_args.protocore_resp_cap = sizeof(resp);
    Modbus.process_adu(protocore_modbus_span());
    size_t pn = Modbus.n;
    uint16_t regs[2];
    uint8_t ex = 0xFF;
    ModbusMaster.parse_response_args.adu = resp;
    ModbusMaster.parse_response_args.len = pn;
    ModbusMaster.parse_response_args.regs_out = regs;
    ModbusMaster.parse_response_args.max_regs = 2;
    ModbusMaster.parse_response_args.exception_out = &ex;
    ModbusMaster.parse_response(modbus_master_work);
    int got = ModbusMaster.i32;
    TEST_ASSERT_EQUAL_INT(2, got);
    TEST_ASSERT_EQUAL_UINT8(0, ex);
    TEST_ASSERT_EQUAL_HEX16(0xAAAA, regs[0]);
    TEST_ASSERT_EQUAL_HEX16(0xBBBB, regs[1]);
    Modbus.get_holding_reg_args.addr = 21;
    Modbus.get_holding_reg(protocore_modbus_span());
    TEST_ASSERT_EQUAL_HEX16(0xBBBB, Modbus.value);
}

void test_fc16_17_guards()
{
    uint8_t adu[32];
    const uint16_t vals[2] = {1, 2};

    ModbusMaster.build_mask_write_args.txid = 1;
    ModbusMaster.build_mask_write_args.unit = 1;
    ModbusMaster.build_mask_write_args.addr = 0;
    ModbusMaster.build_mask_write_args.and_mask = 0;
    ModbusMaster.build_mask_write_args.or_mask = 0;
    ModbusMaster.build_mask_write_args.out = NULL;
    ModbusMaster.build_mask_write_args.cap = 16;
    ModbusMaster.build_mask_write(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);
    ModbusMaster.build_mask_write_args.txid = 1;
    ModbusMaster.build_mask_write_args.unit = 1;
    ModbusMaster.build_mask_write_args.addr = 0;
    ModbusMaster.build_mask_write_args.and_mask = 0;
    ModbusMaster.build_mask_write_args.or_mask = 0;
    ModbusMaster.build_mask_write_args.out = adu;
    ModbusMaster.build_mask_write_args.cap = 8;
    ModbusMaster.build_mask_write(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);

    ModbusMaster.build_read_write_multiple_args.txid = 1;
    ModbusMaster.build_read_write_multiple_args.unit = 1;
    ModbusMaster.build_read_write_multiple_args.read_start = 0;
    ModbusMaster.build_read_write_multiple_args.read_count = 2;
    ModbusMaster.build_read_write_multiple_args.write_start = 0;
    ModbusMaster.build_read_write_multiple_args.values = NULL;
    ModbusMaster.build_read_write_multiple_args.write_count = 2;
    ModbusMaster.build_read_write_multiple_args.out = adu;
    ModbusMaster.build_read_write_multiple_args.cap = sizeof(adu);
    ModbusMaster.build_read_write_multiple(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);
    ModbusMaster.build_read_write_multiple_args.txid = 1;
    ModbusMaster.build_read_write_multiple_args.unit = 1;
    ModbusMaster.build_read_write_multiple_args.read_start = 0;
    ModbusMaster.build_read_write_multiple_args.read_count = 0;
    ModbusMaster.build_read_write_multiple_args.write_start = 0;
    ModbusMaster.build_read_write_multiple_args.values = vals;
    ModbusMaster.build_read_write_multiple_args.write_count = 2;
    ModbusMaster.build_read_write_multiple_args.out = adu;
    ModbusMaster.build_read_write_multiple_args.cap = sizeof(adu);
    ModbusMaster.build_read_write_multiple(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);
    ModbusMaster.build_read_write_multiple_args.txid = 1;
    ModbusMaster.build_read_write_multiple_args.unit = 1;
    ModbusMaster.build_read_write_multiple_args.read_start = 0;
    ModbusMaster.build_read_write_multiple_args.read_count = 126;
    ModbusMaster.build_read_write_multiple_args.write_start = 0;
    ModbusMaster.build_read_write_multiple_args.values = vals;
    ModbusMaster.build_read_write_multiple_args.write_count = 2;
    ModbusMaster.build_read_write_multiple_args.out = adu;
    ModbusMaster.build_read_write_multiple_args.cap = sizeof(adu);
    ModbusMaster.build_read_write_multiple(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);
    ModbusMaster.build_read_write_multiple_args.txid = 1;
    ModbusMaster.build_read_write_multiple_args.unit = 1;
    ModbusMaster.build_read_write_multiple_args.read_start = 0;
    ModbusMaster.build_read_write_multiple_args.read_count = 2;
    ModbusMaster.build_read_write_multiple_args.write_start = 0;
    ModbusMaster.build_read_write_multiple_args.values = vals;
    ModbusMaster.build_read_write_multiple_args.write_count = 0;
    ModbusMaster.build_read_write_multiple_args.out = adu;
    ModbusMaster.build_read_write_multiple_args.cap = sizeof(adu);
    ModbusMaster.build_read_write_multiple(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);
    ModbusMaster.build_read_write_multiple_args.txid = 1;
    ModbusMaster.build_read_write_multiple_args.unit = 1;
    ModbusMaster.build_read_write_multiple_args.read_start = 0;
    ModbusMaster.build_read_write_multiple_args.read_count = 2;
    ModbusMaster.build_read_write_multiple_args.write_start = 0;
    ModbusMaster.build_read_write_multiple_args.values = vals;
    ModbusMaster.build_read_write_multiple_args.write_count = 122;
    ModbusMaster.build_read_write_multiple_args.out = adu;
    ModbusMaster.build_read_write_multiple_args.cap = sizeof(adu);
    ModbusMaster.build_read_write_multiple(modbus_master_work);
    TEST_ASSERT_EQUAL_size_t(0, ModbusMaster.n);

    uint8_t req[16];
    ModbusMaster.build_mask_write_args.txid = 1;
    ModbusMaster.build_mask_write_args.unit = 1;
    ModbusMaster.build_mask_write_args.addr = 60000;
    ModbusMaster.build_mask_write_args.and_mask = 0xFFFF;
    ModbusMaster.build_mask_write_args.or_mask = 0;
    ModbusMaster.build_mask_write_args.out = req;
    ModbusMaster.build_mask_write_args.cap = sizeof(req);
    ModbusMaster.build_mask_write(modbus_master_work);
    size_t rn = ModbusMaster.n;
    uint8_t resp[MODBUS_ADU_MAX];
    Modbus.process_adu_args.req = req;
    Modbus.process_adu_args.req_len = rn;
    Modbus.process_adu_args.resp = resp;
    Modbus.process_adu_args.protocore_resp_cap = sizeof(resp);
    Modbus.process_adu(protocore_modbus_span());
    size_t pn = Modbus.n;
    uint8_t ex = 0;
    ModbusMaster.parse_mask_write_response_args.adu = resp;
    ModbusMaster.parse_mask_write_response_args.len = pn;
    ModbusMaster.parse_mask_write_response_args.addr_out = NULL;
    ModbusMaster.parse_mask_write_response_args.and_out = NULL;
    ModbusMaster.parse_mask_write_response_args.or_out = NULL;
    ModbusMaster.parse_mask_write_response_args.exception_out = &ex;
    ModbusMaster.parse_mask_write_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(0, ModbusMaster.i32);
    TEST_ASSERT_EQUAL_UINT8(MODBUS_EX_ILLEGAL_DATA_ADDRESS, ex);

    uint8_t shortf[10] = {0, 1, 0, 0, 0, 8, 1, 0x16, 0, 0};
    ModbusMaster.parse_mask_write_response_args.adu = shortf;
    ModbusMaster.parse_mask_write_response_args.len = sizeof(shortf);
    ModbusMaster.parse_mask_write_response_args.addr_out = NULL;
    ModbusMaster.parse_mask_write_response_args.and_out = NULL;
    ModbusMaster.parse_mask_write_response_args.or_out = NULL;
    ModbusMaster.parse_mask_write_response_args.exception_out = &ex;
    ModbusMaster.parse_mask_write_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMaster.i32);
    uint8_t badfc[14] = {0, 1, 0, 0, 0, 8, 1, 0x06, 0, 0, 0, 0, 0, 0};
    ModbusMaster.parse_mask_write_response_args.adu = badfc;
    ModbusMaster.parse_mask_write_response_args.len = sizeof(badfc);
    ModbusMaster.parse_mask_write_response_args.addr_out = NULL;
    ModbusMaster.parse_mask_write_response_args.and_out = NULL;
    ModbusMaster.parse_mask_write_response_args.or_out = NULL;
    ModbusMaster.parse_mask_write_response_args.exception_out = &ex;
    ModbusMaster.parse_mask_write_response(modbus_master_work);
    TEST_ASSERT_EQUAL_INT(-1, ModbusMaster.i32);
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
