// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_h2_frame.c
 * @brief HTTP/2 binary framing - implementation. See pc_h2_frame.h.
 */

#include "network_drivers/presentation/http/http2/h2_frame.h"

#if PC_ENABLE_HTTP2

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}
static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

proto_bool pc_h2_parse_header(const uint8_t *buf, size_t len, H2FrameHeader *out)
{
    if (len < H2_FRAME_HEADER_LEN)
    {
        return PROTO_FALSE;
    }
    out->length = ((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2];
    out->type = buf[3];
    out->flags = buf[4];
    out->stream_id = ((uint32_t)(buf[5] & 0x7f) << 24) | ((uint32_t)buf[6] << 16) | ((uint32_t)buf[7] << 8) |
                     buf[8]; // clear the reserved bit
    return PROTO_TRUE;
}

size_t pc_h2_write_header(uint8_t *out, size_t cap, uint32_t length, uint8_t type, uint8_t flags, uint32_t stream_id)
{
    if (cap < H2_FRAME_HEADER_LEN || length > 0xFFFFFF)
    {
        return 0;
    }
    out[0] = (uint8_t)(length >> 16);
    out[1] = (uint8_t)(length >> 8);
    out[2] = (uint8_t)length;
    out[3] = type;
    out[4] = flags;
    out[5] = (uint8_t)((stream_id >> 24) & 0x7f); // reserved bit stays 0
    out[6] = (uint8_t)(stream_id >> 16);
    out[7] = (uint8_t)(stream_id >> 8);
    out[8] = (uint8_t)stream_id;
    return H2_FRAME_HEADER_LEN;
}

void pc_h2_settings_defaults(H2Settings *s)
{
    s->header_table_size = 4096;
    s->enable_push = 1;
    s->max_concurrent_streams = 0xFFFFFFFFu;
    s->initial_window_size = 65535;
    s->max_frame_size = 16384;
    s->max_header_list_size = 0xFFFFFFFFu;
}

proto_bool pc_h2_parse_settings(const uint8_t *payload, size_t len, H2Settings *s)
{
    if (len % 6 != 0) // each entry is 2-byte id + 4-byte value
    {
        return PROTO_FALSE;
    }
    for (size_t i = 0; i < len; i += 6)
    {
        uint16_t id = (uint16_t)(((uint16_t)payload[i] << 8) | payload[i + 1]);
        uint32_t val = rd32(payload + i + 2);
        switch (id)
        {
        case H2_SETTINGS_HEADER_TABLE_SIZE:
            s->header_table_size = val;
            break;
        case H2_SETTINGS_ENABLE_PUSH:
            if (val > 1)
            {
                return PROTO_FALSE; // RFC 9113 sec 6.5.2: must be 0 or 1
            }
            s->enable_push = val;
            break;
        case H2_SETTINGS_MAX_CONCURRENT_STREAMS:
            s->max_concurrent_streams = val;
            break;
        case H2_SETTINGS_INITIAL_WINDOW_SIZE:
            if (val > 0x7FFFFFFF)
            {
                return PROTO_FALSE; // must not exceed 2^31-1
            }
            s->initial_window_size = val;
            break;
        case H2_SETTINGS_MAX_FRAME_SIZE:
            if (val < 16384 || val > 16777215)
            {
                return PROTO_FALSE; // must be in [2^14, 2^24-1]
            }
            s->max_frame_size = val;
            break;
        case H2_SETTINGS_MAX_HEADER_LIST_SIZE:
            s->max_header_list_size = val;
            break;
        default:
            break; // unknown settings are ignored
        }
    }
    return PROTO_TRUE;
}

size_t pc_h2_build_settings(uint8_t *out, size_t cap, const uint16_t *ids, const uint32_t *vals, size_t n)
{
    size_t payload = n * 6;
    if (cap < H2_FRAME_HEADER_LEN + payload)
    {
        return 0;
    }
    pc_h2_write_header(out, cap, (uint32_t)payload, H2_SETTINGS, 0, 0);
    uint8_t *p = out + H2_FRAME_HEADER_LEN;
    for (size_t i = 0; i < n; i++)
    {
        p[0] = (uint8_t)(ids[i] >> 8);
        p[1] = (uint8_t)ids[i];
        wr32(p + 2, vals[i]);
        p += 6;
    }
    return H2_FRAME_HEADER_LEN + payload;
}

size_t pc_h2_build_settings_ack(uint8_t *out, size_t cap)
{
    return pc_h2_write_header(out, cap, 0, H2_SETTINGS, H2_FLAG_ACK, 0);
}

size_t pc_h2_build_window_update(uint8_t *out, size_t cap, uint32_t stream_id, uint32_t increment)
{
    if (cap < H2_FRAME_HEADER_LEN + 4)
    {
        return 0;
    }
    pc_h2_write_header(out, cap, 4, H2_WINDOW_UPDATE, 0, stream_id);
    wr32(out + H2_FRAME_HEADER_LEN, increment & 0x7FFFFFFF); // 31-bit, reserved bit 0
    return H2_FRAME_HEADER_LEN + 4;
}

size_t pc_h2_build_rst_stream(uint8_t *out, size_t cap, uint32_t stream_id, uint32_t error)
{
    if (cap < H2_FRAME_HEADER_LEN + 4)
    {
        return 0;
    }
    pc_h2_write_header(out, cap, 4, H2_RST_STREAM, 0, stream_id);
    wr32(out + H2_FRAME_HEADER_LEN, error);
    return H2_FRAME_HEADER_LEN + 4;
}

size_t pc_h2_build_goaway(uint8_t *out, size_t cap, uint32_t last_stream_id, uint32_t error)
{
    if (cap < H2_FRAME_HEADER_LEN + 8)
    {
        return 0;
    }
    pc_h2_write_header(out, cap, 8, H2_GOAWAY, 0, 0);
    wr32(out + H2_FRAME_HEADER_LEN, last_stream_id & 0x7FFFFFFF);
    wr32(out + H2_FRAME_HEADER_LEN + 4, error);
    return H2_FRAME_HEADER_LEN + 8;
}

size_t pc_h2_build_ping_ack(uint8_t *out, size_t cap, const uint8_t opaque[8])
{
    if (cap < H2_FRAME_HEADER_LEN + 8)
    {
        return 0;
    }
    pc_h2_write_header(out, cap, 8, H2_PING, H2_FLAG_ACK, 0);
    memcpy(out + H2_FRAME_HEADER_LEN, opaque, 8);
    return H2_FRAME_HEADER_LEN + 8;
}

size_t pc_h2_build_headers(uint8_t *out, size_t cap, uint32_t stream_id, const uint8_t *block, size_t block_len,
                           proto_bool end_stream)
{
    if (cap < H2_FRAME_HEADER_LEN + block_len)
    {
        return 0;
    }
    uint8_t flags = H2_FLAG_END_HEADERS | (end_stream ? H2_FLAG_END_STREAM : 0);
    pc_h2_write_header(out, cap, (uint32_t)block_len, H2_HEADERS, flags, stream_id);
    memcpy(out + H2_FRAME_HEADER_LEN, block, block_len);
    return H2_FRAME_HEADER_LEN + block_len;
}

size_t pc_h2_build_data(uint8_t *out, size_t cap, uint32_t stream_id, const uint8_t *data, size_t data_len,
                        proto_bool end_stream)
{
    if (cap < H2_FRAME_HEADER_LEN + data_len)
    {
        return 0;
    }
    pc_h2_write_header(out, cap, (uint32_t)data_len, H2_DATA, end_stream ? H2_FLAG_END_STREAM : 0, stream_id);
    if (data_len)
    {
        memcpy(out + H2_FRAME_HEADER_LEN, data, data_len);
    }
    return H2_FRAME_HEADER_LEN + data_len;
}

#endif // PC_ENABLE_HTTP2
