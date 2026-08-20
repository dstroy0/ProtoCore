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

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_MQTT_SN

#include "services/iot/mqtt/mqtt_sn/mqtt_sn.h"

#include "mmgr/protomem/protomem.h" // mem.cpy: the ClientId, TopicName and Data spans
#include "mmgr/protostr/protostr.h" // str.len: their bounded lengths

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

PROTOCORE_BEGIN_DECLS

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

void protocore_mqttsn_make_flags(uint8_t *restrict work)
{
    (void)work;
    uint8_t f = 0;
    if (MqttsnV.flags.dup)
    {
        f |= MQTTSN_FLAG_DUP;
    }
    f |= (uint8_t)((MqttsnV.flags.qos << MQTTSN_FLAG_QOS_SHIFT) & MQTTSN_FLAG_QOS_MASK);
    if (MqttsnV.flags.retain)
    {
        f |= MQTTSN_FLAG_RETAIN;
    }
    if (MqttsnV.flags.will)
    {
        f |= MQTTSN_FLAG_WILL;
    }
    if (MqttsnV.flags.clean_session)
    {
        f |= MQTTSN_FLAG_CLEAN;
    }
    f |= (uint8_t)(MqttsnV.flags.topic_id_type & MQTTSN_FLAG_TOPICIDTYPE_MASK);
    MqttsnV.flags.octet = f;
    MqttsnV.ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Builders
// ---------------------------------------------------------------------------

// CONNECT: Flags, ProtocolId, Duration, ClientId (sec 5.4.4).
void protocore_mqttsn_build_connect(uint8_t *restrict work)
{
    (void)work;
    MqttsnV.n = 0;
    MqttsnV.ok = PROTO_FALSE;
    uint8_t *buf = MqttsnV.buf.out;
    const size_t cap = MqttsnV.buf.cap;
    if (!buf || !MqttsnV.field.client_id)
    {
        return;
    }
    size_t idlen = str.len(MqttsnV.field.client_id, cap);
    size_t total = 0;
    size_t p = frame_header(buf, cap, MQTTSN_CONNECT, 1 + 1 + MQTTSN_ID_OCTETS + idlen, &total);
    if (!p)
    {
        return;
    }
    buf[p++] = MqttsnV.flags.octet;
    buf[p++] = MQTTSN_PROTOCOL_ID;
    wr16(buf + p, MqttsnV.field.duration);
    p += MQTTSN_ID_OCTETS;
    mem.cpy(buf + p, MqttsnV.field.client_id, idlen);
    MqttsnV.n = total;
    MqttsnV.ok = PROTO_TRUE;
}

// REGISTER: TopicId, MsgId, TopicName (sec 5.4.10).
void protocore_mqttsn_build_register(uint8_t *restrict work)
{
    (void)work;
    MqttsnV.n = 0;
    MqttsnV.ok = PROTO_FALSE;
    uint8_t *buf = MqttsnV.buf.out;
    const size_t cap = MqttsnV.buf.cap;
    if (!buf || !MqttsnV.topic.topic_name)
    {
        return;
    }
    size_t nlen = str.len(MqttsnV.topic.topic_name, cap);
    size_t total = 0;
    size_t p = frame_header(buf, cap, MQTTSN_REGISTER, MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS + nlen, &total);
    if (!p)
    {
        return;
    }
    wr16(buf + p, MqttsnV.topic.topic_id);
    p += MQTTSN_ID_OCTETS;
    wr16(buf + p, MqttsnV.field.msg_id);
    p += MQTTSN_ID_OCTETS;
    mem.cpy(buf + p, MqttsnV.topic.topic_name, nlen);
    MqttsnV.n = total;
    MqttsnV.ok = PROTO_TRUE;
}

// REGACK: TopicId, MsgId, ReturnCode (sec 5.4.11).
void protocore_mqttsn_build_regack(uint8_t *restrict work)
{
    (void)work;
    MqttsnV.n = 0;
    MqttsnV.ok = PROTO_FALSE;
    uint8_t *buf = MqttsnV.buf.out;
    if (!buf)
    {
        return;
    }
    size_t total = 0;
    size_t p = frame_header(buf, MqttsnV.buf.cap, MQTTSN_REGACK, MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS + 1, &total);
    if (!p)
    {
        return;
    }
    wr16(buf + p, MqttsnV.topic.topic_id);
    p += MQTTSN_ID_OCTETS;
    wr16(buf + p, MqttsnV.field.msg_id);
    p += MQTTSN_ID_OCTETS;
    buf[p] = MqttsnV.field.return_code;
    MqttsnV.n = total;
    MqttsnV.ok = PROTO_TRUE;
}

// PUBLISH: Flags, TopicId, MsgId, Data (sec 5.4.12).
void protocore_mqttsn_build_publish(uint8_t *restrict work)
{
    (void)work;
    MqttsnV.n = 0;
    MqttsnV.ok = PROTO_FALSE;
    uint8_t *buf = MqttsnV.buf.out;
    const size_t data_len = MqttsnV.data.data_len;
    if (!buf || (data_len && !MqttsnV.data.data))
    {
        return;
    }
    size_t total = 0;
    size_t p =
        frame_header(buf, MqttsnV.buf.cap, MQTTSN_PUBLISH, 1 + MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS + data_len, &total);
    if (!p)
    {
        return;
    }
    buf[p++] = MqttsnV.flags.octet;
    wr16(buf + p, MqttsnV.topic.topic_id);
    p += MQTTSN_ID_OCTETS;
    wr16(buf + p, MqttsnV.field.msg_id);
    p += MQTTSN_ID_OCTETS;
    if (data_len)
    {
        mem.cpy(buf + p, MqttsnV.data.data, data_len);
    }
    MqttsnV.n = total;
    MqttsnV.ok = PROTO_TRUE;
}

// PUBACK: TopicId, MsgId, ReturnCode (sec 5.4.13).
void protocore_mqttsn_build_puback(uint8_t *restrict work)
{
    (void)work;
    MqttsnV.n = 0;
    MqttsnV.ok = PROTO_FALSE;
    uint8_t *buf = MqttsnV.buf.out;
    if (!buf)
    {
        return;
    }
    size_t total = 0;
    size_t p = frame_header(buf, MqttsnV.buf.cap, MQTTSN_PUBACK, MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS + 1, &total);
    if (!p)
    {
        return;
    }
    wr16(buf + p, MqttsnV.topic.topic_id);
    p += MQTTSN_ID_OCTETS;
    wr16(buf + p, MqttsnV.field.msg_id);
    p += MQTTSN_ID_OCTETS;
    buf[p] = MqttsnV.field.return_code;
    MqttsnV.n = total;
    MqttsnV.ok = PROTO_TRUE;
}

// SUBSCRIBE naming a TopicName: Flags, MsgId, TopicName (sec 5.4.15).
void protocore_mqttsn_build_subscribe_name(uint8_t *restrict work)
{
    (void)work;
    MqttsnV.n = 0;
    MqttsnV.ok = PROTO_FALSE;
    uint8_t *buf = MqttsnV.buf.out;
    const size_t cap = MqttsnV.buf.cap;
    if (!buf || !MqttsnV.topic.topic_name)
    {
        return;
    }
    size_t nlen = str.len(MqttsnV.topic.topic_name, cap);
    size_t total = 0;
    size_t p = frame_header(buf, cap, MQTTSN_SUBSCRIBE, 1 + MQTTSN_ID_OCTETS + nlen, &total);
    if (!p)
    {
        return;
    }
    buf[p++] = MqttsnV.flags.octet;
    wr16(buf + p, MqttsnV.field.msg_id);
    p += MQTTSN_ID_OCTETS;
    mem.cpy(buf + p, MqttsnV.topic.topic_name, nlen);
    MqttsnV.n = total;
    MqttsnV.ok = PROTO_TRUE;
}

// SUBSCRIBE naming a pre-defined TopicId: Flags, MsgId, TopicId (sec 5.4.15).
void protocore_mqttsn_build_subscribe_id(uint8_t *restrict work)
{
    (void)work;
    MqttsnV.n = 0;
    MqttsnV.ok = PROTO_FALSE;
    uint8_t *buf = MqttsnV.buf.out;
    if (!buf)
    {
        return;
    }
    size_t total = 0;
    size_t p = frame_header(buf, MqttsnV.buf.cap, MQTTSN_SUBSCRIBE, 1 + MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS, &total);
    if (!p)
    {
        return;
    }
    buf[p++] = MqttsnV.flags.octet;
    wr16(buf + p, MqttsnV.field.msg_id);
    p += MQTTSN_ID_OCTETS;
    wr16(buf + p, MqttsnV.topic.topic_id);
    MqttsnV.n = total;
    MqttsnV.ok = PROTO_TRUE;
}

// PINGREQ, with the optional ClientId a woken sleeping client includes (sec 5.4.19, sec 6.14).
void protocore_mqttsn_build_pingreq(uint8_t *restrict work)
{
    (void)work;
    MqttsnV.n = 0;
    MqttsnV.ok = PROTO_FALSE;
    uint8_t *buf = MqttsnV.buf.out;
    const size_t cap = MqttsnV.buf.cap;
    if (!buf)
    {
        return;
    }
    size_t idlen = MqttsnV.field.client_id ? str.len(MqttsnV.field.client_id, cap) : 0;
    size_t total = 0;
    size_t p = frame_header(buf, cap, MQTTSN_PINGREQ, idlen, &total);
    if (!p)
    {
        return;
    }
    if (idlen)
    {
        mem.cpy(buf + p, MqttsnV.field.client_id, idlen);
    }
    MqttsnV.n = total;
    MqttsnV.ok = PROTO_TRUE;
}

// DISCONNECT, carrying the sleep Duration when asked (sec 5.4.21, sec 6.14).
void protocore_mqttsn_build_disconnect(uint8_t *restrict work)
{
    (void)work;
    MqttsnV.n = 0;
    MqttsnV.ok = PROTO_FALSE;
    uint8_t *buf = MqttsnV.buf.out;
    if (!buf)
    {
        return;
    }
    size_t total = 0;
    size_t p = frame_header(buf, MqttsnV.buf.cap, MQTTSN_DISCONNECT, MqttsnV.field.with_duration ? MQTTSN_ID_OCTETS : 0,
                            &total);
    if (!p)
    {
        return;
    }
    if (MqttsnV.field.with_duration)
    {
        wr16(buf + p, MqttsnV.field.duration);
    }
    MqttsnV.n = total;
    MqttsnV.ok = PROTO_TRUE;
}

// SEARCHGW: Radius (sec 5.4.2).
void protocore_mqttsn_build_searchgw(uint8_t *restrict work)
{
    (void)work;
    MqttsnV.n = 0;
    MqttsnV.ok = PROTO_FALSE;
    uint8_t *buf = MqttsnV.buf.out;
    if (!buf)
    {
        return;
    }
    size_t total = 0;
    size_t p = frame_header(buf, MqttsnV.buf.cap, MQTTSN_SEARCHGW, 1, &total);
    if (!p)
    {
        return;
    }
    buf[p] = MqttsnV.field.radius;
    MqttsnV.n = total;
    MqttsnV.ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Parsers
// ---------------------------------------------------------------------------

// The Length and MsgType at the head of the buffer, and the Message Variable Part behind them
// (sec 5.2). n reports the whole message length so the caller can advance.
void protocore_mqttsn_parse_header(uint8_t *restrict work)
{
    (void)work;
    MqttsnV.n = 0;
    MqttsnV.ok = PROTO_FALSE;
    const uint8_t *buf = MqttsnV.buf.in;
    const size_t len = MqttsnV.buf.avail;
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
    MqttsnV.header.msg_type = buf[lenfield];
    MqttsnV.header.variable = buf + lenfield + 1;
    MqttsnV.header.variable_len = total - lenfield - 1;
    MqttsnV.n = total;
    MqttsnV.ok = PROTO_TRUE;
}

// CONNACK: ReturnCode (sec 5.4.5).
void protocore_mqttsn_parse_connack(uint8_t *restrict work)
{
    (void)work;
    MqttsnV.ok = PROTO_FALSE;
    if (!MqttsnV.buf.in || MqttsnV.buf.avail < 1)
    {
        return;
    }
    MqttsnV.field.return_code = MqttsnV.buf.in[0];
    MqttsnV.ok = PROTO_TRUE;
}

// REGACK: TopicId, MsgId, ReturnCode (sec 5.4.11).
void protocore_mqttsn_parse_regack(uint8_t *restrict work)
{
    (void)work;
    MqttsnV.ok = PROTO_FALSE;
    const uint8_t *p = MqttsnV.buf.in;
    if (!p || MqttsnV.buf.avail < MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS + 1)
    {
        return;
    }
    MqttsnV.topic.topic_id = rd16(p);
    MqttsnV.field.msg_id = rd16(p + MQTTSN_ID_OCTETS);
    MqttsnV.field.return_code = p[MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS];
    MqttsnV.ok = PROTO_TRUE;
}

// PUBACK, whose Message Variable Part has REGACK's layout (sec 5.4.13).
void protocore_mqttsn_parse_puback(uint8_t *restrict work)
{
    protocore_mqttsn_parse_regack(work);
}

// SUBACK: Flags, TopicId, MsgId, ReturnCode (sec 5.4.16).
void protocore_mqttsn_parse_suback(uint8_t *restrict work)
{
    (void)work;
    MqttsnV.ok = PROTO_FALSE;
    const uint8_t *p = MqttsnV.buf.in;
    if (!p || MqttsnV.buf.avail < 1 + MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS + 1)
    {
        return;
    }
    MqttsnV.flags.octet = p[0];
    MqttsnV.topic.topic_id = rd16(p + 1);
    MqttsnV.field.msg_id = rd16(p + 1 + MQTTSN_ID_OCTETS);
    MqttsnV.field.return_code = p[1 + MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS];
    MqttsnV.ok = PROTO_TRUE;
}

// PUBLISH: Flags, TopicId, MsgId, then the Data that fills the rest (sec 5.4.12).
void protocore_mqttsn_parse_publish(uint8_t *restrict work)
{
    (void)work;
    MqttsnV.ok = PROTO_FALSE;
    const uint8_t *p = MqttsnV.buf.in;
    const size_t len = MqttsnV.buf.avail;
    const size_t head = 1 + MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS;
    if (!p || len < head)
    {
        return;
    }
    MqttsnV.flags.octet = p[0];
    MqttsnV.topic.topic_id = rd16(p + 1);
    MqttsnV.field.msg_id = rd16(p + 1 + MQTTSN_ID_OCTETS);
    MqttsnV.data.data = p + head;
    MqttsnV.data.data_len = len - head;
    MqttsnV.ok = PROTO_TRUE;
}

// REGISTER: TopicId, MsgId, then the TopicName that fills the rest (sec 5.4.10).
void protocore_mqttsn_parse_register(uint8_t *restrict work)
{
    (void)work;
    MqttsnV.ok = PROTO_FALSE;
    const uint8_t *p = MqttsnV.buf.in;
    const size_t len = MqttsnV.buf.avail;
    const size_t head = MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS;
    if (!p || len < head)
    {
        return;
    }
    MqttsnV.topic.topic_id = rd16(p);
    MqttsnV.field.msg_id = rd16(p + MQTTSN_ID_OCTETS);
    MqttsnV.topic.topic_name = (const char *)(p + head);
    MqttsnV.topic.topic_name_len = len - head;
    MqttsnV.ok = PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
MqttsnVars MqttsnV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_MQTT_SN
