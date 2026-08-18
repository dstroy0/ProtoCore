// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_h3_frame.c
 * @brief HTTP/3 framing - implementation. See protocore_h3_frame.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HTTP3

static uint8_t quic_varint_work[16]; // the borrow an entry takes; QuicVarint never reads it

#include "mmgr/protomem.h"
#include "network_drivers/presentation/http/http3/h3_frame.h"
#include "network_drivers/presentation/http/http3/quic_varint.h"

PROTOCORE_BEGIN_DECLS

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void h3_frame_write_header(uint8_t *restrict work);

static void h3_frame_parse_header(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = H3Frame.parse_header_args.buf;
    size_t len = H3Frame.parse_header_args.len;
    H3FrameHeader *out = H3Frame.parse_header_args.out;

    size_t c1 = 0;
    size_t c2 = 0;
    uint64_t type = 0;
    uint64_t length = 0;
    QuicVarint.decode_args.in = buf;
    QuicVarint.decode_args.len = len;
    QuicVarint.decode_args.value = &type;
    QuicVarint.decode_args.consumed = &c1;
    QuicVarint.decode(quic_varint_work);
    if (!QuicVarint.ok)
    {
        H3Frame.ok = PROTO_FALSE;
        return;
    }
    QuicVarint.decode_args.in = buf + c1;
    QuicVarint.decode_args.len = len - c1;
    QuicVarint.decode_args.value = &length;
    QuicVarint.decode_args.consumed = &c2;
    QuicVarint.decode(quic_varint_work);
    if (!QuicVarint.ok)
    {
        H3Frame.ok = PROTO_FALSE;
        return;
    }
    out->type = type;
    out->length = length;
    out->header_len = c1 + c2;
    H3Frame.ok = PROTO_TRUE;
}

static void h3_frame_write_header(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = H3Frame.write_header_args.out;
    size_t cap = H3Frame.write_header_args.cap;
    uint64_t type = H3Frame.write_header_args.type;
    uint64_t length = H3Frame.write_header_args.length;

    QuicVarint.encode_args.out = out;
    QuicVarint.encode_args.cap = cap;
    QuicVarint.encode_args.value = type;
    QuicVarint.encode(quic_varint_work);
    size_t n = QuicVarint.n;
    if (!n)
    {
        H3Frame.n = 0;
        return;
    }
    QuicVarint.encode_args.out = out + n;
    QuicVarint.encode_args.cap = cap - n;
    QuicVarint.encode_args.value = length;
    QuicVarint.encode(quic_varint_work);
    size_t m = QuicVarint.n;
    if (!m)
    {
        H3Frame.n = 0;
        return;
    }
    H3Frame.n = n + m;
}

static void h3_frame_type_reserved(uint8_t *restrict work)
{
    (void)work;
    uint64_t type = H3Frame.type_reserved_args.type;

    // The HTTP/2 frame types that have no HTTP/3 meaning (RFC 9114 sec 11.2.1).
    H3Frame.ok = type == 0x02 || type == 0x06 || type == 0x08 || type == 0x09;
}

static void h3_frame_settings_defaults(uint8_t *restrict work)
{
    (void)work;
    H3Settings *s = H3Frame.settings_defaults_args.s;

    s->qpack_max_table_capacity = 0;
    s->max_field_section_size = 0xFFFFFFFFFFFFFFFFULL; // unlimited
    s->qpack_blocked_streams = 0;
}

static void h3_frame_parse_settings(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *payload = H3Frame.parse_settings_args.payload;
    size_t len = H3Frame.parse_settings_args.len;
    H3Settings *s = H3Frame.parse_settings_args.s;

    size_t off = 0;
    while (off < len)
    {
        size_t c1 = 0;
        size_t c2 = 0;
        uint64_t id = 0;
        uint64_t val = 0;
        QuicVarint.decode_args.in = payload + off;
        QuicVarint.decode_args.len = len - off;
        QuicVarint.decode_args.value = &id;
        QuicVarint.decode_args.consumed = &c1;
        QuicVarint.decode(quic_varint_work);
        if (!QuicVarint.ok)
        {
            H3Frame.ok = PROTO_FALSE;
            return;
        }
        off += c1;
        QuicVarint.decode_args.in = payload + off;
        QuicVarint.decode_args.len = len - off;
        QuicVarint.decode_args.value = &val;
        QuicVarint.decode_args.consumed = &c2;
        QuicVarint.decode(quic_varint_work);
        if (!QuicVarint.ok)
        {
            H3Frame.ok = PROTO_FALSE;
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
            H3Frame.ok = PROTO_FALSE; // reserved HTTP/2 settings identifiers (RFC 9114 sec 7.2.4.1)
            return;
        default:
            break; // unknown / greased settings are ignored
        }
    }
    H3Frame.ok = PROTO_TRUE;
}

static void h3_frame_build_data(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    uint8_t *out = H3Frame.build_data_args.out;
    size_t cap = H3Frame.build_data_args.cap;
    const uint8_t *data = H3Frame.build_data_args.data;
    size_t len = H3Frame.build_data_args.len;

    H3Frame.write_header_args.out = out;
    H3Frame.write_header_args.cap = cap;
    H3Frame.write_header_args.type = H3_DATA;
    H3Frame.write_header_args.length = len;
    h3_frame_write_header(work);
    size_t hn = H3Frame.n;
    if (!hn || hn + len > cap)
    {
        H3Frame.n = 0;
        return;
    }
    if (len)
    {
        mem.cpy(out + hn, data, len);
    }
    H3Frame.n = hn + len;
}

static void h3_frame_build_headers(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    uint8_t *out = H3Frame.build_headers_args.out;
    size_t cap = H3Frame.build_headers_args.cap;
    const uint8_t *block = H3Frame.build_headers_args.block;
    size_t len = H3Frame.build_headers_args.len;

    H3Frame.write_header_args.out = out;
    H3Frame.write_header_args.cap = cap;
    H3Frame.write_header_args.type = H3_HEADERS;
    H3Frame.write_header_args.length = len;
    h3_frame_write_header(work);
    size_t hn = H3Frame.n;
    if (!hn || hn + len > cap)
    {
        H3Frame.n = 0;
        return;
    }
    if (len)
    {
        mem.cpy(out + hn, block, len);
    }
    H3Frame.n = hn + len;
}

static void h3_frame_build_settings(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    uint8_t *out = H3Frame.build_settings_args.out;
    size_t cap = H3Frame.build_settings_args.cap;
    const uint64_t *ids = H3Frame.build_settings_args.ids;
    const uint64_t *vals = H3Frame.build_settings_args.vals;
    size_t n = H3Frame.build_settings_args.n;

    size_t plen = 0;
    for (size_t i = 0; i < n; i++)
    {
        QuicVarint.len_args.value = ids[i];
        QuicVarint.len(quic_varint_work);
        size_t idn = QuicVarint.n;
        QuicVarint.len_args.value = vals[i];
        QuicVarint.len(quic_varint_work);
        plen += idn + QuicVarint.n;
    }
    H3Frame.write_header_args.out = out;
    H3Frame.write_header_args.cap = cap;
    H3Frame.write_header_args.type = H3_SETTINGS;
    H3Frame.write_header_args.length = plen;
    h3_frame_write_header(work);
    size_t o = H3Frame.n;
    if (!o)
    {
        H3Frame.n = 0;
        return;
    }
    for (size_t i = 0; i < n; i++)
    {
        QuicVarint.encode_args.out = out + o;
        QuicVarint.encode_args.cap = cap - o;
        QuicVarint.encode_args.value = ids[i];
        QuicVarint.encode(quic_varint_work);
        size_t a = QuicVarint.n;
        if (!a)
        {
            H3Frame.n = 0;
            return;
        }
        o += a;
        QuicVarint.encode_args.out = out + o;
        QuicVarint.encode_args.cap = cap - o;
        QuicVarint.encode_args.value = vals[i];
        QuicVarint.encode(quic_varint_work);
        size_t b = QuicVarint.n;
        if (!b)
        {
            H3Frame.n = 0;
            return;
        }
        o += b;
    }
    H3Frame.n = o;
}

static void h3_frame_build_goaway(uint8_t *restrict work)
{
    if (!work)
    {
        return; // the pool was short of this module's borrow
    }
    uint8_t *out = H3Frame.build_goaway_args.out;
    size_t cap = H3Frame.build_goaway_args.cap;
    uint64_t stream_id = H3Frame.build_goaway_args.stream_id;

    QuicVarint.len_args.value = stream_id;
    QuicVarint.len(quic_varint_work);
    size_t plen = QuicVarint.n;
    H3Frame.write_header_args.out = out;
    H3Frame.write_header_args.cap = cap;
    H3Frame.write_header_args.type = H3_GOAWAY;
    H3Frame.write_header_args.length = plen;
    h3_frame_write_header(work);
    size_t o = H3Frame.n;
    if (!o)
    {
        H3Frame.n = 0;
        return;
    }
    QuicVarint.encode_args.out = out + o;
    QuicVarint.encode_args.cap = cap - o;
    QuicVarint.encode_args.value = stream_id;
    QuicVarint.encode(quic_varint_work);
    size_t a = QuicVarint.n;
    if (!a)
    {
        H3Frame.n = 0;
        return;
    }
    H3Frame.n = o + a;
}

H3FrameNs H3Frame = {
    .parse_header = h3_frame_parse_header,
    .write_header = h3_frame_write_header,
    .type_reserved = h3_frame_type_reserved,
    .settings_defaults = h3_frame_settings_defaults,
    .parse_settings = h3_frame_parse_settings,
    .build_data = h3_frame_build_data,
    .build_headers = h3_frame_build_headers,
    .build_settings = h3_frame_build_settings,
    .build_goaway = h3_frame_build_goaway,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP3
