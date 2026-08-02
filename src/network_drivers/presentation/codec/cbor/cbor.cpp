// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cbor.cpp
 * @brief Zero-heap CBOR (RFC 8949) encoder implementation.
 */

#include "cbor.h"

#if PC_NEED_CBOR

#include "shared_primitives/bytes.h"
#include <string.h>

static void put(pc_span *w, uint8_t b)
{
    pc_bw_put(w, b);
}

// Write a CBOR head: the major type (top 3 bits) plus the argument, choosing the
// shortest of the 1/2/3/5/9-byte forms (RFC 8949 section 3). The argument after
// the lead byte is big-endian, the same network order as MessagePack.
static void head(pc_span *w, uint8_t major, uint64_t val)
{
    uint8_t m = (uint8_t)(major << 5);
    if (val < 24)
    {
        put(w, (uint8_t)(m | val));
    }
    else if (val < 0x100ULL)
    {
        put(w, (uint8_t)(m | 24));
        pc_bw_put_be(w, val, 1);
    }
    else if (val < 0x10000ULL)
    {
        put(w, (uint8_t)(m | 25));
        pc_bw_put_be(w, val, 2);
    }
    else if (val < 0x100000000ULL)
    {
        put(w, (uint8_t)(m | 26));
        pc_bw_put_be(w, val, 4);
    }
    else
    {
        put(w, (uint8_t)(m | 27));
        pc_bw_put_be(w, val, 8);
    }
}

void pc_cbor_uint(pc_span *w, uint64_t v)
{
    head(w, 0, v);
}

void pc_cbor_int(pc_span *w, int64_t v)
{
    if (v >= 0)
    {
        head(w, 0, (uint64_t)v);
    }
    else
    {
        head(w, 1, (uint64_t)(-1 - v)); // major 1 encodes -1 - n
    }
}

void pc_cbor_bytes(pc_span *w, const uint8_t *data, size_t len)
{
    head(w, 2, (uint64_t)len);
    for (size_t i = 0; i < len; i++)
    {
        put(w, data[i]);
    }
}

void pc_cbor_str_n(pc_span *w, const char *s, size_t len)
{
    head(w, 3, (uint64_t)len);
    for (size_t i = 0; i < len; i++)
    {
        put(w, (uint8_t)s[i]);
    }
}

void pc_cbor_str(pc_span *w, const char *s)
{
    pc_cbor_str_n(w, s, s ? strnlen(s, w->cap + 1) : 0);
}

void pc_cbor_bool(pc_span *w, proto_bool b)
{
    put(w, b ? 0xf5 : 0xf4);
}

void pc_cbor_null(pc_span *w)
{
    put(w, 0xf6);
}

void pc_cbor_float(pc_span *w, float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    put(w, 0xfa); // major 7, single-precision
    pc_bw_put_be(w, bits, 4);
}

void pc_cbor_array(pc_span *w, size_t count)
{
    head(w, 4, (uint64_t)count);
}

void pc_cbor_map(pc_span *w, size_t count)
{
    head(w, 5, (uint64_t)count);
}

void pc_cbor_label(pc_span *w, const char *name, int64_t num)
{
    (void)name; // CBOR carries the integer label form (RFC 8428 sec 6)
    pc_cbor_int(w, num);
}

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------

// Read a CBOR head at r->pos: major type + argument, advancing pos. Sets err and
// returns false on out-of-bounds or a reserved/indefinite additional-info value.
static proto_bool read_head(pc_cspan *r, uint8_t *major, uint64_t *val)
{
    if (r->err || r->pos >= r->len)
    {
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    uint8_t b = r->buf[r->pos];
    uint8_t info = (uint8_t)(b & 0x1f);
    *major = (uint8_t)(b >> 5);
    if (info < 24)
    {
        *val = info;
        r->pos += 1;
        return PROTO_TRUE;
    }
    size_t need;
    switch (info)
    {
    case 24:
        need = 1;
        break;
    case 25:
        need = 2;
        break;
    case 26:
        need = 4;
        break;
    case 27:
        need = 8;
        break;
    default:
        r->err = PROTO_TRUE; // 28-31: reserved / indefinite-length, unsupported
        return PROTO_FALSE;
    }
    // The argument is the `need` big-endian bytes after this head byte, so step over the head first.
    r->pos += 1;
    return pc_br_take_be(r, need, val);
}

pc_codec_type pc_cbor_peek(pc_cspan *r)
{
    if (r->err || r->pos >= r->len)
    {
        return PC_CODEC_INVALID;
    }
    uint8_t b = r->buf[r->pos];
    switch (b >> 5)
    {
    case 0:
        return PC_CODEC_UINT;
    case 1:
        return PC_CODEC_INT;
    case 2:
        return PC_CODEC_BYTES;
    case 3:
        return PC_CODEC_STR;
    case 4:
        return PC_CODEC_ARRAY;
    case 5:
        return PC_CODEC_MAP;
    case 7: {
        uint8_t info = (uint8_t)(b & 0x1f);
        if (info == 20 || info == 21)
        {
            return PC_CODEC_BOOL;
        }
        if (info == 22)
        {
            return PC_CODEC_NULL;
        }
        if (info == 26 || info == 27)
        {
            return PC_CODEC_FLOAT;
        }
        return PC_CODEC_INVALID;
    }
    default:
        return PC_CODEC_INVALID; // major 6 (tags) unsupported
    }
}

proto_bool pc_cbor_read_uint(pc_cspan *r, uint64_t *out)
{
    uint8_t m;
    uint64_t v;
    if (!read_head(r, &m, &v))
    {
        return PROTO_FALSE;
    }
    if (m != 0)
    {
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    *out = v;
    return PROTO_TRUE;
}

proto_bool pc_cbor_read_int(pc_cspan *r, int64_t *out)
{
    uint8_t m;
    uint64_t v;
    if (!read_head(r, &m, &v))
    {
        return PROTO_FALSE;
    }
    if (m == 0)
    {
        *out = (int64_t)v;
    }
    else if (m == 1)
    {
        *out = -1 - (int64_t)v;
    }
    else
    {
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

proto_bool pc_cbor_read_bool(pc_cspan *r, proto_bool *out)
{
    if (r->err || r->pos >= r->len)
    {
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    uint8_t b = r->buf[r->pos];
    if (b == 0xf4)
    {
        *out = PROTO_FALSE;
    }
    else if (b == 0xf5)
    {
        *out = PROTO_TRUE;
    }
    else
    {
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    r->pos += 1;
    return PROTO_TRUE;
}

proto_bool pc_cbor_read_null(pc_cspan *r)
{
    if (r->err || r->pos >= r->len || r->buf[r->pos] != 0xf6)
    {
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    r->pos += 1;
    return PROTO_TRUE;
}

proto_bool pc_cbor_read_float(pc_cspan *r, float *out)
{
    if (r->err || r->pos >= r->len)
    {
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    uint8_t b = r->buf[r->pos];
    if (b == 0xfa) // single
    {
        uint64_t v;
        r->pos += 1; // step over the head byte; the argument follows it
        if (!pc_br_take_be(r, 4, &v))
        {
            return PROTO_FALSE;
        }
        uint32_t bits = (uint32_t)v;
        memcpy(out, &bits, sizeof(*out));
        return PROTO_TRUE;
    }
    if (b == 0xfb) // double -> narrow to float
    {
        uint64_t bits;
        r->pos += 1; // step over the head byte; the argument follows it
        if (!pc_br_take_be(r, 8, &bits))
        {
            return PROTO_FALSE;
        }
        double d;
        memcpy(&d, &bits, sizeof(d));
        *out = (float)d;
        return PROTO_TRUE;
    }
    r->err = PROTO_TRUE;
    return PROTO_FALSE;
}

// Shared body for text (major 3) and byte (major 2) strings.
static proto_bool read_str(pc_cspan *r, uint8_t want_major, const uint8_t **out, size_t *len)
{
    uint8_t m;
    uint64_t v;
    if (!read_head(r, &m, &v))
    {
        return PROTO_FALSE;
    }
    if (m != want_major || r->pos + v > r->len)
    {
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    *out = &r->buf[r->pos];
    *len = (size_t)v;
    r->pos += (size_t)v;
    return PROTO_TRUE;
}

proto_bool pc_cbor_read_str(pc_cspan *r, const char **out, size_t *len)
{
    return read_str(r, 3, (const uint8_t **)out, len);
}

proto_bool pc_cbor_read_bytes(pc_cspan *r, const uint8_t **out, size_t *len)
{
    return read_str(r, 2, out, len);
}

proto_bool pc_cbor_read_array(pc_cspan *r, size_t *count)
{
    uint8_t m;
    uint64_t v;
    if (!read_head(r, &m, &v))
    {
        return PROTO_FALSE;
    }
    if (m != 4)
    {
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    *count = (size_t)v;
    return PROTO_TRUE;
}

proto_bool pc_cbor_read_map(pc_cspan *r, size_t *count)
{
    uint8_t m;
    uint64_t v;
    if (!read_head(r, &m, &v))
    {
        return PROTO_FALSE;
    }
    if (m != 5)
    {
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    *count = (size_t)v;
    return PROTO_TRUE;
}

#endif // PC_NEED_CBOR
