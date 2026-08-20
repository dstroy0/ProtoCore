// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_h3_frame.c
 * @brief HTTP/3 framing - implementation. See protocore_h3_frame.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HTTP3

#include "mmgr/protomem/protomem.h"
#include "network_drivers/presentation/http/http3/h3_frame/h3_frame.h"
#include "network_drivers/presentation/http/http3/quic_varint/quic_varint.h"

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_h3_frame_write_header(uint8_t *restrict work);

void protocore_h3_frame_parse_header(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = H3FrameV.parse_header_args.buf;
    size_t len = H3FrameV.parse_header_args.len;
    H3FrameHeader *out = H3FrameV.parse_header_args.out;

    size_t c1 = 0;
    size_t c2 = 0;
    uint64_t type = 0;
    uint64_t length = 0;
    QuicVarintV.decode_args.in = buf;
    QuicVarintV.decode_args.len = len;
    QuicVarintV.decode_args.value = &type;
    QuicVarintV.decode_args.consumed = &c1;
    QuicVarint.decode(work);
    if (!QuicVarintV.ok)
    {
        H3FrameV.ok = PROTO_FALSE;
        return;
    }
    QuicVarintV.decode_args.in = buf + c1;
    QuicVarintV.decode_args.len = len - c1;
    QuicVarintV.decode_args.value = &length;
    QuicVarintV.decode_args.consumed = &c2;
    QuicVarint.decode(work);
    if (!QuicVarintV.ok)
    {
        H3FrameV.ok = PROTO_FALSE;
        return;
    }
    out->type = type;
    out->length = length;
    out->header_len = c1 + c2;
    H3FrameV.ok = PROTO_TRUE;
}

void protocore_h3_frame_write_header(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = H3FrameV.write_header_args.out;
    size_t cap = H3FrameV.write_header_args.cap;
    uint64_t type = H3FrameV.write_header_args.type;
    uint64_t length = H3FrameV.write_header_args.length;

    QuicVarintV.encode_args.out = out;
    QuicVarintV.encode_args.cap = cap;
    QuicVarintV.encode_args.value = type;
    QuicVarint.encode(work);
    size_t n = QuicVarintV.n;
    if (!n)
    {
        H3FrameV.n = 0;
        return;
    }
    QuicVarintV.encode_args.out = out + n;
    QuicVarintV.encode_args.cap = cap - n;
    QuicVarintV.encode_args.value = length;
    QuicVarint.encode(work);
    size_t m = QuicVarintV.n;
    if (!m)
    {
        H3FrameV.n = 0;
        return;
    }
    H3FrameV.n = n + m;
}

void protocore_h3_frame_type_reserved(uint8_t *restrict work)
{
    (void)work;
    uint64_t type = H3FrameV.type_reserved_args.type;

    // The HTTP/2 frame types that have no HTTP/3 meaning (RFC 9114 sec 11.2.1).
    H3FrameV.ok = type == 0x02 || type == 0x06 || type == 0x08 || type == 0x09;
}

void protocore_h3_frame_settings_defaults(uint8_t *restrict work)
{
    (void)work;
    H3Settings *s = H3FrameV.settings_defaults_args.s;

    s->qpack_max_table_capacity = 0;
    s->max_field_section_size = 0xFFFFFFFFFFFFFFFFULL; // unlimited
    s->qpack_blocked_streams = 0;
}

void protocore_h3_frame_parse_settings(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *payload = H3FrameV.parse_settings_args.payload;
    size_t len = H3FrameV.parse_settings_args.len;
    H3Settings *s = H3FrameV.parse_settings_args.s;

    size_t off = 0;
    while (off < len)
    {
        size_t c1 = 0;
        size_t c2 = 0;
        uint64_t id = 0;
        uint64_t val = 0;
        QuicVarintV.decode_args.in = payload + off;
        QuicVarintV.decode_args.len = len - off;
        QuicVarintV.decode_args.value = &id;
        QuicVarintV.decode_args.consumed = &c1;
        QuicVarint.decode(work);
        if (!QuicVarintV.ok)
        {
            H3FrameV.ok = PROTO_FALSE;
            return;
        }
        off += c1;
        QuicVarintV.decode_args.in = payload + off;
        QuicVarintV.decode_args.len = len - off;
        QuicVarintV.decode_args.value = &val;
        QuicVarintV.decode_args.consumed = &c2;
        QuicVarint.decode(work);
        if (!QuicVarintV.ok)
        {
            H3FrameV.ok = PROTO_FALSE;
            return;
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
            H3FrameV.ok = PROTO_FALSE; // reserved HTTP/2 settings identifiers (RFC 9114 sec 7.2.4.1)
            return;
        default:
            break; // unknown / greased settings are ignored
        }
    }
    H3FrameV.ok = PROTO_TRUE;
}

void protocore_h3_frame_build_data(uint8_t *restrict work)
{
    uint8_t *out = H3FrameV.build_data_args.out;
    size_t cap = H3FrameV.build_data_args.cap;
    const uint8_t *data = H3FrameV.build_data_args.data;
    size_t len = H3FrameV.build_data_args.len;

    H3FrameV.write_header_args.out = out;
    H3FrameV.write_header_args.cap = cap;
    H3FrameV.write_header_args.type = H3_DATA;
    H3FrameV.write_header_args.length = len;
    protocore_h3_frame_write_header(work);
    size_t hn = H3FrameV.n;
    if (!hn || hn + len > cap)
    {
        H3FrameV.n = 0;
        return;
    }
    if (len)
    {
        mem.cpy(out + hn, data, len);
    }
    H3FrameV.n = hn + len;
}

void protocore_h3_frame_build_headers(uint8_t *restrict work)
{
    uint8_t *out = H3FrameV.build_headers_args.out;
    size_t cap = H3FrameV.build_headers_args.cap;
    const uint8_t *block = H3FrameV.build_headers_args.block;
    size_t len = H3FrameV.build_headers_args.len;

    H3FrameV.write_header_args.out = out;
    H3FrameV.write_header_args.cap = cap;
    H3FrameV.write_header_args.type = H3_HEADERS;
    H3FrameV.write_header_args.length = len;
    protocore_h3_frame_write_header(work);
    size_t hn = H3FrameV.n;
    if (!hn || hn + len > cap)
    {
        H3FrameV.n = 0;
        return;
    }
    if (len)
    {
        mem.cpy(out + hn, block, len);
    }
    H3FrameV.n = hn + len;
}

void protocore_h3_frame_build_settings(uint8_t *restrict work)
{
    uint8_t *out = H3FrameV.build_settings_args.out;
    size_t cap = H3FrameV.build_settings_args.cap;
    const uint64_t *ids = H3FrameV.build_settings_args.ids;
    const uint64_t *vals = H3FrameV.build_settings_args.vals;
    size_t n = H3FrameV.build_settings_args.n;

    size_t plen = 0;
    for (size_t i = 0; i < n; i++)
    {
        QuicVarintV.len_args.value = ids[i];
        QuicVarint.len(work);
        size_t idn = QuicVarintV.n;
        QuicVarintV.len_args.value = vals[i];
        QuicVarint.len(work);
        plen += idn + QuicVarintV.n;
    }
    H3FrameV.write_header_args.out = out;
    H3FrameV.write_header_args.cap = cap;
    H3FrameV.write_header_args.type = H3_SETTINGS;
    H3FrameV.write_header_args.length = plen;
    protocore_h3_frame_write_header(work);
    size_t o = H3FrameV.n;
    if (!o)
    {
        H3FrameV.n = 0;
        return;
    }
    for (size_t i = 0; i < n; i++)
    {
        QuicVarintV.encode_args.out = out + o;
        QuicVarintV.encode_args.cap = cap - o;
        QuicVarintV.encode_args.value = ids[i];
        QuicVarint.encode(work);
        size_t a = QuicVarintV.n;
        if (!a)
        {
            H3FrameV.n = 0;
            return;
        }
        o += a;
        QuicVarintV.encode_args.out = out + o;
        QuicVarintV.encode_args.cap = cap - o;
        QuicVarintV.encode_args.value = vals[i];
        QuicVarint.encode(work);
        size_t b = QuicVarintV.n;
        if (!b)
        {
            H3FrameV.n = 0;
            return;
        }
        o += b;
    }
    H3FrameV.n = o;
}

void protocore_h3_frame_build_goaway(uint8_t *restrict work)
{
    uint8_t *out = H3FrameV.build_goaway_args.out;
    size_t cap = H3FrameV.build_goaway_args.cap;
    uint64_t stream_id = H3FrameV.build_goaway_args.stream_id;

    QuicVarintV.len_args.value = stream_id;
    QuicVarint.len(work);
    size_t plen = QuicVarintV.n;
    H3FrameV.write_header_args.out = out;
    H3FrameV.write_header_args.cap = cap;
    H3FrameV.write_header_args.type = H3_GOAWAY;
    H3FrameV.write_header_args.length = plen;
    protocore_h3_frame_write_header(work);
    size_t o = H3FrameV.n;
    if (!o)
    {
        H3FrameV.n = 0;
        return;
    }
    QuicVarintV.encode_args.out = out + o;
    QuicVarintV.encode_args.cap = cap - o;
    QuicVarintV.encode_args.value = stream_id;
    QuicVarint.encode(work);
    size_t a = QuicVarintV.n;
    if (!a)
    {
        H3FrameV.n = 0;
        return;
    }
    H3FrameV.n = o + a;
}

/** @brief The operands and the outcome. */
H3FrameVars H3FrameV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP3
