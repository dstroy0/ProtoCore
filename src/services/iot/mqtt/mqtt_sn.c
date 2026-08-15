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

/**
 * @brief The codec's calls - what MqttsnNs points at.
 *
 * @var MqttsnInternal::ns  the handle a caller sets a call's members on
 *
 * No storage member: the codec works in the caller's buffer and holds nothing between calls.
 */
struct MqttsnInternal
{
    MqttsnNs *ns;
};

static struct MqttsnInternal s_mqttsn = {.ns = &Mqttsn};

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

static void mqttsn_make_flags(struct MqttsnInternal *restrict ctx)
{
    uint8_t f = 0;
    if (ctx->ns->flags.dup)
    {
        f |= MQTTSN_FLAG_DUP;
    }
    f |= (uint8_t)((ctx->ns->flags.qos << MQTTSN_FLAG_QOS_SHIFT) & MQTTSN_FLAG_QOS_MASK);
    if (ctx->ns->flags.retain)
    {
        f |= MQTTSN_FLAG_RETAIN;
    }
    if (ctx->ns->flags.will)
    {
        f |= MQTTSN_FLAG_WILL;
    }
    if (ctx->ns->flags.clean_session)
    {
        f |= MQTTSN_FLAG_CLEAN;
    }
    f |= (uint8_t)(ctx->ns->flags.topic_id_type & MQTTSN_FLAG_TOPICIDTYPE_MASK);
    ctx->ns->flags.octet = f;
    ctx->ns->ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Builders
// ---------------------------------------------------------------------------

// CONNECT: Flags, ProtocolId, Duration, ClientId (sec 5.4.4).
static void mqttsn_build_connect(struct MqttsnInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    uint8_t *buf = ctx->ns->buf.out;
    const size_t cap = ctx->ns->buf.cap;
    if (!buf || !ctx->ns->field.client_id)
    {
        return;
    }
    size_t idlen = str.len(ctx->ns->field.client_id, cap);
    size_t total = 0;
    size_t p = frame_header(buf, cap, MQTTSN_CONNECT, 1 + 1 + MQTTSN_ID_OCTETS + idlen, &total);
    if (!p)
    {
        return;
    }
    buf[p++] = ctx->ns->flags.octet;
    buf[p++] = MQTTSN_PROTOCOL_ID;
    wr16(buf + p, ctx->ns->field.duration);
    p += MQTTSN_ID_OCTETS;
    mem.cpy(buf + p, ctx->ns->field.client_id, idlen);
    ctx->ns->n = total;
    ctx->ns->ok = PROTO_TRUE;
}

// REGISTER: TopicId, MsgId, TopicName (sec 5.4.10).
static void mqttsn_build_register(struct MqttsnInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    uint8_t *buf = ctx->ns->buf.out;
    const size_t cap = ctx->ns->buf.cap;
    if (!buf || !ctx->ns->topic.topic_name)
    {
        return;
    }
    size_t nlen = str.len(ctx->ns->topic.topic_name, cap);
    size_t total = 0;
    size_t p = frame_header(buf, cap, MQTTSN_REGISTER, MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS + nlen, &total);
    if (!p)
    {
        return;
    }
    wr16(buf + p, ctx->ns->topic.topic_id);
    p += MQTTSN_ID_OCTETS;
    wr16(buf + p, ctx->ns->field.msg_id);
    p += MQTTSN_ID_OCTETS;
    mem.cpy(buf + p, ctx->ns->topic.topic_name, nlen);
    ctx->ns->n = total;
    ctx->ns->ok = PROTO_TRUE;
}

// REGACK: TopicId, MsgId, ReturnCode (sec 5.4.11).
static void mqttsn_build_regack(struct MqttsnInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    uint8_t *buf = ctx->ns->buf.out;
    if (!buf)
    {
        return;
    }
    size_t total = 0;
    size_t p = frame_header(buf, ctx->ns->buf.cap, MQTTSN_REGACK, MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS + 1, &total);
    if (!p)
    {
        return;
    }
    wr16(buf + p, ctx->ns->topic.topic_id);
    p += MQTTSN_ID_OCTETS;
    wr16(buf + p, ctx->ns->field.msg_id);
    p += MQTTSN_ID_OCTETS;
    buf[p] = ctx->ns->field.return_code;
    ctx->ns->n = total;
    ctx->ns->ok = PROTO_TRUE;
}

// PUBLISH: Flags, TopicId, MsgId, Data (sec 5.4.12).
static void mqttsn_build_publish(struct MqttsnInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    uint8_t *buf = ctx->ns->buf.out;
    const size_t data_len = ctx->ns->data.data_len;
    if (!buf || (data_len && !ctx->ns->data.data))
    {
        return;
    }
    size_t total = 0;
    size_t p =
        frame_header(buf, ctx->ns->buf.cap, MQTTSN_PUBLISH, 1 + MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS + data_len, &total);
    if (!p)
    {
        return;
    }
    buf[p++] = ctx->ns->flags.octet;
    wr16(buf + p, ctx->ns->topic.topic_id);
    p += MQTTSN_ID_OCTETS;
    wr16(buf + p, ctx->ns->field.msg_id);
    p += MQTTSN_ID_OCTETS;
    if (data_len)
    {
        mem.cpy(buf + p, ctx->ns->data.data, data_len);
    }
    ctx->ns->n = total;
    ctx->ns->ok = PROTO_TRUE;
}

// PUBACK: TopicId, MsgId, ReturnCode (sec 5.4.13).
static void mqttsn_build_puback(struct MqttsnInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    uint8_t *buf = ctx->ns->buf.out;
    if (!buf)
    {
        return;
    }
    size_t total = 0;
    size_t p = frame_header(buf, ctx->ns->buf.cap, MQTTSN_PUBACK, MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS + 1, &total);
    if (!p)
    {
        return;
    }
    wr16(buf + p, ctx->ns->topic.topic_id);
    p += MQTTSN_ID_OCTETS;
    wr16(buf + p, ctx->ns->field.msg_id);
    p += MQTTSN_ID_OCTETS;
    buf[p] = ctx->ns->field.return_code;
    ctx->ns->n = total;
    ctx->ns->ok = PROTO_TRUE;
}

// SUBSCRIBE naming a TopicName: Flags, MsgId, TopicName (sec 5.4.15).
static void mqttsn_build_subscribe_name(struct MqttsnInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    uint8_t *buf = ctx->ns->buf.out;
    const size_t cap = ctx->ns->buf.cap;
    if (!buf || !ctx->ns->topic.topic_name)
    {
        return;
    }
    size_t nlen = str.len(ctx->ns->topic.topic_name, cap);
    size_t total = 0;
    size_t p = frame_header(buf, cap, MQTTSN_SUBSCRIBE, 1 + MQTTSN_ID_OCTETS + nlen, &total);
    if (!p)
    {
        return;
    }
    buf[p++] = ctx->ns->flags.octet;
    wr16(buf + p, ctx->ns->field.msg_id);
    p += MQTTSN_ID_OCTETS;
    mem.cpy(buf + p, ctx->ns->topic.topic_name, nlen);
    ctx->ns->n = total;
    ctx->ns->ok = PROTO_TRUE;
}

// SUBSCRIBE naming a pre-defined TopicId: Flags, MsgId, TopicId (sec 5.4.15).
static void mqttsn_build_subscribe_id(struct MqttsnInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    uint8_t *buf = ctx->ns->buf.out;
    if (!buf)
    {
        return;
    }
    size_t total = 0;
    size_t p = frame_header(buf, ctx->ns->buf.cap, MQTTSN_SUBSCRIBE, 1 + MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS, &total);
    if (!p)
    {
        return;
    }
    buf[p++] = ctx->ns->flags.octet;
    wr16(buf + p, ctx->ns->field.msg_id);
    p += MQTTSN_ID_OCTETS;
    wr16(buf + p, ctx->ns->topic.topic_id);
    ctx->ns->n = total;
    ctx->ns->ok = PROTO_TRUE;
}

// PINGREQ, with the optional ClientId a woken sleeping client includes (sec 5.4.19, sec 6.14).
static void mqttsn_build_pingreq(struct MqttsnInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    uint8_t *buf = ctx->ns->buf.out;
    const size_t cap = ctx->ns->buf.cap;
    if (!buf)
    {
        return;
    }
    size_t idlen = ctx->ns->field.client_id ? str.len(ctx->ns->field.client_id, cap) : 0;
    size_t total = 0;
    size_t p = frame_header(buf, cap, MQTTSN_PINGREQ, idlen, &total);
    if (!p)
    {
        return;
    }
    if (idlen)
    {
        mem.cpy(buf + p, ctx->ns->field.client_id, idlen);
    }
    ctx->ns->n = total;
    ctx->ns->ok = PROTO_TRUE;
}

// DISCONNECT, carrying the sleep Duration when asked (sec 5.4.21, sec 6.14).
static void mqttsn_build_disconnect(struct MqttsnInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    uint8_t *buf = ctx->ns->buf.out;
    if (!buf)
    {
        return;
    }
    size_t total = 0;
    size_t p = frame_header(buf, ctx->ns->buf.cap, MQTTSN_DISCONNECT,
                            ctx->ns->field.with_duration ? MQTTSN_ID_OCTETS : 0, &total);
    if (!p)
    {
        return;
    }
    if (ctx->ns->field.with_duration)
    {
        wr16(buf + p, ctx->ns->field.duration);
    }
    ctx->ns->n = total;
    ctx->ns->ok = PROTO_TRUE;
}

// SEARCHGW: Radius (sec 5.4.2).
static void mqttsn_build_searchgw(struct MqttsnInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    uint8_t *buf = ctx->ns->buf.out;
    if (!buf)
    {
        return;
    }
    size_t total = 0;
    size_t p = frame_header(buf, ctx->ns->buf.cap, MQTTSN_SEARCHGW, 1, &total);
    if (!p)
    {
        return;
    }
    buf[p] = ctx->ns->field.radius;
    ctx->ns->n = total;
    ctx->ns->ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Parsers
// ---------------------------------------------------------------------------

// The Length and MsgType at the head of the buffer, and the Message Variable Part behind them
// (sec 5.2). n reports the whole message length so the caller can advance.
static void mqttsn_parse_header(struct MqttsnInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    const uint8_t *buf = ctx->ns->buf.in;
    const size_t len = ctx->ns->buf.avail;
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
    ctx->ns->header.msg_type = buf[lenfield];
    ctx->ns->header.variable = buf + lenfield + 1;
    ctx->ns->header.variable_len = total - lenfield - 1;
    ctx->ns->n = total;
    ctx->ns->ok = PROTO_TRUE;
}

// CONNACK: ReturnCode (sec 5.4.5).
static void mqttsn_parse_connack(struct MqttsnInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->buf.in || ctx->ns->buf.avail < 1)
    {
        return;
    }
    ctx->ns->field.return_code = ctx->ns->buf.in[0];
    ctx->ns->ok = PROTO_TRUE;
}

// REGACK: TopicId, MsgId, ReturnCode (sec 5.4.11).
static void mqttsn_parse_regack(struct MqttsnInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    const uint8_t *p = ctx->ns->buf.in;
    if (!p || ctx->ns->buf.avail < MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS + 1)
    {
        return;
    }
    ctx->ns->topic.topic_id = rd16(p);
    ctx->ns->field.msg_id = rd16(p + MQTTSN_ID_OCTETS);
    ctx->ns->field.return_code = p[MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS];
    ctx->ns->ok = PROTO_TRUE;
}

// PUBACK, whose Message Variable Part has REGACK's layout (sec 5.4.13).
static void mqttsn_parse_puback(struct MqttsnInternal *restrict ctx)
{
    mqttsn_parse_regack(ctx);
}

// SUBACK: Flags, TopicId, MsgId, ReturnCode (sec 5.4.16).
static void mqttsn_parse_suback(struct MqttsnInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    const uint8_t *p = ctx->ns->buf.in;
    if (!p || ctx->ns->buf.avail < 1 + MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS + 1)
    {
        return;
    }
    ctx->ns->flags.octet = p[0];
    ctx->ns->topic.topic_id = rd16(p + 1);
    ctx->ns->field.msg_id = rd16(p + 1 + MQTTSN_ID_OCTETS);
    ctx->ns->field.return_code = p[1 + MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS];
    ctx->ns->ok = PROTO_TRUE;
}

// PUBLISH: Flags, TopicId, MsgId, then the Data that fills the rest (sec 5.4.12).
static void mqttsn_parse_publish(struct MqttsnInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    const uint8_t *p = ctx->ns->buf.in;
    const size_t len = ctx->ns->buf.avail;
    const size_t head = 1 + MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS;
    if (!p || len < head)
    {
        return;
    }
    ctx->ns->flags.octet = p[0];
    ctx->ns->topic.topic_id = rd16(p + 1);
    ctx->ns->field.msg_id = rd16(p + 1 + MQTTSN_ID_OCTETS);
    ctx->ns->data.data = p + head;
    ctx->ns->data.data_len = len - head;
    ctx->ns->ok = PROTO_TRUE;
}

// REGISTER: TopicId, MsgId, then the TopicName that fills the rest (sec 5.4.10).
static void mqttsn_parse_register(struct MqttsnInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    const uint8_t *p = ctx->ns->buf.in;
    const size_t len = ctx->ns->buf.avail;
    const size_t head = MQTTSN_ID_OCTETS + MQTTSN_ID_OCTETS;
    if (!p || len < head)
    {
        return;
    }
    ctx->ns->topic.topic_id = rd16(p);
    ctx->ns->field.msg_id = rd16(p + MQTTSN_ID_OCTETS);
    ctx->ns->topic.topic_name = (const char *)(p + head);
    ctx->ns->topic.topic_name_len = len - head;
    ctx->ns->ok = PROTO_TRUE;
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
                   .parse_register = mqttsn_parse_register,
                   .internal = &s_mqttsn};

#endif // PROTOCORE_ENABLE_MQTT_SN
