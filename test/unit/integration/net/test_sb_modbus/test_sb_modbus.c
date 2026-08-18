// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "services/fieldbus/modbus/modbus.h"
#include "services/southbound/sb_modbus.h"
#include "services/southbound/southbound.h"
#include <unity.h>

static uint8_t sb_modbus_work[16]; // the borrow an entry takes; SbModbus never reads it

static int loopback_txn(void *io, const uint8_t *req, size_t req_len, uint8_t *resp, size_t resp_cap)
{
    (void)io;
    Modbus.process_adu_args.req = req;
    Modbus.process_adu_args.req_len = req_len;
    Modbus.process_adu_args.resp = resp;
    Modbus.process_adu_args.protocore_resp_cap = resp_cap;
    Modbus.process_adu(protocore_modbus_span());
    size_t pn = Modbus.n;
    return (pn == 0) ? -1 : (int)pn;
}

static int fail_txn(void *io, const uint8_t *req, size_t req_len, uint8_t *resp, size_t resp_cap)
{
    (void)io;
    (void)req;
    (void)req_len;
    (void)resp;
    (void)resp_cap;
    return -42;
}

static protocore_sb_modbus_ctx g_ctx;
static SouthboundDriver g_drv;

// Fill a driver instance from the transport seam and the slave it addresses.
static int32_t sbm_init(protocore_sb_modbus_ctx *ctx, protocore_sb_modbus_txn txn, void *io, ModbusFunction fc,
                        uint8_t unit)
{
    SbModbus.ctx = ctx;
    SbModbus.txn = txn;
    SbModbus.io = io;
    SbModbus.fc = fc;
    SbModbus.unit = unit;
    SbModbus.init(sb_modbus_work);
    return SbModbus.i32;
}

// Bind a vtable to an instance.
static int32_t sbm_driver(SouthboundDriver *drv_out, const char *name, protocore_sb_modbus_ctx *ctx)
{
    SbModbus.drv_out = drv_out;
    SbModbus.name = name;
    SbModbus.ctx = ctx;
    SbModbus.driver(sb_modbus_work);
    return SbModbus.i32;
}

static int32_t sb_add(const SouthboundDriver *drv)
{
    Southbound.drv = drv;
    Southbound.add(protocore_southbound_span());
    return Southbound.i32;
}

static int32_t sb_read(const char *name, uint32_t point, int32_t *value_out)
{
    Southbound.name = name;
    Southbound.point.point = point;
    Southbound.point.value_out = value_out;
    Southbound.read(protocore_southbound_span());
    return Southbound.i32;
}

static int32_t sb_write(const char *name, uint32_t point, int32_t value)
{
    Southbound.name = name;
    Southbound.point.point = point;
    Southbound.point.value = value;
    Southbound.write(protocore_southbound_span());
    return Southbound.i32;
}

static int32_t sb_read_block(const char *name, uint32_t first, int32_t *out, size_t n)
{
    Southbound.name = name;
    Southbound.block.first = first;
    Southbound.block.out = out;
    Southbound.block.n = n;
    Southbound.read_block(protocore_southbound_span());
    return Southbound.i32;
}

static int32_t sb_write_block(const char *name, uint32_t first, const int32_t *in, size_t n)
{
    Southbound.name = name;
    Southbound.block.first = first;
    Southbound.block.in = in;
    Southbound.block.n = n;
    Southbound.write_block(protocore_southbound_span());
    return Southbound.i32;
}

void setUp()
{
    Modbus.server_init(protocore_modbus_span());
    Southbound.clear(protocore_southbound_span());
}
void tearDown()
{
}

void test_read_single_holding()
{
    Modbus.set_holding_reg_args.addr = 10;
    Modbus.set_holding_reg_args.value = 0xBEEF;
    Modbus.set_holding_reg(protocore_modbus_span());
    TEST_ASSERT_EQUAL_INT(SB_OK, sbm_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1));
    TEST_ASSERT_EQUAL_INT(SB_OK, sbm_driver(&g_drv, "plc", &g_ctx));
    TEST_ASSERT_EQUAL_INT(SB_OK, sb_add(&g_drv));

    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(SB_OK, sb_read("plc", 10, &v));
    TEST_ASSERT_EQUAL_INT32(0xBEEF, v);
}

void test_read_block_matrix()
{
    for (uint16_t i = 0; i < 4; i++)
    {
        Modbus.set_holding_reg_args.addr = (uint16_t)(20 + i);
        Modbus.set_holding_reg_args.value = (uint16_t)(0x1000 + i);
        Modbus.set_holding_reg(protocore_modbus_span());
    }
    sbm_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1);
    sbm_driver(&g_drv, "plc", &g_ctx);
    sb_add(&g_drv);

    int32_t vals[4] = {0, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(4, sb_read_block("plc", 20, vals, 4));
    TEST_ASSERT_EQUAL_INT32(0x1000, vals[0]);
    TEST_ASSERT_EQUAL_INT32(0x1001, vals[1]);
    TEST_ASSERT_EQUAL_INT32(0x1002, vals[2]);
    TEST_ASSERT_EQUAL_INT32(0x1003, vals[3]);
}

void test_read_input_registers()
{
    Modbus.set_input_reg_args.addr = 5;
    Modbus.set_input_reg_args.value = 0x0777;
    Modbus.set_input_reg(protocore_modbus_span());
    sbm_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_INPUT_REGS, 1);
    sbm_driver(&g_drv, "sensor", &g_ctx);
    sb_add(&g_drv);

    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(SB_OK, sb_read("sensor", 5, &v));
    TEST_ASSERT_EQUAL_INT32(0x0777, v);
}

void test_modbus_exception_surfaces()
{
    sbm_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1);
    sbm_driver(&g_drv, "plc", &g_ctx);
    sb_add(&g_drv);

    int32_t v = 123;
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SB_MODBUS_EXCEPTION, sb_read("plc", 60000, &v));
    TEST_ASSERT_EQUAL_UINT8(MODBUS_EX_ILLEGAL_DATA_ADDRESS, g_ctx.last_exception);
    TEST_ASSERT_EQUAL_INT32(123, v);
}

void test_transport_error_propagates()
{
    sbm_init(&g_ctx, fail_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1);
    sbm_driver(&g_drv, "plc", &g_ctx);
    sb_add(&g_drv);

    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(-42, sb_read("plc", 0, &v));
    int32_t blk[2] = {0, 0};
    TEST_ASSERT_EQUAL_INT(-42, sb_read_block("plc", 0, blk, 2));
}

void test_write_single_round_trip()
{
    sbm_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1);
    sbm_driver(&g_drv, "plc", &g_ctx);
    sb_add(&g_drv);

    TEST_ASSERT_EQUAL_INT(SB_OK, sb_write("plc", 8, 0x4242));
    Modbus.get_holding_reg_args.addr = 8;
    Modbus.get_holding_reg(protocore_modbus_span());
    TEST_ASSERT_EQUAL_HEX16(0x4242, Modbus.value);
    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(SB_OK, sb_read("plc", 8, &v));
    TEST_ASSERT_EQUAL_INT32(0x4242, v);
}

void test_write_block_round_trip()
{
    sbm_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1);
    sbm_driver(&g_drv, "plc", &g_ctx);
    sb_add(&g_drv);

    int32_t in[3] = {0x0A0A, 0x0B0B, 0x0C0C};
    TEST_ASSERT_EQUAL_INT(3, sb_write_block("plc", 40, in, 3));
    int32_t back[3] = {0, 0, 0};
    TEST_ASSERT_EQUAL_INT(3, sb_read_block("plc", 40, back, 3));
    TEST_ASSERT_EQUAL_INT32(0x0A0A, back[0]);
    TEST_ASSERT_EQUAL_INT32(0x0B0B, back[1]);
    TEST_ASSERT_EQUAL_INT32(0x0C0C, back[2]);
}

void test_input_registers_read_only()
{
    sbm_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_INPUT_REGS, 1);
    sbm_driver(&g_drv, "sensor", &g_ctx);
    sb_add(&g_drv);

    TEST_ASSERT_NULL(g_drv.write);
    TEST_ASSERT_NULL(g_drv.write_block);
    TEST_ASSERT_EQUAL_INT(SB_ERR_UNSUPPORTED, sb_write("sensor", 0, 1));
    int32_t in[2] = {1, 2};
    TEST_ASSERT_EQUAL_INT(SB_ERR_UNSUPPORTED, sb_write_block("sensor", 0, in, 2));
}

void test_write_bounds()
{
    sbm_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1);
    sbm_driver(&g_drv, "plc", &g_ctx);
    sb_add(&g_drv);

    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, sb_write("plc", 0, 0x10000));
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, sb_write("plc", 0, -1));
    int32_t big[124];
    for (int i = 0; i < 124; i++)
    {
        big[i] = 1;
    }
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, sb_write_block("plc", 0, big, 124));
    int32_t bad[2] = {0, 0x10000};
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, sb_write_block("plc", 0, bad, 2));
}

void test_init_rejects_bad_args()
{
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, sbm_init(&g_ctx, NULL, NULL, MODBUS_FC_READ_HOLDING_REGS, 1));
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, sbm_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_WRITE_SINGLE_REG, 1));
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, sbm_init(NULL, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1));
}

void test_read_bounds()
{
    sbm_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1);
    sbm_driver(&g_drv, "plc", &g_ctx);
    sb_add(&g_drv);

    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, sb_read("plc", 0x10000, &v));
    int32_t blk[130];
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, sb_read_block("plc", 0, blk, 126));

    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, sb_read_block("plc", 0xFFFF, blk, 2));
}

void test_txid_increments()
{
    Modbus.set_holding_reg_args.addr = 0;
    Modbus.set_holding_reg_args.value = 0x0001;
    Modbus.set_holding_reg(protocore_modbus_span());
    sbm_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1);
    sbm_driver(&g_drv, "plc", &g_ctx);
    sb_add(&g_drv);

    int32_t v = 0;
    TEST_ASSERT_EQUAL_UINT16(0, g_ctx.txid);
    sb_read("plc", 0, &v);
    TEST_ASSERT_EQUAL_UINT16(1, g_ctx.txid);
    sb_read("plc", 0, &v);
    TEST_ASSERT_EQUAL_UINT16(2, g_ctx.txid);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_read_single_holding);
    RUN_TEST(test_read_block_matrix);
    RUN_TEST(test_read_input_registers);
    RUN_TEST(test_modbus_exception_surfaces);
    RUN_TEST(test_transport_error_propagates);
    RUN_TEST(test_write_single_round_trip);
    RUN_TEST(test_write_block_round_trip);
    RUN_TEST(test_input_registers_read_only);
    RUN_TEST(test_write_bounds);
    RUN_TEST(test_init_rejects_bad_args);
    RUN_TEST(test_read_bounds);
    RUN_TEST(test_txid_increments);
    return UNITY_END();
}
