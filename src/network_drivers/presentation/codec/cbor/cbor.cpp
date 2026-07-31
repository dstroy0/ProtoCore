// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file cbor.cpp
 * @brief Zero-heap CBOR (RFC 8949) encoder implementation.
 */

#include "cbor.h"

#if PC_ENABLE_CBOR

#include "shared_primitives/bytes.h"
#include <string.h>

void pc_cbor_init(CborWriter *w, uint8_t *buf, size_t cap)
{
    pc_bw_init(w, buf, cap);
}

size_t pc_cbor_len(const CborWriter *w)
{
    return pc_bw_len(w);
}

bool pc_cbor_ok(const CborWriter *w)
{
    return pc_bw_ok(w);
}

static void put(CborWriter *w, uint8_t b)
{
    pc_bw_put(w, b);
}

// Write a CBOR head: the major type (top 3 bits) plus the argument, choosing the
// shortest of the 1/2/3/5/9-byte forms (RFC 8949 section 3). The argument after
// the lead byte is big-endian, the same network order as MessagePack.
static void head(CborWriter *w, uint8_t major, uint64_t val)
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

void pc_cbor_uint(CborWriter *w, uint64_t v)
{
    head(w, 0, v);
}

void pc_cbor_int(CborWriter *w, int64_t v)
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

void pc_cbor_bytes(CborWriter *w, const uint8_t *data, size_t len)
{
    head(w, 2, (uint64_t)len);
    for (size_t i = 0; i < len; i++)
    {
        put(w, data[i]);
    }
}

void pc_cbor_text_n(CborWriter *w, const char *s, size_t len)
{
    head(w, 3, (uint64_t)len);
    for (size_t i = 0; i < len; i++)
    {
        put(w, (uint8_t)s[i]);
    }
}

void pc_cbor_text(CborWriter *w, const char *s)
{
    pc_cbor_text_n(w, s, s ? strnlen(s, w->cap + 1) : 0);
}

void pc_cbor_bool(CborWriter *w, bool b)
{
    put(w, b ? 0xf5 : 0xf4);
}

void pc_cbor_null(CborWriter *w)
{
    put(w, 0xf6);
}

void pc_cbor_float(CborWriter *w, float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    put(w, 0xfa); // major 7, single-precision
    pc_bw_put_be(w, bits, 4);
}

void pc_cbor_array(CborWriter *w, size_t count)
{
    head(w, 4, (uint64_t)count);
}

void pc_cbor_map(CborWriter *w, size_t count)
{
    head(w, 5, (uint64_t)count);
}

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------

void pc_cbor_reader_init(CborReader *r, const uint8_t *buf, size_t len)
{
    pc_br_init(r, buf, len);
}

bool pc_cbor_reader_ok(const CborReader *r)
{
    return pc_br_ok(r);
}

// Read a CBOR head at r->pos: major type + argument, advancing pos. Sets err and
// returns false on out-of-bounds or a reserved/indefinite additional-info value.
static bool read_head(CborReader *r, uint8_t *major, uint64_t *val)
{
    if (r->err || r->pos >= r->len)
    {
        r->err = true;
        return false;
    }
    uint8_t b = r->buf[r->pos];
    uint8_t info = (uint8_t)(b & 0x1f);
    *major = (uint8_t)(b >> 5);
    if (info < 24)
    {
        *val = info;
        r->pos += 1;
        return true;
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
        r->err = true; // 28-31: reserved / indefinite-length, unsupported
        return false;
    }
    // The argument is the `need` big-endian bytes after this head byte.
    return pc_br_take_be(r, need, val);
}

CborType pc_cbor_peek(CborReader *r)
{
    if (r->err || r->pos >= r->len)
    {
        return CborType::CBOR_TYPE_INVALID;
    }
    uint8_t b = r->buf[r->pos];
    switch (b >> 5)
    {
    case 0:
        return CborType::CBOR_TYPE_UINT;
    case 1:
        return CborType::CBOR_TYPE_INT;
    case 2:
        return CborType::CBOR_TYPE_BYTES;
    case 3:
        return CborType::CBOR_TYPE_TEXT;
    case 4:
        return CborType::CBOR_TYPE_ARRAY;
    case 5:
        return CborType::CBOR_TYPE_MAP;
    case 7: {
        uint8_t info = (uint8_t)(b & 0x1f);
        if (info == 20 || info == 21)
        {
            return CborType::CBOR_TYPE_BOOL;
        }
        if (info == 22)
        {
            return CborType::CBOR_TYPE_NULL;
        }
        if (info == 26 || info == 27)
        {
            return CborType::CBOR_TYPE_FLOAT;
        }
        return CborType::CBOR_TYPE_INVALID;
    }
    default:
        return CborType::CBOR_TYPE_INVALID; // major 6 (tags) unsupported
    }
}

bool pc_cbor_read_uint(CborReader *r, uint64_t *out)
{
    uint8_t m;
    uint64_t v;
    if (!read_head(r, &m, &v))
    {
        return false;
    }
    if (m != 0)
    {
        r->err = true;
        return false;
    }
    *out = v;
    return true;
}

bool pc_cbor_read_int(CborReader *r, int64_t *out)
{
    uint8_t m;
    uint64_t v;
    if (!read_head(r, &m, &v))
    {
        return false;
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
        r->err = true;
        return false;
    }
    return true;
}

bool pc_cbor_read_bool(CborReader *r, bool *out)
{
    if (r->err || r->pos >= r->len)
    {
        r->err = true;
        return false;
    }
    uint8_t b = r->buf[r->pos];
    if (b == 0xf4)
    {
        *out = false;
    }
    else if (b == 0xf5)
    {
        *out = true;
    }
    else
    {
        r->err = true;
        return false;
    }
    r->pos += 1;
    return true;
}

bool pc_cbor_read_null(CborReader *r)
{
    if (r->err || r->pos >= r->len || r->buf[r->pos] != 0xf6)
    {
        r->err = true;
        return false;
    }
    r->pos += 1;
    return true;
}

bool pc_cbor_read_float(CborReader *r, float *out)
{
    if (r->err || r->pos >= r->len)
    {
        r->err = true;
        return false;
    }
    uint8_t b = r->buf[r->pos];
    if (b == 0xfa) // single
    {
        uint64_t v;
        if (!pc_br_take_be(r, 4, &v))
        {
            return false;
        }
        uint32_t bits = (uint32_t)v;
        memcpy(out, &bits, sizeof(*out));
        return true;
    }
    if (b == 0xfb) // double -> narrow to float
    {
        uint64_t bits;
        if (!pc_br_take_be(r, 8, &bits))
        {
            return false;
        }
        double d;
        memcpy(&d, &bits, sizeof(d));
        *out = (float)d;
        return true;
    }
    r->err = true;
    return false;
}

// Shared body for text (major 3) and byte (major 2) strings.
static bool read_str(CborReader *r, uint8_t want_major, const uint8_t **out, size_t *len)
{
    uint8_t m;
    uint64_t v;
    if (!read_head(r, &m, &v))
    {
        return false;
    }
    if (m != want_major || r->pos + v > r->len)
    {
        r->err = true;
        return false;
    }
    *out = &r->buf[r->pos];
    *len = (size_t)v;
    r->pos += (size_t)v;
    return true;
}

bool pc_cbor_read_text(CborReader *r, const char **out, size_t *len)
{
    return read_str(r, 3, (const uint8_t **)out, len);
}

bool pc_cbor_read_bytes(CborReader *r, const uint8_t **out, size_t *len)
{
    return read_str(r, 2, out, len);
}

bool pc_cbor_read_array(CborReader *r, size_t *count)
{
    uint8_t m;
    uint64_t v;
    if (!read_head(r, &m, &v))
    {
        return false;
    }
    if (m != 4)
    {
        r->err = true;
        return false;
    }
    *count = (size_t)v;
    return true;
}

bool pc_cbor_read_map(CborReader *r, size_t *count)
{
    uint8_t m;
    uint64_t v;
    if (!read_head(r, &m, &v))
    {
        return false;
    }
    if (m != 5)
    {
        r->err = true;
        return false;
    }
    *count = (size_t)v;
    return true;
}

#endif // PC_ENABLE_CBOR
