// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_h3_frame.c
 * @brief HTTP/3 framing - implementation. See protocore_h3_frame.h.
 */

#include "protocore_config.h" // the entry point: the widths

#include "mmgr/protomem/protomem.h"
#include "network_drivers/presentation/http/http3/h3_frame/h3_frame.h"
#include "network_drivers/presentation/http/http3/quic_varint/quic_varint.h"

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

proto_bool protocore_h3_frame_parse_header(uint8_t *restrict work, const uint8_t *buf, size_t len, H3FrameHeader *out)
{
    size_t c1 = 0;
    size_t c2 = 0;
    uint64_t type = 0;
    uint64_t length = 0;
    proto_bool quic_varint_ok = QuicVarint.decode(work, buf, len, &type, &c1);
    if (!quic_varint_ok)
    {
        return PROTO_FALSE;
    }
    quic_varint_ok = QuicVarint.decode(work, buf + c1, len - c1, &length, &c2);
    if (!quic_varint_ok)
    {
        return PROTO_FALSE;
    }
    out->type = type;
    out->length = length;
    out->header_len = c1 + c2;
    return PROTO_TRUE;
}

size_t protocore_h3_frame_write_header(uint8_t *restrict work, uint8_t *out, size_t cap, uint64_t type, uint64_t length)
{
    size_t quic_varint_n = QuicVarint.encode(work, out, cap, type);
    size_t n = quic_varint_n;
    if (!n)
    {
        return 0;
    }
    quic_varint_n = QuicVarint.encode(work, out + n, cap - n, length);
    size_t m = quic_varint_n;
    if (!m)
    {
        return 0;
    }
    return n + m;
}

proto_bool protocore_h3_frame_type_reserved(uint8_t *restrict work, uint64_t type)
{
    (void)work;

    // The HTTP/2 frame types that have no HTTP/3 meaning (RFC 9114 sec 11.2.1).
    return type == 0x02 || type == 0x06 || type == 0x08 || type == 0x09;
}

void protocore_h3_frame_settings_defaults(uint8_t *restrict work, H3Settings *s)
{
    (void)work;

    s->qpack_max_table_capacity = 0;
    s->max_field_section_size = 0xFFFFFFFFFFFFFFFFULL; // unlimited
    s->qpack_blocked_streams = 0;
}

proto_bool protocore_h3_frame_parse_settings(uint8_t *restrict work, const uint8_t *payload, size_t len, H3Settings *s)
{
    proto_bool ok = PROTO_FALSE;

    size_t off = 0;
    while (off < len)
    {
        size_t c1 = 0;
        size_t c2 = 0;
        uint64_t id = 0;
        uint64_t val = 0;
        proto_bool quic_varint_ok = QuicVarint.decode(work, payload + off, len - off, &id, &c1);
        if (!quic_varint_ok)
        {
            return PROTO_FALSE;
        }
        off += c1;
        quic_varint_ok = QuicVarint.decode(work, payload + off, len - off, &val, &c2);
        if (!quic_varint_ok)
        {
            return PROTO_FALSE;
        }
        off += c2;
        switch (id)
        {
        case H3_SETTINGS_QPACK_MAX_TABLE_CAPACITY:
            s->qpack_max_table_capacity = val;
            break;
        case H3_SETTINGS_MAX_FIELD_SECTION_SIZE:
            s->max_field_section_size = val;
            break;
        case H3_SETTINGS_QPACK_BLOCKED_STREAMS:
            s->qpack_blocked_streams = val;
            break;
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
            ok = PROTO_FALSE; // reserved HTTP/2 settings identifiers (RFC 9114 sec 7.2.4.1)
            return ok;
        default:
            break; // unknown / greased settings are ignored
        }
    }
    return PROTO_TRUE;
}

size_t protocore_h3_frame_build_data(uint8_t *restrict work, uint8_t *out, size_t cap, const uint8_t *data, size_t len)
{
    size_t h3_frame_n = H3Frame.write_header(work, out, cap, H3_DATA, len);
    size_t hn = h3_frame_n;
    if (!hn || hn + len > cap)
    {
        return 0;
    }
    if (len)
    {
        mem.cpy(out + hn, data, len);
    }
    return hn + len;
}

size_t protocore_h3_frame_build_headers(uint8_t *restrict work, uint8_t *out, size_t cap, const uint8_t *block,
                                        size_t len)
{
    size_t h3_frame_n = H3Frame.write_header(work, out, cap, H3_HEADERS, len);
    size_t hn = h3_frame_n;
    if (!hn || hn + len > cap)
    {
        return 0;
    }
    if (len)
    {
        mem.cpy(out + hn, block, len);
    }
    return hn + len;
}

size_t protocore_h3_frame_build_settings(uint8_t *restrict work, uint8_t *out, size_t cap, const uint64_t *ids,
                                         const uint64_t *vals, size_t n)
{
    size_t plen = 0;
    for (size_t i = 0; i < n; i++)
    {
        size_t quic_varint_n = QuicVarint.len(work, ids[i]);
        size_t idn = quic_varint_n;
        quic_varint_n = QuicVarint.len(work, vals[i]);
        plen += idn + quic_varint_n;
    }
    size_t h3_frame_n = H3Frame.write_header(work, out, cap, H3_SETTINGS, plen);
    size_t o = h3_frame_n;
    if (!o)
    {
        return 0;
    }
    for (size_t i = 0; i < n; i++)
    {
        size_t quic_varint_n = QuicVarint.encode(work, out + o, cap - o, ids[i]);
        size_t a = quic_varint_n;
        if (!a)
        {
            return 0;
        }
        o += a;
        quic_varint_n = QuicVarint.encode(work, out + o, cap - o, vals[i]);
        size_t b = quic_varint_n;
        if (!b)
        {
            return 0;
        }
        o += b;
    }
    return o;
}

size_t protocore_h3_frame_build_goaway(uint8_t *restrict work, uint8_t *out, size_t cap, uint64_t stream_id)
{
    size_t quic_varint_n = QuicVarint.len(work, stream_id);
    size_t plen = quic_varint_n;
    size_t h3_frame_n = H3Frame.write_header(work, out, cap, H3_GOAWAY, plen);
    size_t o = h3_frame_n;
    if (!o)
    {
        return 0;
    }
    quic_varint_n = QuicVarint.encode(work, out + o, cap - o, stream_id);
    size_t a = quic_varint_n;
    if (!a)
    {
        return 0;
    }
    return o + a;
}
