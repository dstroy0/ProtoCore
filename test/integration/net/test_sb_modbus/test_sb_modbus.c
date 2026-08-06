// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the Modbus-master southbound driver adapter
// (services/net/southbound/sb_modbus): register the driver, then read register points
// by name through the southbound facade, driven end to end against the real slave
// codec (pc_modbus_process_adu) via a mock transaction seam - no hardware needed.

#include "services/fieldbus/modbus/modbus.h"
#include "services/net/southbound/sb_modbus.h"
#include "services/net/southbound/southbound.h"
#include <unity.h>

// A transaction seam that routes the master's request straight into the slave codec, so a read through
// the southbound facade is a full master<->slave round trip against the shared (global) slave model.
static int loopback_txn(void *io, const uint8_t *req, size_t req_len, uint8_t *resp, size_t resp_cap)
{
    (void)io;
    size_t pn = pc_modbus_process_adu(req, req_len, resp, resp_cap);
    return (pn == 0) ? -1 : (int)pn;
}

// A transaction seam that always fails at the transport layer (a distinct negative than any Modbus code).
static int fail_txn(void *io, const uint8_t *req, size_t req_len, uint8_t *resp, size_t resp_cap)
{
    (void)io;
    (void)req;
    (void)req_len;
    (void)resp;
    (void)resp_cap;
    return -42;
}

static pc_sb_modbus_ctx g_ctx;
static SouthboundDriver g_drv;

void setUp()
{
    pc_modbus_server_init();
    pc_southbound_clear();
}
void tearDown()
{
}

// Register a holding-register driver over the loopback seam and read one point through the facade.
void test_read_single_holding()
{
    pc_modbus_set_holding_reg(10, 0xBEEF);
    TEST_ASSERT_EQUAL_INT(SB_OK, pc_sb_modbus_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1));
    TEST_ASSERT_EQUAL_INT(SB_OK, pc_sb_modbus_driver(&g_drv, "plc", &g_ctx));
    TEST_ASSERT_EQUAL_INT(SB_OK, pc_southbound_register(&g_drv));

    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(SB_OK, pc_southbound_read("plc", 10, &v));
    TEST_ASSERT_EQUAL_INT32(0xBEEF, v);
}

// The atomic register matrix: a block read of a contiguous span in one request.
void test_read_block_matrix()
{
    for (uint16_t i = 0; i < 4; i++)
    {
        pc_modbus_set_holding_reg((uint16_t)(20 + i), (uint16_t)(0x1000 + i));
    }
    pc_sb_modbus_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1);
    pc_sb_modbus_driver(&g_drv, "plc", &g_ctx);
    pc_southbound_register(&g_drv);

    int32_t vals[4] = {0, 0, 0, 0};
    TEST_ASSERT_EQUAL_INT(4, pc_southbound_read_block("plc", 20, vals, 4));
    TEST_ASSERT_EQUAL_INT32(0x1000, vals[0]);
    TEST_ASSERT_EQUAL_INT32(0x1001, vals[1]);
    TEST_ASSERT_EQUAL_INT32(0x1002, vals[2]);
    TEST_ASSERT_EQUAL_INT32(0x1003, vals[3]);
}

// Input registers (FC 0x04) are a valid function code too.
void test_read_input_registers()
{
    pc_modbus_set_input_reg(5, 0x0777);
    pc_sb_modbus_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_INPUT_REGS, 1);
    pc_sb_modbus_driver(&g_drv, "sensor", &g_ctx);
    pc_southbound_register(&g_drv);

    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(SB_OK, pc_southbound_read("sensor", 5, &v));
    TEST_ASSERT_EQUAL_INT32(0x0777, v);
}

// A Modbus exception reply (out-of-range address) surfaces as PC_SB_MODBUS_EXCEPTION with the raw
// exception code captured in the context - distinct from a transport error.
void test_modbus_exception_surfaces()
{
    pc_sb_modbus_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1);
    pc_sb_modbus_driver(&g_drv, "plc", &g_ctx);
    pc_southbound_register(&g_drv);

    int32_t v = 123;
    TEST_ASSERT_EQUAL_INT(PC_SB_MODBUS_EXCEPTION, pc_southbound_read("plc", 60000, &v));
    TEST_ASSERT_EQUAL_UINT8(MODBUS_EX_ILLEGAL_DATA_ADDRESS, g_ctx.last_exception);
    TEST_ASSERT_EQUAL_INT32(123, v); // value_out untouched on failure
}

// A transport-layer failure is propagated unchanged (not confused with a Modbus exception).
void test_transport_error_propagates()
{
    pc_sb_modbus_init(&g_ctx, fail_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1);
    pc_sb_modbus_driver(&g_drv, "plc", &g_ctx);
    pc_southbound_register(&g_drv);

    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(-42, pc_southbound_read("plc", 0, &v));
    int32_t blk[2] = {0, 0};
    TEST_ASSERT_EQUAL_INT(-42, pc_southbound_read_block("plc", 0, blk, 2));
}

// A holding-register driver writes a single point (FC 0x06) through the facade; the slave stores it and
// a read-back returns the written value.
void test_write_single_round_trip()
{
    pc_sb_modbus_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1);
    pc_sb_modbus_driver(&g_drv, "plc", &g_ctx);
    pc_southbound_register(&g_drv);

    TEST_ASSERT_EQUAL_INT(SB_OK, pc_southbound_write("plc", 8, 0x4242));
    TEST_ASSERT_EQUAL_HEX16(0x4242, pc_modbus_get_holding_reg(8)); // stored on the slave
    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(SB_OK, pc_southbound_read("plc", 8, &v)); // and reads back through the facade
    TEST_ASSERT_EQUAL_INT32(0x4242, v);
}

// The atomic register matrix write: a contiguous span written in one request (FC 0x10).
void test_write_block_round_trip()
{
    pc_sb_modbus_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1);
    pc_sb_modbus_driver(&g_drv, "plc", &g_ctx);
    pc_southbound_register(&g_drv);

    int32_t in[3] = {0x0A0A, 0x0B0B, 0x0C0C};
    TEST_ASSERT_EQUAL_INT(3, pc_southbound_write_block("plc", 40, in, 3)); // count written
    int32_t back[3] = {0, 0, 0};
    TEST_ASSERT_EQUAL_INT(3, pc_southbound_read_block("plc", 40, back, 3));
    TEST_ASSERT_EQUAL_INT32(0x0A0A, back[0]);
    TEST_ASSERT_EQUAL_INT32(0x0B0B, back[1]);
    TEST_ASSERT_EQUAL_INT32(0x0C0C, back[2]);
}

// An input-register driver is read-only (a Modbus input register cannot be written): write / write_block
// are unbound and the framework reports SB_ERR_UNSUPPORTED.
void test_input_registers_read_only()
{
    pc_sb_modbus_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_INPUT_REGS, 1);
    pc_sb_modbus_driver(&g_drv, "sensor", &g_ctx);
    pc_southbound_register(&g_drv);

    TEST_ASSERT_NULL(g_drv.write);
    TEST_ASSERT_NULL(g_drv.write_block);
    TEST_ASSERT_EQUAL_INT(SB_ERR_UNSUPPORTED, pc_southbound_write("sensor", 0, 1));
    int32_t in[2] = {1, 2};
    TEST_ASSERT_EQUAL_INT(SB_ERR_UNSUPPORTED, pc_southbound_write_block("sensor", 0, in, 2));
}

// Write bounds: a value outside a 16-bit register, and a block wider than one FC 0x10 request (123).
void test_write_bounds()
{
    pc_sb_modbus_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1);
    pc_sb_modbus_driver(&g_drv, "plc", &g_ctx);
    pc_southbound_register(&g_drv);

    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, pc_southbound_write("plc", 0, 0x10000)); // > 16-bit value
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, pc_southbound_write("plc", 0, -1));      // negative value
    int32_t big[124];
    for (int i = 0; i < 124; i++)
    {
        big[i] = 1;
    }
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, pc_southbound_write_block("plc", 0, big, 124)); // > 123 regs/request
    int32_t bad[2] = {0, 0x10000};
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, pc_southbound_write_block("plc", 0, bad, 2)); // out-of-range value
}

// init rejects a null seam and a function code that is not a read (only 0x03 / 0x04 are valid).
void test_init_rejects_bad_args()
{
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, pc_sb_modbus_init(&g_ctx, NULL, NULL, MODBUS_FC_READ_HOLDING_REGS, 1));
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, pc_sb_modbus_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_WRITE_SINGLE_REG, 1));
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, pc_sb_modbus_init(NULL, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1));
}

// Bounds: a register address past 16 bits, a zero-length block, and a block wider than one Modbus
// request (125) are all rejected before any transport call.
void test_read_bounds()
{
    pc_sb_modbus_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1);
    pc_sb_modbus_driver(&g_drv, "plc", &g_ctx);
    pc_southbound_register(&g_drv);

    int32_t v = 0;
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, pc_southbound_read("plc", 0x10000, &v)); // > 0xFFFF
    int32_t blk[130];
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, pc_southbound_read_block("plc", 0, blk, 126)); // > 125 regs/request
    // A span that would run past the 16-bit address space.
    TEST_ASSERT_EQUAL_INT(SB_ERR_ARG, pc_southbound_read_block("plc", 0xFFFF, blk, 2));
}

// The transaction id rolls forward one per request (the caller's slave-correlation token).
void test_txid_increments()
{
    pc_modbus_set_holding_reg(0, 0x0001);
    pc_sb_modbus_init(&g_ctx, loopback_txn, NULL, MODBUS_FC_READ_HOLDING_REGS, 1);
    pc_sb_modbus_driver(&g_drv, "plc", &g_ctx);
    pc_southbound_register(&g_drv);

    int32_t v = 0;
    TEST_ASSERT_EQUAL_UINT16(0, g_ctx.txid);
    pc_southbound_read("plc", 0, &v);
    TEST_ASSERT_EQUAL_UINT16(1, g_ctx.txid);
    pc_southbound_read("plc", 0, &v);
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
