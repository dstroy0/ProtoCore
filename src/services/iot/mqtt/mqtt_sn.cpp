// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mqtt_sn.cpp
 * @brief MQTT-SN v1.2 message builder + parser (pure, host-tested).
 */

#include "services/iot/mqtt/mqtt_sn.h"

#if PC_ENABLE_MQTT_SN

#include <string.h>

uint8_t pc_mqttsn_make_flags(bool dup, uint8_t qos, bool retain, bool will, bool clean, uint8_t topic_id_type)
{
    uint8_t f = 0;
    if (dup)
    {
        f |= MQTTSN_FLAG_DUP;
    }
    f |= (uint8_t)((qos & 0x03) << MQTTSN_FLAG_QOS_SHIFT) & MQTTSN_FLAG_QOS_MASK;
    if (retain)
    {
        f |= MQTTSN_FLAG_RETAIN;
    }
    if (will)
    {
        f |= MQTTSN_FLAG_WILL;
    }
    if (clean)
    {
        f |= MQTTSN_FLAG_CLEAN;
    }
    f |= (uint8_t)(topic_id_type & MQTTSN_FLAG_TOPICIDTYPE_MASK);
    return f;
}

// Write the [Length][MsgType] header for a message carrying @p body_len body octets.
// Returns the body-start offset (always >= 2) and sets *total to the full message length,
// or 0 on overflow / a body too large for the 16-bit Length field. The Length field value
// is the whole message length including the Length field itself (per spec); it is 1 octet
// when that total is <= 255, otherwise 0x01 + a big-endian uint16.
static size_t frame_header(uint8_t *buf, size_t cap, uint8_t msg_type, size_t body_len, size_t *total)
{
    size_t core = 1 + body_len; // MsgType + body
    size_t lenfield;
    size_t t;
    if (1 + core <= 255)
    {
        lenfield = 1;
        t = 1 + core;
    }
    else
    {
        lenfield = 3;
        t = 3 + core;
    }
    if (t > 0xFFFF || t > cap)
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

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

size_t pc_mqttsn_build_connect(uint8_t *buf, size_t cap, uint8_t flags, uint16_t duration, const char *client_id)
{
    if (!buf || !client_id)
    {
        return 0;
    }
    size_t idlen = strnlen(client_id, cap);
    size_t total, p = frame_header(buf, cap, MQTTSN_CONNECT, 1 + 1 + 2 + idlen, &total);
    if (!p)
    {
        return 0;
    }
    buf[p++] = flags;
    buf[p++] = MQTTSN_PROTOCOL_ID;
    wr16(buf + p, duration);
    p += 2;
    memcpy(buf + p, client_id, idlen);
    return total;
}

size_t pc_mqttsn_build_register(uint8_t *buf, size_t cap, uint16_t topic_id, uint16_t msg_id, const char *topic_name)
{
    if (!buf || !topic_name)
    {
        return 0;
    }
    size_t nlen = strnlen(topic_name, cap);
    size_t total, p = frame_header(buf, cap, MQTTSN_REGISTER, 2 + 2 + nlen, &total);
    if (!p)
    {
        return 0;
    }
    wr16(buf + p, topic_id);
    p += 2;
    wr16(buf + p, msg_id);
    p += 2;
    memcpy(buf + p, topic_name, nlen);
    return total;
}

size_t pc_mqttsn_build_regack(uint8_t *buf, size_t cap, uint16_t topic_id, uint16_t msg_id, uint8_t ret_code)
{
    if (!buf)
    {
        return 0;
    }
    size_t total, p = frame_header(buf, cap, MQTTSN_REGACK, 2 + 2 + 1, &total);
    if (!p)
    {
        return 0;
    }
    wr16(buf + p, topic_id);
    p += 2;
    wr16(buf + p, msg_id);
    p += 2;
    buf[p] = ret_code;
    return total;
}

size_t pc_mqttsn_build_publish(uint8_t *buf, size_t cap, uint8_t flags, uint16_t topic_id, uint16_t msg_id,
                               const uint8_t *data, size_t data_len)
{
    if (!buf || (data_len && !data))
    {
        return 0;
    }
    size_t total, p = frame_header(buf, cap, MQTTSN_PUBLISH, 1 + 2 + 2 + data_len, &total);
    if (!p)
    {
        return 0;
    }
    buf[p++] = flags;
    wr16(buf + p, topic_id);
    p += 2;
    wr16(buf + p, msg_id);
    p += 2;
    if (data_len)
    {
        memcpy(buf + p, data, data_len);
    }
    return total;
}

size_t pc_mqttsn_build_puback(uint8_t *buf, size_t cap, uint16_t topic_id, uint16_t msg_id, uint8_t ret_code)
{
    if (!buf)
    {
        return 0;
    }
    size_t total, p = frame_header(buf, cap, MQTTSN_PUBACK, 2 + 2 + 1, &total);
    if (!p)
    {
        return 0;
    }
    wr16(buf + p, topic_id);
    p += 2;
    wr16(buf + p, msg_id);
    p += 2;
    buf[p] = ret_code;
    return total;
}

size_t pc_mqttsn_build_subscribe_name(uint8_t *buf, size_t cap, uint8_t flags, uint16_t msg_id, const char *topic_name)
{
    if (!buf || !topic_name)
    {
        return 0;
    }
    size_t nlen = strnlen(topic_name, cap);
    size_t total, p = frame_header(buf, cap, MQTTSN_SUBSCRIBE, 1 + 2 + nlen, &total);
    if (!p)
    {
        return 0;
    }
    buf[p++] = flags;
    wr16(buf + p, msg_id);
    p += 2;
    memcpy(buf + p, topic_name, nlen);
    return total;
}

size_t pc_mqttsn_build_subscribe_id(uint8_t *buf, size_t cap, uint8_t flags, uint16_t msg_id, uint16_t topic_id)
{
    if (!buf)
    {
        return 0;
    }
    size_t total, p = frame_header(buf, cap, MQTTSN_SUBSCRIBE, 1 + 2 + 2, &total);
    if (!p)
    {
        return 0;
    }
    buf[p++] = flags;
    wr16(buf + p, msg_id);
    p += 2;
    wr16(buf + p, topic_id);
    return total;
}

size_t pc_mqttsn_build_pingreq(uint8_t *buf, size_t cap, const char *client_id)
{
    if (!buf)
    {
        return 0;
    }
    size_t idlen = client_id ? strnlen(client_id, cap) : 0;
    size_t total, p = frame_header(buf, cap, MQTTSN_PINGREQ, idlen, &total);
    if (!p)
    {
        return 0;
    }
    if (idlen)
    {
        memcpy(buf + p, client_id, idlen);
    }
    return total;
}

size_t pc_mqttsn_build_disconnect(uint8_t *buf, size_t cap, bool with_duration, uint16_t duration)
{
    if (!buf)
    {
        return 0;
    }
    size_t total, p = frame_header(buf, cap, MQTTSN_DISCONNECT, with_duration ? 2 : 0, &total);
    if (!p)
    {
        return 0;
    }
    if (with_duration)
    {
        wr16(buf + p, duration);
    }
    return total;
}

size_t pc_mqttsn_build_searchgw(uint8_t *buf, size_t cap, uint8_t radius)
{
    if (!buf)
    {
        return 0;
    }
    size_t total, p = frame_header(buf, cap, MQTTSN_SEARCHGW, 1, &total);
    if (!p)
    {
        return 0;
    }
    buf[p] = radius;
    return total;
}

bool pc_mqttsn_parse_header(const uint8_t *buf, size_t len, MqttsnHeader *out, size_t *consumed)
{
    if (!buf || !out || !consumed || len < 2)
    {
        return false;
    }
    size_t lenfield;
    size_t total;
    if (buf[0] == MQTTSN_LEN3_PREFIX)
    {
        if (len < 3)
        {
            return false;
        }
        total = ((size_t)buf[1] << 8) | buf[2];
        lenfield = 3;
    }
    else
    {
        total = buf[0];
        lenfield = 1;
    }
    if (total < lenfield + 1) // must hold the Length field + a MsgType octet
    {
        return false;
    }
    if (total > len) // message not fully buffered yet
    {
        return false;
    }
    out->msg_type = buf[lenfield];
    out->payload = buf + lenfield + 1;
    out->payload_len = total - lenfield - 1;
    *consumed = total;
    return true;
}

bool pc_mqttsn_parse_connack(const uint8_t *payload, size_t len, uint8_t *ret_code)
{
    if (!payload || len < 1)
    {
        return false;
    }
    if (ret_code)
    {
        *ret_code = payload[0];
    }
    return true;
}

bool pc_mqttsn_parse_regack(const uint8_t *payload, size_t len, uint16_t *topic_id, uint16_t *msg_id, uint8_t *ret_code)
{
    if (!payload || len < 5)
    {
        return false;
    }
    if (topic_id)
    {
        *topic_id = rd16(payload);
    }
    if (msg_id)
    {
        *msg_id = rd16(payload + 2);
    }
    if (ret_code)
    {
        *ret_code = payload[4];
    }
    return true;
}

bool pc_mqttsn_parse_puback(const uint8_t *payload, size_t len, uint16_t *topic_id, uint16_t *msg_id, uint8_t *ret_code)
{
    return pc_mqttsn_parse_regack(payload, len, topic_id, msg_id, ret_code); // identical layout
}

bool pc_mqttsn_parse_suback(const uint8_t *payload, size_t len, uint8_t *flags, uint16_t *topic_id, uint16_t *msg_id,
                            uint8_t *ret_code)
{
    if (!payload || len < 6)
    {
        return false;
    }
    if (flags)
    {
        *flags = payload[0];
    }
    if (topic_id)
    {
        *topic_id = rd16(payload + 1);
    }
    if (msg_id)
    {
        *msg_id = rd16(payload + 3);
    }
    if (ret_code)
    {
        *ret_code = payload[5];
    }
    return true;
}

bool pc_mqttsn_parse_publish(const uint8_t *payload, size_t len, uint8_t *flags, uint16_t *topic_id, uint16_t *msg_id,
                             const uint8_t **data, size_t *data_len)
{
    if (!payload || len < 5)
    {
        return false;
    }
    if (flags)
    {
        *flags = payload[0];
    }
    if (topic_id)
    {
        *topic_id = rd16(payload + 1);
    }
    if (msg_id)
    {
        *msg_id = rd16(payload + 3);
    }
    if (data)
    {
        *data = payload + 5;
    }
    if (data_len)
    {
        *data_len = len - 5;
    }
    return true;
}

bool pc_mqttsn_parse_register(const uint8_t *payload, size_t len, uint16_t *topic_id, uint16_t *msg_id,
                              const char **topic_name, size_t *topic_name_len)
{
    if (!payload || len < 4)
    {
        return false;
    }
    if (topic_id)
    {
        *topic_id = rd16(payload);
    }
    if (msg_id)
    {
        *msg_id = rd16(payload + 2);
    }
    if (topic_name)
    {
        *topic_name = (const char *)(payload + 4);
    }
    if (topic_name_len)
    {
        *topic_name_len = len - 4;
    }
    return true;
}

#endif // PC_ENABLE_MQTT_SN
