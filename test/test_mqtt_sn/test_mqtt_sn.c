// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the MQTT-SN v1.2 wire codec (services/iot/mqtt/mqtt_sn): the message
// builders, the Length+MsgType header parser, and the typed payload parsers. Pure
// host tests, with wire-byte assertions checked against the v1.2 spec layout.

#include "services/iot/mqtt/mqtt_sn.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_make_flags()
{
    // DUP, QoS 2, retain, will, clean, short topic name.
    uint8_t f = pc_mqttsn_make_flags(true, 2, true, true, true, MQTTSN_TOPIC_SHORT);
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_FLAG_DUP, f & MQTTSN_FLAG_DUP);
    TEST_ASSERT_EQUAL_UINT8(2, (uint8_t)((f & MQTTSN_FLAG_QOS_MASK) >> MQTTSN_FLAG_QOS_SHIFT));
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_FLAG_RETAIN, f & MQTTSN_FLAG_RETAIN);
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_FLAG_WILL, f & MQTTSN_FLAG_WILL);
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_FLAG_CLEAN, f & MQTTSN_FLAG_CLEAN);
    TEST_ASSERT_EQUAL_UINT8(MQTTSN_TOPIC_SHORT, f & MQTTSN_FLAG_TOPICIDTYPE_MASK);
    // QoS -1 encodes as 0b11.
    TEST_ASSERT_EQUAL_UINT8(3,
                            (uint8_t)((pc_mqttsn_make_flags(false, 3, false, false, false, 0) & MQTTSN_FLAG_QOS_MASK) >>
                                      MQTTSN_FLAG_QOS_SHIFT));
}

// CONNECT bytes: Length, MsgType=0x04, Flags, ProtocolId=0x01, Duration(BE), ClientId.
void test_build_connect_bytes()
{
    uint8_t buf[32];
    uint8_t flags = pc_mqttsn_make_flags(false, 0, false, false, true, MQTTSN_TOPIC_NORMAL); // clean session
    size_t n = pc_mqttsn_build_connect(buf, sizeof(buf), flags, 30, "dev1");
    // total = 1(len) + 1(type) + 1(flags) + 1(protoid) + 2(duration) + 4(clientid) = 10
    TEST_ASSERT_EQUAL_size_t(10, n);
    const uint8_t expect[] = {10, MQTTSN_CONNECT, MQTTSN_FLAG_CLEAN, MQTTSN_PROTOCOL_ID, 0x00, 0x1E, 'd', 'e', 'v',
                              '1'};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

void test_build_publish_bytes()
{
    uint8_t buf[32];
    uint8_t flags = pc_mqttsn_make_flags(false, 1, false, false, false, MQTTSN_TOPIC_NORMAL);
    const uint8_t data[] = {0xDE, 0xAD};
    size_t n = pc_mqttsn_build_publish(buf, sizeof(buf), flags, 0x0007, 0x0001, data, sizeof(data));
    // total = 1+1+1(flags)+2(topic)+2(msgid)+2(data) = 9
    TEST_ASSERT_EQUAL_size_t(9, n);
    const uint8_t expect[] = {9, MQTTSN_PUBLISH, (1 << MQTTSN_FLAG_QOS_SHIFT), 0x00, 0x07, 0x00, 0x01, 0xDE, 0xAD};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

// REGISTER round-trips through the header parser + typed parser.
void test_register_round_trip()
{
    uint8_t buf[32];
    size_t n = pc_mqttsn_build_register(buf, sizeof(buf), 0x0000, 0x0042, "sensors/temp");
    TEST_ASSERT_GREATER_THAN(0, (int)n);

    MqttsnHeader h;
    size_t consumed;
    TEST_ASSERT_TRUE(pc_mqttsn_parse_header(buf, n, &h, &consumed));
    TEST_ASSERT_EQUAL_size_t(n, consumed);
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_REGISTER, h.msg_type);

    uint16_t topic_id, msg_id;
    const char *name;
    size_t name_len;
    TEST_ASSERT_TRUE(pc_mqttsn_parse_register(h.payload, h.payload_len, &topic_id, &msg_id, &name, &name_len));
    TEST_ASSERT_EQUAL_HEX16(0x0000, topic_id);
    TEST_ASSERT_EQUAL_HEX16(0x0042, msg_id);
    TEST_ASSERT_EQUAL_size_t(12, name_len);
    TEST_ASSERT_EQUAL_MEMORY("sensors/temp", name, name_len);
}

void test_parse_connack_regack_suback_publish()
{
    uint8_t rc;
    const uint8_t connack[] = {0x03, MQTTSN_CONNACK, MQTTSN_RC_ACCEPTED};
    MqttsnHeader h;
    size_t c;
    TEST_ASSERT_TRUE(pc_mqttsn_parse_header(connack, sizeof(connack), &h, &c));
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_CONNACK, h.msg_type);
    TEST_ASSERT_TRUE(pc_mqttsn_parse_connack(h.payload, h.payload_len, &rc));
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_RC_ACCEPTED, rc);

    // REGACK: TopicId=0x0009, MsgId=0x0042, rc=accepted.
    const uint8_t regack[] = {0x07, MQTTSN_REGACK, 0x00, 0x09, 0x00, 0x42, MQTTSN_RC_ACCEPTED};
    uint16_t tid, mid;
    TEST_ASSERT_TRUE(pc_mqttsn_parse_header(regack, sizeof(regack), &h, &c));
    TEST_ASSERT_TRUE(pc_mqttsn_parse_regack(h.payload, h.payload_len, &tid, &mid, &rc));
    TEST_ASSERT_EQUAL_HEX16(0x0009, tid);
    TEST_ASSERT_EQUAL_HEX16(0x0042, mid);

    // SUBACK: flags, TopicId=0x0009, MsgId=0x0001, rc=accepted.
    const uint8_t suback[] = {0x08, MQTTSN_SUBACK, (1 << MQTTSN_FLAG_QOS_SHIFT), 0x00, 0x09, 0x00, 0x01, 0x00};
    uint8_t fl;
    TEST_ASSERT_TRUE(pc_mqttsn_parse_header(suback, sizeof(suback), &h, &c));
    TEST_ASSERT_TRUE(pc_mqttsn_parse_suback(h.payload, h.payload_len, &fl, &tid, &mid, &rc));
    TEST_ASSERT_EQUAL_HEX16(0x0009, tid);
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_RC_ACCEPTED, rc);

    // Inbound PUBLISH: flags, TopicId=0x0009, MsgId=0x0000, data "hi".
    const uint8_t pub[] = {0x09, MQTTSN_PUBLISH, 0x00, 0x00, 0x09, 0x00, 0x00, 'h', 'i'};
    const uint8_t *data;
    size_t dlen;
    TEST_ASSERT_TRUE(pc_mqttsn_parse_header(pub, sizeof(pub), &h, &c));
    TEST_ASSERT_TRUE(pc_mqttsn_parse_publish(h.payload, h.payload_len, &fl, &tid, &mid, &data, &dlen));
    TEST_ASSERT_EQUAL_HEX16(0x0009, tid);
    TEST_ASSERT_EQUAL_size_t(2, dlen);
    TEST_ASSERT_EQUAL_MEMORY("hi", data, 2);
}

// The 3-octet Length form (0x01 + big-endian uint16) is used past 255 octets and parses back.
void test_three_octet_length()
{
    static uint8_t buf[600];
    static uint8_t payload[400];
    memset(payload, 0xAB, sizeof(payload));
    size_t n = pc_mqttsn_build_publish(buf, sizeof(buf), 0, 0x0001, 0x0001, payload, sizeof(payload));
    // total = 3(len) + 1(type) + 1(flags) + 2 + 2 + 400 = 409
    TEST_ASSERT_EQUAL_size_t(409, n);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, buf[1]); // 409 >> 8
    TEST_ASSERT_EQUAL_HEX8(0x99, buf[2]); // 409 & 0xFF
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_PUBLISH, buf[3]);

    MqttsnHeader h;
    size_t consumed;
    TEST_ASSERT_TRUE(pc_mqttsn_parse_header(buf, n, &h, &consumed));
    TEST_ASSERT_EQUAL_size_t(409, consumed);
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_PUBLISH, h.msg_type);
    TEST_ASSERT_EQUAL_size_t(405, h.payload_len); // 400 data + 5 header fields
}

void test_optional_fields()
{
    uint8_t buf[16];
    // PINGREQ with no client id is a 2-byte keep-alive.
    TEST_ASSERT_EQUAL_size_t(2, pc_mqttsn_build_pingreq(buf, sizeof(buf), NULL));
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_PINGREQ, buf[1]);
    // DISCONNECT without a duration is 2 bytes; with one it is 4.
    TEST_ASSERT_EQUAL_size_t(2, pc_mqttsn_build_disconnect(buf, sizeof(buf), false, 0));
    TEST_ASSERT_EQUAL_size_t(4, pc_mqttsn_build_disconnect(buf, sizeof(buf), true, 60));
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[2]);
    TEST_ASSERT_EQUAL_HEX8(0x3C, buf[3]); // 60
    // SEARCHGW carries a 1-byte radius.
    TEST_ASSERT_EQUAL_size_t(3, pc_mqttsn_build_searchgw(buf, sizeof(buf), 2));
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_SEARCHGW, buf[1]);
    TEST_ASSERT_EQUAL_HEX8(2, buf[2]);
}

void test_overflow_and_malformed()
{
    uint8_t small[4];
    TEST_ASSERT_EQUAL_size_t(0, pc_mqttsn_build_connect(small, sizeof(small), 0, 0, "toolongclientid"));

    MqttsnHeader h;
    size_t c;
    TEST_ASSERT_FALSE(pc_mqttsn_parse_header(NULL, 0, &h, &c));
    const uint8_t partial[] = {0x09, MQTTSN_PUBLISH, 0x00}; // declares 9 octets, only 3 buffered
    TEST_ASSERT_FALSE(pc_mqttsn_parse_header(partial, sizeof(partial), &h, &c));
    const uint8_t bad_three[] = {0x01, 0x00}; // 3-octet indicator but buffer too short
    TEST_ASSERT_FALSE(pc_mqttsn_parse_header(bad_three, sizeof(bad_three), &h, &c));
    const uint8_t zero_len[] = {0x00, 0x00}; // total length 0 is impossible
    TEST_ASSERT_FALSE(pc_mqttsn_parse_header(zero_len, sizeof(zero_len), &h, &c));
    // A payload too short for its typed parser is rejected.
    const uint8_t short_regack[] = {0x04, MQTTSN_REGACK, 0x00, 0x09};
    TEST_ASSERT_TRUE(pc_mqttsn_parse_header(short_regack, sizeof(short_regack), &h, &c));
    TEST_ASSERT_FALSE(pc_mqttsn_parse_regack(h.payload, h.payload_len, NULL, NULL, NULL));
}

// REGACK and PUBACK share the TopicId+MsgId+ReturnCode layout; build + parse both.
void test_build_regack_puback()
{
    uint8_t buf[16];
    uint16_t tid = 0, mid = 0;
    uint8_t rc = 0;

    size_t n = pc_mqttsn_build_regack(buf, sizeof(buf), 0x0009, 0x0042, MQTTSN_RC_ACCEPTED);
    TEST_ASSERT_EQUAL_size_t(7, n); // 1(len)+1(type)+2+2+1
    const uint8_t reg_expect[] = {7, MQTTSN_REGACK, 0x00, 0x09, 0x00, 0x42, MQTTSN_RC_ACCEPTED};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(reg_expect, buf, n);
    TEST_ASSERT_TRUE(pc_mqttsn_parse_regack(buf + 2, n - 2, &tid, &mid, &rc));
    TEST_ASSERT_EQUAL_HEX16(0x0009, tid);
    TEST_ASSERT_EQUAL_HEX16(0x0042, mid);
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_RC_ACCEPTED, rc);

    n = pc_mqttsn_build_puback(buf, sizeof(buf), 0x0007, 0x0003, 0x01);
    TEST_ASSERT_EQUAL_size_t(7, n);
    const uint8_t pub_expect[] = {7, MQTTSN_PUBACK, 0x00, 0x07, 0x00, 0x03, 0x01};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(pub_expect, buf, n);
    TEST_ASSERT_TRUE(pc_mqttsn_parse_puback(buf + 2, n - 2, &tid, &mid, &rc));
    TEST_ASSERT_EQUAL_HEX16(0x0007, tid);
    TEST_ASSERT_EQUAL_HEX8(0x01, rc);
}

// SUBSCRIBE by topic name and by predefined topic id.
void test_build_subscribe_variants()
{
    uint8_t buf[16];
    uint8_t flags = pc_mqttsn_make_flags(false, 1, false, false, false, MQTTSN_TOPIC_NORMAL);

    size_t n = pc_mqttsn_build_subscribe_name(buf, sizeof(buf), flags, 0x0005, "a/b");
    TEST_ASSERT_EQUAL_size_t(8, n); // 1+1+1(flags)+2(msgid)+3(name)
    const uint8_t nm_expect[] = {8, MQTTSN_SUBSCRIBE, (1 << MQTTSN_FLAG_QOS_SHIFT), 0x00, 0x05, 'a', '/', 'b'};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(nm_expect, buf, n);

    n = pc_mqttsn_build_subscribe_id(buf, sizeof(buf), flags, 0x0006, 0x0102);
    TEST_ASSERT_EQUAL_size_t(7, n); // 1+1+1(flags)+2(msgid)+2(topicid)
    const uint8_t id_expect[] = {7, MQTTSN_SUBSCRIBE, (1 << MQTTSN_FLAG_QOS_SHIFT), 0x00, 0x06, 0x01, 0x02};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(id_expect, buf, n);
}

// PINGREQ may carry a client id (the memcpy path).
void test_pingreq_with_client_id()
{
    uint8_t buf[16];
    size_t n = pc_mqttsn_build_pingreq(buf, sizeof(buf), "node7");
    TEST_ASSERT_EQUAL_size_t(7, n); // 1+1+5
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_PINGREQ, buf[1]);
    TEST_ASSERT_EQUAL_MEMORY("node7", buf + 2, 5);
}

// Every builder rejects a null out (and a null string/data where applicable) and a
// buffer too small for the framed message.
void test_build_guards()
{
    uint8_t buf[32];
    const uint8_t d[2] = {1, 2};
    TEST_ASSERT_EQUAL_size_t(0, pc_mqttsn_build_connect(NULL, 32, 0, 0, "x"));
    TEST_ASSERT_EQUAL_size_t(0, pc_mqttsn_build_register(NULL, 32, 0, 0, "x"));
    TEST_ASSERT_EQUAL_size_t(0, pc_mqttsn_build_regack(NULL, 32, 0, 0, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_mqttsn_build_publish(NULL, 32, 0, 0, 0, d, 2));
    TEST_ASSERT_EQUAL_size_t(0, pc_mqttsn_build_puback(NULL, 32, 0, 0, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_mqttsn_build_subscribe_name(NULL, 32, 0, 0, "x"));
    TEST_ASSERT_EQUAL_size_t(0, pc_mqttsn_build_subscribe_id(NULL, 32, 0, 0, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_mqttsn_build_pingreq(NULL, 32, "x"));
    TEST_ASSERT_EQUAL_size_t(0, pc_mqttsn_build_disconnect(NULL, 32, false, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_mqttsn_build_searchgw(NULL, 32, 0));
    // null string / data
    TEST_ASSERT_EQUAL_size_t(0, pc_mqttsn_build_connect(buf, 32, 0, 0, NULL));
    TEST_ASSERT_EQUAL_size_t(0, pc_mqttsn_build_register(buf, 32, 0, 0, NULL));
    TEST_ASSERT_EQUAL_size_t(0, pc_mqttsn_build_subscribe_name(buf, 32, 0, 0, NULL));
    TEST_ASSERT_EQUAL_size_t(0, pc_mqttsn_build_publish(buf, 32, 0, 0, 0, NULL, 2)); // data_len && !data
    // buffer too small -> frame_header overflow -> 0
    uint8_t tiny[4];
    TEST_ASSERT_EQUAL_size_t(0, pc_mqttsn_build_register(tiny, sizeof(tiny), 0, 0, "sensors"));
    TEST_ASSERT_EQUAL_size_t(0, pc_mqttsn_build_regack(tiny, sizeof(tiny), 0, 0, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_mqttsn_build_puback(tiny, sizeof(tiny), 0, 0, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_mqttsn_build_publish(tiny, sizeof(tiny), 0, 0, 0, d, 2));
    TEST_ASSERT_EQUAL_size_t(0, pc_mqttsn_build_subscribe_name(tiny, sizeof(tiny), 0, 0, "topic"));
    TEST_ASSERT_EQUAL_size_t(0, pc_mqttsn_build_subscribe_id(tiny, sizeof(tiny), 0, 0, 0));
    TEST_ASSERT_EQUAL_size_t(0, pc_mqttsn_build_disconnect(tiny, 2, true, 0)); // needs 4
    TEST_ASSERT_EQUAL_size_t(0, pc_mqttsn_build_searchgw(tiny, 2, 0));         // needs 3
    TEST_ASSERT_EQUAL_size_t(0, pc_mqttsn_build_pingreq(tiny, 2, "toolong"));  // needs 9
}

// Typed payload parsers reject payloads shorter than their fixed layout.
void test_parse_typed_rejections()
{
    uint16_t tid = 0, mid = 0;
    uint8_t rc = 0, fl = 0;
    const uint8_t *data = NULL;
    size_t dlen = 0, nlen = 0;
    const char *name = NULL;
    const uint8_t one[6] = {0};

    TEST_ASSERT_FALSE(pc_mqttsn_parse_connack(NULL, 1, &rc));
    TEST_ASSERT_FALSE(pc_mqttsn_parse_connack(one, 0, &rc));                           // len < 1
    TEST_ASSERT_FALSE(pc_mqttsn_parse_regack(one, 4, &tid, &mid, &rc));                // len < 5
    TEST_ASSERT_FALSE(pc_mqttsn_parse_puback(one, 4, &tid, &mid, &rc));                // len < 5
    TEST_ASSERT_FALSE(pc_mqttsn_parse_suback(one, 5, &fl, &tid, &mid, &rc));           // len < 6
    TEST_ASSERT_FALSE(pc_mqttsn_parse_publish(one, 4, &fl, &tid, &mid, &data, &dlen)); // len < 5
    TEST_ASSERT_FALSE(pc_mqttsn_parse_register(one, 3, &tid, &mid, &name, &nlen));     // len < 4
    TEST_ASSERT_TRUE(pc_mqttsn_parse_connack(one, 1, &rc));                            // exactly 1 is valid
}

// Every typed payload parser refuses a null payload pointer, independently of the length check.
void test_parse_typed_null_payload()
{
    uint16_t tid = 0, mid = 0;
    uint8_t rc = 0, fl = 0;
    const uint8_t *data = NULL;
    size_t dlen = 0, nlen = 0;
    const char *name = NULL;

    TEST_ASSERT_FALSE(pc_mqttsn_parse_connack(NULL, 8, &rc));
    TEST_ASSERT_FALSE(pc_mqttsn_parse_regack(NULL, 8, &tid, &mid, &rc));
    TEST_ASSERT_FALSE(pc_mqttsn_parse_puback(NULL, 8, &tid, &mid, &rc));
    TEST_ASSERT_FALSE(pc_mqttsn_parse_suback(NULL, 8, &fl, &tid, &mid, &rc));
    TEST_ASSERT_FALSE(pc_mqttsn_parse_publish(NULL, 8, &fl, &tid, &mid, &data, &dlen));
    TEST_ASSERT_FALSE(pc_mqttsn_parse_register(NULL, 8, &tid, &mid, &name, &nlen));
}

// Every output of every typed parser is optional: a caller that only wants to know whether the
// payload is well-formed may omit all of them.
void test_parse_typed_outputs_are_optional()
{
    const uint8_t p[8] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    TEST_ASSERT_TRUE(pc_mqttsn_parse_connack(p, sizeof(p), NULL));
    TEST_ASSERT_TRUE(pc_mqttsn_parse_regack(p, sizeof(p), NULL, NULL, NULL));
    TEST_ASSERT_TRUE(pc_mqttsn_parse_puback(p, sizeof(p), NULL, NULL, NULL));
    TEST_ASSERT_TRUE(pc_mqttsn_parse_suback(p, sizeof(p), NULL, NULL, NULL, NULL));
    TEST_ASSERT_TRUE(pc_mqttsn_parse_publish(p, sizeof(p), NULL, NULL, NULL, NULL, NULL));
    TEST_ASSERT_TRUE(pc_mqttsn_parse_register(p, sizeof(p), NULL, NULL, NULL, NULL));
}

// pc_mqttsn_parse_header() refuses a null header output, a null consumed output, and a buffer too
// short to hold even the Length and MsgType octets.
void test_parse_header_output_guards()
{
    const uint8_t frame[4] = {4, MQTTSN_PUBACK, 0x00, 0x01};
    MqttsnHeader h;
    size_t c = 0;
    TEST_ASSERT_FALSE(pc_mqttsn_parse_header(frame, sizeof(frame), NULL, &c));
    TEST_ASSERT_FALSE(pc_mqttsn_parse_header(frame, sizeof(frame), &h, NULL));
    TEST_ASSERT_FALSE(pc_mqttsn_parse_header(frame, 1, &h, &c)); // shorter than Length + MsgType
    TEST_ASSERT_TRUE(pc_mqttsn_parse_header(frame, sizeof(frame), &h, &c));
    TEST_ASSERT_EQUAL_size_t(4, c);
}

// A PUBLISH may carry no data at all, and a body that would push the message past the 16-bit
// Length field is refused before a single octet is written.
void test_publish_empty_and_oversized_body()
{
    uint8_t buf[16];
    size_t n = pc_mqttsn_build_publish(buf, sizeof(buf), 0x20, 0x0009, 0x0001, NULL, 0);
    TEST_ASSERT_EQUAL_size_t(7, n); // 1(len)+1(type)+1(flags)+2(topic)+2(msgid), no data
    const uint8_t expect[] = {7, MQTTSN_PUBLISH, 0x20, 0x00, 0x09, 0x00, 0x01};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);

    uint8_t fl = 0;
    uint16_t tid = 0, mid = 0;
    const uint8_t *data = NULL;
    size_t dlen = 99;
    TEST_ASSERT_TRUE(pc_mqttsn_parse_publish(buf + 2, n - 2, &fl, &tid, &mid, &data, &dlen));
    TEST_ASSERT_EQUAL_HEX16(0x0009, tid);
    TEST_ASSERT_EQUAL_size_t(0, dlen);

    // 70000 data octets cannot be described by the 16-bit Length field.
    const uint8_t d[2] = {1, 2};
    TEST_ASSERT_EQUAL_size_t(0, pc_mqttsn_build_publish(buf, sizeof(buf), 0, 1, 1, d, 70000));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_typed_null_payload);
    RUN_TEST(test_parse_typed_outputs_are_optional);
    RUN_TEST(test_parse_header_output_guards);
    RUN_TEST(test_publish_empty_and_oversized_body);
    RUN_TEST(test_make_flags);
    RUN_TEST(test_build_connect_bytes);
    RUN_TEST(test_build_publish_bytes);
    RUN_TEST(test_register_round_trip);
    RUN_TEST(test_parse_connack_regack_suback_publish);
    RUN_TEST(test_three_octet_length);
    RUN_TEST(test_optional_fields);
    RUN_TEST(test_overflow_and_malformed);
    RUN_TEST(test_build_regack_puback);
    RUN_TEST(test_build_subscribe_variants);
    RUN_TEST(test_pingreq_with_client_id);
    RUN_TEST(test_build_guards);
    RUN_TEST(test_parse_typed_rejections);
    return UNITY_END();
}
