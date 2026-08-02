// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_h3_frame.cpp
 * @brief HTTP/3 framing - implementation. See pc_h3_frame.h.
 */

#include "network_drivers/presentation/http/http3/h3_frame.h"

#if PC_ENABLE_HTTP3

#include "network_drivers/presentation/http/http3/quic_varint.h"
#include <string.h>

proto_bool pc_h3_frame_parse(const uint8_t *buf, size_t len, H3Frame *out)
{
    size_t c1 = 0;
    size_t c2 = 0;
    uint64_t type = 0;
    uint64_t length = 0;
    if (!pc_quic_varint_decode(buf, len, &type, &c1))
    {
        return PROTO_FALSE;
    }
    if (!pc_quic_varint_decode(buf + c1, len - c1, &length, &c2))
    {
        return PROTO_FALSE;
    }
    out->type = type;
    out->length = length;
    out->header_len = c1 + c2;
    return PROTO_TRUE;
}

size_t pc_h3_frame_write_header(uint8_t *out, size_t cap, uint64_t type, uint64_t length)
{
    size_t n = pc_quic_varint_encode(out, cap, type);
    if (!n)
    {
        return 0;
    }
    size_t m = pc_quic_varint_encode(out + n, cap - n, length);
    if (!m)
    {
        return 0;
    }
    return n + m;
}

proto_bool pc_h3_frame_type_reserved(uint64_t type)
{
    // The HTTP/2 frame types that have no HTTP/3 meaning (RFC 9114 sec 11.2.1).
    return type == 0x02 || type == 0x06 || type == 0x08 || type == 0x09;
}

void pc_h3_settings_defaults(H3Settings *s)
{
    s->pc_qpack_max_table_capacity = 0;
    s->max_field_section_size = 0xFFFFFFFFFFFFFFFFULL; // unlimited
    s->pc_qpack_blocked_streams = 0;
}

proto_bool pc_h3_parse_settings(const uint8_t *payload, size_t len, H3Settings *s)
{
    size_t off = 0;
    while (off < len)
    {
        size_t c1 = 0;
        size_t c2 = 0;
        uint64_t id = 0;
        uint64_t val = 0;
        if (!pc_quic_varint_decode(payload + off, len - off, &id, &c1))
        {
            return PROTO_FALSE;
        }
        off += c1;
        if (!pc_quic_varint_decode(payload + off, len - off, &val, &c2))
        {
            return PROTO_FALSE;
        }
        off += c2;
        switch (id)
        {
        case H3Setting::H3_SETTINGS_QPACK_MAX_TABLE_CAPACITY:
            s->pc_qpack_max_table_capacity = val;
            break;
        case H3Setting::H3_SETTINGS_MAX_FIELD_SECTION_SIZE:
            s->max_field_section_size = val;
            break;
        case H3Setting::H3_SETTINGS_QPACK_BLOCKED_STREAMS:
            s->pc_qpack_blocked_streams = val;
            break;
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
            return PROTO_FALSE; // reserved HTTP/2 settings identifiers (RFC 9114 sec 7.2.4.1)
        default:
            break; // unknown / greased settings are ignored
        }
    }
    return PROTO_TRUE;
}

size_t pc_h3_build_data(uint8_t *out, size_t cap, const uint8_t *data, size_t len)
{
    size_t hn = pc_h3_frame_write_header(out, cap, H3FrameType::H3_DATA, len);
    if (!hn || hn + len > cap)
    {
        return 0;
    }
    if (len)
    {
        memcpy(out + hn, data, len);
    }
    return hn + len;
}

size_t pc_h3_build_headers(uint8_t *out, size_t cap, const uint8_t *block, size_t len)
{
    size_t hn = pc_h3_frame_write_header(out, cap, H3FrameType::H3_HEADERS, len);
    if (!hn || hn + len > cap)
    {
        return 0;
    }
    if (len)
    {
        memcpy(out + hn, block, len);
    }
    return hn + len;
}

size_t pc_h3_build_settings(uint8_t *out, size_t cap, const uint64_t *ids, const uint64_t *vals, size_t n)
{
    size_t plen = 0;
    for (size_t i = 0; i < n; i++)
    {
        plen += pc_quic_varint_len(ids[i]) + pc_quic_varint_len(vals[i]);
    }
    size_t o = pc_h3_frame_write_header(out, cap, H3FrameType::H3_SETTINGS, plen);
    if (!o)
    {
        return 0;
    }
    for (size_t i = 0; i < n; i++)
    {
        size_t a = pc_quic_varint_encode(out + o, cap - o, ids[i]);
        if (!a)
        {
            return 0;
        }
        o += a;
        size_t b = pc_quic_varint_encode(out + o, cap - o, vals[i]);
        if (!b)
        {
            return 0;
        }
        o += b;
    }
    return o;
}

size_t pc_h3_build_goaway(uint8_t *out, size_t cap, uint64_t stream_id)
{
    size_t plen = pc_quic_varint_len(stream_id);
    size_t o = pc_h3_frame_write_header(out, cap, H3FrameType::H3_GOAWAY, plen);
    if (!o)
    {
        return 0;
    }
    size_t a = pc_quic_varint_encode(out + o, cap - o, stream_id);
    if (!a)
    {
        return 0;
    }
    return o + a;
}

#endif // PC_ENABLE_HTTP3
