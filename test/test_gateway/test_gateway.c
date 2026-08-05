// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the radio / wireless gateway bridge (services/net/gateway): an uplink
// envelopes a received frame and publishes it (with src address / port / rssi / seq),
// fail-closed when there is no sink / an unknown port / the rate cap is hit / the sink
// refuses; a downlink routes to the port transmit callback; the topic helper formats
// <prefix>/<port>/<addr>; and the port table limits hold. Pure host tests; the DMA +
// FORWARD-lane ingest that feeds it is HW-verified separately.
//
// The env sizes PC_GW_MAX_PORTS = 4.

#include "services/net/gateway/gateway.h"

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
    pc_gateway_kind kind;
} UpMsg;
static UpMsg g_up[64];
static size_t g_up_n;
static proto_bool g_up_accept = PROTO_TRUE;

static proto_bool cap_uplink(const pc_gateway_msg *m, void *)
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

static proto_bool add_port(uint8_t id, pc_gateway_kind kind, uint16_t rate, proto_bool withtx)
{
    pc_gateway_port_config c = {0};
    c.port_id = id;
    c.kind = kind;
    c.tx = withtx ? cap_tx : NULL;
    c.rate_cap = rate;
    return pc_gateway_add_port(&c);
}

static pc_gateway_stats stats()
{
    pc_gateway_stats st;
    pc_gateway_get_stats(&st);
    return st;
}

void setUp()
{
    g_up_n = 0;
    g_down_n = 0;
    g_up_accept = PROTO_TRUE;
    g_tx_accept = PROTO_TRUE;
    pc_gateway_reset();
    pc_gateway_test_set_now(0);
}
void tearDown()
{
    pc_gateway_reset();
}

void test_uplink_envelopes_and_publishes()
{
    TEST_ASSERT_TRUE(add_port(0, PC_GW_LORA, 0, PROTO_FALSE));
    pc_gateway_set_uplink_cb(cap_uplink, NULL);
    const uint8_t hi[2] = {'h', 'i'};
    TEST_ASSERT_TRUE(pc_gateway_uplink(0, 0x42, hi, 2, -50));
    TEST_ASSERT_EQUAL_size_t(1, g_up_n);
    TEST_ASSERT_EQUAL_UINT16(0x42, g_up[0].src_addr);
    TEST_ASSERT_EQUAL_UINT8(0, g_up[0].port_id);
    TEST_ASSERT_EQUAL_UINT8(PC_GW_LORA, g_up[0].kind);
    TEST_ASSERT_EQUAL_INT16(-50, g_up[0].rssi);
    TEST_ASSERT_EQUAL_UINT32(0, g_up[0].seq);
    TEST_ASSERT_EQUAL_MEMORY(hi, g_up[0].payload, 2);
    TEST_ASSERT_EQUAL_UINT32(1, stats().up_published);
}

void test_uplink_no_sink_drops()
{
    add_port(0, PC_GW_LORA, 0, PROTO_FALSE); // no pc_gateway_set_uplink_cb()
    const uint8_t x[1] = {1};
    TEST_ASSERT_FALSE(pc_gateway_uplink(0, 1, x, 1, 0));
    TEST_ASSERT_EQUAL_UINT32(1, stats().up_dropped);
    TEST_ASSERT_EQUAL_UINT32(0, stats().up_published);
}

void test_uplink_unknown_port_drops()
{
    pc_gateway_set_uplink_cb(cap_uplink, NULL);
    const uint8_t x[1] = {1};
    TEST_ASSERT_FALSE(pc_gateway_uplink(9, 1, x, 1, 0)); // port 9 never registered
    TEST_ASSERT_EQUAL_UINT32(1, stats().up_dropped);
    TEST_ASSERT_EQUAL_size_t(0, g_up_n);
}

void test_uplink_rate_cap()
{
    add_port(0, PC_GW_NRF24, 2, PROTO_FALSE); // 2 uplinks / second
    pc_gateway_set_uplink_cb(cap_uplink, NULL);
    const uint8_t x[1] = {7};
    TEST_ASSERT_TRUE(pc_gateway_uplink(0, 1, x, 1, 0));
    TEST_ASSERT_TRUE(pc_gateway_uplink(0, 1, x, 1, 0));
    TEST_ASSERT_FALSE(pc_gateway_uplink(0, 1, x, 1, 0)); // 3rd in the window -> dropped
    TEST_ASSERT_EQUAL_size_t(2, g_up_n);
    TEST_ASSERT_EQUAL_UINT32(1, stats().up_dropped);
    pc_gateway_test_set_now(1000); // next window
    TEST_ASSERT_TRUE(pc_gateway_uplink(0, 1, x, 1, 0));
    TEST_ASSERT_EQUAL_size_t(3, g_up_n);
}

void test_uplink_sink_refusal_counted()
{
    add_port(0, PC_GW_LORA, 0, PROTO_FALSE);
    pc_gateway_set_uplink_cb(cap_uplink, NULL);
    g_up_accept = PROTO_FALSE; // northbound stack refuses
    const uint8_t x[1] = {1};
    TEST_ASSERT_FALSE(pc_gateway_uplink(0, 1, x, 1, 0));
    TEST_ASSERT_EQUAL_UINT32(1, stats().up_dropped);
    TEST_ASSERT_EQUAL_UINT32(0, stats().up_published);
}

void test_downlink_transmits()
{
    add_port(0, PC_GW_LORA, 0, PROTO_TRUE); // with a tx callback
    const uint8_t cmd[3] = {'c', 'm', 'd'};
    TEST_ASSERT_TRUE(pc_gateway_downlink(0, 0x10, cmd, 3));
    TEST_ASSERT_EQUAL_size_t(1, g_down_n);
    TEST_ASSERT_EQUAL_UINT8(0, g_down[0].port_id);
    TEST_ASSERT_EQUAL_UINT16(0x10, g_down[0].dst);
    TEST_ASSERT_EQUAL_MEMORY(cmd, g_down[0].payload, 3);
    TEST_ASSERT_EQUAL_UINT32(1, stats().down_sent);
}

void test_downlink_no_tx_or_unknown_port_drops()
{
    add_port(0, PC_GW_LORA, 0, PROTO_FALSE); // receive-only (null tx)
    const uint8_t x[1] = {1};
    TEST_ASSERT_FALSE(pc_gateway_downlink(0, 1, x, 1)); // no tx
    TEST_ASSERT_FALSE(pc_gateway_downlink(9, 1, x, 1)); // unknown port
    TEST_ASSERT_EQUAL_UINT32(2, stats().down_dropped);
}

void test_downlink_tx_refusal_counted()
{
    add_port(0, PC_GW_LORA, 0, PROTO_TRUE);
    g_tx_accept = PROTO_FALSE; // radio refuses
    const uint8_t x[1] = {1};
    TEST_ASSERT_FALSE(pc_gateway_downlink(0, 1, x, 1));
    TEST_ASSERT_EQUAL_UINT32(1, stats().down_dropped);
    TEST_ASSERT_EQUAL_UINT32(0, stats().down_sent);
}

void test_topic_format()
{
    pc_gateway_msg m = {0};
    m.port_id = 2;
    m.src_addr = 0x42; // 66
    char buf[32];
    uint16_t n = pc_gateway_topic(&m, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("gw/2/66", buf);
    TEST_ASSERT_EQUAL_UINT16(7, n);

    pc_gateway_set_topic_prefix("lora");
    n = pc_gateway_topic(&m, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("lora/2/66", buf);

    pc_gateway_set_topic_prefix(NULL); // null -> falls back to PC_GW_DEFAULT_PREFIX
    n = pc_gateway_topic(&m, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("gw/2/66", buf);

    char tiny[4];
    TEST_ASSERT_EQUAL_UINT16(0, pc_gateway_topic(&m, tiny, sizeof(tiny))); // too small -> 0
    TEST_ASSERT_EQUAL_UINT16(0, pc_gateway_topic(&m, NULL, sizeof(buf)));  // null buf -> 0
}

void test_add_port_validation_and_table_full()
{
    TEST_ASSERT_FALSE(pc_gateway_add_port(NULL));
    TEST_ASSERT_TRUE(add_port(0, PC_GW_LORA, 0, PROTO_FALSE));
    TEST_ASSERT_FALSE(add_port(0, PC_GW_LORA, 0, PROTO_FALSE)); // duplicate id
    TEST_ASSERT_TRUE(add_port(1, PC_GW_NRF24, 0, PROTO_FALSE));
    TEST_ASSERT_TRUE(add_port(2, PC_GW_ZIGBEE, 0, PROTO_FALSE));
    TEST_ASSERT_TRUE(add_port(3, PC_GW_BLE, 0, PROTO_FALSE));
    TEST_ASSERT_FALSE(add_port(4, PC_GW_LORA, 0, PROTO_FALSE)); // table full (PC_GW_MAX_PORTS = 4)
}

void test_seq_increments_per_uplink()
{
    add_port(0, PC_GW_LORA, 0, PROTO_FALSE);
    pc_gateway_set_uplink_cb(cap_uplink, NULL);
    const uint8_t x[1] = {1};
    pc_gateway_uplink(0, 1, x, 1, 0);
    pc_gateway_uplink(0, 2, x, 1, 0);
    TEST_ASSERT_EQUAL_size_t(2, g_up_n);
    TEST_ASSERT_EQUAL_UINT32(0, g_up[0].seq);
    TEST_ASSERT_EQUAL_UINT32(1, g_up[1].seq);
}

void test_topic_zero_and_overflow_steps()
{
    pc_gateway_reset();
    pc_gateway_set_topic_prefix("gw");
    char buf[64];
    pc_gateway_msg m = {0};
    m.port_id = 0; // zero -> put_u32 '0' branch
    m.src_addr = 0;
    TEST_ASSERT_TRUE(pc_gateway_topic(&m, buf, sizeof(buf)) > 0);
    TEST_ASSERT_EQUAL_UINT16(0, pc_gateway_topic(NULL, buf, sizeof(buf))); // null msg
    TEST_ASSERT_EQUAL_UINT16(0, pc_gateway_topic(&m, buf, 0));             // zero buflen
    for (uint16_t cap = 1; cap <= 6; cap++)                                // fail at successive append steps:
    {
        TEST_ASSERT_EQUAL_UINT16(0, pc_gateway_topic(&m, buf, cap)); // prefix, both '/'s, both digits
    }
}

void test_get_stats_null_out_is_noop()
{
    add_port(0, PC_GW_LORA, 0, PROTO_FALSE);
    pc_gateway_set_uplink_cb(cap_uplink, NULL);
    const uint8_t x[1] = {1};
    pc_gateway_uplink(0, 1, x, 1, 0);
    pc_gateway_get_stats(NULL);                        // out == nullptr -> must not write/crash
    TEST_ASSERT_EQUAL_UINT32(1, stats().up_published); // real stats unaffected
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_uplink_envelopes_and_publishes);
    RUN_TEST(test_uplink_no_sink_drops);
    RUN_TEST(test_uplink_unknown_port_drops);
    RUN_TEST(test_uplink_rate_cap);
    RUN_TEST(test_uplink_sink_refusal_counted);
    RUN_TEST(test_downlink_transmits);
    RUN_TEST(test_downlink_no_tx_or_unknown_port_drops);
    RUN_TEST(test_downlink_tx_refusal_counted);
    RUN_TEST(test_topic_format);
    RUN_TEST(test_add_port_validation_and_table_full);
    RUN_TEST(test_seq_increments_per_uplink);
    RUN_TEST(test_topic_zero_and_overflow_steps);
    RUN_TEST(test_get_stats_null_out_is_noop);
    return UNITY_END();
}
