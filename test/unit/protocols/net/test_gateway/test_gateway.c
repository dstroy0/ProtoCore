// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "server/clock/clock.h"
#include "server/net/gateway/gateway.h"
#include <string.h>

#include <unity.h>

typedef struct
{
    uint8_t payload[512];
    size_t payload_len;
    uint32_t seq;
    uint16_t src_addr;
    uint16_t len;
    int16_t rssi;
    uint8_t port_id;
    protocore_gateway_kind kind;
} UpMsg;
static UpMsg g_up[64];
static size_t g_up_n;
static proto_bool g_up_accept = PROTO_TRUE;

static proto_bool cap_uplink(const protocore_gateway_msg *m, void *)
{
    if (!g_up_accept)
    {
        return PROTO_FALSE;
    }
    UpMsg u;
    u.payload_len = (size_t)(m->len) < 512 ? (size_t)(m->len) : 512;
    memcpy(u.payload, m->payload, u.payload_len);
    u.seq = m->seq;
    u.src_addr = m->src_addr;
    u.len = m->len;
    u.rssi = m->rssi;
    u.port_id = m->port_id;
    u.kind = m->kind;
    if (g_up_n < 64)
    {
        g_up[g_up_n++] = u;
    }
    return PROTO_TRUE;
}

typedef struct
{
    uint8_t payload[512];
    size_t payload_len;
    uint16_t dst;
    uint8_t port_id;
} DownMsg;
static DownMsg g_down[64];
static size_t g_down_n;
static proto_bool g_tx_accept = PROTO_TRUE;

static proto_bool cap_tx(uint8_t port, uint16_t dst, const uint8_t *d, uint16_t n, void *)
{
    if (!g_tx_accept)
    {
        return PROTO_FALSE;
    }
    DownMsg x;
    x.payload_len = (size_t)(n) < 512 ? (size_t)(n) : 512;
    memcpy(x.payload, d, x.payload_len);
    x.dst = dst;
    x.port_id = port;
    if (g_down_n < 64)
    {
        g_down[g_down_n++] = x;
    }
    return PROTO_TRUE;
}

static proto_bool add_port(uint8_t id, protocore_gateway_kind kind, uint16_t rate, proto_bool withtx)
{
    protocore_gateway_port_config c = {0};
    c.port_id = id;
    c.kind = kind;
    c.tx = withtx ? cap_tx : NULL;
    c.rate_cap = rate;
    Gateway.add_port_args.cfg = &c;
    Gateway.add_port(protocore_gateway_span());
    return Gateway.ok;
}

static protocore_gateway_stats stats()
{
    protocore_gateway_stats st;
    Gateway.get_stats_args.out = &st;
    Gateway.get_stats(protocore_gateway_span());
    return st;
}

static uint32_t g_now_ms;
static uint32_t test_clock(void)
{
    return g_now_ms;
}
static void set_now(uint32_t ms)
{
    g_now_ms = ms;
}

void setUp()
{
    g_up_n = 0;
    g_down_n = 0;
    g_up_accept = PROTO_TRUE;
    g_tx_accept = PROTO_TRUE;
    Gateway.reset(protocore_gateway_span());
    Clock.src.fn = test_clock;
    Clock.src.ticks_per_second = 1000;
    Clock.set_ms(Clock.internal);
    set_now(0);
}
void tearDown()
{
    Gateway.reset(protocore_gateway_span());
}

void test_uplink_envelopes_and_publishes()
{
    TEST_ASSERT_TRUE(add_port(0, PROTOCORE_GW_LORA, 0, PROTO_FALSE));
    Gateway.set_uplink_cb_args.fn = cap_uplink;
    Gateway.set_uplink_cb_args.ctx = NULL;
    Gateway.set_uplink_cb(protocore_gateway_span());
    const uint8_t hi[2] = {'h', 'i'};
    Gateway.uplink_args.port_id = 0;
    Gateway.uplink_args.src_addr = 0x42;
    Gateway.uplink_args.payload = hi;
    Gateway.uplink_args.len = 2;
    Gateway.uplink_args.rssi = -50;
    Gateway.uplink(protocore_gateway_span());
    TEST_ASSERT_TRUE(Gateway.ok);
    TEST_ASSERT_EQUAL_size_t(1, g_up_n);
    TEST_ASSERT_EQUAL_UINT16(0x42, g_up[0].src_addr);
    TEST_ASSERT_EQUAL_UINT8(0, g_up[0].port_id);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_GW_LORA, g_up[0].kind);
    TEST_ASSERT_EQUAL_INT16(-50, g_up[0].rssi);
    TEST_ASSERT_EQUAL_UINT32(0, g_up[0].seq);
    TEST_ASSERT_EQUAL_MEMORY(hi, g_up[0].payload, 2);
    TEST_ASSERT_EQUAL_UINT32(1, stats().up_published);
}

void test_uplink_no_sink_drops()
{
    add_port(0, PROTOCORE_GW_LORA, 0, PROTO_FALSE);
    const uint8_t x[1] = {1};
    Gateway.uplink_args.port_id = 0;
    Gateway.uplink_args.src_addr = 1;
    Gateway.uplink_args.payload = x;
    Gateway.uplink_args.len = 1;
    Gateway.uplink_args.rssi = 0;
    Gateway.uplink(protocore_gateway_span());
    TEST_ASSERT_FALSE(Gateway.ok);
    TEST_ASSERT_EQUAL_UINT32(1, stats().up_dropped);
    TEST_ASSERT_EQUAL_UINT32(0, stats().up_published);
}

void test_uplink_unknown_port_drops()
{
    Gateway.set_uplink_cb_args.fn = cap_uplink;
    Gateway.set_uplink_cb_args.ctx = NULL;
    Gateway.set_uplink_cb(protocore_gateway_span());
    const uint8_t x[1] = {1};
    Gateway.uplink_args.port_id = 9;
    Gateway.uplink_args.src_addr = 1;
    Gateway.uplink_args.payload = x;
    Gateway.uplink_args.len = 1;
    Gateway.uplink_args.rssi = 0;
    Gateway.uplink(protocore_gateway_span());
    TEST_ASSERT_FALSE(Gateway.ok);
    TEST_ASSERT_EQUAL_UINT32(1, stats().up_dropped);
    TEST_ASSERT_EQUAL_size_t(0, g_up_n);
}

void test_uplink_rate_cap()
{
    add_port(0, PROTOCORE_GW_NRF24, 2, PROTO_FALSE);
    Gateway.set_uplink_cb_args.fn = cap_uplink;
    Gateway.set_uplink_cb_args.ctx = NULL;
    Gateway.set_uplink_cb(protocore_gateway_span());
    const uint8_t x[1] = {7};
    Gateway.uplink_args.port_id = 0;
    Gateway.uplink_args.src_addr = 1;
    Gateway.uplink_args.payload = x;
    Gateway.uplink_args.len = 1;
    Gateway.uplink_args.rssi = 0;
    Gateway.uplink(protocore_gateway_span());
    TEST_ASSERT_TRUE(Gateway.ok);
    Gateway.uplink_args.port_id = 0;
    Gateway.uplink_args.src_addr = 1;
    Gateway.uplink_args.payload = x;
    Gateway.uplink_args.len = 1;
    Gateway.uplink_args.rssi = 0;
    Gateway.uplink(protocore_gateway_span());
    TEST_ASSERT_TRUE(Gateway.ok);
    Gateway.uplink_args.port_id = 0;
    Gateway.uplink_args.src_addr = 1;
    Gateway.uplink_args.payload = x;
    Gateway.uplink_args.len = 1;
    Gateway.uplink_args.rssi = 0;
    Gateway.uplink(protocore_gateway_span());
    TEST_ASSERT_FALSE(Gateway.ok);
    TEST_ASSERT_EQUAL_size_t(2, g_up_n);
    TEST_ASSERT_EQUAL_UINT32(1, stats().up_dropped);
    set_now(1000);
    Gateway.uplink_args.port_id = 0;
    Gateway.uplink_args.src_addr = 1;
    Gateway.uplink_args.payload = x;
    Gateway.uplink_args.len = 1;
    Gateway.uplink_args.rssi = 0;
    Gateway.uplink(protocore_gateway_span());
    TEST_ASSERT_TRUE(Gateway.ok);
    TEST_ASSERT_EQUAL_size_t(3, g_up_n);
}

void test_uplink_sink_refusal_counted()
{
    add_port(0, PROTOCORE_GW_LORA, 0, PROTO_FALSE);
    Gateway.set_uplink_cb_args.fn = cap_uplink;
    Gateway.set_uplink_cb_args.ctx = NULL;
    Gateway.set_uplink_cb(protocore_gateway_span());
    g_up_accept = PROTO_FALSE;
    const uint8_t x[1] = {1};
    Gateway.uplink_args.port_id = 0;
    Gateway.uplink_args.src_addr = 1;
    Gateway.uplink_args.payload = x;
    Gateway.uplink_args.len = 1;
    Gateway.uplink_args.rssi = 0;
    Gateway.uplink(protocore_gateway_span());
    TEST_ASSERT_FALSE(Gateway.ok);
    TEST_ASSERT_EQUAL_UINT32(1, stats().up_dropped);
    TEST_ASSERT_EQUAL_UINT32(0, stats().up_published);
}

void test_downlink_transmits()
{
    add_port(0, PROTOCORE_GW_LORA, 0, PROTO_TRUE);
    const uint8_t cmd[3] = {'c', 'm', 'd'};
    Gateway.downlink_args.port_id = 0;
    Gateway.downlink_args.dst_addr = 0x10;
    Gateway.downlink_args.payload = cmd;
    Gateway.downlink_args.len = 3;
    Gateway.downlink(protocore_gateway_span());
    TEST_ASSERT_TRUE(Gateway.ok);
    TEST_ASSERT_EQUAL_size_t(1, g_down_n);
    TEST_ASSERT_EQUAL_UINT8(0, g_down[0].port_id);
    TEST_ASSERT_EQUAL_UINT16(0x10, g_down[0].dst);
    TEST_ASSERT_EQUAL_MEMORY(cmd, g_down[0].payload, 3);
    TEST_ASSERT_EQUAL_UINT32(1, stats().down_sent);
}

void test_downlink_no_tx_or_unknown_port_drops()
{
    add_port(0, PROTOCORE_GW_LORA, 0, PROTO_FALSE);
    const uint8_t x[1] = {1};
    Gateway.downlink_args.port_id = 0;
    Gateway.downlink_args.dst_addr = 1;
    Gateway.downlink_args.payload = x;
    Gateway.downlink_args.len = 1;
    Gateway.downlink(protocore_gateway_span());
    TEST_ASSERT_FALSE(Gateway.ok);
    Gateway.downlink_args.port_id = 9;
    Gateway.downlink_args.dst_addr = 1;
    Gateway.downlink_args.payload = x;
    Gateway.downlink_args.len = 1;
    Gateway.downlink(protocore_gateway_span());
    TEST_ASSERT_FALSE(Gateway.ok);
    TEST_ASSERT_EQUAL_UINT32(2, stats().down_dropped);
}

void test_downlink_tx_refusal_counted()
{
    add_port(0, PROTOCORE_GW_LORA, 0, PROTO_TRUE);
    g_tx_accept = PROTO_FALSE;
    const uint8_t x[1] = {1};
    Gateway.downlink_args.port_id = 0;
    Gateway.downlink_args.dst_addr = 1;
    Gateway.downlink_args.payload = x;
    Gateway.downlink_args.len = 1;
    Gateway.downlink(protocore_gateway_span());
    TEST_ASSERT_FALSE(Gateway.ok);
    TEST_ASSERT_EQUAL_UINT32(1, stats().down_dropped);
    TEST_ASSERT_EQUAL_UINT32(0, stats().down_sent);
}

void test_topic_format()
{
    protocore_gateway_msg m = {0};
    m.port_id = 2;
    m.src_addr = 0x42;
    char buf[32];
    Gateway.topic_args.msg = &m;
    Gateway.topic_args.buf = buf;
    Gateway.topic_args.buflen = sizeof(buf);
    Gateway.topic(protocore_gateway_span());
    uint16_t n = Gateway.n;
    TEST_ASSERT_EQUAL_STRING("gw/2/66", buf);
    TEST_ASSERT_EQUAL_UINT16(7, n);

    Gateway.set_topic_prefix_args.prefix = "lora";
    Gateway.set_topic_prefix(protocore_gateway_span());
    Gateway.topic_args.msg = &m;
    Gateway.topic_args.buf = buf;
    Gateway.topic_args.buflen = sizeof(buf);
    Gateway.topic(protocore_gateway_span());
    n = Gateway.n;
    TEST_ASSERT_EQUAL_STRING("lora/2/66", buf);

    Gateway.set_topic_prefix_args.prefix = NULL;
    Gateway.set_topic_prefix(protocore_gateway_span());
    Gateway.topic_args.msg = &m;
    Gateway.topic_args.buf = buf;
    Gateway.topic_args.buflen = sizeof(buf);
    Gateway.topic(protocore_gateway_span());
    n = Gateway.n;
    TEST_ASSERT_EQUAL_STRING("gw/2/66", buf);

    char tiny[4];
    Gateway.topic_args.msg = &m;
    Gateway.topic_args.buf = tiny;
    Gateway.topic_args.buflen = sizeof(tiny);
    Gateway.topic(protocore_gateway_span());
    TEST_ASSERT_EQUAL_UINT16(0, Gateway.n);
    Gateway.topic_args.msg = &m;
    Gateway.topic_args.buf = NULL;
    Gateway.topic_args.buflen = sizeof(buf);
    Gateway.topic(protocore_gateway_span());
    TEST_ASSERT_EQUAL_UINT16(0, Gateway.n);
}

void test_add_port_validation_and_table_full()
{
    Gateway.add_port_args.cfg = NULL;
    Gateway.add_port(protocore_gateway_span());
    TEST_ASSERT_FALSE(Gateway.ok);
    TEST_ASSERT_TRUE(add_port(0, PROTOCORE_GW_LORA, 0, PROTO_FALSE));
    TEST_ASSERT_FALSE(add_port(0, PROTOCORE_GW_LORA, 0, PROTO_FALSE));
    TEST_ASSERT_TRUE(add_port(1, PROTOCORE_GW_NRF24, 0, PROTO_FALSE));
    TEST_ASSERT_TRUE(add_port(2, PROTOCORE_GW_ZIGBEE, 0, PROTO_FALSE));
    TEST_ASSERT_TRUE(add_port(3, PROTOCORE_GW_BLE, 0, PROTO_FALSE));
    TEST_ASSERT_FALSE(add_port(4, PROTOCORE_GW_LORA, 0, PROTO_FALSE));
}

void test_seq_increments_per_uplink()
{
    add_port(0, PROTOCORE_GW_LORA, 0, PROTO_FALSE);
    Gateway.set_uplink_cb_args.fn = cap_uplink;
    Gateway.set_uplink_cb_args.ctx = NULL;
    Gateway.set_uplink_cb(protocore_gateway_span());
    const uint8_t x[1] = {1};
    Gateway.uplink_args.port_id = 0;
    Gateway.uplink_args.src_addr = 1;
    Gateway.uplink_args.payload = x;
    Gateway.uplink_args.len = 1;
    Gateway.uplink_args.rssi = 0;
    Gateway.uplink(protocore_gateway_span());
    Gateway.uplink_args.port_id = 0;
    Gateway.uplink_args.src_addr = 2;
    Gateway.uplink_args.payload = x;
    Gateway.uplink_args.len = 1;
    Gateway.uplink_args.rssi = 0;
    Gateway.uplink(protocore_gateway_span());
    TEST_ASSERT_EQUAL_size_t(2, g_up_n);
    TEST_ASSERT_EQUAL_UINT32(0, g_up[0].seq);
    TEST_ASSERT_EQUAL_UINT32(1, g_up[1].seq);
}

void test_topic_zero_and_overflow_steps()
{
    Gateway.reset(protocore_gateway_span());
    Gateway.set_topic_prefix_args.prefix = "gw";
    Gateway.set_topic_prefix(protocore_gateway_span());
    char buf[64];
    protocore_gateway_msg m = {0};
    m.port_id = 0;
    m.src_addr = 0;
    Gateway.topic_args.msg = &m;
    Gateway.topic_args.buf = buf;
    Gateway.topic_args.buflen = sizeof(buf);
    Gateway.topic(protocore_gateway_span());
    TEST_ASSERT_TRUE(Gateway.n > 0);
    Gateway.topic_args.msg = NULL;
    Gateway.topic_args.buf = buf;
    Gateway.topic_args.buflen = sizeof(buf);
    Gateway.topic(protocore_gateway_span());
    TEST_ASSERT_EQUAL_UINT16(0, Gateway.n);
    Gateway.topic_args.msg = &m;
    Gateway.topic_args.buf = buf;
    Gateway.topic_args.buflen = 0;
    Gateway.topic(protocore_gateway_span());
    TEST_ASSERT_EQUAL_UINT16(0, Gateway.n);
    for (uint16_t cap = 1; cap <= 6; cap++)
    {
        Gateway.topic_args.msg = &m;
        Gateway.topic_args.buf = buf;
        Gateway.topic_args.buflen = cap;
        Gateway.topic(protocore_gateway_span());
        TEST_ASSERT_EQUAL_UINT16(0, Gateway.n);
    }
}

void test_get_stats_null_out_is_noop()
{
    add_port(0, PROTOCORE_GW_LORA, 0, PROTO_FALSE);
    Gateway.set_uplink_cb_args.fn = cap_uplink;
    Gateway.set_uplink_cb_args.ctx = NULL;
    Gateway.set_uplink_cb(protocore_gateway_span());
    const uint8_t x[1] = {1};
    Gateway.uplink_args.port_id = 0;
    Gateway.uplink_args.src_addr = 1;
    Gateway.uplink_args.payload = x;
    Gateway.uplink_args.len = 1;
    Gateway.uplink_args.rssi = 0;
    Gateway.uplink(protocore_gateway_span());
    Gateway.get_stats_args.out = NULL;
    Gateway.get_stats(protocore_gateway_span());
    TEST_ASSERT_EQUAL_UINT32(1, stats().up_published);
}

