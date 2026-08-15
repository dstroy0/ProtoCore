// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "server/net/iface_bridge/iface_bridge.h"
#include "shared/ip/ip.h"
#include <string.h>

#include <unity.h>

void setUp()
{
    protocore_iface_bridge_clear();
}
void tearDown()
{
}

static BridgeTarget uart_target()
{
    BridgeTarget t;
    memset(&t, 0, sizeof(t));
    t.bus = BRIDGE_BUS_UART;
    t.mode = BRIDGE_MODE_STREAM;
    t.unit = 1;
    t.rate = 115200;
    return t;
}

static BridgeTarget i2c_target(uint16_t addr)
{
    BridgeTarget t;
    memset(&t, 0, sizeof(t));
    t.bus = BRIDGE_BUS_I2C;
    t.mode = BRIDGE_MODE_TRANSACTION;
    t.unit = 0;
    t.addr_cs = addr;
    t.rate = 400000;
    return t;
}

void test_map_and_find()
{
    BridgeTarget u = uart_target();
    TEST_ASSERT_TRUE(protocore_iface_bridge_map("192.168.1.50", 4001, BRIDGE_PROTO_TCP, &u));
    TEST_ASSERT_EQUAL_UINT8(1, protocore_iface_bridge_count());

    const BridgeRule *r = protocore_iface_bridge_find(4001, BRIDGE_PROTO_TCP);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(BRIDGE_BUS_UART, r->target.bus);
    TEST_ASSERT_EQUAL_UINT16(4001, r->listen_port);
    TEST_ASSERT_EQUAL(PROTOCORE_IP_V4, r->listen_ip.family);
    TEST_ASSERT_EQUAL_UINT8(192, r->listen_ip.bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(50, r->listen_ip.bytes[3]);

    TEST_ASSERT_NULL(protocore_iface_bridge_find(4001, BRIDGE_PROTO_UDP));
    TEST_ASSERT_NULL(protocore_iface_bridge_find(4002, BRIDGE_PROTO_TCP));
}

void test_any_interface_and_dedup()
{
    BridgeTarget i = i2c_target(0x40);
    TEST_ASSERT_TRUE(protocore_iface_bridge_map(NULL, 5000, BRIDGE_PROTO_TCP, &i));
    const BridgeRule *r = protocore_iface_bridge_find(5000, BRIDGE_PROTO_TCP);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(PROTOCORE_IP_NONE, r->listen_ip.family);
    TEST_ASSERT_EQUAL_UINT16(0x40, r->target.addr_cs);

    BridgeTarget u = uart_target();
    TEST_ASSERT_FALSE(protocore_iface_bridge_map("10.0.0.1", 5000, BRIDGE_PROTO_TCP, &u));

    TEST_ASSERT_TRUE(protocore_iface_bridge_map(NULL, 5000, BRIDGE_PROTO_UDP, &i));
    TEST_ASSERT_EQUAL_UINT8(2, protocore_iface_bridge_count());
}

void test_bad_address_rejected()
{
    BridgeTarget u = uart_target();
    TEST_ASSERT_FALSE(protocore_iface_bridge_map("not.an.ip", 6000, BRIDGE_PROTO_TCP, &u));
    TEST_ASSERT_EQUAL_UINT8(0, protocore_iface_bridge_count());
}

void test_table_full()
{
    BridgeTarget u = uart_target();
    for (uint16_t p = 0; p < PROTOCORE_BRIDGE_MAX_RULES; p++)
    {
        TEST_ASSERT_TRUE(protocore_iface_bridge_map(NULL, (uint16_t)(7000 + p), BRIDGE_PROTO_TCP, &u));
    }
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_BRIDGE_MAX_RULES, protocore_iface_bridge_count());
    TEST_ASSERT_FALSE(protocore_iface_bridge_map(NULL, 9999, BRIDGE_PROTO_TCP, &u));
    protocore_iface_bridge_clear();
    TEST_ASSERT_EQUAL_UINT8(0, protocore_iface_bridge_count());
}

void test_txn_roundtrip()
{
    const uint8_t wr[3] = {0xAA, 0xBB, 0xCC};
    uint8_t frame[16];
    size_t n = protocore_iface_bridge_txn_build(frame, sizeof(frame), wr, 3, 5);
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_BRIDGE_TXN_HDR + 3, n);
    const uint8_t expect[] = {0x00, 0x03, 0x00, 0x05, 0xAA, 0xBB, 0xCC};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, frame, n);

    uint16_t wl = 0;
    uint16_t rl = 0;
    const uint8_t *wd = NULL;
    size_t used = protocore_iface_bridge_txn_parse(frame, n, &wl, &rl, &wd);
    TEST_ASSERT_EQUAL_size_t(n, used);
    TEST_ASSERT_EQUAL_UINT16(3, wl);
    TEST_ASSERT_EQUAL_UINT16(5, rl);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(wr, wd, 3);
}

void test_txn_partial_and_readonly()
{

    const uint8_t hdr2[2] = {0x00, 0x03};
    TEST_ASSERT_EQUAL_size_t(0, protocore_iface_bridge_txn_parse(hdr2, sizeof(hdr2), NULL, NULL, NULL));

    const uint8_t partial[6] = {0x00, 0x03, 0x00, 0x00, 0xAA, 0xBB};
    TEST_ASSERT_EQUAL_size_t(0, protocore_iface_bridge_txn_parse(partial, sizeof(partial), NULL, NULL, NULL));

    const uint8_t readonly[4] = {0x00, 0x00, 0x00, 0x08};
    uint16_t wl = 9;
    uint16_t rl = 0;
    const uint8_t *wd = NULL;
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_BRIDGE_TXN_HDR,
                             protocore_iface_bridge_txn_parse(readonly, sizeof(readonly), &wl, &rl, &wd));
    TEST_ASSERT_EQUAL_UINT16(0, wl);
    TEST_ASSERT_EQUAL_UINT16(8, rl);
}

void test_build_overflow_fails_closed()
{
    const uint8_t wr[4] = {1, 2, 3, 4};
    uint8_t small[6];
    TEST_ASSERT_EQUAL_size_t(0, protocore_iface_bridge_txn_build(small, sizeof(small), wr, 4, 0));
    TEST_ASSERT_EQUAL_size_t(0, protocore_iface_bridge_txn_parse(NULL, 10, NULL, NULL, NULL));
}

void test_null_arg_guards()
{

    TEST_ASSERT_FALSE(protocore_iface_bridge_add(NULL));
    TEST_ASSERT_EQUAL_UINT8(0, protocore_iface_bridge_count());

    TEST_ASSERT_FALSE(protocore_iface_bridge_map("10.0.0.1", 7600, BRIDGE_PROTO_TCP, NULL));
    TEST_ASSERT_EQUAL_UINT8(0, protocore_iface_bridge_count());
}

void test_map_empty_ip_is_any_interface()
{

    BridgeTarget u = uart_target();
    TEST_ASSERT_TRUE(protocore_iface_bridge_map("", 5200, BRIDGE_PROTO_TCP, &u));
    const BridgeRule *r = protocore_iface_bridge_find(5200, BRIDGE_PROTO_TCP);
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(PROTOCORE_IP_NONE, r->listen_ip.family);
}

void test_txn_parse_null_outputs()
{

    const uint8_t frame[6] = {0x00, 0x02, 0x00, 0x01, 0xAA, 0xBB};
    size_t used = protocore_iface_bridge_txn_parse(frame, sizeof(frame), NULL, NULL, NULL);
    TEST_ASSERT_EQUAL_size_t(sizeof(frame), used);
}

void test_txn_build_edge_cases()
{
    uint8_t out[16];

    TEST_ASSERT_EQUAL_size_t(0, protocore_iface_bridge_txn_build(NULL, sizeof(out), NULL, 0, 0));

    size_t n0 = protocore_iface_bridge_txn_build(out, sizeof(out), NULL, 0, 8);
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_BRIDGE_TXN_HDR, n0);
    const uint8_t expect0[] = {0x00, 0x00, 0x00, 0x08};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect0, out, n0);

    size_t n1 = protocore_iface_bridge_txn_build(out, sizeof(out), NULL, 5, 0);
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_BRIDGE_TXN_HDR + 5, n1);
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
