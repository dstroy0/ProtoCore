// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the MQTT-SN wire codec (services/iot/mqtt/mqtt_sn.h).
//
// The governing document is "MQTT For Sensor Networks (MQTT-SN) Protocol Specification Version 1.2"
// (Stanford-Clark and Truong, IBM, 14 November 2013). It is neither an OASIS Standard nor an RFC, so
// the anchors here are the field layouts that document publishes: the sec 5.2 message form
// `[Length][MsgType][Message Variable Part]`, the sec 5.2.2 Table 3 MsgType octets, the sec 5.3.4
// Table 4 Flags bit positions, the sec 5.3.10 Table 5 ReturnCodes, and the per-message Variable Parts
// of sec 5.4.
//
// test_length_field_switches_at_255 is load-bearing: sec 5.2.1 says the Length is one octet while the
// whole message is at most 255 octets and otherwise the octet 0x01 followed by a big-endian uint16 of
// that same total. Length is what separates one datagram's messages, and it is the one field whose
// width changes with the message, so its boundary is where a codec silently desynchronizes.

#include "services/iot/mqtt/mqtt_sn.h"
#include <string.h>

#include <unity.h>

static uint8_t mqtt_sn_work[16]; // the borrow an entry takes; Mqttsn never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static uint8_t g_out[1024];

static void bind_out(void)
{
    memset(g_out, 0, sizeof(g_out));
    Mqttsn.buf.out = g_out;
    Mqttsn.buf.cap = sizeof(g_out);
}

static uint8_t make_flags(proto_bool dup, uint8_t qos, proto_bool retain, proto_bool will, proto_bool clean,
                          uint8_t topic_id_type)
{
    Mqttsn.flags.dup = dup;
    Mqttsn.flags.qos = qos;
    Mqttsn.flags.retain = retain;
    Mqttsn.flags.will = will;
    Mqttsn.flags.clean_session = clean;
    Mqttsn.flags.topic_id_type = topic_id_type;
    Mqttsn.make_flags(mqtt_sn_work);
    return Mqttsn.flags.octet;
}

static proto_bool parse_header(const uint8_t *msg, size_t len)
{
    Mqttsn.buf.in = msg;
    Mqttsn.buf.avail = len;
    Mqttsn.parse_header(mqtt_sn_work);
    return Mqttsn.ok;
}

// Point the typed parsers at the Message Variable Part the header parse found.
static void seat_variable(void)
{
    Mqttsn.buf.in = Mqttsn.header.variable;
    Mqttsn.buf.avail = Mqttsn.header.variable_len;
}

// sec 5.2.1: "Length ... is 1-octet long and specifies the total number of octets contained in the
// message (including the Length field itself). If the first octet is coded 0x01 then the Length
// field is 3-octet long", the following two octets being that same total, most significant first.
void test_length_field_switches_at_255(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x01, MQTTSN_LEN3_PREFIX);

    // A PUBLISH is Length + MsgType + Flags + TopicId 2 + MsgId 2 + Data, so the 1-octet form covers
    // a Data of 249 octets (1+1+1+2+2+249 = 256 would not, 248 gives 255 exactly).
    static uint8_t data[1000];
    for (size_t i = 0; i < sizeof(data); i++)
    {
        data[i] = (uint8_t)i;
    }
    (void)make_flags(PROTO_FALSE, 0, PROTO_FALSE, PROTO_FALSE, PROTO_FALSE, MQTTSN_TOPIC_NORMAL);

    // 248 octets of Data: total 1 + 1 + 1 + 2 + 2 + 248 = 255, the widest 1-octet form.
    bind_out();
    Mqttsn.topic.topic_id = 0x0102;
    Mqttsn.field.msg_id = 0x0304;
    Mqttsn.data.data = data;
    Mqttsn.data.data_len = 248;
    Mqttsn.build_publish(mqtt_sn_work);
    TEST_ASSERT_TRUE(Mqttsn.ok);
    TEST_ASSERT_EQUAL_UINT(255u, Mqttsn.n);
    TEST_ASSERT_EQUAL_HEX8(255, g_out[0]);
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_PUBLISH, g_out[1]);

    // One octet more of Data pushes the whole message past 255, so the Length becomes 0x01 and a
    // big-endian uint16 of the new total: 3 + 1 + 1 + 2 + 2 + 249 = 258 = 0x0102.
    bind_out();
    Mqttsn.data.data_len = 249;
    Mqttsn.build_publish(mqtt_sn_work);
    TEST_ASSERT_TRUE(Mqttsn.ok);
    TEST_ASSERT_EQUAL_UINT(258u, Mqttsn.n);
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_LEN3_PREFIX, g_out[0]);
    TEST_ASSERT_EQUAL_HEX8(0x01, g_out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x02, g_out[2]);
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_PUBLISH, g_out[3]);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(data, g_out + 3 + 1 + 1 + 2 + 2, 249);

    // Both forms read back with the same total, and the header parse points at the Variable Part.
    TEST_ASSERT_TRUE(parse_header(g_out, 258));
    TEST_ASSERT_EQUAL_UINT(258u, Mqttsn.n);
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_PUBLISH, Mqttsn.header.msg_type);
    TEST_ASSERT_EQUAL_UINT(258u - 4u, Mqttsn.header.variable_len);
    TEST_ASSERT_EQUAL_PTR(g_out + 4, Mqttsn.header.variable);

    // The 3-octet form reaches 65535 octets, which is what a 16-bit Length field expresses.
    bind_out();
    Mqttsn.buf.cap = 300;
    Mqttsn.data.data_len = 1000; // more than the buffer can hold
    Mqttsn.build_publish(mqtt_sn_work);
    TEST_ASSERT_FALSE(Mqttsn.ok);
    TEST_ASSERT_EQUAL_UINT(0u, Mqttsn.n);
}

// sec 5.3.4 Table 4: DUP is bit 7, QoS bits 6-5, Retain bit 4, Will bit 3, CleanSession bit 2 and
// TopicIdType bits 1-0. sec 6.8 gives QoS 3 the meaning "QoS level -1".
void test_flags_octet_bit_positions(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x80, MQTTSN_FLAG_DUP);
    TEST_ASSERT_EQUAL_HEX8(0x60, MQTTSN_FLAG_QOS_MASK);
    TEST_ASSERT_EQUAL_HEX8(0x10, MQTTSN_FLAG_RETAIN);
    TEST_ASSERT_EQUAL_HEX8(0x08, MQTTSN_FLAG_WILL);
    TEST_ASSERT_EQUAL_HEX8(0x04, MQTTSN_FLAG_CLEAN);
    TEST_ASSERT_EQUAL_HEX8(0x03, MQTTSN_FLAG_TOPICIDTYPE_MASK);

    TEST_ASSERT_EQUAL_HEX8(0x00, make_flags(PROTO_FALSE, 0, PROTO_FALSE, PROTO_FALSE, PROTO_FALSE, 0));
    TEST_ASSERT_EQUAL_HEX8(0x80, make_flags(PROTO_TRUE, 0, PROTO_FALSE, PROTO_FALSE, PROTO_FALSE, 0));
    TEST_ASSERT_EQUAL_HEX8(0x20, make_flags(PROTO_FALSE, 1, PROTO_FALSE, PROTO_FALSE, PROTO_FALSE, 0));
    TEST_ASSERT_EQUAL_HEX8(0x40, make_flags(PROTO_FALSE, 2, PROTO_FALSE, PROTO_FALSE, PROTO_FALSE, 0));
    TEST_ASSERT_EQUAL_HEX8(0x60, make_flags(PROTO_FALSE, 3, PROTO_FALSE, PROTO_FALSE, PROTO_FALSE, 0));
    TEST_ASSERT_EQUAL_HEX8(0x10, make_flags(PROTO_FALSE, 0, PROTO_TRUE, PROTO_FALSE, PROTO_FALSE, 0));
    TEST_ASSERT_EQUAL_HEX8(0x08, make_flags(PROTO_FALSE, 0, PROTO_FALSE, PROTO_TRUE, PROTO_FALSE, 0));
    TEST_ASSERT_EQUAL_HEX8(0x04, make_flags(PROTO_FALSE, 0, PROTO_FALSE, PROTO_FALSE, PROTO_TRUE, 0));

    // TopicIdType: 0b00 normal, 0b01 pre-defined, 0b10 short topic name (sec 5.3.4).
    TEST_ASSERT_EQUAL_HEX8(0x00, MQTTSN_TOPIC_NORMAL);
    TEST_ASSERT_EQUAL_HEX8(0x01, MQTTSN_TOPIC_PREDEFINED);
    TEST_ASSERT_EQUAL_HEX8(0x02, MQTTSN_TOPIC_SHORT);
    TEST_ASSERT_EQUAL_HEX8(0x01,
                           make_flags(PROTO_FALSE, 0, PROTO_FALSE, PROTO_FALSE, PROTO_FALSE, MQTTSN_TOPIC_PREDEFINED));
    TEST_ASSERT_EQUAL_HEX8(0x02, make_flags(PROTO_FALSE, 0, PROTO_FALSE, PROTO_FALSE, PROTO_FALSE, MQTTSN_TOPIC_SHORT));

    // Every field at once: DUP + QoS 2 + Retain + Will + CleanSession + short topic name.
    TEST_ASSERT_EQUAL_HEX8(0xDE, make_flags(PROTO_TRUE, 2, PROTO_TRUE, PROTO_TRUE, PROTO_TRUE, MQTTSN_TOPIC_SHORT));
}

// sec 5.4.4: CONNECT is Length, MsgType, Flags, ProtocolId, Duration, ClientId, with sec 5.3.8 fixing
// ProtocolId at 0x01 ("all other values are reserved").
void test_connect_variable_part(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x01, MQTTSN_PROTOCOL_ID);
    bind_out();
    (void)make_flags(PROTO_FALSE, 0, PROTO_FALSE, PROTO_TRUE, PROTO_TRUE, MQTTSN_TOPIC_NORMAL);
    Mqttsn.field.client_id = "node-1";
    Mqttsn.field.duration = 60;
    Mqttsn.build_connect(mqtt_sn_work);
    TEST_ASSERT_TRUE(Mqttsn.ok);
    // 1 Length + 1 MsgType + 1 Flags + 1 ProtocolId + 2 Duration + 6 ClientId = 12.
    static const uint8_t WANT[12] = {
        12, MQTTSN_CONNECT, 0x0C, 0x01, 0x00, 0x3C, 'n', 'o', 'd', 'e', '-', '1',
    };
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), Mqttsn.n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT, g_out, sizeof(WANT));

    // The gateway's CONNACK is Length, MsgType, ReturnCode: three octets (sec 5.4.5).
    static const uint8_t CONNACK[3] = {3, MQTTSN_CONNACK, MQTTSN_RC_ACCEPTED};
    TEST_ASSERT_TRUE(parse_header(CONNACK, sizeof(CONNACK)));
    TEST_ASSERT_EQUAL_UINT(3u, Mqttsn.n);
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_CONNACK, Mqttsn.header.msg_type);
    TEST_ASSERT_EQUAL_UINT(1u, Mqttsn.header.variable_len);
    seat_variable();
    Mqttsn.parse_connack(mqtt_sn_work);
    TEST_ASSERT_TRUE(Mqttsn.ok);
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_RC_ACCEPTED, Mqttsn.field.return_code);
}

// sec 5.4.10: REGISTER is TopicId, MsgId, TopicName, and sec 5.4.10 has the client code TopicId
// 0x0000 in a REGISTER it sends. sec 5.3: TopicId and MsgId are two octets, most significant first.
void test_register_and_regack(void)
{
    bind_out();
    Mqttsn.topic.topic_id = 0x0000;
    Mqttsn.topic.topic_name = "sensors/1/temp";
    Mqttsn.field.msg_id = 0x1234;
    Mqttsn.build_register(mqtt_sn_work);
    TEST_ASSERT_TRUE(Mqttsn.ok);
    // 1 + 1 + 2 + 2 + 14 = 20.
    TEST_ASSERT_EQUAL_UINT(20u, Mqttsn.n);
    TEST_ASSERT_EQUAL_HEX8(20, g_out[0]);
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_REGISTER, g_out[1]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00, g_out[3]);
    TEST_ASSERT_EQUAL_HEX8(0x12, g_out[4]);
    TEST_ASSERT_EQUAL_HEX8(0x34, g_out[5]);
    TEST_ASSERT_EQUAL_MEMORY("sensors/1/temp", g_out + 6, 14);

    // It reads back with the TopicName pointed at where it lies.
    TEST_ASSERT_TRUE(parse_header(g_out, 20));
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_REGISTER, Mqttsn.header.msg_type);
    seat_variable();
    Mqttsn.parse_register(mqtt_sn_work);
    TEST_ASSERT_TRUE(Mqttsn.ok);
    TEST_ASSERT_EQUAL_UINT16(0x0000, Mqttsn.topic.topic_id);
    TEST_ASSERT_EQUAL_UINT16(0x1234, Mqttsn.field.msg_id);
    TEST_ASSERT_EQUAL_UINT(14u, Mqttsn.topic.topic_name_len);
    TEST_ASSERT_EQUAL_MEMORY("sensors/1/temp", Mqttsn.topic.topic_name, 14);
    TEST_ASSERT_EQUAL_PTR(g_out + 6, Mqttsn.topic.topic_name);

    // sec 5.4.11: REGACK is TopicId, MsgId, ReturnCode, so seven octets whole.
    bind_out();
    Mqttsn.topic.topic_id = 0x00AB;
    Mqttsn.field.msg_id = 0x1234;
    Mqttsn.field.return_code = MQTTSN_RC_ACCEPTED;
    Mqttsn.build_regack(mqtt_sn_work);
    TEST_ASSERT_TRUE(Mqttsn.ok);
    static const uint8_t REGACK[7] = {7, MQTTSN_REGACK, 0x00, 0xAB, 0x12, 0x34, MQTTSN_RC_ACCEPTED};
    TEST_ASSERT_EQUAL_UINT(sizeof(REGACK), Mqttsn.n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(REGACK, g_out, sizeof(REGACK));

    TEST_ASSERT_TRUE(parse_header(REGACK, sizeof(REGACK)));
    seat_variable();
    Mqttsn.parse_regack(mqtt_sn_work);
    TEST_ASSERT_TRUE(Mqttsn.ok);
    TEST_ASSERT_EQUAL_UINT16(0x00AB, Mqttsn.topic.topic_id);
    TEST_ASSERT_EQUAL_UINT16(0x1234, Mqttsn.field.msg_id);
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_RC_ACCEPTED, Mqttsn.field.return_code);
}

// sec 5.4.12: PUBLISH is Flags, TopicId, MsgId, Data, and sec 5.4.13 PUBACK is TopicId, MsgId,
// ReturnCode, which is REGACK's layout.
void test_publish_and_puback(void)
{
    static const uint8_t DATA[4] = {0xde, 0xad, 0xbe, 0xef};
    bind_out();
    const uint8_t flags = make_flags(PROTO_TRUE, 1, PROTO_TRUE, PROTO_FALSE, PROTO_FALSE, MQTTSN_TOPIC_PREDEFINED);
    Mqttsn.topic.topic_id = 0x00AB;
    Mqttsn.field.msg_id = 0x0007;
    Mqttsn.data.data = DATA;
    Mqttsn.data.data_len = sizeof(DATA);
    Mqttsn.build_publish(mqtt_sn_work);
    TEST_ASSERT_TRUE(Mqttsn.ok);
    // 1 + 1 + 1 + 2 + 2 + 4 = 11.
    const uint8_t WANT[11] = {11, MQTTSN_PUBLISH, flags, 0x00, 0xAB, 0x00, 0x07, 0xde, 0xad, 0xbe, 0xef};
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT), Mqttsn.n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT, g_out, sizeof(WANT));

    TEST_ASSERT_TRUE(parse_header(g_out, 11));
    seat_variable();
    Mqttsn.parse_publish(mqtt_sn_work);
    TEST_ASSERT_TRUE(Mqttsn.ok);
    TEST_ASSERT_EQUAL_HEX8(flags, Mqttsn.flags.octet);
    TEST_ASSERT_EQUAL_UINT16(0x00AB, Mqttsn.topic.topic_id);
    TEST_ASSERT_EQUAL_UINT16(0x0007, Mqttsn.field.msg_id);
    TEST_ASSERT_EQUAL_UINT(4u, Mqttsn.data.data_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(DATA, Mqttsn.data.data, 4);

    // A PUBLISH carrying no Data at all: the Variable Part is exactly its five header octets.
    bind_out();
    Mqttsn.data.data = NULL;
    Mqttsn.data.data_len = 0;
    Mqttsn.build_publish(mqtt_sn_work);
    TEST_ASSERT_TRUE(Mqttsn.ok);
    TEST_ASSERT_EQUAL_UINT(7u, Mqttsn.n);
    TEST_ASSERT_TRUE(parse_header(g_out, 7));
    seat_variable();
    Mqttsn.parse_publish(mqtt_sn_work);
    TEST_ASSERT_TRUE(Mqttsn.ok);
    TEST_ASSERT_EQUAL_UINT(0u, Mqttsn.data.data_len);

    // PUBACK, seven octets: Length, MsgType, TopicId, MsgId, ReturnCode.
    bind_out();
    Mqttsn.topic.topic_id = 0x00AB;
    Mqttsn.field.msg_id = 0x0007;
    Mqttsn.field.return_code = MQTTSN_RC_INVALID_TOPIC_ID;
    Mqttsn.build_puback(mqtt_sn_work);
    TEST_ASSERT_TRUE(Mqttsn.ok);
    static const uint8_t PUBACK[7] = {7, MQTTSN_PUBACK, 0x00, 0xAB, 0x00, 0x07, MQTTSN_RC_INVALID_TOPIC_ID};
    TEST_ASSERT_EQUAL_UINT(sizeof(PUBACK), Mqttsn.n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(PUBACK, g_out, sizeof(PUBACK));

    TEST_ASSERT_TRUE(parse_header(PUBACK, sizeof(PUBACK)));
    seat_variable();
    Mqttsn.parse_puback(mqtt_sn_work);
    TEST_ASSERT_TRUE(Mqttsn.ok);
    TEST_ASSERT_EQUAL_UINT16(0x00AB, Mqttsn.topic.topic_id);
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_RC_INVALID_TOPIC_ID, Mqttsn.field.return_code);
}

// sec 5.4.15: SUBSCRIBE is Flags, MsgId, then either a TopicName or a two-octet TopicId, which is
// what the Flags TopicIdType names. sec 5.4.16: SUBACK is Flags, TopicId, MsgId, ReturnCode.
void test_subscribe_by_name_and_by_id(void)
{
    bind_out();
    const uint8_t by_name = make_flags(PROTO_FALSE, 1, PROTO_FALSE, PROTO_FALSE, PROTO_FALSE, MQTTSN_TOPIC_NORMAL);
    Mqttsn.field.msg_id = 0x0002;
    Mqttsn.topic.topic_name = "a/b";
    Mqttsn.build_subscribe_name(mqtt_sn_work);
    TEST_ASSERT_TRUE(Mqttsn.ok);
    // 1 + 1 + 1 + 2 + 3 = 8.
    const uint8_t WANT_NAME[8] = {8, MQTTSN_SUBSCRIBE, by_name, 0x00, 0x02, 'a', '/', 'b'};
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT_NAME), Mqttsn.n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT_NAME, g_out, sizeof(WANT_NAME));

    bind_out();
    const uint8_t by_id = make_flags(PROTO_FALSE, 1, PROTO_FALSE, PROTO_FALSE, PROTO_FALSE, MQTTSN_TOPIC_PREDEFINED);
    Mqttsn.topic.topic_id = 0x0101;
    Mqttsn.build_subscribe_id(mqtt_sn_work);
    TEST_ASSERT_TRUE(Mqttsn.ok);
    // 1 + 1 + 1 + 2 + 2 = 7.
    const uint8_t WANT_ID[7] = {7, MQTTSN_SUBSCRIBE, by_id, 0x00, 0x02, 0x01, 0x01};
    TEST_ASSERT_EQUAL_UINT(sizeof(WANT_ID), Mqttsn.n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(WANT_ID, g_out, sizeof(WANT_ID));

    // SUBACK: eight octets, the granted QoS riding in the Flags octet (sec 5.4.16).
    const uint8_t granted = make_flags(PROTO_FALSE, 1, PROTO_FALSE, PROTO_FALSE, PROTO_FALSE, MQTTSN_TOPIC_NORMAL);
    const uint8_t SUBACK[8] = {8, MQTTSN_SUBACK, granted, 0x00, 0xAB, 0x00, 0x02, MQTTSN_RC_ACCEPTED};
    TEST_ASSERT_TRUE(parse_header(SUBACK, sizeof(SUBACK)));
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_SUBACK, Mqttsn.header.msg_type);
    seat_variable();
    Mqttsn.parse_suback(mqtt_sn_work);
    TEST_ASSERT_TRUE(Mqttsn.ok);
    TEST_ASSERT_EQUAL_HEX8(granted, Mqttsn.flags.octet);
    TEST_ASSERT_EQUAL_UINT16(0x00AB, Mqttsn.topic.topic_id);
    TEST_ASSERT_EQUAL_UINT16(0x0002, Mqttsn.field.msg_id);
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_RC_ACCEPTED, Mqttsn.field.return_code);
}

// sec 5.4.19 and sec 6.14: a PINGREQ carries the ClientId when a sleeping client wakes and nothing at
// all as a keep-alive. sec 5.4.21: a DISCONNECT carries the sleep Duration or nothing.
void test_pingreq_and_disconnect_optional_fields(void)
{
    bind_out();
    Mqttsn.field.client_id = NULL;
    Mqttsn.build_pingreq(mqtt_sn_work);
    TEST_ASSERT_TRUE(Mqttsn.ok);
    TEST_ASSERT_EQUAL_UINT(2u, Mqttsn.n);
    TEST_ASSERT_EQUAL_HEX8(2, g_out[0]);
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_PINGREQ, g_out[1]);

    bind_out();
    Mqttsn.field.client_id = "node-1";
    Mqttsn.build_pingreq(mqtt_sn_work);
    TEST_ASSERT_TRUE(Mqttsn.ok);
    TEST_ASSERT_EQUAL_UINT(8u, Mqttsn.n);
    TEST_ASSERT_EQUAL_MEMORY("node-1", g_out + 2, 6);

    bind_out();
    Mqttsn.field.with_duration = PROTO_FALSE;
    Mqttsn.build_disconnect(mqtt_sn_work);
    TEST_ASSERT_TRUE(Mqttsn.ok);
    TEST_ASSERT_EQUAL_UINT(2u, Mqttsn.n);
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_DISCONNECT, g_out[1]);

    bind_out();
    Mqttsn.field.with_duration = PROTO_TRUE;
    Mqttsn.field.duration = 3600;
    Mqttsn.build_disconnect(mqtt_sn_work);
    TEST_ASSERT_TRUE(Mqttsn.ok);
    static const uint8_t SLEEP[4] = {4, MQTTSN_DISCONNECT, 0x0E, 0x10}; // 3600 = 0x0E10
    TEST_ASSERT_EQUAL_UINT(sizeof(SLEEP), Mqttsn.n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(SLEEP, g_out, sizeof(SLEEP));

    // sec 5.4.2: SEARCHGW is Length, MsgType, Radius, and Radius 0x00 broadcasts to all nodes.
    bind_out();
    Mqttsn.field.radius = 0x00;
    Mqttsn.build_searchgw(mqtt_sn_work);
    TEST_ASSERT_TRUE(Mqttsn.ok);
    static const uint8_t SEARCHGW[3] = {3, MQTTSN_SEARCHGW, 0x00};
    TEST_ASSERT_EQUAL_UINT(sizeof(SEARCHGW), Mqttsn.n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(SEARCHGW, g_out, sizeof(SEARCHGW));
}

// sec 5.2.2 Table 3: the MsgType octets, which is what a receiver dispatches on.
void test_table_3_msgtype_values(void)
{
    TEST_ASSERT_EQUAL_HEX8(0x00, MQTTSN_ADVERTISE);
    TEST_ASSERT_EQUAL_HEX8(0x01, MQTTSN_SEARCHGW);
    TEST_ASSERT_EQUAL_HEX8(0x02, MQTTSN_GWINFO);
    TEST_ASSERT_EQUAL_HEX8(0x04, MQTTSN_CONNECT);
    TEST_ASSERT_EQUAL_HEX8(0x05, MQTTSN_CONNACK);
    TEST_ASSERT_EQUAL_HEX8(0x06, MQTTSN_WILLTOPICREQ);
    TEST_ASSERT_EQUAL_HEX8(0x07, MQTTSN_WILLTOPIC);
    TEST_ASSERT_EQUAL_HEX8(0x08, MQTTSN_WILLMSGREQ);
    TEST_ASSERT_EQUAL_HEX8(0x09, MQTTSN_WILLMSG);
    TEST_ASSERT_EQUAL_HEX8(0x0A, MQTTSN_REGISTER);
    TEST_ASSERT_EQUAL_HEX8(0x0B, MQTTSN_REGACK);
    TEST_ASSERT_EQUAL_HEX8(0x0C, MQTTSN_PUBLISH);
    TEST_ASSERT_EQUAL_HEX8(0x0D, MQTTSN_PUBACK);
    TEST_ASSERT_EQUAL_HEX8(0x0E, MQTTSN_PUBCOMP);
    TEST_ASSERT_EQUAL_HEX8(0x0F, MQTTSN_PUBREC);
    TEST_ASSERT_EQUAL_HEX8(0x10, MQTTSN_PUBREL);
    TEST_ASSERT_EQUAL_HEX8(0x12, MQTTSN_SUBSCRIBE);
    TEST_ASSERT_EQUAL_HEX8(0x13, MQTTSN_SUBACK);
    TEST_ASSERT_EQUAL_HEX8(0x14, MQTTSN_UNSUBSCRIBE);
    TEST_ASSERT_EQUAL_HEX8(0x15, MQTTSN_UNSUBACK);
    TEST_ASSERT_EQUAL_HEX8(0x16, MQTTSN_PINGREQ);
    TEST_ASSERT_EQUAL_HEX8(0x17, MQTTSN_PINGRESP);
    TEST_ASSERT_EQUAL_HEX8(0x18, MQTTSN_DISCONNECT);

    // sec 5.3.10 Table 5: the ReturnCode values.
    TEST_ASSERT_EQUAL_HEX8(0x00, MQTTSN_RC_ACCEPTED);
    TEST_ASSERT_EQUAL_HEX8(0x01, MQTTSN_RC_CONGESTION);
    TEST_ASSERT_EQUAL_HEX8(0x02, MQTTSN_RC_INVALID_TOPIC_ID);
    TEST_ASSERT_EQUAL_HEX8(0x03, MQTTSN_RC_NOT_SUPPORTED);
}

// A header whose Length does not agree with what is buffered, or does not cover its own field plus a
// MsgType, is refused rather than pointing a Variable Part past the end.
void test_header_parse_refuses_an_inconsistent_length(void)
{
    static const uint8_t SHORT_TOTAL[4] = {1, 0x00, 0x02, MQTTSN_PINGREQ};
    // The 3-octet form declaring a total of 2 cannot cover its own three octets plus a MsgType.
    TEST_ASSERT_FALSE(parse_header(SHORT_TOTAL, sizeof(SHORT_TOTAL)));

    // A 3-octet Length declaring a total of 3 covers its own field and nothing else, so there is no
    // MsgType octet in it.
    static const uint8_t NO_TYPE[4] = {1, 0x00, 0x03, MQTTSN_PINGREQ};
    TEST_ASSERT_FALSE(parse_header(NO_TYPE, sizeof(NO_TYPE)));

    // A Length claiming more octets than are buffered waits rather than decoding.
    static const uint8_t NOT_ALL_THERE[3] = {7, MQTTSN_REGACK, 0x00};
    TEST_ASSERT_FALSE(parse_header(NOT_ALL_THERE, sizeof(NOT_ALL_THERE)));

    // Fewer than two octets is not a header, and neither is no buffer at all.
    static const uint8_t ONE[1] = {2};
    TEST_ASSERT_FALSE(parse_header(ONE, 1));
    TEST_ASSERT_FALSE(parse_header(NULL, 8));

    // The 3-octet form with only two octets buffered is incomplete.
    static const uint8_t PARTIAL3[2] = {MQTTSN_LEN3_PREFIX, 0x01};
    TEST_ASSERT_FALSE(parse_header(PARTIAL3, sizeof(PARTIAL3)));

    // A datagram carrying two messages walks by the total each header reports.
    uint8_t two[16];
    bind_out();
    Mqttsn.field.client_id = NULL;
    Mqttsn.build_pingreq(mqtt_sn_work);
    memcpy(two, g_out, 2);
    bind_out();
    Mqttsn.field.radius = 3;
    Mqttsn.build_searchgw(mqtt_sn_work);
    memcpy(two + 2, g_out, 3);

    TEST_ASSERT_TRUE(parse_header(two, 5));
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_PINGREQ, Mqttsn.header.msg_type);
    TEST_ASSERT_EQUAL_UINT(2u, Mqttsn.n);
    TEST_ASSERT_TRUE(parse_header(two + 2, 3));
    TEST_ASSERT_EQUAL_HEX8(MQTTSN_SEARCHGW, Mqttsn.header.msg_type);
    TEST_ASSERT_EQUAL_UINT(3u, Mqttsn.n);
}

// A Message Variable Part shorter than the message's own layout is refused by every typed parser.
void test_typed_parsers_refuse_a_short_variable_part(void)
{
    static const uint8_t FOUR[4] = {0x00, 0x01, 0x00, 0x02};

    Mqttsn.buf.in = FOUR;
    Mqttsn.buf.avail = 4; // REGACK needs 5
    Mqttsn.parse_regack(mqtt_sn_work);
    TEST_ASSERT_FALSE(Mqttsn.ok);
    Mqttsn.parse_puback(mqtt_sn_work);
    TEST_ASSERT_FALSE(Mqttsn.ok);

    Mqttsn.buf.avail = 4; // PUBLISH needs 5, SUBACK 6
    Mqttsn.parse_publish(mqtt_sn_work);
    TEST_ASSERT_FALSE(Mqttsn.ok);
    Mqttsn.buf.avail = 5;
    Mqttsn.parse_suback(mqtt_sn_work);
    TEST_ASSERT_FALSE(Mqttsn.ok);

    Mqttsn.buf.avail = 3; // REGISTER needs 4
    Mqttsn.parse_register(mqtt_sn_work);
    TEST_ASSERT_FALSE(Mqttsn.ok);

    Mqttsn.buf.avail = 0; // CONNACK needs its one ReturnCode octet
    Mqttsn.parse_connack(mqtt_sn_work);
    TEST_ASSERT_FALSE(Mqttsn.ok);

    Mqttsn.buf.in = NULL;
    Mqttsn.buf.avail = 16;
    Mqttsn.parse_publish(mqtt_sn_work);
    TEST_ASSERT_FALSE(Mqttsn.ok);
    Mqttsn.parse_register(mqtt_sn_work);
    TEST_ASSERT_FALSE(Mqttsn.ok);
    Mqttsn.parse_suback(mqtt_sn_work);
    TEST_ASSERT_FALSE(Mqttsn.ok);
    Mqttsn.parse_connack(mqtt_sn_work);
    TEST_ASSERT_FALSE(Mqttsn.ok);
}

// A build that cannot fit the whole message writes nothing and says so, and a builder missing an
// argument it needs is refused rather than reading through a null.
void test_builders_fail_closed(void)
{
    uint8_t small[4];
    memset(small, 0xAA, sizeof(small));
    Mqttsn.buf.out = small;
    Mqttsn.buf.cap = sizeof(small);
    Mqttsn.field.client_id = "node-1";
    Mqttsn.field.duration = 60;
    Mqttsn.build_connect(mqtt_sn_work); // needs 12
    TEST_ASSERT_FALSE(Mqttsn.ok);
    TEST_ASSERT_EQUAL_UINT(0u, Mqttsn.n);
    TEST_ASSERT_EQUAL_HEX8(0xAA, small[0]);

    bind_out();
    Mqttsn.field.client_id = NULL;
    Mqttsn.build_connect(mqtt_sn_work);
    TEST_ASSERT_FALSE(Mqttsn.ok);

    bind_out();
    Mqttsn.topic.topic_name = NULL;
    Mqttsn.build_register(mqtt_sn_work);
    TEST_ASSERT_FALSE(Mqttsn.ok);
    Mqttsn.build_subscribe_name(mqtt_sn_work);
    TEST_ASSERT_FALSE(Mqttsn.ok);

    bind_out();
    Mqttsn.data.data = NULL;
    Mqttsn.data.data_len = 4; // octets promised but not lent
    Mqttsn.build_publish(mqtt_sn_work);
    TEST_ASSERT_FALSE(Mqttsn.ok);

    Mqttsn.buf.out = NULL;
    Mqttsn.buf.cap = sizeof(g_out);
    Mqttsn.build_pingreq(mqtt_sn_work);
    TEST_ASSERT_FALSE(Mqttsn.ok);
    Mqttsn.build_disconnect(mqtt_sn_work);
    TEST_ASSERT_FALSE(Mqttsn.ok);
    Mqttsn.build_searchgw(mqtt_sn_work);
    TEST_ASSERT_FALSE(Mqttsn.ok);
    Mqttsn.build_regack(mqtt_sn_work);
    TEST_ASSERT_FALSE(Mqttsn.ok);
    Mqttsn.build_puback(mqtt_sn_work);
    TEST_ASSERT_FALSE(Mqttsn.ok);
    Mqttsn.build_subscribe_id(mqtt_sn_work);
    TEST_ASSERT_FALSE(Mqttsn.ok);
}
