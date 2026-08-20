// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "server/net/iface_bridge/iface_bridge/iface_bridge.h"
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
    IfaceBridgeV.map_args.ip = "192.168.1.50";
    IfaceBridgeV.map_args.port = 4001;
    IfaceBridgeV.map_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridgeV.map_args.target = &u;
    IfaceBridge.map(protocore_iface_bridge_span());
    TEST_ASSERT_TRUE(IfaceBridgeV.ok);
    IfaceBridge.count(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_UINT8(1, IfaceBridgeV.u8);

    IfaceBridgeV.find_args.port = 4001;
    IfaceBridgeV.find_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridge.find(protocore_iface_bridge_span());
    const BridgeRule *r = IfaceBridgeV.rule;
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(BRIDGE_BUS_UART, r->target.bus);
    TEST_ASSERT_EQUAL_UINT16(4001, r->listen_port);
    TEST_ASSERT_EQUAL(PROTOCORE_IP_V4, r->listen_ip.family);
    TEST_ASSERT_EQUAL_UINT8(192, r->listen_ip.bytes[0]);
    TEST_ASSERT_EQUAL_UINT8(50, r->listen_ip.bytes[3]);

    IfaceBridgeV.find_args.port = 4001;
    IfaceBridgeV.find_args.proto = BRIDGE_PROTO_UDP;
    IfaceBridge.find(protocore_iface_bridge_span());
    TEST_ASSERT_NULL(IfaceBridgeV.rule);
    IfaceBridgeV.find_args.port = 4002;
    IfaceBridgeV.find_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridge.find(protocore_iface_bridge_span());
    TEST_ASSERT_NULL(IfaceBridgeV.rule);
}

void test_any_interface_and_dedup()
{
    BridgeTarget i = i2c_target(0x40);
    IfaceBridgeV.map_args.ip = NULL;
    IfaceBridgeV.map_args.port = 5000;
    IfaceBridgeV.map_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridgeV.map_args.target = &i;
    IfaceBridge.map(protocore_iface_bridge_span());
    TEST_ASSERT_TRUE(IfaceBridgeV.ok);
    IfaceBridgeV.find_args.port = 5000;
    IfaceBridgeV.find_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridge.find(protocore_iface_bridge_span());
    const BridgeRule *r = IfaceBridgeV.rule;
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(PROTOCORE_IP_NONE, r->listen_ip.family);
    TEST_ASSERT_EQUAL_UINT16(0x40, r->target.addr_cs);

    BridgeTarget u = uart_target();
    IfaceBridgeV.map_args.ip = "10.0.0.1";
    IfaceBridgeV.map_args.port = 5000;
    IfaceBridgeV.map_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridgeV.map_args.target = &u;
    IfaceBridge.map(protocore_iface_bridge_span());
    TEST_ASSERT_FALSE(IfaceBridgeV.ok);

    IfaceBridgeV.map_args.ip = NULL;
    IfaceBridgeV.map_args.port = 5000;
    IfaceBridgeV.map_args.proto = BRIDGE_PROTO_UDP;
    IfaceBridgeV.map_args.target = &i;
    IfaceBridge.map(protocore_iface_bridge_span());
    TEST_ASSERT_TRUE(IfaceBridgeV.ok);
    IfaceBridge.count(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_UINT8(2, IfaceBridgeV.u8);
}

void test_bad_address_rejected()
{
    BridgeTarget u = uart_target();
    IfaceBridgeV.map_args.ip = "not.an.ip";
    IfaceBridgeV.map_args.port = 6000;
    IfaceBridgeV.map_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridgeV.map_args.target = &u;
    IfaceBridge.map(protocore_iface_bridge_span());
    TEST_ASSERT_FALSE(IfaceBridgeV.ok);
    IfaceBridge.count(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_UINT8(0, IfaceBridgeV.u8);
}

void test_table_full()
{
    BridgeTarget u = uart_target();
    for (uint16_t p = 0; p < PROTOCORE_BRIDGE_MAX_RULES; p++)
    {
        IfaceBridgeV.map_args.ip = NULL;
        IfaceBridgeV.map_args.port = (uint16_t)(7000 + p);
        IfaceBridgeV.map_args.proto = BRIDGE_PROTO_TCP;
        IfaceBridgeV.map_args.target = &u;
        IfaceBridge.map(protocore_iface_bridge_span());
        TEST_ASSERT_TRUE(IfaceBridgeV.ok);
    }
    IfaceBridge.count(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_BRIDGE_MAX_RULES, IfaceBridgeV.u8);
    IfaceBridgeV.map_args.ip = NULL;
    IfaceBridgeV.map_args.port = 9999;
    IfaceBridgeV.map_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridgeV.map_args.target = &u;
    IfaceBridge.map(protocore_iface_bridge_span());
    TEST_ASSERT_FALSE(IfaceBridgeV.ok);
    IfaceBridge.clear(protocore_iface_bridge_span());
    IfaceBridge.count(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_UINT8(0, IfaceBridgeV.u8);
}

void test_txn_roundtrip()
{
    const uint8_t wr[3] = {0xAA, 0xBB, 0xCC};
    uint8_t frame[16];
    IfaceBridgeV.txn_build_args.out = frame;
    IfaceBridgeV.txn_build_args.cap = sizeof(frame);
    IfaceBridgeV.txn_build_args.write_data = wr;
    IfaceBridgeV.txn_build_args.write_len = 3;
    IfaceBridgeV.txn_build_args.read_len = 5;
    IfaceBridge.txn_build(protocore_iface_bridge_span());
    size_t n = IfaceBridgeV.n;
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_BRIDGE_TXN_HDR + 3, n);
    const uint8_t expect[] = {0x00, 0x03, 0x00, 0x05, 0xAA, 0xBB, 0xCC};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, frame, n);

    uint16_t wl = 0;
    uint16_t rl = 0;
    const uint8_t *wd = NULL;
    IfaceBridgeV.txn_parse_args.buf = frame;
    IfaceBridgeV.txn_parse_args.len = n;
    IfaceBridgeV.txn_parse_args.write_len = &wl;
    IfaceBridgeV.txn_parse_args.read_len = &rl;
    IfaceBridgeV.txn_parse_args.write_data = &wd;
    IfaceBridge.txn_parse(protocore_iface_bridge_span());
    size_t used = IfaceBridgeV.n;
    TEST_ASSERT_EQUAL_size_t(n, used);
    TEST_ASSERT_EQUAL_UINT16(3, wl);
    TEST_ASSERT_EQUAL_UINT16(5, rl);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(wr, wd, 3);
}

void test_txn_partial_and_readonly()
{

    const uint8_t hdr2[2] = {0x00, 0x03};
    IfaceBridgeV.txn_parse_args.buf = hdr2;
    IfaceBridgeV.txn_parse_args.len = sizeof(hdr2);
    IfaceBridgeV.txn_parse_args.write_len = NULL;
    IfaceBridgeV.txn_parse_args.read_len = NULL;
    IfaceBridgeV.txn_parse_args.write_data = NULL;
    IfaceBridge.txn_parse(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_size_t(0, IfaceBridgeV.n);

    const uint8_t partial[6] = {0x00, 0x03, 0x00, 0x00, 0xAA, 0xBB};
    IfaceBridgeV.txn_parse_args.buf = partial;
    IfaceBridgeV.txn_parse_args.len = sizeof(partial);
    IfaceBridgeV.txn_parse_args.write_len = NULL;
    IfaceBridgeV.txn_parse_args.read_len = NULL;
    IfaceBridgeV.txn_parse_args.write_data = NULL;
    IfaceBridge.txn_parse(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_size_t(0, IfaceBridgeV.n);

    const uint8_t readonly[4] = {0x00, 0x00, 0x00, 0x08};
    uint16_t wl = 9;
    uint16_t rl = 0;
    const uint8_t *wd = NULL;
    IfaceBridgeV.txn_parse_args.buf = readonly;
    IfaceBridgeV.txn_parse_args.len = sizeof(readonly);
    IfaceBridgeV.txn_parse_args.write_len = &wl;
    IfaceBridgeV.txn_parse_args.read_len = &rl;
    IfaceBridgeV.txn_parse_args.write_data = &wd;
    IfaceBridge.txn_parse(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_BRIDGE_TXN_HDR, IfaceBridgeV.n);
    TEST_ASSERT_EQUAL_UINT16(0, wl);
    TEST_ASSERT_EQUAL_UINT16(8, rl);
}

void test_build_overflow_fails_closed()
{
    const uint8_t wr[4] = {1, 2, 3, 4};
    uint8_t small[6];
    IfaceBridgeV.txn_build_args.out = small;
    IfaceBridgeV.txn_build_args.cap = sizeof(small);
    IfaceBridgeV.txn_build_args.write_data = wr;
    IfaceBridgeV.txn_build_args.write_len = 4;
    IfaceBridgeV.txn_build_args.read_len = 0;
    IfaceBridge.txn_build(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_size_t(0, IfaceBridgeV.n);
    IfaceBridgeV.txn_parse_args.buf = NULL;
    IfaceBridgeV.txn_parse_args.len = 10;
    IfaceBridgeV.txn_parse_args.write_len = NULL;
    IfaceBridgeV.txn_parse_args.read_len = NULL;
    IfaceBridgeV.txn_parse_args.write_data = NULL;
    IfaceBridge.txn_parse(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_size_t(0, IfaceBridgeV.n);
}

void test_null_arg_guards()
{

    IfaceBridgeV.add_args.rule = NULL;
    IfaceBridge.add(protocore_iface_bridge_span());
    TEST_ASSERT_FALSE(IfaceBridgeV.ok);
    IfaceBridge.count(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_UINT8(0, IfaceBridgeV.u8);

    IfaceBridgeV.map_args.ip = "10.0.0.1";
    IfaceBridgeV.map_args.port = 7600;
    IfaceBridgeV.map_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridgeV.map_args.target = NULL;
    IfaceBridge.map(protocore_iface_bridge_span());
    TEST_ASSERT_FALSE(IfaceBridgeV.ok);
    IfaceBridge.count(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_UINT8(0, IfaceBridgeV.u8);
}

void test_map_empty_ip_is_any_interface()
{

    BridgeTarget u = uart_target();
    IfaceBridgeV.map_args.ip = "";
    IfaceBridgeV.map_args.port = 5200;
    IfaceBridgeV.map_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridgeV.map_args.target = &u;
    IfaceBridge.map(protocore_iface_bridge_span());
    TEST_ASSERT_TRUE(IfaceBridgeV.ok);
    IfaceBridgeV.find_args.port = 5200;
    IfaceBridgeV.find_args.proto = BRIDGE_PROTO_TCP;
    IfaceBridge.find(protocore_iface_bridge_span());
    const BridgeRule *r = IfaceBridgeV.rule;
    TEST_ASSERT_NOT_NULL(r);
    TEST_ASSERT_EQUAL(PROTOCORE_IP_NONE, r->listen_ip.family);
}

void test_txn_parse_null_outputs()
{

    const uint8_t frame[6] = {0x00, 0x02, 0x00, 0x01, 0xAA, 0xBB};
    IfaceBridgeV.txn_parse_args.buf = frame;
    IfaceBridgeV.txn_parse_args.len = sizeof(frame);
    IfaceBridgeV.txn_parse_args.write_len = NULL;
    IfaceBridgeV.txn_parse_args.read_len = NULL;
    IfaceBridgeV.txn_parse_args.write_data = NULL;
    IfaceBridge.txn_parse(protocore_iface_bridge_span());
    size_t used = IfaceBridgeV.n;
    TEST_ASSERT_EQUAL_size_t(sizeof(frame), used);
}

void test_txn_build_edge_cases()
{
    uint8_t out[16];

    IfaceBridgeV.txn_build_args.out = NULL;
    IfaceBridgeV.txn_build_args.cap = sizeof(out);
    IfaceBridgeV.txn_build_args.write_data = NULL;
    IfaceBridgeV.txn_build_args.write_len = 0;
    IfaceBridgeV.txn_build_args.read_len = 0;
    IfaceBridge.txn_build(protocore_iface_bridge_span());
    TEST_ASSERT_EQUAL_size_t(0, IfaceBridgeV.n);

    IfaceBridgeV.txn_build_args.out = out;
    IfaceBridgeV.txn_build_args.cap = sizeof(out);
    IfaceBridgeV.txn_build_args.write_data = NULL;
    IfaceBridgeV.txn_build_args.write_len = 0;
    IfaceBridgeV.txn_build_args.read_len = 8;
    IfaceBridge.txn_build(protocore_iface_bridge_span());
    size_t n0 = IfaceBridgeV.n;
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_BRIDGE_TXN_HDR, n0);
    const uint8_t expect0[] = {0x00, 0x00, 0x00, 0x08};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect0, out, n0);

    IfaceBridgeV.txn_build_args.out = out;
    IfaceBridgeV.txn_build_args.cap = sizeof(out);
    IfaceBridgeV.txn_build_args.write_data = NULL;
    IfaceBridgeV.txn_build_args.write_len = 5;
    IfaceBridgeV.txn_build_args.read_len = 0;
    IfaceBridge.txn_build(protocore_iface_bridge_span());
    size_t n1 = IfaceBridgeV.n;
    TEST_ASSERT_EQUAL_size_t(PROTOCORE_BRIDGE_TXN_HDR + 5, n1);
    TEST_ASSERT_EQUAL_UINT8(0x00, out[0]);
    TEST_ASSERT_EQUAL_UINT8(0x05, out[1]);
}
