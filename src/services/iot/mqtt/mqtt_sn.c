// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mqtt_sn.c
 * @brief The MQTT-SN v1.2 wire codec: the Length and MsgType header (sec 5.2) and the Message
 *        Variable Part of each message (sec 5.3, sec 5.4).
 *
 * Every call works in the buffer the caller lends and holds nothing between calls, so the whole file
 * is host-testable.
 */

#include "services/iot/mqtt/mqtt_sn.h"

#if PROTOCORE_ENABLE_MQTT_SN

#include "mmgr/protomem.h" // mem.cpy: the ClientId, TopicName and Data spans
#include "mmgr/protostr.h" // str.len: their bounded lengths

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

#define MQTTSN_LEN1_MAX 255   // the 1-octet Length form reaches this total (sec 5.2.1)
#define MQTTSN_LEN3_OCTETS 3  // the 3-octet Length form: the prefix and a big-endian uint16
#define MQTTSN_LEN_MAX 0xFFFF // what the 3-octet form can encode (sec 5.2.1)

#define MQTTSN_ID_OCTETS 2 // TopicId, MsgId and Duration are two octets each (sec 5.3)

// ---------------------------------------------------------------------------
// Typedefs
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// The octet moves every message is made of (pure)
// ---------------------------------------------------------------------------

// Write v as the two-octet big-endian integer the wire uses (sec 5.3).
static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

// Read that same two-octet big-endian integer.
static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

// Write the Length and MsgType header for a message carrying body_len octets of Message Variable
// Part. Returns the offset the body starts at (always at least 2) and sets *total to the whole
// message length, or 0 on overflow or a body past the 16-bit Length field. The Length value counts
// the Length field itself: one octet while that total is at most 255, otherwise the prefix and a
// big-endian uint16 (sec 5.2.1).
static size_t frame_header(uint8_t *buf, size_t cap, uint8_t msg_type, size_t body_len, size_t *total)
{
    size_t core = 1 + body_len; // MsgType plus the Message Variable Part
    size_t lenfield;
    size_t t;
    if (1 + core <= MQTTSN_LEN1_MAX)
    {
        lenfield = 1;
        t = 1 + core;
    }
    else
    {
        lenfield = MQTTSN_LEN3_OCTETS;
        t = MQTTSN_LEN3_OCTETS + core;
    }
    if (t > MQTTSN_LEN_MAX || t > cap)
    {
        return 0;
    }
    size_t pos = 0;
    if (lenfield == 1)
    {
        buf[pos++] = (uint8_t)t;
    }
    else
    {
        buf[pos++] = MQTTSN_LEN3_PREFIX;
        buf[pos++] = (uint8_t)(t >> 8);
        buf[pos++] = (uint8_t)(t & 0xFF);
    }
    buf[pos++] = msg_type;
    *total = t;
    return pos;
}

// ---------------------------------------------------------------------------
// Flags
// ---------------------------------------------------------------------------

static void mqttsn_make_flags(uint8_t *restrict work)
{
    (void)work;
    uint8_t f = 0;
    if (Mqttsn.flags.dup)
    {
        f |= MQTTSN_FLAG_DUP;
    }
    f |= (uint8_t)((Mqttsn.flags.qos << MQTTSN_FLAG_QOS_SHIFT) & MQTTSN_FLAG_QOS_MASK);
    if (Mqttsn.flags.retain)
    {
        f |= MQTTSN_FLAG_RETAIN;
    }
    if (Mqttsn.flags.will)
    {
        f |= MQTTSN_FLAG_WILL;
    }
    if (Mqttsn.flags.clean_session)
    {
        f |= MQTTSN_FLAG_CLEAN;
    }
    f |= (uint8_t)(Mqttsn.flags.topic_id_type & MQTTSN_FLAG_TOPICIDTYPE_MASK);
    Mqttsn.flags.octet = f;
    Mqttsn.ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Builders
// ---------------------------------------------------------------------------

// CONNECT: Flags, ProtocolId, Duration, ClientId (sec 5.4.4).
static void mqttsn_build_connect(uint8_t *restrict work)
{
    (void)work;
    Mqttsn.n = 0;
    Mqttsn.ok = PROTO_FALSE;
    uint8_t *buf = Mqttsn.buf.out;
    const size_t cap = Mqttsn.buf.cap;
    if (!buf || !Mqttsn.field.client_id)
    {
        return;
    }
    size_t idlen = str.len(Mqttsn.field.client_id, cap);
    size_t total = 0;
    size_t p = frame_header(buf, cap, MQTTSN_CONNECT, 1 + 1 + MQTTSN_ID_OCTETS + idlen, &total);
    if (!p)
    {
        return;
    }
    buf[p++] = Mqttsn.flags.octet;
    buf[p++] = MQTTSN_PROTOCOL_ID;
    wr16(buf + p, Mqttsn.field.duration);
    p += MQTTSN_ID_OCTETS;
    mem.cpy(buf + p, Mqttsn.field.client_id, idlen);
    Mqttsn.n = total;
    Mqttsn.ok = PROTO_TRUE;
}

// REGISTER: TopicId, MsgId, TopicName (sec 5.4.10).
static void mqttsn_build_register(uint8_t *restrict work)
{
    (void)work;
    Mqttsn.n = 0;
    Mqttsn.ok = PROTO_FALSE;
    uint8_t *buf = Mqttsn.buf.out;
    const size_t cap = Mqttsn.buf.cap;
    if (!buf || !Mqttsn.topic.topic_name)
    {
        return;
    }
    size_t nlen = str.len(Mqttsn.topic.topic_name, cap);
    size_t total = 0;
    size_t p = frame_header(buf, cap, MQTTSN_REGISTER, MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS + nlen, &total);
    if (!p)
    {
        return;
    }
    wr16(buf + p, Mqttsn.topic.topic_id);
    p += MQTTSN_ID_OCTETS;
    wr16(buf + p, Mqttsn.field.msg_id);
    p += MQTTSN_ID_OCTETS;
    mem.cpy(buf + p, Mqttsn.topic.topic_name, nlen);
    Mqttsn.n = total;
    Mqttsn.ok = PROTO_TRUE;
}

// REGACK: TopicId, MsgId, ReturnCode (sec 5.4.11).
static void mqttsn_build_regack(uint8_t *restrict work)
{
    (void)work;
    Mqttsn.n = 0;
    Mqttsn.ok = PROTO_FALSE;
    uint8_t *buf = Mqttsn.buf.out;
    if (!buf)
    {
        return;
    }
    size_t total = 0;
    size_t p = frame_header(buf, Mqttsn.buf.cap, MQTTSN_REGACK, MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS + 1, &total);
    if (!p)
    {
        return;
    }
    wr16(buf + p, Mqttsn.topic.topic_id);
    p += MQTTSN_ID_OCTETS;
    wr16(buf + p, Mqttsn.field.msg_id);
    p += MQTTSN_ID_OCTETS;
    buf[p] = Mqttsn.field.return_code;
    Mqttsn.n = total;
    Mqttsn.ok = PROTO_TRUE;
}

// PUBLISH: Flags, TopicId, MsgId, Data (sec 5.4.12).
static void mqttsn_build_publish(uint8_t *restrict work)
{
    (void)work;
    Mqttsn.n = 0;
    Mqttsn.ok = PROTO_FALSE;
    uint8_t *buf = Mqttsn.buf.out;
    const size_t data_len = Mqttsn.data.data_len;
    if (!buf || (data_len && !Mqttsn.data.data))
    {
        return;
    }
    size_t total = 0;
    size_t p =
        frame_header(buf, Mqttsn.buf.cap, MQTTSN_PUBLISH, 1 + MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS + data_len, &total);
    if (!p)
    {
        return;
    }
    buf[p++] = Mqttsn.flags.octet;
    wr16(buf + p, Mqttsn.topic.topic_id);
    p += MQTTSN_ID_OCTETS;
    wr16(buf + p, Mqttsn.field.msg_id);
    p += MQTTSN_ID_OCTETS;
    if (data_len)
    {
        mem.cpy(buf + p, Mqttsn.data.data, data_len);
    }
    Mqttsn.n = total;
    Mqttsn.ok = PROTO_TRUE;
}

// PUBACK: TopicId, MsgId, ReturnCode (sec 5.4.13).
static void mqttsn_build_puback(uint8_t *restrict work)
{
    (void)work;
    Mqttsn.n = 0;
    Mqttsn.ok = PROTO_FALSE;
    uint8_t *buf = Mqttsn.buf.out;
    if (!buf)
    {
        return;
    }
    size_t total = 0;
    size_t p = frame_header(buf, Mqttsn.buf.cap, MQTTSN_PUBACK, MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS + 1, &total);
    if (!p)
    {
        return;
    }
    wr16(buf + p, Mqttsn.topic.topic_id);
    p += MQTTSN_ID_OCTETS;
    wr16(buf + p, Mqttsn.field.msg_id);
    p += MQTTSN_ID_OCTETS;
    buf[p] = Mqttsn.field.return_code;
    Mqttsn.n = total;
    Mqttsn.ok = PROTO_TRUE;
}

// SUBSCRIBE naming a TopicName: Flags, MsgId, TopicName (sec 5.4.15).
static void mqttsn_build_subscribe_name(uint8_t *restrict work)
{
    (void)work;
    Mqttsn.n = 0;
    Mqttsn.ok = PROTO_FALSE;
    uint8_t *buf = Mqttsn.buf.out;
    const size_t cap = Mqttsn.buf.cap;
    if (!buf || !Mqttsn.topic.topic_name)
    {
        return;
    }
    size_t nlen = str.len(Mqttsn.topic.topic_name, cap);
    size_t total = 0;
    size_t p = frame_header(buf, cap, MQTTSN_SUBSCRIBE, 1 + MQTTSN_ID_OCTETS + nlen, &total);
    if (!p)
    {
        return;
    }
    buf[p++] = Mqttsn.flags.octet;
    wr16(buf + p, Mqttsn.field.msg_id);
    p += MQTTSN_ID_OCTETS;
    mem.cpy(buf + p, Mqttsn.topic.topic_name, nlen);
    Mqttsn.n = total;
    Mqttsn.ok = PROTO_TRUE;
}

// SUBSCRIBE naming a pre-defined TopicId: Flags, MsgId, TopicId (sec 5.4.15).
static void mqttsn_build_subscribe_id(uint8_t *restrict work)
{
    (void)work;
    Mqttsn.n = 0;
    Mqttsn.ok = PROTO_FALSE;
    uint8_t *buf = Mqttsn.buf.out;
    if (!buf)
    {
        return;
    }
    size_t total = 0;
    size_t p = frame_header(buf, Mqttsn.buf.cap, MQTTSN_SUBSCRIBE, 1 + MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS, &total);
    if (!p)
    {
        return;
    }
    buf[p++] = Mqttsn.flags.octet;
    wr16(buf + p, Mqttsn.field.msg_id);
    p += MQTTSN_ID_OCTETS;
    wr16(buf + p, Mqttsn.topic.topic_id);
    Mqttsn.n = total;
    Mqttsn.ok = PROTO_TRUE;
}

// PINGREQ, with the optional ClientId a woken sleeping client includes (sec 5.4.19, sec 6.14).
static void mqttsn_build_pingreq(uint8_t *restrict work)
{
    (void)work;
    Mqttsn.n = 0;
    Mqttsn.ok = PROTO_FALSE;
    uint8_t *buf = Mqttsn.buf.out;
    const size_t cap = Mqttsn.buf.cap;
    if (!buf)
    {
        return;
    }
    size_t idlen = Mqttsn.field.client_id ? str.len(Mqttsn.field.client_id, cap) : 0;
    size_t total = 0;
    size_t p = frame_header(buf, cap, MQTTSN_PINGREQ, idlen, &total);
    if (!p)
    {
        return;
    }
    if (idlen)
    {
        mem.cpy(buf + p, Mqttsn.field.client_id, idlen);
    }
    Mqttsn.n = total;
    Mqttsn.ok = PROTO_TRUE;
}

// DISCONNECT, carrying the sleep Duration when asked (sec 5.4.21, sec 6.14).
static void mqttsn_build_disconnect(uint8_t *restrict work)
{
    (void)work;
    Mqttsn.n = 0;
    Mqttsn.ok = PROTO_FALSE;
    uint8_t *buf = Mqttsn.buf.out;
    if (!buf)
    {
        return;
    }
    size_t total = 0;
    size_t p =
        frame_header(buf, Mqttsn.buf.cap, MQTTSN_DISCONNECT, Mqttsn.field.with_duration ? MQTTSN_ID_OCTETS : 0, &total);
    if (!p)
    {
        return;
    }
    if (Mqttsn.field.with_duration)
    {
        wr16(buf + p, Mqttsn.field.duration);
    }
    Mqttsn.n = total;
    Mqttsn.ok = PROTO_TRUE;
}

// SEARCHGW: Radius (sec 5.4.2).
static void mqttsn_build_searchgw(uint8_t *restrict work)
{
    (void)work;
    Mqttsn.n = 0;
    Mqttsn.ok = PROTO_FALSE;
    uint8_t *buf = Mqttsn.buf.out;
    if (!buf)
    {
        return;
    }
    size_t total = 0;
    size_t p = frame_header(buf, Mqttsn.buf.cap, MQTTSN_SEARCHGW, 1, &total);
    if (!p)
    {
        return;
    }
    buf[p] = Mqttsn.field.radius;
    Mqttsn.n = total;
    Mqttsn.ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Parsers
// ---------------------------------------------------------------------------

// The Length and MsgType at the head of the buffer, and the Message Variable Part behind them
// (sec 5.2). n reports the whole message length so the caller can advance.
static void mqttsn_parse_header(uint8_t *restrict work)
{
    (void)work;
    Mqttsn.n = 0;
    Mqttsn.ok = PROTO_FALSE;
    const uint8_t *buf = Mqttsn.buf.in;
    const size_t len = Mqttsn.buf.avail;
    if (!buf || len < 2)
    {
        return;
    }
    size_t lenfield;
    size_t total;
    if (buf[0] == MQTTSN_LEN3_PREFIX)
    {
        if (len < MQTTSN_LEN3_OCTETS)
        {
            return;
        }
        total = ((size_t)buf[1] << 8) | buf[2];
        lenfield = MQTTSN_LEN3_OCTETS;
    }
    else
    {
        total = buf[0];
        lenfield = 1;
    }
    if (total < lenfield + 1)
    {
        return; // the Length must cover itself and a MsgType octet
    }
    if (total > len)
    {
        return; // the message is not fully buffered yet
    }
    Mqttsn.header.msg_type = buf[lenfield];
    Mqttsn.header.variable = buf + lenfield + 1;
    Mqttsn.header.variable_len = total - lenfield - 1;
    Mqttsn.n = total;
    Mqttsn.ok = PROTO_TRUE;
}

// CONNACK: ReturnCode (sec 5.4.5).
static void mqttsn_parse_connack(uint8_t *restrict work)
{
    (void)work;
    Mqttsn.ok = PROTO_FALSE;
    if (!Mqttsn.buf.in || Mqttsn.buf.avail < 1)
    {
        return;
    }
    Mqttsn.field.return_code = Mqttsn.buf.in[0];
    Mqttsn.ok = PROTO_TRUE;
}

// REGACK: TopicId, MsgId, ReturnCode (sec 5.4.11).
static void mqttsn_parse_regack(uint8_t *restrict work)
{
    (void)work;
    Mqttsn.ok = PROTO_FALSE;
    const uint8_t *p = Mqttsn.buf.in;
    if (!p || Mqttsn.buf.avail < MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS + 1)
    {
        return;
    }
    Mqttsn.topic.topic_id = rd16(p);
    Mqttsn.field.msg_id = rd16(p + MQTTSN_ID_OCTETS);
    Mqttsn.field.return_code = p[MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS];
    Mqttsn.ok = PROTO_TRUE;
}

// PUBACK, whose Message Variable Part has REGACK's layout (sec 5.4.13).
static void mqttsn_parse_puback(uint8_t *restrict work)
{
    mqttsn_parse_regack(work);
}

// SUBACK: Flags, TopicId, MsgId, ReturnCode (sec 5.4.16).
static void mqttsn_parse_suback(uint8_t *restrict work)
{
    (void)work;
    Mqttsn.ok = PROTO_FALSE;
    const uint8_t *p = Mqttsn.buf.in;
    if (!p || Mqttsn.buf.avail < 1 + MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS + 1)
    {
        return;
    }
    Mqttsn.flags.octet = p[0];
    Mqttsn.topic.topic_id = rd16(p + 1);
    Mqttsn.field.msg_id = rd16(p + 1 + MQTTSN_ID_OCTETS);
    Mqttsn.field.return_code = p[1 + MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS];
    Mqttsn.ok = PROTO_TRUE;
}

// PUBLISH: Flags, TopicId, MsgId, then the Data that fills the rest (sec 5.4.12).
static void mqttsn_parse_publish(uint8_t *restrict work)
{
    (void)work;
    Mqttsn.ok = PROTO_FALSE;
    const uint8_t *p = Mqttsn.buf.in;
    const size_t len = Mqttsn.buf.avail;
    const size_t head = 1 + MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS;
    if (!p || len < head)
    {
        return;
    }
    Mqttsn.flags.octet = p[0];
    Mqttsn.topic.topic_id = rd16(p + 1);
    Mqttsn.field.msg_id = rd16(p + 1 + MQTTSN_ID_OCTETS);
    Mqttsn.data.data = p + head;
    Mqttsn.data.data_len = len - head;
    Mqttsn.ok = PROTO_TRUE;
}

// REGISTER: TopicId, MsgId, then the TopicName that fills the rest (sec 5.4.10).
static void mqttsn_parse_register(uint8_t *restrict work)
{
    (void)work;
    Mqttsn.ok = PROTO_FALSE;
    const uint8_t *p = Mqttsn.buf.in;
    const size_t len = Mqttsn.buf.avail;
    const size_t head = MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS;
    if (!p || len < head)
    {
        return;
    }
    Mqttsn.topic.topic_id = rd16(p);
    Mqttsn.field.msg_id = rd16(p + MQTTSN_ID_OCTETS);
    Mqttsn.topic.topic_name = (const char *)(p + head);
    Mqttsn.topic.topic_name_len = len - head;
    Mqttsn.ok = PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
MqttsnNs Mqttsn = {.make_flags = mqttsn_make_flags,
                   .build_connect = mqttsn_build_connect,
                   .build_register = mqttsn_build_register,
                   .build_regack = mqttsn_build_regack,
                   .build_publish = mqttsn_build_publish,
                   .build_puback = mqttsn_build_puback,
                   .build_subscribe_name = mqttsn_build_subscribe_name,
                   .build_subscribe_id = mqttsn_build_subscribe_id,
                   .build_pingreq = mqttsn_build_pingreq,
                   .build_disconnect = mqttsn_build_disconnect,
                   .build_searchgw = mqttsn_build_searchgw,
                   .parse_header = mqttsn_parse_header,
                   .parse_connack = mqttsn_parse_connack,
                   .parse_regack = mqttsn_parse_regack,
                   .parse_puback = mqttsn_parse_puback,
                   .parse_suback = mqttsn_parse_suback,
                   .parse_publish = mqttsn_parse_publish,
                   .parse_register = mqttsn_parse_register};

#endif // PROTOCORE_ENABLE_MQTT_SN
