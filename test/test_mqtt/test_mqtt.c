// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host unit tests for the MQTT 3.1.1 packet codec (env:native_mqtt).

#include "services/iot/mqtt/mqtt.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// --- Remaining Length varint (MQTT 3.1.1 2.2.3) ---

static void rl_roundtrip(uint32_t v, size_t expect_bytes)
{
    uint8_t b[4];
    size_t n = pc_mqtt_encode_remlen(b, v);
    TEST_ASSERT_EQUAL_size_t(expect_bytes, n);
    uint32_t out = 0;
    size_t used = 0;
    TEST_ASSERT_TRUE(pc_mqtt_decode_remlen(b, n, &out, &used));
    TEST_ASSERT_EQUAL_UINT32(v, out);
    TEST_ASSERT_EQUAL_size_t(n, used);
}

void test_remlen_boundaries()
{
    rl_roundtrip(0, 1);
    rl_roundtrip(127, 1);
    rl_roundtrip(128, 2);
    rl_roundtrip(16383, 2);
    rl_roundtrip(16384, 3);
    rl_roundtrip(2097151, 3);
    rl_roundtrip(2097152, 4);
    rl_roundtrip(268435455, 4);
}

void test_remlen_too_big()
{
    uint8_t b[4];
    TEST_ASSERT_EQUAL_size_t(0, pc_mqtt_encode_remlen(b, 268435456u));
}

void test_remlen_decode_incomplete()
{
    uint8_t b[2] = {0x80, 0x80}; // both continuation, truncated
    uint32_t v;
    size_t used;
    TEST_ASSERT_FALSE(pc_mqtt_decode_remlen(b, 2, &v, &used));
}

void test_remlen_decode_malformed()
{
    uint8_t b[5] = {0x80, 0x80, 0x80, 0x80, 0x01}; // 5 bytes = malformed
    uint32_t v;
    size_t used;
    TEST_ASSERT_FALSE(pc_mqtt_decode_remlen(b, 5, &v, &used));
}

// --- CONNECT ---

void test_connect_minimal()
{
    MqttConnectOpts o;
    memset(&o, 0, sizeof(o));
    o.client_id = "dev1";
    o.clean_session = true;
    o.keepalive_s = 30;
    uint8_t buf[64];
    size_t len = pc_mqtt_build_connect(buf, sizeof(buf), &o);
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_EQUAL_HEX8(0x10, buf[0]); // CONNECT, flags 0

    uint8_t type, flags;
    uint32_t rl;
    size_t hl;
    TEST_ASSERT_TRUE(pc_mqtt_parse_fixed_header(buf, len, &type, &flags, &rl, &hl));
    TEST_ASSERT_EQUAL(MQTT_CONNECT, type);
    const uint8_t *b = buf + hl;
    TEST_ASSERT_EQUAL_UINT8(0, b[0]);
    TEST_ASSERT_EQUAL_UINT8(4, b[1]);
    TEST_ASSERT_EQUAL_MEMORY("MQTT", b + 2, 4);
    TEST_ASSERT_EQUAL_HEX8(0x04, b[6]); // protocol level 4
    TEST_ASSERT_EQUAL_HEX8(0x02, b[7]); // clean session only
    TEST_ASSERT_EQUAL_UINT8(30, b[9]);  // keepalive low byte
    TEST_ASSERT_EQUAL_MEMORY("\x00\x04"
                             "dev1",
                             b + 10, 6); // client id field
}

void test_connect_full()
{
    MqttConnectOpts o;
    memset(&o, 0, sizeof(o));
    o.client_id = "c";
    o.user = "u";
    o.pass = "p";
    o.clean_session = true;
    o.will_topic = "w/t";
    o.will_msg = (const uint8_t *)"bye";
    o.will_len = 3;
    o.will_qos = 1;
    o.will_retain = true;
    uint8_t buf[128];
    size_t len = pc_mqtt_build_connect(buf, sizeof(buf), &o);
    TEST_ASSERT_GREATER_THAN(0, len);
    uint8_t type, flags;
    uint32_t rl;
    size_t hl;
    TEST_ASSERT_TRUE(pc_mqtt_parse_fixed_header(buf, len, &type, &flags, &rl, &hl));
    // flags: clean(0x02)|will(0x04)|willQoS1(0x08)|willRetain(0x20)|user(0x80)|pass(0x40) = 0xEE
    TEST_ASSERT_EQUAL_HEX8(0xEE, buf[hl + 7]);
}

// --- PUBLISH ---

void test_publish_qos0_roundtrip()
{
    uint8_t buf[64];
    size_t len = pc_mqtt_build_publish(buf, sizeof(buf), "a/b", (const uint8_t *)"hi", 2, 0, 0, false, false);
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_EQUAL_HEX8(0x30, buf[0]); // PUBLISH, qos0

    uint8_t type, flags;
    uint32_t rl;
    size_t hl;
    TEST_ASSERT_TRUE(pc_mqtt_parse_fixed_header(buf, len, &type, &flags, &rl, &hl));
    TEST_ASSERT_EQUAL(MQTT_PUBLISH, type);
    char topic[32];
    size_t tlen, plen;
    const uint8_t *payload;
    uint16_t pid;
    TEST_ASSERT_TRUE(pc_mqtt_parse_publish(buf + hl, rl, flags, topic, sizeof(topic), &tlen, &payload, &plen, &pid));
    TEST_ASSERT_EQUAL_STRING("a/b", topic);
    TEST_ASSERT_EQUAL_size_t(2, plen);
    TEST_ASSERT_EQUAL_MEMORY("hi", payload, 2);
    TEST_ASSERT_EQUAL_UINT16(0, pid);
}

void test_publish_qos1_flags_and_id()
{
    uint8_t buf[64];
    size_t len = pc_mqtt_build_publish(buf, sizeof(buf), "t", (const uint8_t *)"x", 1, 1, 0x1234, true, true);
    TEST_ASSERT_GREATER_THAN(0, len);
    // PUBLISH(0x30) | dup(0x08) | qos1(0x02) | retain(0x01) = 0x3B
    TEST_ASSERT_EQUAL_HEX8(0x3B, buf[0]);
    uint8_t type, flags;
    uint32_t rl;
    size_t hl;
    TEST_ASSERT_TRUE(pc_mqtt_parse_fixed_header(buf, len, &type, &flags, &rl, &hl));
    char topic[8];
    size_t tlen, plen;
    const uint8_t *payload;
    uint16_t pid;
    TEST_ASSERT_TRUE(pc_mqtt_parse_publish(buf + hl, rl, flags, topic, sizeof(topic), &tlen, &payload, &plen, &pid));
    TEST_ASSERT_EQUAL_UINT16(0x1234, pid);
    TEST_ASSERT_EQUAL_size_t(1, plen);
}

void test_publish_topic_overflow_rejected()
{
    uint8_t buf[64];
    size_t len = pc_mqtt_build_publish(buf, sizeof(buf), "abcdef", (const uint8_t *)"", 0, 0, 0, false, false);
    uint8_t type, flags;
    uint32_t rl;
    size_t hl;
    pc_mqtt_parse_fixed_header(buf, len, &type, &flags, &rl, &hl);
    char topic[4]; // too small for "abcdef"
    size_t tlen, plen;
    const uint8_t *payload;
    uint16_t pid;
    TEST_ASSERT_FALSE(pc_mqtt_parse_publish(buf + hl, rl, flags, topic, sizeof(topic), &tlen, &payload, &plen, &pid));
}

// MQTT-3.3.1-4: a PUBLISH with both QoS bits set (QoS 3) is malformed and must be
// rejected (the handler then closes the connection). Build a valid QoS-1 PUBLISH and
// flip the flags to QoS 3.
void test_publish_qos3_rejected()
{
    uint8_t buf[64];
    size_t len = pc_mqtt_build_publish(buf, sizeof(buf), "t", (const uint8_t *)"x", 1, 1, 1, false, false);
    uint8_t type, flags;
    uint32_t rl;
    size_t hl;
    pc_mqtt_parse_fixed_header(buf, len, &type, &flags, &rl, &hl);
    flags |= 0x06; // force both QoS bits (bits 1-2) set
    char topic[16];
    size_t tlen, plen;
    const uint8_t *payload;
    uint16_t pid;
    TEST_ASSERT_FALSE(pc_mqtt_parse_publish(buf + hl, rl, flags, topic, sizeof(topic), &tlen, &payload, &plen, &pid));
}

// MQTT-3.3.2-2: a PUBLISH Topic Name MUST NOT contain wildcard characters.
void test_publish_wildcard_topic_rejected()
{
    uint8_t buf[64];
    TEST_ASSERT_EQUAL_size_t(
        0, pc_mqtt_build_publish(buf, sizeof(buf), "a/+/b", (const uint8_t *)"x", 1, 0, 0, false, false));
    TEST_ASSERT_EQUAL_size_t(
        0, pc_mqtt_build_publish(buf, sizeof(buf), "a/#", (const uint8_t *)"x", 1, 0, 0, false, false));
    // A plain topic with no wildcards still builds.
    TEST_ASSERT_GREATER_THAN(
        0, pc_mqtt_build_publish(buf, sizeof(buf), "a/b/c", (const uint8_t *)"x", 1, 0, 0, false, false));
}

// MQTT 1.5.3: a UTF-8 encoded string MUST be well-formed and MUST NOT contain U+0000.
void test_publish_topic_nul_or_bad_utf8_rejected()
{
    char topic[16];
    size_t tlen, plen;
    const uint8_t *payload;
    uint16_t pid;
    // topic length 2, bytes {0xC3,0x28} = invalid UTF-8 sequence, qos0 (flags 0).
    const uint8_t bad_utf8[] = {0x00, 0x02, 0xC3, 0x28};
    TEST_ASSERT_FALSE(
        pc_mqtt_parse_publish(bad_utf8, sizeof(bad_utf8), 0, topic, sizeof(topic), &tlen, &payload, &plen, &pid));
    // topic length 2, bytes {'a',0x00} = embedded NUL.
    const uint8_t embedded_nul[] = {0x00, 0x02, 'a', 0x00};
    TEST_ASSERT_FALSE(pc_mqtt_parse_publish(embedded_nul, sizeof(embedded_nul), 0, topic, sizeof(topic), &tlen,
                                            &payload, &plen, &pid));
    // A well-formed topic of the same shape still parses.
    const uint8_t ok[] = {0x00, 0x03, 'a', '/', 'b'};
    TEST_ASSERT_TRUE(pc_mqtt_parse_publish(ok, sizeof(ok), 0, topic, sizeof(topic), &tlen, &payload, &plen, &pid));
    TEST_ASSERT_EQUAL_STRING("a/b", topic);
}

// --- SUBSCRIBE / UNSUBSCRIBE ---

void test_subscribe()
{
    uint8_t buf[64];
    size_t len = pc_mqtt_build_subscribe(buf, sizeof(buf), 10, "s/#", 1);
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_EQUAL_HEX8(0x82, buf[0]); // SUBSCRIBE, required flags 0010
    uint8_t type, flags;
    uint32_t rl;
    size_t hl;
    TEST_ASSERT_TRUE(pc_mqtt_parse_fixed_header(buf, len, &type, &flags, &rl, &hl));
    const uint8_t *b = buf + hl;
    TEST_ASSERT_EQUAL_UINT16(10, (uint16_t)((b[0] << 8) | b[1]));
    TEST_ASSERT_EQUAL_MEMORY("\x00\x03"
                             "s/#",
                             b + 2, 5);
    TEST_ASSERT_EQUAL_UINT8(1, b[7]); // requested QoS
}

void test_unsubscribe()
{
    uint8_t buf[64];
    size_t len = pc_mqtt_build_unsubscribe(buf, sizeof(buf), 11, "s/#");
    TEST_ASSERT_GREATER_THAN(0, len);
    TEST_ASSERT_EQUAL_HEX8(0xA2, buf[0]); // UNSUBSCRIBE, required flags 0010
}

// --- ACKs / CONNACK / SUBACK / ping / disconnect ---

void test_ack_packets()
{
    uint8_t buf[8];
    TEST_ASSERT_EQUAL_size_t(4, pc_mqtt_build_ack(buf, sizeof(buf), MQTT_PUBACK, 0x2222));
    TEST_ASSERT_EQUAL_HEX8(0x40, buf[0]);
    TEST_ASSERT_EQUAL_UINT16(0x2222, pc_mqtt_parse_ack(buf + 2, 2));

    TEST_ASSERT_EQUAL_size_t(4, pc_mqtt_build_ack(buf, sizeof(buf), MQTT_PUBREL, 0x3333));
    TEST_ASSERT_EQUAL_HEX8(0x62, buf[0]); // PUBREL requires flags 0010
}

void test_connack()
{
    uint8_t ok[2] = {0x01, 0x00};
    bool sp = false;
    TEST_ASSERT_EQUAL_INT(0, pc_mqtt_parse_connack(ok, 2, &sp));
    TEST_ASSERT_TRUE(sp);
    uint8_t bad[2] = {0x00, 0x05};
    TEST_ASSERT_EQUAL_INT(5, pc_mqtt_parse_connack(bad, 2, &sp));
    TEST_ASSERT_FALSE(sp);
    TEST_ASSERT_EQUAL_INT(-1, pc_mqtt_parse_connack(bad, 1, &sp));
}

void test_suback()
{
    uint8_t b[3] = {0x00, 0x0A, 0x01}; // pid 10, granted QoS 1
    uint16_t pid;
    uint8_t rc;
    TEST_ASSERT_TRUE(pc_mqtt_parse_suback(b, 3, &pid, &rc));
    TEST_ASSERT_EQUAL_UINT16(10, pid);
    TEST_ASSERT_EQUAL_UINT8(1, rc);
}

void test_ping_disconnect()
{
    uint8_t buf[4];
    TEST_ASSERT_EQUAL_size_t(2, pc_mqtt_build_pingreq(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8(0xC0, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[1]);
    TEST_ASSERT_EQUAL_size_t(2, pc_mqtt_build_disconnect(buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_HEX8(0xE0, buf[0]);
}

void test_fixed_header_multibyte_remlen()
{
    // Remaining length 300 -> 2-byte field {0xAC, 0x02}.
    uint8_t buf[4] = {0x30, 0xAC, 0x02};
    uint8_t type, flags;
    uint32_t rl;
    size_t hl;
    TEST_ASSERT_TRUE(pc_mqtt_parse_fixed_header(buf, 3, &type, &flags, &rl, &hl));
    TEST_ASSERT_EQUAL_UINT32(300, rl);
    TEST_ASSERT_EQUAL_size_t(3, hl);
}

// Every builder rejects null args / bad QoS, an over-large body, and an output buffer
// too small to hold the composed packet.
void test_build_guards_and_overflow()
{
    uint8_t out[64];
    static char big_topic[1030];
    memset(big_topic, 'a', sizeof(big_topic) - 1);
    big_topic[sizeof(big_topic) - 1] = '\0'; // > PC_MQTT_BUF_SIZE (1024)

    MqttConnectOpts o;
    memset(&o, 0, sizeof(o));
    o.client_id = "c";
    TEST_ASSERT_EQUAL_UINT(0, pc_mqtt_build_connect(NULL, sizeof(out), &o));
    TEST_ASSERT_EQUAL_UINT(0, pc_mqtt_build_connect(out, sizeof(out), NULL));
    MqttConnectOpts no_id = o;
    no_id.client_id = NULL;
    TEST_ASSERT_EQUAL_UINT(0, pc_mqtt_build_connect(out, sizeof(out), &no_id));
    // Oversized will payload overflows the body scratch.
    MqttConnectOpts wo = o;
    wo.will_topic = "w";
    wo.will_len = 2000;
    TEST_ASSERT_EQUAL_UINT(0, pc_mqtt_build_connect(out, sizeof(out), &wo));
    // A valid CONNECT that does not fit the output buffer (compose cap check).
    TEST_ASSERT_EQUAL_UINT(0, pc_mqtt_build_connect(out, 2, &o));

    TEST_ASSERT_EQUAL_UINT(0, pc_mqtt_build_publish(NULL, sizeof(out), "t", NULL, 0, 0, 0, false, false));
    TEST_ASSERT_EQUAL_UINT(0, pc_mqtt_build_publish(out, sizeof(out), "t", NULL, 0, 3, 0, false, false));    // qos>2
    TEST_ASSERT_EQUAL_UINT(0, pc_mqtt_build_publish(out, sizeof(out), "t", NULL, 2000, 0, 0, false, false)); // body
    TEST_ASSERT_EQUAL_UINT(0,
                           pc_mqtt_build_publish(out, 2, "topic", (const uint8_t *)"hi", 2, 0, 0, false, false)); // cap

    TEST_ASSERT_EQUAL_UINT(0, pc_mqtt_build_subscribe(NULL, sizeof(out), 1, "t", 0));
    TEST_ASSERT_EQUAL_UINT(0, pc_mqtt_build_subscribe(out, sizeof(out), 1, "t", 3));       // qos>2
    TEST_ASSERT_EQUAL_UINT(0, pc_mqtt_build_subscribe(out, sizeof(out), 1, big_topic, 0)); // body overflow

    TEST_ASSERT_EQUAL_UINT(0, pc_mqtt_build_unsubscribe(NULL, sizeof(out), 1, "t"));
    TEST_ASSERT_EQUAL_UINT(0, pc_mqtt_build_unsubscribe(out, sizeof(out), 1, big_topic)); // body overflow

    TEST_ASSERT_EQUAL_UINT(0, pc_mqtt_build_ack(NULL, 4, (MqttType)4, 1));
    TEST_ASSERT_EQUAL_UINT(0, pc_mqtt_build_ack(out, 3, (MqttType)4, 1)); // cap < 4
    TEST_ASSERT_EQUAL_UINT(0, pc_mqtt_build_pingreq(NULL, 2));
    TEST_ASSERT_EQUAL_UINT(0, pc_mqtt_build_pingreq(out, 1)); // cap < 2
    TEST_ASSERT_EQUAL_UINT(0, pc_mqtt_build_disconnect(NULL, 2));
    TEST_ASSERT_EQUAL_UINT(0, pc_mqtt_build_disconnect(out, 1)); // cap < 2
}

// The parsers reject truncated / malformed inputs.
void test_parse_guards()
{
    uint8_t type, flags;
    uint32_t rl;
    size_t hl;
    uint8_t one[1] = {0x30};
    TEST_ASSERT_FALSE(pc_mqtt_parse_fixed_header(one, 1, &type, &flags, &rl, &hl)); // avail < 2
    uint8_t bad_rl[5] = {0x30, 0x80, 0x80, 0x80, 0x80};
    TEST_ASSERT_FALSE(pc_mqtt_parse_fixed_header(bad_rl, 5, &type, &flags, &rl, &hl)); // malformed remlen

    char topic[32];
    size_t tl, pl;
    const uint8_t *pay;
    uint16_t pid;
    TEST_ASSERT_FALSE(pc_mqtt_parse_publish(NULL, 0, 0, topic, sizeof(topic), &tl, &pay, &pl, &pid));
    uint8_t claim[2] = {0x00, 0x10}; // tlen=16 but remaining_len=2
    TEST_ASSERT_FALSE(pc_mqtt_parse_publish(claim, 2, 0, topic, sizeof(topic), &tl, &pay, &pl, &pid));
    uint8_t q1[4] = {0x00, 0x02, 'a', 'b'}; // qos1, topic fills the buffer, no room for packet id
    TEST_ASSERT_FALSE(pc_mqtt_parse_publish(q1, 4, 0x02, topic, sizeof(topic), &tl, &pay, &pl, &pid));

    TEST_ASSERT_EQUAL_UINT16(0, pc_mqtt_parse_ack(NULL, 0));
    uint16_t spid;
    uint8_t rc;
    uint8_t two[2] = {0, 0};
    TEST_ASSERT_FALSE(pc_mqtt_parse_suback(two, 2, &spid, &rc)); // remaining_len < 3
}

// On a host build the transport entry points are inert stubs that fail closed.
void test_host_transport_stubs()
{
    pc_mqtt_set_message_cb(NULL);
    MqttConnectOpts o;
    memset(&o, 0, sizeof(o));
    o.client_id = "c";
    TEST_ASSERT_FALSE(pc_mqtt_connect("h", 1883, false, &o));
    TEST_ASSERT_FALSE(pc_mqtt_publish("t", NULL, 0, 0, false));
    TEST_ASSERT_FALSE(pc_mqtt_subscribe("t", 0));
    TEST_ASSERT_FALSE(pc_mqtt_unsubscribe("t"));
    TEST_ASSERT_FALSE(pc_mqtt_loop());
    TEST_ASSERT_FALSE(pc_mqtt_connected());
    pc_mqtt_disconnect();
}

// Builders reject a null topic pointer even when the output buffer is valid
// (the `!out || !topic || ...` guards need out valid / topic null to hit the
// middle clause), and an empty-but-non-null field still round-trips through
// put_field's zero-length branch (no memcpy of a zero-length payload).
void test_build_null_topic_guards_and_empty_field()
{
    uint8_t out[64];
    TEST_ASSERT_EQUAL_UINT(0, pc_mqtt_build_publish(out, sizeof(out), NULL, NULL, 0, 0, 0, false, false));
    TEST_ASSERT_EQUAL_UINT(0, pc_mqtt_build_subscribe(out, sizeof(out), 1, NULL, 0));
    TEST_ASSERT_EQUAL_UINT(0, pc_mqtt_build_unsubscribe(out, sizeof(out), 1, NULL));

    // Empty (non-null) topic: put_field writes just the 2-byte zero length, no memcpy.
    size_t len = pc_mqtt_build_unsubscribe(out, sizeof(out), 1, "");
    TEST_ASSERT_GREATER_THAN(0, len);
    uint8_t type, flags;
    uint32_t rl;
    size_t hl;
    TEST_ASSERT_TRUE(pc_mqtt_parse_fixed_header(out, len, &type, &flags, &rl, &hl));
    TEST_ASSERT_EQUAL_UINT32(4, rl); // pid(2) + empty topic length prefix(2)
}

// Parsers reject a too-short remaining_len even with a non-null buffer, and
// treat null optional out-params as "caller doesn't want this" rather than
// crashing.
void test_parse_short_len_and_null_outparam_guards()
{
    char topic[8];
    size_t tl, pl;
    const uint8_t *pay;
    uint16_t pid;
    uint8_t one[1] = {0x00};
    TEST_ASSERT_FALSE(pc_mqtt_parse_publish(one, 1, 0, topic, sizeof(topic), &tl, &pay, &pl, &pid));

    uint8_t ack_one[1] = {0x00};
    TEST_ASSERT_EQUAL_UINT16(0, pc_mqtt_parse_ack(ack_one, 1));

    uint8_t ok[2] = {0x00, 0x00};
    TEST_ASSERT_EQUAL_INT(-1, pc_mqtt_parse_connack(NULL, 2, NULL));
    TEST_ASSERT_EQUAL_INT(0, pc_mqtt_parse_connack(ok, 2, NULL)); // session_present output not requested

    uint8_t sb[3] = {0x00, 0x0A, 0x01};
    TEST_ASSERT_FALSE(pc_mqtt_parse_suback(NULL, 3, &pid, NULL));
    TEST_ASSERT_TRUE(pc_mqtt_parse_suback(sb, 3, NULL, NULL)); // caller wants neither out-param
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_build_guards_and_overflow);
    RUN_TEST(test_parse_guards);
    RUN_TEST(test_build_null_topic_guards_and_empty_field);
    RUN_TEST(test_parse_short_len_and_null_outparam_guards);
    RUN_TEST(test_host_transport_stubs);
    RUN_TEST(test_remlen_boundaries);
    RUN_TEST(test_remlen_too_big);
    RUN_TEST(test_remlen_decode_incomplete);
    RUN_TEST(test_remlen_decode_malformed);
    RUN_TEST(test_connect_minimal);
    RUN_TEST(test_connect_full);
    RUN_TEST(test_publish_qos0_roundtrip);
    RUN_TEST(test_publish_qos1_flags_and_id);
    RUN_TEST(test_publish_topic_overflow_rejected);
    RUN_TEST(test_publish_qos3_rejected);
    RUN_TEST(test_publish_wildcard_topic_rejected);
    RUN_TEST(test_publish_topic_nul_or_bad_utf8_rejected);
    RUN_TEST(test_subscribe);
    RUN_TEST(test_unsubscribe);
    RUN_TEST(test_ack_packets);
    RUN_TEST(test_connack);
    RUN_TEST(test_suback);
    RUN_TEST(test_ping_disconnect);
    RUN_TEST(test_fixed_header_multibyte_remlen);
    return UNITY_END();
}
