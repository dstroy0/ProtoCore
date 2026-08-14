// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "services/fieldbus/modbus/modbus.h"
#include "services/southbound/sb_modbus.h"
#include "services/southbound/southbound.h"
#include <unity.h>

static int loopback_txn(void *io, const uint8_t *req, size_t req_len, uint8_t *resp, size_t resp_cap)
{
    (void)io;
    size_t pn = protocore_modbus_process_adu(req, req_len, resp, resp_cap);
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

void setUp()
{
    protocore_modbus_server_init();
    protocore_southbound_clear();
}
void tearDown()
{
}

void test_read_single_holding()
{
    protocore_modbus_set_holding_reg(10, 0xBEEF);
    TEST_ASSERT_EQUAL_INT(SB_OK, protocore_sb_modbus_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1));
    TEST_ASSERT_EQUAL_INT(SB_OK, protocore_sb_modbus_driver(&g_drv, "plc", &g_ctx));
    TEST_ASSERT_EQUAL_INT(SB_OK, protocore_southbound_register(&g_drv));

    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(SB_OK, protocore_southbound_read("plc", 10, &v));
    TEST_ASSERT_EQUAL_INT32(0xBEEF, v);
}

void test_read_block_matrix()
{
    for (uint16_t i = 0; i < 4; i++)
    {
        protocore_modbus_set_holding_reg((uint16_t)(20 + i), (uint16_t)(0x1000 + i));
    }
    protocore_sb_modbus_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1);
    protocore_sb_modbus_driver(&g_drv, "plc", &g_ctx);
    protocore_southbound_register(&g_drv);

    int32_t vals[4] = {0, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(4, protocore_southbound_read_block("plc", 20, vals, 4));
    TEST_ASSERT_EQUAL_INT32(0x1000, vals[0]);
    TEST_ASSERT_EQUAL_INT32(0x1001, vals[1]);
    TEST_ASSERT_EQUAL_INT32(0x1002, vals[2]);
    TEST_ASSERT_EQUAL_INT32(0x1003, vals[3]);
}

void test_read_input_registers()
{
    protocore_modbus_set_input_reg(5, 0x0777);
    protocore_sb_modbus_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_INPUT_REGS, 1);
    protocore_sb_modbus_driver(&g_drv, "sensor", &g_ctx);
    protocore_southbound_register(&g_drv);

    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(SB_OK, protocore_southbound_read("sensor", 5, &v));
    TEST_ASSERT_EQUAL_INT32(0x0777, v);
}

void test_modbus_exception_surfaces()
{
    protocore_sb_modbus_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1);
    protocore_sb_modbus_driver(&g_drv, "plc", &g_ctx);
    protocore_southbound_register(&g_drv);

    int32_t v = 123;
    TEST_ASSERT_EQUAL_INT(PROTOCORE_SB_MODBUS_EXCEPTION, protocore_southbound_read("plc", 60000, &v));
    TEST_ASSERT_EQUAL_UINT8(MODBUS_EX_ILLEGAL_DATA_ADDRESS, g_ctx.last_exception);
    TEST_ASSERT_EQUAL_INT32(123, v);
}

void test_transport_error_propagates()
{
    protocore_sb_modbus_init(&g_ctx, fail_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1);
    protocore_sb_modbus_driver(&g_drv, "plc", &g_ctx);
    protocore_southbound_register(&g_drv);

    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(-42, protocore_southbound_read("plc", 0, &v));
    int32_t blk[2] = {0, 0};
    TEST_ASSERT_EQUAL_INT(-42, protocore_southbound_read_block("plc", 0, blk, 2));
}

void test_write_single_round_trip()
{
    protocore_sb_modbus_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1);
    protocore_sb_modbus_driver(&g_drv, "plc", &g_ctx);
    protocore_southbound_register(&g_drv);

    TEST_ASSERT_EQUAL_INT(SB_OK, protocore_southbound_write("plc", 8, 0x4242));
    TEST_ASSERT_EQUAL_HEX16(0x4242, protocore_modbus_get_holding_reg(8));
    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(SB_OK, protocore_southbound_read("plc", 8, &v));
    TEST_ASSERT_EQUAL_INT32(0x4242, v);
}

void test_write_block_round_trip()
{
    protocore_sb_modbus_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1);
    protocore_sb_modbus_driver(&g_drv, "plc", &g_ctx);
    protocore_southbound_register(&g_drv);

    int32_t in[3] = {0x0A0A, 0x0B0B, 0x0C0C};
    TEST_ASSERT_EQUAL_INT(3, protocore_southbound_write_block("plc", 40, in, 3));
    int32_t back[3] = {0, 0, 0};
    TEST_ASSERT_EQUAL_INT(3, protocore_southbound_read_block("plc", 40, back, 3));
    TEST_ASSERT_EQUAL_INT32(0x0A0A, back[0]);
    TEST_ASSERT_EQUAL_INT32(0x0B0B, back[1]);
    TEST_ASSERT_EQUAL_INT32(0x0C0C, back[2]);
}

void test_input_registers_read_only()
{
    protocore_sb_modbus_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_INPUT_REGS, 1);
    protocore_sb_modbus_driver(&g_drv, "sensor", &g_ctx);
    protocore_southbound_register(&g_drv);

    TEST_ASSERT_NULL(g_drv.write);
    TEST_ASSERT_NULL(g_drv.write_block);
    TEST_ASSERT_EQUAL_INT(SB_ERR_UNSUPPORTED, protocore_southbound_write("sensor", 0, 1));
    int32_t in[2] = {1, 2};
    TEST_ASSERT_EQUAL_INT(SB_ERR_UNSUPPORTED, protocore_southbound_write_block("sensor", 0, in, 2));
}

void test_write_bounds()
{
    protocore_sb_modbus_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1);
    protocore_sb_modbus_driver(&g_drv, "plc", &g_ctx);
    protocore_southbound_register(&g_drv);

    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, protocore_southbound_write("plc", 0, 0x10000));
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, protocore_southbound_write("plc", 0, -1));
    int32_t big[124];
    for (int i = 0; i < 124; i++)
    {
        big[i] = 1;
    }
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, protocore_southbound_write_block("plc", 0, big, 124));
    int32_t bad[2] = {0, 0x10000};
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, protocore_southbound_write_block("plc", 0, bad, 2));
}

void test_init_rejects_bad_args()
{
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, protocore_sb_modbus_init(&g_ctx, NULL, NULL, MODBUS_FC_READ_HOLDING_REGS, 1));
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, protocore_sb_modbus_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_WRITE_SINGLE_REG, 1));
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, protocore_sb_modbus_init(NULL, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1));
}

void test_read_bounds()
{
    protocore_sb_modbus_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1);
    protocore_sb_modbus_driver(&g_drv, "plc", &g_ctx);
    protocore_southbound_register(&g_drv);

    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, protocore_southbound_read("plc", 0x10000, &v));
    int32_t blk[130];
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, protocore_southbound_read_block("plc", 0, blk, 126));

    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, protocore_southbound_read_block("plc", 0xFFFF, blk, 2));
}

void test_txid_increments()
{
    protocore_modbus_set_holding_reg(0, 0x0001);
    protocore_sb_modbus_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1);
    protocore_sb_modbus_driver(&g_drv, "plc", &g_ctx);
    protocore_southbound_register(&g_drv);

    int32_t v = 0;
    TEST_ASSERT_EQUAL_UINT16(0, g_ctx.txid);
    protocore_southbound_read("plc", 0, &v);
    TEST_ASSERT_EQUAL_UINT16(1, g_ctx.txid);
    protocore_southbound_read("plc", 0, &v);
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
