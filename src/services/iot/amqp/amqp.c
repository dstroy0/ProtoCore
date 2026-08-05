// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file amqp.c
 * @brief AMQP 0-9-1 frame builder + parser (pure, host-tested).
 */

#include "services/iot/amqp/amqp.h"

#if PC_ENABLE_AMQP

#include "shared_primitives/endian.h"

size_t pc_amqp_protocol_header(uint8_t *buf, size_t cap)
{
    static const uint8_t hdr[8] = {'A', 'M', 'Q', 'P', 0, 0, 9, 1};
    if (!buf || cap < sizeof(hdr))
    {
        return 0;
    }
    memcpy(buf, hdr, sizeof(hdr));
    return sizeof(hdr);
}

// Write a frame header (type, channel, size) at buf; the caller fills the payload + 0xCE.
static size_t write_frame_header(uint8_t *buf, uint8_t type, uint16_t channel, uint32_t size)
{
    size_t p = 0;
    buf[p++] = type;
    p += pc_wr16be(buf + p, channel);
    p += pc_wr32be(buf + p, size);
    return p; // 7
}

size_t pc_amqp_build_frame(uint8_t *buf, size_t cap, uint8_t type, uint16_t channel, const uint8_t *payload,
                           size_t payload_len)
{
    if (!buf || (payload_len && !payload) || payload_len > 0xFFFFFFFFu)
    {
        return 0;
    }
    size_t total = AMQP_FRAME_OVERHEAD + payload_len;
    if (total > cap)
    {
        return 0;
    }
    size_t p = write_frame_header(buf, type, channel, (uint32_t)payload_len);
    if (payload_len)
    {
        memcpy(buf + p, payload, payload_len);
        p += payload_len;
    }
    buf[p++] = AMQP_FRAME_END;
    return p;
}

size_t pc_amqp_build_method(uint8_t *buf, size_t cap, uint16_t channel, uint16_t class_id, uint16_t method_id,
                            const uint8_t *args, size_t args_len)
{
    if (!buf || (args_len && !args))
    {
        return 0;
    }
    size_t payload_len = 4 + args_len; // class-id + method-id + args
    size_t total = AMQP_FRAME_OVERHEAD + payload_len;
    if (total > cap)
    {
        return 0;
    }
    // Write directly into buf (no temp): header, then the method payload, then the 0xCE end.
    size_t p = write_frame_header(buf, AMQP_FRAME_METHOD, channel, (uint32_t)payload_len);
    p += pc_wr16be(buf + p, class_id);
    p += pc_wr16be(buf + p, method_id);
    if (args_len)
    {
        memcpy(buf + p, args, args_len);
        p += args_len;
    }
    buf[p++] = AMQP_FRAME_END;
    return p;
}

size_t pc_amqp_build_content_header(uint8_t *buf, size_t cap, uint16_t channel, uint16_t class_id, uint64_t body_size,
                                    uint16_t property_flags, const uint8_t *properties, size_t properties_len)
{
    if (!buf || (properties_len && !properties))
    {
        return 0;
    }
    size_t payload_len = 2 + 2 + 8 + 2 + properties_len; // class-id + weight + body-size + property-flags + props
    size_t total = AMQP_FRAME_OVERHEAD + payload_len;
    if (total > cap)
    {
        return 0;
    }
    size_t p = write_frame_header(buf, AMQP_FRAME_HEADER, channel, (uint32_t)payload_len);
    p += pc_wr16be(buf + p, class_id);
    p += pc_wr16be(buf + p, 0); // weight (deprecated, always 0)
    p += pc_wr64be(buf + p, body_size);
    p += pc_wr16be(buf + p, property_flags);
    if (properties_len)
    {
        memcpy(buf + p, properties, properties_len);
        p += properties_len;
    }
    buf[p++] = AMQP_FRAME_END;
    return p;
}

size_t pc_amqp_build_heartbeat(uint8_t *buf, size_t cap)
{
    return pc_amqp_build_frame(buf, cap, AMQP_FRAME_HEARTBEAT, 0, NULL, 0);
}

proto_bool pc_amqp_parse_frame(const uint8_t *buf, size_t len, AmqpFrame *out, size_t *consumed)
{
    if (!buf || !out || len < AMQP_FRAME_OVERHEAD)
    {
        return PROTO_FALSE;
    }
    uint32_t size = pc_rd32be(buf + 3);
    // Compare against the remaining capacity without adding (a 32-bit size_t would wrap if we
    // computed 8 + size first), so an attacker-controlled size can't slip past the bound.
    if (size > len - AMQP_FRAME_OVERHEAD)
    {
        return PROTO_FALSE; // not fully buffered
    }
    size_t total = AMQP_FRAME_OVERHEAD + (size_t)size; // header(7) + payload + frame-end(1)
    if (buf[7 + size] != AMQP_FRAME_END)
    {
        return PROTO_FALSE; // missing / corrupt frame terminator
    }
    out->type = buf[0];
    out->channel = pc_rd16be(buf + 1);
    out->payload = buf + 7;
    out->payload_len = size;
    if (consumed)
    {
        *consumed = total;
    }
    return PROTO_TRUE;
}

proto_bool pc_amqp_parse_method(const uint8_t *payload, size_t payload_len, uint16_t *class_id, uint16_t *method_id,
                                const uint8_t **args, size_t *args_len)
{
    if (!payload || payload_len < 4)
    {
        return PROTO_FALSE;
    }
    if (class_id)
    {
        *class_id = pc_rd16be(payload);
    }
    if (method_id)
    {
        *method_id = pc_rd16be(payload + 2);
    }
    if (args)
    {
        *args = payload + 4;
    }
    if (args_len)
    {
        *args_len = payload_len - 4;
    }
    return PROTO_TRUE;
}

#endif // PC_ENABLE_AMQP
