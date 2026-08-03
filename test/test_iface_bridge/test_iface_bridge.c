// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the interface-bridge pure core (services/net/iface_bridge): the address:port -> bus rule
// table (register / find / dedup / capacity, with the full pc_ip bind address preserved) and the
// write-then-read transaction frame codec (big-endian header, partial-frame handling, round-trip). The
// bus I/O and listener are ESP32-only; these tests cover the portable logic.

#include "services/net/iface_bridge/iface_bridge.h"
#include <string.h>
#include <unity.h>

void setUp()
{
    pc_iface_bridge_clear();
}
void tearDown()
{
}

static BridgeTarget uart_target()
{
    BridgeTarget t;
    memset(&t, 0, sizeof(t));
    t.bus = uart;
    t.mode = stream;
    t.unit = 1;
    t.rate = 115200;
    return t;
}

static BridgeTarget i2c_target(uint16_t addr)
{
    BridgeTarget t;
    memset(&t, 0, sizeof(t));
    t.bus = i2c;
    t.mode = transaction;
    t.unit = 0;
    t.addr_cs = addr;
    t.rate = 400000;
    return t;
}

void test_map_and_find()
{
    BridgeTarget u = uart_target();
    TEST_ASSERT_TRUE(pc_iface_bridge_map("192.168.1.50", 4001, tcp, &u));
    TEST_ASSERT_EQUAL_UINT8(1, pc_iface_bridge_count());

    const BridgeRule *r = pc_iface_bridge_find(4001, tcp);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(uart, r->target.bus);
    TEST_ASSERT_EQUAL_UINT16(4001, r->listen_port);
    TEST_ASSERT_EQUAL(PC_IP_V4, r->listen_ip.family); // full bind address preserved
    TEST_ASSERT_EQUAL_UINT8(192, r->listen_ip.bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(50, r->listen_ip.bytes[3]);

    TEST_ASSERT_NULL(pc_iface_bridge_find(4001, udp)); // proto must match
    TEST_ASSERT_NULL(pc_iface_bridge_find(4002, tcp)); // port must match
}

void test_any_interface_and_dedup()
{
    BridgeTarget i = i2c_target(0x40);
    TEST_ASSERT_TRUE(pc_iface_bridge_map(NULL, 5000, tcp, &i)); // any interface
    const BridgeRule *r = pc_iface_bridge_find(5000, tcp);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(PC_IP_NONE, r->listen_ip.family);
    TEST_ASSERT_EQUAL_UINT16(0x40, r->target.addr_cs);

    // Same port+proto is a duplicate -> rejected.
    BridgeTarget u = uart_target();
    TEST_ASSERT_FALSE(pc_iface_bridge_map("10.0.0.1", 5000, tcp, &u));
    // Same port on the other transport is fine.
    TEST_ASSERT_TRUE(pc_iface_bridge_map(NULL, 5000, udp, &i));
    TEST_ASSERT_EQUAL_UINT8(2, pc_iface_bridge_count());
}

void test_bad_address_rejected()
{
    BridgeTarget u = uart_target();
    TEST_ASSERT_FALSE(pc_iface_bridge_map("not.an.ip", 6000, tcp, &u));
    TEST_ASSERT_EQUAL_UINT8(0, pc_iface_bridge_count());
}

void test_table_full()
{
    BridgeTarget u = uart_target();
    for (uint16_t p = 0; p < PC_BRIDGE_MAX_RULES; p++)
    {
        TEST_ASSERT_TRUE(pc_iface_bridge_map(NULL, (uint16_t)(7000 + p), tcp, &u));
    }
    TEST_ASSERT_EQUAL_UINT8(PC_BRIDGE_MAX_RULES, pc_iface_bridge_count());
    TEST_ASSERT_FALSE(pc_iface_bridge_map(NULL, 9999, tcp, &u)); // full
    pc_iface_bridge_clear();
    TEST_ASSERT_EQUAL_UINT8(0, pc_iface_bridge_count());
}

void test_txn_roundtrip()
{
    const uint8_t wr[3] = {0xAA, 0xBB, 0xCC};
    uint8_t frame[16];
    size_t n = pc_iface_bridge_txn_build(frame, sizeof(frame), wr, 3, 5);
    TEST_ASSERT_EQUAL_size_t(PC_BRIDGE_TXN_HDR + 3, n);
    const uint8_t expect[] = {0x00, 0x03, 0x00, 0x05, 0xAA, 0xBB, 0xCC}; // write_len=3, read_len=5
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, frame, n);

    uint16_t wl = 0;
    uint16_t rl = 0;
    const uint8_t *wd = NULL;
    size_t used = pc_iface_bridge_txn_parse(frame, n, &wl, &rl, &wd);
    TEST_ASSERT_EQUAL_size_t(n, used);
    TEST_ASSERT_EQUAL_UINT16(3, wl);
    TEST_ASSERT_EQUAL_UINT16(5, rl);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(wr, wd, 3);
}

void test_txn_partial_and_readonly()
{
    // Partial header (< 4 bytes) -> need more.
    const uint8_t hdr2[2] = {0x00, 0x03};
    TEST_ASSERT_EQUAL_size_t(0, pc_iface_bridge_txn_parse(hdr2, sizeof(hdr2), NULL, NULL, NULL));

    // Header claims write_len 3 but only 2 payload bytes present -> need more.
    const uint8_t partial[6] = {0x00, 0x03, 0x00, 0x00, 0xAA, 0xBB};
    TEST_ASSERT_EQUAL_size_t(0, pc_iface_bridge_txn_parse(partial, sizeof(partial), NULL, NULL, NULL));

    // Read-only transaction (write_len 0) completes at just the header.
    const uint8_t readonly[4] = {0x00, 0x00, 0x00, 0x08};
    uint16_t wl = 9;
    uint16_t rl = 0;
    const uint8_t *wd = NULL;
    TEST_ASSERT_EQUAL_size_t(PC_BRIDGE_TXN_HDR, pc_iface_bridge_txn_parse(readonly, sizeof(readonly), &wl, &rl, &wd));
    TEST_ASSERT_EQUAL_UINT16(0, wl);
    TEST_ASSERT_EQUAL_UINT16(8, rl);
}

void test_build_overflow_fails_closed()
{
    const uint8_t wr[4] = {1, 2, 3, 4};
    uint8_t small[6]; // needs 4 + 4 = 8
    TEST_ASSERT_EQUAL_size_t(0, pc_iface_bridge_txn_build(small, sizeof(small), wr, 4, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_iface_bridge_txn_parse(NULL, 10, NULL, NULL, NULL));
}

void test_null_arg_guards()
{
    // add() with a NULL rule pointer fails closed and touches nothing.
    TEST_ASSERT_FALSE(pc_iface_bridge_add(NULL));
    TEST_ASSERT_EQUAL_UINT8(0, pc_iface_bridge_count());

    // map() with a NULL target fails closed and touches nothing.
    TEST_ASSERT_FALSE(pc_iface_bridge_map("10.0.0.1", 7600, tcp, NULL));
    TEST_ASSERT_EQUAL_UINT8(0, pc_iface_bridge_count());
}

void test_map_empty_ip_is_any_interface()
{
    // A non-NULL but empty ip string is treated the same as NULL: "any interface".
    BridgeTarget u = uart_target();
    TEST_ASSERT_TRUE(pc_iface_bridge_map("", 5200, tcp, &u));
    const BridgeRule *r = pc_iface_bridge_find(5200, tcp);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(PC_IP_NONE, r->listen_ip.family);
}

void test_txn_parse_null_outputs()
{
    // A complete frame parsed with every output pointer NULL: the caller can probe "is this frame
    // complete?" (non-zero return) without wanting the decoded fields.
    const uint8_t frame[6] = {0x00, 0x02, 0x00, 0x01, 0xAA, 0xBB}; // write_len=2, read_len=1
    size_t used = pc_iface_bridge_txn_parse(frame, sizeof(frame), NULL, NULL, NULL);
    TEST_ASSERT_EQUAL_size_t(sizeof(frame), used);
}

void test_txn_build_edge_cases()
{
    uint8_t out[16];

    // out == NULL fails closed regardless of cap.
    TEST_ASSERT_EQUAL_size_t(0, pc_iface_bridge_txn_build(NULL, sizeof(out), NULL, 0, 0));

    // write_len == 0: header-only frame; the payload copy is skipped even though write_data is NULL.
    size_t n0 = pc_iface_bridge_txn_build(out, sizeof(out), NULL, 0, 8);
    TEST_ASSERT_EQUAL_size_t(PC_BRIDGE_TXN_HDR, n0);
    const uint8_t expect0[] = {0x00, 0x00, 0x00, 0x08};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect0, out, n0);

    // write_len > 0 but write_data == NULL: header is still built, payload copy is skipped (fails
    // safe rather than dereferencing a NULL source).
    size_t n1 = pc_iface_bridge_txn_build(out, sizeof(out), NULL, 5, 0);
    TEST_ASSERT_EQUAL_size_t(PC_BRIDGE_TXN_HDR + 5, n1);
    TEST_ASSERT_EQUAL_UINT8(0x00, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0x05, out[1]);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_map_and_find);
    RUN_TEST(test_any_interface_and_dedup);
    RUN_TEST(test_bad_address_rejected);
    RUN_TEST(test_table_full);
    RUN_TEST(test_txn_roundtrip);
    RUN_TEST(test_txn_partial_and_readonly);
    RUN_TEST(test_build_overflow_fails_closed);
    RUN_TEST(test_null_arg_guards);
    RUN_TEST(test_map_empty_ip_is_any_interface);
    RUN_TEST(test_txn_parse_null_outputs);
    RUN_TEST(test_txn_build_edge_cases);
    return UNITY_END();
}
