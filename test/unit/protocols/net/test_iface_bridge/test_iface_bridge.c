// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "server/net/iface_bridge/iface_bridge.h"
#include "shared/ip/ip.h"
#include <string.h>

#include <unity.h>

void setUp()
{
    IfaceBridge.clear(protocore_iface_bridge_span());
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
    IfaceBridge.map_args.ip = "192.168.1.50";
    IfaceBridge.map_args.port = 4001;
    IfaceBridge.map_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridge.map_args.target = &u;
    IfaceBridge.map(protocore_iface_bridge_span());
    TEST_ASSERT_TRUE(IfaceBridge.ok);
    IfaceBridge.count(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_UINT8(1, IfaceBridge.u8);

    IfaceBridge.find_args.port = 4001;
    IfaceBridge.find_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridge.find(protocore_iface_bridge_span());
    const BridgeRule *r = IfaceBridge.rule;
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(BRIDGE_BUS_UART, r->target.bus);
    TEST_ASSERT_EQUAL_UINT16(4001, r->listen_port);
    TEST_ASSERT_EQUAL(PROTOCORE_IP_V4, r->listen_ip.family);
    TEST_ASSERT_EQUAL_UINT8(192, r->listen_ip.bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(50, r->listen_ip.bytes[3]);

    IfaceBridge.find_args.port = 4001;
    IfaceBridge.find_args.proto = BRIDGE_PROTO_UDP;
    IfaceBridge.find(protocore_iface_bridge_span());
    TEST_ASSERT_NULL(IfaceBridge.rule);
    IfaceBridge.find_args.port = 4002;
    IfaceBridge.find_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridge.find(protocore_iface_bridge_span());
    TEST_ASSERT_NULL(IfaceBridge.rule);
}

void test_any_interface_and_dedup()
{
    BridgeTarget i = i2c_target(0x40);
    IfaceBridge.map_args.ip = NULL;
    IfaceBridge.map_args.port = 5000;
    IfaceBridge.map_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridge.map_args.target = &i;
    IfaceBridge.map(protocore_iface_bridge_span());
    TEST_ASSERT_TRUE(IfaceBridge.ok);
    IfaceBridge.find_args.port = 5000;
    IfaceBridge.find_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridge.find(protocore_iface_bridge_span());
    const BridgeRule *r = IfaceBridge.rule;
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(PROTOCORE_IP_NONE, r->listen_ip.family);
    TEST_ASSERT_EQUAL_UINT16(0x40, r->target.addr_cs);

    BridgeTarget u = uart_target();
    IfaceBridge.map_args.ip = "10.0.0.1";
    IfaceBridge.map_args.port = 5000;
    IfaceBridge.map_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridge.map_args.target = &u;
    IfaceBridge.map(protocore_iface_bridge_span());
    TEST_ASSERT_FALSE(IfaceBridge.ok);

    IfaceBridge.map_args.ip = NULL;
    IfaceBridge.map_args.port = 5000;
    IfaceBridge.map_args.proto = BRIDGE_PROTO_UDP;
    IfaceBridge.map_args.target = &i;
    IfaceBridge.map(protocore_iface_bridge_span());
    TEST_ASSERT_TRUE(IfaceBridge.ok);
    IfaceBridge.count(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_UINT8(2, IfaceBridge.u8);
}

void test_bad_address_rejected()
{
    BridgeTarget u = uart_target();
    IfaceBridge.map_args.ip = "not.an.ip";
    IfaceBridge.map_args.port = 6000;
    IfaceBridge.map_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridge.map_args.target = &u;
    IfaceBridge.map(protocore_iface_bridge_span());
    TEST_ASSERT_FALSE(IfaceBridge.ok);
    IfaceBridge.count(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_UINT8(0, IfaceBridge.u8);
}

void test_table_full()
{
    BridgeTarget u = uart_target();
    for (uint16_t p = 0; p < PROTOCORE_BRIDGE_MAX_RULES; p++)
    {
        IfaceBridge.map_args.ip = NULL;
        IfaceBridge.map_args.port = (uint16_t)(7000 + p);
        IfaceBridge.map_args.proto = BRIDGE_PROTO_TCP;
        IfaceBridge.map_args.target = &u;
        IfaceBridge.map(protocore_iface_bridge_span());
        TEST_ASSERT_TRUE(IfaceBridge.ok);
    }
    IfaceBridge.count(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_BRIDGE_MAX_RULES, IfaceBridge.u8);
    IfaceBridge.map_args.ip = NULL;
    IfaceBridge.map_args.port = 9999;
    IfaceBridge.map_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridge.map_args.target = &u;
    IfaceBridge.map(protocore_iface_bridge_span());
    TEST_ASSERT_FALSE(IfaceBridge.ok);
    IfaceBridge.clear(protocore_iface_bridge_span());
    IfaceBridge.count(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_UINT8(0, IfaceBridge.u8);
}

void test_txn_roundtrip()
{
    const uint8_t wr[3] = {0xAA, 0xBB, 0xCC};
    uint8_t frame[16];
    IfaceBridge.txn_build_args.out = frame;
    IfaceBridge.txn_build_args.cap = sizeof(frame);
    IfaceBridge.txn_build_args.write_data = wr;
    IfaceBridge.txn_build_args.write_len = 3;
    IfaceBridge.txn_build_args.read_len = 5;
    IfaceBridge.txn_build(protocore_iface_bridge_span());
    size_t n = IfaceBridge.n;
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_BRIDGE_TXN_HDR + 3, n);
    const uint8_t expect[] = {0x00, 0x03, 0x00, 0x05, 0xAA, 0xBB, 0xCC};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, frame, n);

    uint16_t wl = 0;
    uint16_t rl = 0;
    const uint8_t *wd = NULL;
    IfaceBridge.txn_parse_args.buf = frame;
    IfaceBridge.txn_parse_args.len = n;
    IfaceBridge.txn_parse_args.write_len = &wl;
    IfaceBridge.txn_parse_args.read_len = &rl;
    IfaceBridge.txn_parse_args.write_data = &wd;
    IfaceBridge.txn_parse(protocore_iface_bridge_span());
    size_t used = IfaceBridge.n;
    TEST_ASSERT_EQUAL_size_t(n, used);
    TEST_ASSERT_EQUAL_UINT16(3, wl);
    TEST_ASSERT_EQUAL_UINT16(5, rl);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(wr, wd, 3);
}

void test_txn_partial_and_readonly()
{

    const uint8_t hdr2[2] = {0x00, 0x03};
    IfaceBridge.txn_parse_args.buf = hdr2;
    IfaceBridge.txn_parse_args.len = sizeof(hdr2);
    IfaceBridge.txn_parse_args.write_len = NULL;
    IfaceBridge.txn_parse_args.read_len = NULL;
    IfaceBridge.txn_parse_args.write_data = NULL;
    IfaceBridge.txn_parse(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_size_t(0, IfaceBridge.n);

    const uint8_t partial[6] = {0x00, 0x03, 0x00, 0x00, 0xAA, 0xBB};
    IfaceBridge.txn_parse_args.buf = partial;
    IfaceBridge.txn_parse_args.len = sizeof(partial);
    IfaceBridge.txn_parse_args.write_len = NULL;
    IfaceBridge.txn_parse_args.read_len = NULL;
    IfaceBridge.txn_parse_args.write_data = NULL;
    IfaceBridge.txn_parse(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_size_t(0, IfaceBridge.n);

    const uint8_t readonly[4] = {0x00, 0x00, 0x00, 0x08};
    uint16_t wl = 9;
    uint16_t rl = 0;
    const uint8_t *wd = NULL;
    IfaceBridge.txn_parse_args.buf = readonly;
    IfaceBridge.txn_parse_args.len = sizeof(readonly);
    IfaceBridge.txn_parse_args.write_len = &wl;
    IfaceBridge.txn_parse_args.read_len = &rl;
    IfaceBridge.txn_parse_args.write_data = &wd;
    IfaceBridge.txn_parse(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_BRIDGE_TXN_HDR, IfaceBridge.n);
    TEST_ASSERT_EQUAL_UINT16(0, wl);
    TEST_ASSERT_EQUAL_UINT16(8, rl);
}

void test_build_overflow_fails_closed()
{
    const uint8_t wr[4] = {1, 2, 3, 4};
    uint8_t small[6];
    IfaceBridge.txn_build_args.out = small;
    IfaceBridge.txn_build_args.cap = sizeof(small);
    IfaceBridge.txn_build_args.write_data = wr;
    IfaceBridge.txn_build_args.write_len = 4;
    IfaceBridge.txn_build_args.read_len = 0;
    IfaceBridge.txn_build(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_size_t(0, IfaceBridge.n);
    IfaceBridge.txn_parse_args.buf = NULL;
    IfaceBridge.txn_parse_args.len = 10;
    IfaceBridge.txn_parse_args.write_len = NULL;
    IfaceBridge.txn_parse_args.read_len = NULL;
    IfaceBridge.txn_parse_args.write_data = NULL;
    IfaceBridge.txn_parse(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_size_t(0, IfaceBridge.n);
}

void test_null_arg_guards()
{

    IfaceBridge.add_args.rule = NULL;
    IfaceBridge.add(protocore_iface_bridge_span());
    TEST_ASSERT_FALSE(IfaceBridge.ok);
    IfaceBridge.count(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_UINT8(0, IfaceBridge.u8);

    IfaceBridge.map_args.ip = "10.0.0.1";
    IfaceBridge.map_args.port = 7600;
    IfaceBridge.map_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridge.map_args.target = NULL;
    IfaceBridge.map(protocore_iface_bridge_span());
    TEST_ASSERT_FALSE(IfaceBridge.ok);
    IfaceBridge.count(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_UINT8(0, IfaceBridge.u8);
}

void test_map_empty_ip_is_any_interface()
{

    BridgeTarget u = uart_target();
    IfaceBridge.map_args.ip = "";
    IfaceBridge.map_args.port = 5200;
    IfaceBridge.map_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridge.map_args.target = &u;
    IfaceBridge.map(protocore_iface_bridge_span());
    TEST_ASSERT_TRUE(IfaceBridge.ok);
    IfaceBridge.find_args.port = 5200;
    IfaceBridge.find_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridge.find(protocore_iface_bridge_span());
    const BridgeRule *r = IfaceBridge.rule;
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(PROTOCORE_IP_NONE, r->listen_ip.family);
}

void test_txn_parse_null_outputs()
{

    const uint8_t frame[6] = {0x00, 0x02, 0x00, 0x01, 0xAA, 0xBB};
    IfaceBridge.txn_parse_args.buf = frame;
    IfaceBridge.txn_parse_args.len = sizeof(frame);
    IfaceBridge.txn_parse_args.write_len = NULL;
    IfaceBridge.txn_parse_args.read_len = NULL;
    IfaceBridge.txn_parse_args.write_data = NULL;
    IfaceBridge.txn_parse(protocore_iface_bridge_span());
    size_t used = IfaceBridge.n;
    TEST_ASSERT_EQUAL_size_t(sizeof(frame), used);
}

void test_txn_build_edge_cases()
{
    uint8_t out[16];

    IfaceBridge.txn_build_args.out = NULL;
    IfaceBridge.txn_build_args.cap = sizeof(out);
    IfaceBridge.txn_build_args.write_data = NULL;
    IfaceBridge.txn_build_args.write_len = 0;
    IfaceBridge.txn_build_args.read_len = 0;
    IfaceBridge.txn_build(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_size_t(0, IfaceBridge.n);

    IfaceBridge.txn_build_args.out = out;
    IfaceBridge.txn_build_args.cap = sizeof(out);
    IfaceBridge.txn_build_args.write_data = NULL;
    IfaceBridge.txn_build_args.write_len = 0;
    IfaceBridge.txn_build_args.read_len = 8;
    IfaceBridge.txn_build(protocore_iface_bridge_span());
    size_t n0 = IfaceBridge.n;
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_BRIDGE_TXN_HDR, n0);
    const uint8_t expect0[] = {0x00, 0x00, 0x00, 0x08};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect0, out, n0);

    IfaceBridge.txn_build_args.out = out;
    IfaceBridge.txn_build_args.cap = sizeof(out);
    IfaceBridge.txn_build_args.write_data = NULL;
    IfaceBridge.txn_build_args.write_len = 5;
    IfaceBridge.txn_build_args.read_len = 0;
    IfaceBridge.txn_build(protocore_iface_bridge_span());
    size_t n1 = IfaceBridge.n;
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
