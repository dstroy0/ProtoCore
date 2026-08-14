// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the MQTT Control Packet codec (services/iot/mqtt/mqtt.h).
//
// The governing document is "MQTT Version 3.1.1", an OASIS Standard of 29 October 2014, not an IETF
// RFC. It publishes the octets of nearly everything asserted here: Table 2.1 the type values,
// Table 2.2 the reserved flag bits, Table 2.4 the Remaining Length boundaries with their encodings,
// Figures 3.2 and 3.3 the CONNECT Protocol Name and Level, Figure 3.6 a worked variable header,
// Figure 3.11 a worked PUBLISH variable header, Figure 3.23 a worked SUBSCRIBE payload, and
// Table 3.1 the Connect Return codes.
//
// test_table_2_4_remaining_length_boundaries is load-bearing: sec 2.2.3 Table 2.4 prints each width's
// first and last value with the exact octets beside it, and Remaining Length is what separates one
// Control Packet from the next on a byte stream, so an off-by-one there desynchronizes the session.

#include "services/iot/mqtt/mqtt.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static uint8_t g_out[512];
static uint8_t g_body[512];
static char g_topic[64];

static void bind_buffers(void)
{
    Mqtt.buf.out = g_out;
    Mqtt.buf.cap = sizeof(g_out);
    Mqtt.buf.body = g_body;
    Mqtt.buf.body_cap = sizeof(g_body);
    memset(g_out, 0, sizeof(g_out));
}

// MQTT 3.1.1 sec 2.2.3 Table 2.4 "Size of Remaining Length field", verbatim: each row's From and To
// value with the octets the table prints for it.
void test_table_2_4_remaining_length_boundaries(void)
{
    struct
    {
        uint32_t value;
        size_t len;
        uint8_t octets[4];
    } static const TABLE[] = {
        {0u, 1, {0x00, 0, 0, 0}},                        // 1 digit, From
        {127u, 1, {0x7F, 0, 0, 0}},                      // 1 digit, To
        {128u, 2, {0x80, 0x01, 0, 0}},                   // 2 digits, From
        {16383u, 2, {0xFF, 0x7F, 0, 0}},                 // 2 digits, To
        {16384u, 3, {0x80, 0x80, 0x01, 0}},              // 3 digits, From
        {2097151u, 3, {0xFF, 0xFF, 0x7F, 0}},            // 3 digits, To
        {2097152u, 4, {0x80, 0x80, 0x80, 0x01}},         // 4 digits, From
        {268435455u, 4, {0xFF, 0xFF, 0xFF, 0x7F}},       // 4 digits, To
    };
    TEST_ASSERT_EQUAL_UINT32(268435455u, PROTOCORE_MQTT_REMAINING_LENGTH_MAX);

    for (size_t i = 0; i < sizeof(TABLE) / sizeof(TABLE[0]); i++)
    {
        bind_buffers();
        Mqtt.packet.remaining_length = TABLE[i].value;
        Mqtt.encode_remaining_length(Mqtt.internal);
        TEST_ASSERT_TRUE(Mqtt.ok);
        TEST_ASSERT_EQUAL_UINT(TABLE[i].len, Mqtt.n);
        TEST_ASSERT_EQUAL_UINT8_ARRAY(TABLE[i].octets, g_out, TABLE[i].len);

        Mqtt.buf.in = TABLE[i].octets;
        Mqtt.buf.avail = 4;
        Mqtt.packet.remaining_length = 0xFFFFFFFFu;
        Mqtt.decode_remaining_length(Mqtt.internal);
        TEST_ASSERT_TRUE(Mqtt.ok);
        TEST_ASSERT_EQUAL_UINT32(TABLE[i].value, Mqtt.packet.remaining_length);
        TEST_ASSERT_EQUAL_UINT(TABLE[i].len, Mqtt.n);
    }
}

// sec 2.2.3 non normative comment: "the number 64 decimal is encoded as a single byte, decimal value
// 64, hexadecimal 0x40. The number 321 decimal (= 65 + 2*128) is encoded as two bytes, least
// significant first. The first byte is 65+128 = 193 ... The second byte is 2."
void test_remaining_length_worked_examples(void)
{
    bind_buffers();
    Mqtt.packet.remaining_length = 64u;
    Mqtt.encode_remaining_length(Mqtt.internal);
    TEST_ASSERT_EQUAL_UINT(1u, Mqtt.n);
    TEST_ASSERT_EQUAL_HEX8(0x40, g_out[0]);

    bind_buffers();
    Mqtt.packet.remaining_length = 321u;
    Mqtt.encode_remaining_length(Mqtt.internal);
    TEST_ASSERT_EQUAL_UINT(2u, Mqtt.n);
    TEST_ASSERT_EQUAL_UINT8(193, g_out[0]);
    TEST_ASSERT_EQUAL_UINT8(2, g_out[1]);
}

// sec 2.2.3: "The maximum number of bytes in the Remaining Length field is four", and the decoder's
// published algorithm throws on a fifth.
void test_remaining_length_bounds(void)
{
    bind_buffers();
    Mqtt.packet.remaining_length = 268435456u; // one past the table's largest
    Mqtt.encode_remaining_length(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);
    TEST_ASSERT_EQUAL_UINT(0u, Mqtt.n);

    // Four continuation octets: the field would run to a fifth.
    static const uint8_t FIVE[5] = {0x80, 0x80, 0x80, 0x80, 0x01};
    Mqtt.buf.in = FIVE;
    Mqtt.buf.avail = sizeof(FIVE);
    Mqtt.decode_remaining_length(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);

    // A field whose continuation octet is the last one buffered is incomplete, not malformed.
    static const uint8_t PARTIAL[1] = {0x80};
    Mqtt.buf.in = PARTIAL;
    Mqtt.buf.avail = 1;
    Mqtt.decode_remaining_length(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);

    // One octet short of what the field needs, and a null source.
    bind_buffers();
    Mqtt.buf.cap = 1;
    Mqtt.packet.remaining_length = 128u;
    Mqtt.encode_remaining_length(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);
    Mqtt.buf.out = NULL;
    Mqtt.encode_remaining_length(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);
    Mqtt.buf.in = NULL;
    Mqtt.decode_remaining_length(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);
}

// sec 3.1.2.1 Figure 3.2: the Protocol Name is the UTF-8 encoded string "MQTT", so bytes 1..6 of the
// variable header are 0x00 0x04 'M' 'Q' 'T' 'T'. sec 3.1.2.2 Figure 3.3: the Protocol Level for
// version 3.1.1 is 4 (0x04). sec 3.1.2.3 Figure 3.6 prints a Connect Flags byte with User Name 1,
// Password 1, Will Retain 0, Will QoS 01, Will Flag 1, Clean Session 1, Reserved 0 - so 0xCE - and a
// Keep Alive of 10. sec 3.1.3 MQTT-3.1.3-1 fixes the payload order: Client Identifier, Will Topic,
// Will Message, User Name, Password.
void test_connect_matches_figure_3_6(void)
{
    static const uint8_t WILL[2] = {'w', 'm'};
    static const uint8_t WANT[29] = {
        0x10, 0x1B,                         // CONNECT (type 1), Remaining Length 27
        0x00, 0x04, 0x4D, 0x51, 0x54, 0x54, // Protocol Name "MQTT"
        0x04,                               // Protocol Level 4
        0xCE,                               // Connect Flags
        0x00, 0x0A,                         // Keep Alive 10
        0x00, 0x01, 0x63,                   // Client Identifier "c"
        0x00, 0x02, 0x77, 0x74,             // Will Topic "wt"
        0x00, 0x02, 0x77, 0x6D,             // Will Message "wm"
        0x00, 0x01, 0x75,                   // User Name "u"
        0x00, 0x01, 0x70,                   // Password "p"
    };
    bind_buffers();
    Mqtt.session.client_id = "c";
    Mqtt.session.user_name = "u";
    Mqtt.session.password = "p";
    Mqtt.session.keep_alive = 10;
    Mqtt.session.clean_session = PROTO_TRUE;
    Mqtt.will.topic = "wt";
    Mqtt.will.message = WILL;
    Mqtt.will.message_len = sizeof(WILL);
    Mqtt.will.qos = 1;
    Mqtt.will.retain = PROTO_FALSE;
    Mqtt.build_connect(Mqtt.internal);
    TEST_ASSERT_TRUE(Mqtt.ok);
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), Mqtt.n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT, g_out, sizeof(WANT));
    TEST_ASSERT_EQUAL_HEX8(0x04, PROTOCORE_MQTT_PROTOCOL_LEVEL);
}

// sec 3.1.2.5: a null Will Topic clears the Will Flag, and with it Will QoS and Will Retain.
// sec 3.1.2.8 and sec 3.1.2.9: a null User Name or Password clears its flag and omits the field.
void test_connect_flags_follow_the_fields_present(void)
{
    static const uint8_t WANT[14] = {
        0x10, 0x0C,                         // Remaining Length 12
        0x00, 0x04, 0x4D, 0x51, 0x54, 0x54, // "MQTT"
        0x04,                               // Level 4
        0x00,                               // no flag set at all
        0x00, 0x00,                         // Keep Alive 0 turns the mechanism off (sec 3.1.2.10)
        0x00, 0x00,                         // a zero-length Client Identifier (sec 3.1.3.1)
    };
    bind_buffers();
    Mqtt.session.client_id = "";
    Mqtt.session.user_name = NULL;
    Mqtt.session.password = NULL;
    Mqtt.session.keep_alive = 0;
    Mqtt.session.clean_session = PROTO_FALSE;
    Mqtt.will.topic = NULL;
    Mqtt.will.message = NULL;
    Mqtt.will.message_len = 0;
    Mqtt.will.qos = 2;
    Mqtt.will.retain = PROTO_TRUE;
    Mqtt.build_connect(Mqtt.internal);
    TEST_ASSERT_TRUE(Mqtt.ok);
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), Mqtt.n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT, g_out, sizeof(WANT));

    // Will QoS 2 and Will Retain set, with a Will Topic present this time: bits 4-3 are 10 and bit 5
    // is 1, so the flags byte is 0b0011_0100 = 0x34.
    bind_buffers();
    Mqtt.will.topic = "t";
    Mqtt.build_connect(Mqtt.internal);
    TEST_ASSERT_TRUE(Mqtt.ok);
    TEST_ASSERT_EQUAL_HEX8(0x34, g_out[9]);
}

// sec 3.3.2.3 Figure 3.11 prints the variable header for a PUBLISH with Topic Name "a/b" and Packet
// Identifier 10: 0x00 0x03 'a' '/' 'b' 0x00 0x0A. sec 3.3.1 Figure 3.10 puts the type nibble at 3
// with DUP in bit 3, QoS in bits 2-1 and RETAIN in bit 0.
void test_publish_matches_figure_3_11(void)
{
    static const uint8_t WANT[9] = {
        0x32,                         // type 3, DUP 0, QoS 1, RETAIN 0
        0x07,                         // Remaining Length 7
        0x00, 0x03, 0x61, 0x2F, 0x62, // Topic Name "a/b"
        0x00, 0x0A,                   // Packet Identifier 10
    };
    bind_buffers();
    Mqtt.message.topic_name = "a/b";
    Mqtt.message.payload = NULL;
    Mqtt.message.payload_len = 0;
    Mqtt.message.qos = 1;
    Mqtt.message.retain = PROTO_FALSE;
    Mqtt.message.dup = PROTO_FALSE;
    Mqtt.packet.packet_id = 10;
    Mqtt.build_publish(Mqtt.internal);
    TEST_ASSERT_TRUE(Mqtt.ok);
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), Mqtt.n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT, g_out, sizeof(WANT));
}

// sec 3.3.1: DUP is bit 3, QoS is bits 2-1 and RETAIN is bit 0 of byte 1. sec 3.3.2.2: the Packet
// Identifier is present only at QoS 1 or 2.
void test_publish_fixed_header_flags(void)
{
    static const uint8_t PAYLOAD[2] = {'h', 'i'};
    struct
    {
        uint8_t qos;
        proto_bool retain;
        proto_bool dup;
        uint8_t byte1;
        size_t total;
    } static const CASES[] = {
        {0, PROTO_FALSE, PROTO_FALSE, 0x30, 9},  // no Packet Identifier at QoS 0
        {0, PROTO_TRUE, PROTO_FALSE, 0x31, 9},   // RETAIN, bit 0
        {1, PROTO_FALSE, PROTO_FALSE, 0x32, 11}, // QoS 1, bits 2-1
        {2, PROTO_FALSE, PROTO_FALSE, 0x34, 11}, // QoS 2
        {1, PROTO_FALSE, PROTO_TRUE, 0x3A, 11},  // DUP, bit 3
        {2, PROTO_TRUE, PROTO_TRUE, 0x3D, 11},   // all three at once
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        bind_buffers();
        Mqtt.message.topic_name = "a/b";
        Mqtt.message.payload = PAYLOAD;
        Mqtt.message.payload_len = sizeof(PAYLOAD);
        Mqtt.message.qos = CASES[i].qos;
        Mqtt.message.retain = CASES[i].retain;
        Mqtt.message.dup = CASES[i].dup;
        Mqtt.packet.packet_id = 10;
        Mqtt.build_publish(Mqtt.internal);
        TEST_ASSERT_TRUE(Mqtt.ok);
        TEST_ASSERT_EQUAL_HEX8(CASES[i].byte1, g_out[0]);
        TEST_ASSERT_EQUAL_UINT(CASES[i].total, Mqtt.n);
    }

    // A QoS above 2 has no encoding.
    bind_buffers();
    Mqtt.message.qos = 3;
    Mqtt.build_publish(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);
}

// MQTT-3.3.2-2: "The Topic Name in the PUBLISH Packet MUST NOT contain wildcard characters." A
// SUBSCRIBE Topic Filter may (sec 4.7.1), so the refusal is publish-only.
void test_publish_refuses_wildcards(void)
{
    static const char *const BAD[] = {"+", "#", "a/+", "a/#", "sport/+/player1", "a/b/#"};
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        bind_buffers();
        Mqtt.message.topic_name = BAD[i];
        Mqtt.message.payload = NULL;
        Mqtt.message.payload_len = 0;
        Mqtt.message.qos = 0;
        Mqtt.message.retain = PROTO_FALSE;
        Mqtt.message.dup = PROTO_FALSE;
        Mqtt.build_publish(Mqtt.internal);
        TEST_ASSERT_FALSE_MESSAGE(Mqtt.ok, BAD[i]);
    }
    // The same filters are accepted by a SUBSCRIBE.
    for (size_t i = 0; i < sizeof(BAD) / sizeof(BAD[0]); i++)
    {
        bind_buffers();
        Mqtt.filter.topic_filter = BAD[i];
        Mqtt.filter.qos = 0;
        Mqtt.packet.packet_id = 1;
        Mqtt.build_subscribe(Mqtt.internal);
        TEST_ASSERT_TRUE_MESSAGE(Mqtt.ok, BAD[i]);
    }
}

// sec 3.8.3.1 Figure 3.23 prints the payload for a SUBSCRIBE carrying Topic Name "a/b" at Requested
// QoS 0x01. sec 3.8.1 Table 2.2 fixes the SUBSCRIBE fixed-header flags at 0,0,1,0.
void test_subscribe_matches_figure_3_23(void)
{
    static const uint8_t WANT[10] = {
        0x82,                         // type 8 with the reserved flags 0010
        0x08,                         // Remaining Length 8
        0x00, 0x0A,                   // Packet Identifier 10
        0x00, 0x03, 0x61, 0x2F, 0x62, // Topic Filter "a/b"
        0x01,                         // Requested QoS 1
    };
    bind_buffers();
    Mqtt.filter.topic_filter = "a/b";
    Mqtt.filter.qos = 1;
    Mqtt.packet.packet_id = 10;
    Mqtt.build_subscribe(Mqtt.internal);
    TEST_ASSERT_TRUE(Mqtt.ok);
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), Mqtt.n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT, g_out, sizeof(WANT));

    // MQTT-3.8.3-4: the Requested QoS must be 0, 1 or 2.
    bind_buffers();
    Mqtt.filter.qos = 3;
    Mqtt.build_subscribe(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);
}

// sec 3.10.1 Table 2.2: UNSUBSCRIBE carries the same reserved flags 0,0,1,0, and its payload is the
// Topic Filter with no QoS byte behind it (sec 3.10.3).
void test_unsubscribe_reserved_flags(void)
{
    static const uint8_t WANT[9] = {
        0xA2, 0x07, 0x00, 0x0A, 0x00, 0x03, 0x61, 0x2F, 0x62,
    };
    bind_buffers();
    Mqtt.filter.topic_filter = "a/b";
    Mqtt.packet.packet_id = 10;
    Mqtt.build_unsubscribe(Mqtt.internal);
    TEST_ASSERT_TRUE(Mqtt.ok);
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), Mqtt.n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT, g_out, sizeof(WANT));
}

// sec 3.4 to sec 3.7: PUBACK, PUBREC, PUBREL and PUBCOMP are a fixed header plus a Packet Identifier
// and nothing else, so four octets whole with a Remaining Length of 2. Table 2.2 gives PUBREL alone
// the reserved flags 0,0,1,0.
void test_ack_packets_are_four_octets(void)
{
    struct
    {
        MqttType type;
        uint8_t byte1;
    } static const CASES[] = {
        {MQTT_PUBACK, 0x40},
        {MQTT_PUBREC, 0x50},
        {MQTT_PUBREL, 0x62},
        {MQTT_PUBCOMP, 0x70},
    };
    for (size_t i = 0; i < sizeof(CASES) / sizeof(CASES[0]); i++)
    {
        bind_buffers();
        Mqtt.packet.type = CASES[i].type;
        Mqtt.packet.packet_id = 0x1234;
        Mqtt.build_ack(Mqtt.internal);
        TEST_ASSERT_TRUE(Mqtt.ok);
        TEST_ASSERT_EQUAL_UINT(4u, Mqtt.n);
        TEST_ASSERT_EQUAL_HEX8(CASES[i].byte1, g_out[0]);
        TEST_ASSERT_EQUAL_HEX8(0x02, g_out[1]);
        TEST_ASSERT_EQUAL_HEX8(0x12, g_out[2]);
        TEST_ASSERT_EQUAL_HEX8(0x34, g_out[3]);
    }
    // Table 2.1: the type values are what the nibble carries.
    TEST_ASSERT_EQUAL_INT(1, MQTT_CONNECT);
    TEST_ASSERT_EQUAL_INT(2, MQTT_CONNACK);
    TEST_ASSERT_EQUAL_INT(3, MQTT_PUBLISH);
    TEST_ASSERT_EQUAL_INT(4, MQTT_PUBACK);
    TEST_ASSERT_EQUAL_INT(5, MQTT_PUBREC);
    TEST_ASSERT_EQUAL_INT(6, MQTT_PUBREL);
    TEST_ASSERT_EQUAL_INT(7, MQTT_PUBCOMP);
    TEST_ASSERT_EQUAL_INT(8, MQTT_SUBSCRIBE);
    TEST_ASSERT_EQUAL_INT(9, MQTT_SUBACK);
    TEST_ASSERT_EQUAL_INT(10, MQTT_UNSUBSCRIBE);
    TEST_ASSERT_EQUAL_INT(11, MQTT_UNSUBACK);
    TEST_ASSERT_EQUAL_INT(12, MQTT_PINGREQ);
    TEST_ASSERT_EQUAL_INT(13, MQTT_PINGRESP);
    TEST_ASSERT_EQUAL_INT(14, MQTT_DISCONNECT);
}

// sec 3.12 and sec 3.14: PINGREQ and DISCONNECT have no variable header and no payload, so each is a
// fixed header with a Remaining Length of 0.
void test_pingreq_and_disconnect_are_two_octets(void)
{
    bind_buffers();
    Mqtt.build_pingreq(Mqtt.internal);
    TEST_ASSERT_TRUE(Mqtt.ok);
    TEST_ASSERT_EQUAL_UINT(2u, Mqtt.n);
    TEST_ASSERT_EQUAL_HEX8(0xC0, g_out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[1]);

    bind_buffers();
    Mqtt.build_disconnect(Mqtt.internal);
    TEST_ASSERT_TRUE(Mqtt.ok);
    TEST_ASSERT_EQUAL_UINT(2u, Mqtt.n);
    TEST_ASSERT_EQUAL_HEX8(0xE0, g_out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[1]);
}

// sec 2.2: the fixed header is byte 1's type and flags then the Remaining Length, and the parse
// reports how many octets that took. A packet is not readable until the whole header is buffered.
void test_parse_fixed_header(void)
{
    bind_buffers();
    Mqtt.message.topic_name = "a/b";
    Mqtt.message.payload = NULL;
    Mqtt.message.payload_len = 0;
    Mqtt.message.qos = 2;
    Mqtt.message.retain = PROTO_TRUE;
    Mqtt.message.dup = PROTO_FALSE;
    Mqtt.packet.packet_id = 10;
    Mqtt.build_publish(Mqtt.internal);
    const size_t total = Mqtt.n;

    Mqtt.buf.in = g_out;
    Mqtt.buf.avail = total;
    Mqtt.parse_fixed_header(Mqtt.internal);
    TEST_ASSERT_TRUE(Mqtt.ok);
    TEST_ASSERT_EQUAL_UINT(2u, Mqtt.n);
    TEST_ASSERT_EQUAL_INT(MQTT_PUBLISH, Mqtt.packet.type);
    TEST_ASSERT_EQUAL_HEX8(0x05, Mqtt.packet.flags); // QoS 2 in bits 2-1, RETAIN in bit 0
    TEST_ASSERT_EQUAL_UINT32(total - 2u, Mqtt.packet.remaining_length);

    // One octet is never a whole fixed header.
    Mqtt.buf.avail = 1;
    Mqtt.parse_fixed_header(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);
    Mqtt.buf.in = NULL;
    Mqtt.buf.avail = total;
    Mqtt.parse_fixed_header(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);

    // A two-octet Remaining Length is read as two octets.
    static const uint8_t LONG_HEADER[4] = {0x30, 0x80, 0x01, 0x00};
    Mqtt.buf.in = LONG_HEADER;
    Mqtt.buf.avail = sizeof(LONG_HEADER);
    Mqtt.parse_fixed_header(Mqtt.internal);
    TEST_ASSERT_TRUE(Mqtt.ok);
    TEST_ASSERT_EQUAL_UINT(3u, Mqtt.n);
    TEST_ASSERT_EQUAL_UINT32(128u, Mqtt.packet.remaining_length);
}

// sec 3.3: the PUBLISH body is the Topic Name, the Packet Identifier when QoS is above 0, then the
// Payload, whose length is what the Remaining Length leaves over.
void test_parse_publish_round_trip(void)
{
    static const uint8_t PAYLOAD[3] = {0xde, 0xad, 0x01};
    bind_buffers();
    Mqtt.message.topic_name = "sport/tennis";
    Mqtt.message.payload = PAYLOAD;
    Mqtt.message.payload_len = sizeof(PAYLOAD);
    Mqtt.message.qos = 1;
    Mqtt.message.retain = PROTO_TRUE;
    Mqtt.message.dup = PROTO_TRUE;
    Mqtt.packet.packet_id = 0x0102;
    Mqtt.build_publish(Mqtt.internal);
    TEST_ASSERT_TRUE(Mqtt.ok);
    const size_t total = Mqtt.n;

    Mqtt.buf.in = g_out;
    Mqtt.buf.avail = total;
    Mqtt.parse_fixed_header(Mqtt.internal);
    TEST_ASSERT_TRUE(Mqtt.ok);
    const size_t hdr = Mqtt.n;

    Mqtt.buf.in = g_out + hdr;
    Mqtt.buf.avail = total - hdr;
    Mqtt.message.topic_out = g_topic;
    Mqtt.message.topic_cap = sizeof(g_topic);
    Mqtt.parse_publish(Mqtt.internal);
    TEST_ASSERT_TRUE(Mqtt.ok);
    TEST_ASSERT_EQUAL_STRING("sport/tennis", g_topic);
    TEST_ASSERT_EQUAL_UINT(12u, Mqtt.message.topic_len);
    TEST_ASSERT_EQUAL_UINT8(1, Mqtt.message.qos);
    TEST_ASSERT_TRUE(Mqtt.message.retain);
    TEST_ASSERT_TRUE(Mqtt.message.dup);
    TEST_ASSERT_EQUAL_UINT16(0x0102, Mqtt.packet.packet_id);
    TEST_ASSERT_EQUAL_UINT(sizeof(PAYLOAD), Mqtt.message.payload_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(PAYLOAD, Mqtt.message.payload, sizeof(PAYLOAD));

    // sec 3.3.2.2: at QoS 0 there is no Packet Identifier, so the whole body past the Topic Name is
    // Payload.
    bind_buffers();
    Mqtt.message.topic_name = "a";
    Mqtt.message.payload = PAYLOAD;
    Mqtt.message.payload_len = sizeof(PAYLOAD);
    Mqtt.message.qos = 0;
    Mqtt.message.retain = PROTO_FALSE;
    Mqtt.message.dup = PROTO_FALSE;
    Mqtt.build_publish(Mqtt.internal);
    Mqtt.buf.in = g_out + 2;
    Mqtt.buf.avail = Mqtt.n - 2;
    Mqtt.packet.remaining_length = (uint32_t)(Mqtt.n - 2);
    Mqtt.packet.flags = 0x00;
    Mqtt.message.topic_out = g_topic;
    Mqtt.message.topic_cap = sizeof(g_topic);
    Mqtt.parse_publish(Mqtt.internal);
    TEST_ASSERT_TRUE(Mqtt.ok);
    TEST_ASSERT_EQUAL_UINT16(0, Mqtt.packet.packet_id);
    TEST_ASSERT_EQUAL_UINT(sizeof(PAYLOAD), Mqtt.message.payload_len);
}

// MQTT-3.3.1-4: "A PUBLISH Packet MUST NOT have both QoS bits set to 1." MQTT-1.5.3-1 and
// MQTT-1.5.3-2: a UTF-8 encoded string must be well-formed and must not encode U+0000.
void test_parse_publish_refuses_a_malformed_body(void)
{
    // Both QoS bits set.
    static const uint8_t BODY[5] = {0x00, 0x01, 'a', 0x00, 0x0A};
    Mqtt.buf.in = BODY;
    Mqtt.buf.avail = sizeof(BODY);
    Mqtt.packet.remaining_length = sizeof(BODY);
    Mqtt.packet.flags = 0x06; // QoS bits 2-1 both set
    Mqtt.message.topic_out = g_topic;
    Mqtt.message.topic_cap = sizeof(g_topic);
    Mqtt.parse_publish(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);

    // A Topic Name that is not well-formed UTF-8.
    static const uint8_t BAD_UTF8[3] = {0x00, 0x01, 0xFF};
    Mqtt.buf.in = BAD_UTF8;
    Mqtt.buf.avail = sizeof(BAD_UTF8);
    Mqtt.packet.remaining_length = sizeof(BAD_UTF8);
    Mqtt.packet.flags = 0x00;
    Mqtt.parse_publish(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);

    // A Topic Name encoding U+0000.
    static const uint8_t NUL_IN_TOPIC[4] = {0x00, 0x02, 'a', 0x00};
    Mqtt.buf.in = NUL_IN_TOPIC;
    Mqtt.buf.avail = sizeof(NUL_IN_TOPIC);
    Mqtt.packet.remaining_length = sizeof(NUL_IN_TOPIC);
    Mqtt.parse_publish(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);

    // A Topic Name longer than the Remaining Length allows.
    static const uint8_t OVER[4] = {0x00, 0x40, 'a', 'b'};
    Mqtt.buf.in = OVER;
    Mqtt.buf.avail = sizeof(OVER);
    Mqtt.packet.remaining_length = sizeof(OVER);
    Mqtt.parse_publish(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);

    // A Topic Name that does not fit the caller's buffer, NUL included.
    static const uint8_t THREE[5] = {0x00, 0x03, 'a', 'b', 'c'};
    Mqtt.buf.in = THREE;
    Mqtt.buf.avail = sizeof(THREE);
    Mqtt.packet.remaining_length = sizeof(THREE);
    Mqtt.message.topic_cap = 3; // three octets plus a NUL needs four
    Mqtt.parse_publish(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);
    Mqtt.message.topic_cap = 4;
    Mqtt.parse_publish(Mqtt.internal);
    TEST_ASSERT_TRUE(Mqtt.ok);
    TEST_ASSERT_EQUAL_STRING("abc", g_topic);

    // A body too short to hold even the Topic Name's length prefix.
    static const uint8_t STUB[1] = {0x00};
    Mqtt.buf.in = STUB;
    Mqtt.buf.avail = 1;
    Mqtt.packet.remaining_length = 1;
    Mqtt.message.topic_cap = sizeof(g_topic);
    Mqtt.parse_publish(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);

    // No destination for the Topic Name at all.
    Mqtt.buf.in = THREE;
    Mqtt.buf.avail = sizeof(THREE);
    Mqtt.packet.remaining_length = sizeof(THREE);
    Mqtt.message.topic_out = NULL;
    Mqtt.parse_publish(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);
}

// sec 3.2.2.3 Table 3.1 "Connect Return code values": 0 accepted, 1 unacceptable protocol version,
// 2 identifier rejected, 3 Server unavailable, 4 bad user name or password, 5 not authorized.
// sec 3.2.2.2: Session Present is bit 0 of the Connect Acknowledge Flags.
void test_parse_connack_table_3_1(void)
{
    for (uint8_t code = 0; code <= 5; code++)
    {
        const uint8_t body[2] = {0x00, code};
        Mqtt.buf.in = body;
        Mqtt.buf.avail = sizeof(body);
        Mqtt.packet.remaining_length = 2;
        Mqtt.parse_connack(Mqtt.internal);
        TEST_ASSERT_TRUE(Mqtt.ok);
        TEST_ASSERT_EQUAL_INT32(code, Mqtt.i32);
        TEST_ASSERT_FALSE(Mqtt.session_present);
    }

    static const uint8_t PRESENT[2] = {0x01, 0x00};
    Mqtt.buf.in = PRESENT;
    Mqtt.buf.avail = sizeof(PRESENT);
    Mqtt.packet.remaining_length = 2;
    Mqtt.parse_connack(Mqtt.internal);
    TEST_ASSERT_TRUE(Mqtt.ok);
    TEST_ASSERT_TRUE(Mqtt.session_present);
    TEST_ASSERT_EQUAL_INT32(0, Mqtt.i32);

    // A body shorter than the two octets sec 3.2.2 defines reports -1 rather than a code.
    Mqtt.packet.remaining_length = 1;
    Mqtt.parse_connack(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);
    TEST_ASSERT_EQUAL_INT32(-1, Mqtt.i32);
    TEST_ASSERT_FALSE(Mqtt.session_present);
}

// sec 3.9.3: a SUBACK payload return code is 0x00 (max QoS 0), 0x01 (max QoS 1), 0x02 (max QoS 2) or
// 0x80 (Failure). sec 3.9.2: the variable header is the Packet Identifier of the SUBSCRIBE.
void test_parse_suback_return_codes(void)
{
    static const uint8_t CODE[] = {0x00, 0x01, 0x02, 0x80};
    TEST_ASSERT_EQUAL_HEX8(0x80, PROTOCORE_MQTT_SUBACK_FAILURE);
    for (size_t i = 0; i < sizeof(CODE) / sizeof(CODE[0]); i++)
    {
        const uint8_t body[3] = {0x00, 0x0A, CODE[i]};
        Mqtt.buf.in = body;
        Mqtt.buf.avail = sizeof(body);
        Mqtt.packet.remaining_length = 3;
        Mqtt.parse_suback(Mqtt.internal);
        TEST_ASSERT_TRUE(Mqtt.ok);
        TEST_ASSERT_EQUAL_UINT16(10, Mqtt.packet.packet_id);
        TEST_ASSERT_EQUAL_HEX8(CODE[i], Mqtt.u8);
    }

    // A SUBACK with no return code at all reports Failure rather than a subscription that took.
    static const uint8_t SHORT[2] = {0x00, 0x0A};
    Mqtt.buf.in = SHORT;
    Mqtt.buf.avail = sizeof(SHORT);
    Mqtt.packet.remaining_length = 2;
    Mqtt.parse_suback(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);
    TEST_ASSERT_EQUAL_HEX8(PROTOCORE_MQTT_SUBACK_FAILURE, Mqtt.u8);
}

// sec 2.3.1: a PUBACK, PUBREC, PUBREL, PUBCOMP or UNSUBACK body is the two-octet Packet Identifier,
// and no real identifier is 0.
void test_parse_ack_packet_identifier(void)
{
    bind_buffers();
    Mqtt.packet.type = MQTT_PUBREL;
    Mqtt.packet.packet_id = 0xBEEF;
    Mqtt.build_ack(Mqtt.internal);
    TEST_ASSERT_EQUAL_UINT(4u, Mqtt.n);

    Mqtt.packet.packet_id = 0;
    Mqtt.buf.in = g_out + 2;
    Mqtt.buf.avail = 2;
    Mqtt.packet.remaining_length = 2;
    Mqtt.parse_ack(Mqtt.internal);
    TEST_ASSERT_TRUE(Mqtt.ok);
    TEST_ASSERT_EQUAL_UINT16(0xBEEF, Mqtt.packet.packet_id);

    Mqtt.packet.remaining_length = 1;
    Mqtt.parse_ack(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);
    TEST_ASSERT_EQUAL_UINT16(0, Mqtt.packet.packet_id);
}

// A build that cannot fit the whole Control Packet writes nothing and reports 0, so a truncated
// packet never reaches the Network Connection.
void test_builds_refuse_short_buffers(void)
{
    bind_buffers();
    Mqtt.buf.cap = 8;
    Mqtt.message.topic_name = "a/b";
    Mqtt.message.payload = NULL;
    Mqtt.message.payload_len = 0;
    Mqtt.message.qos = 1;
    Mqtt.message.retain = PROTO_FALSE;
    Mqtt.message.dup = PROTO_FALSE;
    Mqtt.packet.packet_id = 10;
    Mqtt.build_publish(Mqtt.internal); // needs 9
    TEST_ASSERT_FALSE(Mqtt.ok);
    TEST_ASSERT_EQUAL_UINT(0u, Mqtt.n);

    // The body scratch is checked before a single octet of it is used.
    bind_buffers();
    Mqtt.buf.body_cap = 4;
    Mqtt.build_publish(Mqtt.internal); // the body alone needs 7
    TEST_ASSERT_FALSE(Mqtt.ok);

    bind_buffers();
    Mqtt.buf.out = NULL;
    Mqtt.build_publish(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);
    bind_buffers();
    Mqtt.buf.body = NULL;
    Mqtt.build_publish(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);
    bind_buffers();
    Mqtt.message.topic_name = NULL;
    Mqtt.build_publish(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);

    // A CONNECT with no Client Identifier has no payload it can write (MQTT-3.1.3-3).
    bind_buffers();
    Mqtt.session.client_id = NULL;
    Mqtt.build_connect(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);

    // The four-octet and two-octet packets check their own room.
    bind_buffers();
    Mqtt.buf.cap = 3;
    Mqtt.packet.type = MQTT_PUBACK;
    Mqtt.build_ack(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);
    bind_buffers();
    Mqtt.buf.cap = 1;
    Mqtt.build_pingreq(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);
    bind_buffers();
    Mqtt.buf.cap = 1;
    Mqtt.build_disconnect(Mqtt.internal);
    TEST_ASSERT_FALSE(Mqtt.ok);
}
