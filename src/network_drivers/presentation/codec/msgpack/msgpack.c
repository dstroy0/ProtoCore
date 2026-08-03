// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file msgpack.c
 * @brief Zero-heap MessagePack encoder and decoder implementation.
 */

#include "msgpack.h"

#if PC_ENABLE_MSGPACK

#include "shared_primitives/bytes.h"
#include <string.h>

// Thin local names over the shared byte verbs (bytes.h) so the call sites
// below read the same as before; the cursor invariants live in one place.
static void put(pc_span *w, uint8_t b)
{
    pc_bw_put(w, b);
}

// Write the low @p nbytes of @p val, big-endian (MessagePack is network order).
static void put_be(pc_span *w, uint64_t val, int32_t nbytes)
{
    pc_bw_put_be(w, val, nbytes);
}

void pc_msgpack_uint(pc_span *w, uint64_t v)
{
    if (v <= 0x7f)
    {
        put(w, (uint8_t)v); // positive fixint
    }
    else if (v <= 0xff)
    {
        put(w, 0xcc);
        put(w, (uint8_t)v);
    }
    else if (v <= 0xffff)
    {
        put(w, 0xcd);
        put_be(w, v, 2);
    }
    else if (v <= 0xffffffffULL)
    {
        put(w, 0xce);
        put_be(w, v, 4);
    }
    else
    {
        put(w, 0xcf);
        put_be(w, v, 8);
    }
}

void pc_msgpack_int(pc_span *w, int64_t v)
{
    if (v >= 0)
    {
        pc_msgpack_uint(w, (uint64_t)v);
        return;
    }
    if (v >= -32)
    {
        put(w, (uint8_t)v); // negative fixint (two's-complement byte 0xe0..0xff)
    }
    else if (v >= -128)
    {
        put(w, 0xd0);
        put(w, (uint8_t)v);
    }
    else if (v >= -32768)
    {
        put(w, 0xd1);
        put_be(w, (uint64_t)(uint16_t)v, 2);
    }
    else if (v >= -2147483648LL)
    {
        put(w, 0xd2);
        put_be(w, (uint64_t)(uint32_t)v, 4);
    }
    else
    {
        put(w, 0xd3);
        put_be(w, (uint64_t)v, 8);
    }
}

void pc_msgpack_str_n(pc_span *w, const char *s, size_t len)
{
    if (len <= 31)
    {
        put(w, (uint8_t)(0xa0 | len)); // fixstr
    }
    else if (len <= 0xff)
    {
        put(w, 0xd9);
        put(w, (uint8_t)len);
    }
    else if (len <= 0xffff)
    {
        put(w, 0xda);
        put_be(w, len, 2);
    }
    else
    {
        put(w, 0xdb);
        put_be(w, len, 4);
    }
    for (size_t i = 0; i < len; i++)
    {
        put(w, (uint8_t)s[i]);
    }
}

void pc_msgpack_str(pc_span *w, const char *s)
{
    pc_msgpack_str_n(w, s, s ? strnlen(s, w->cap + 1) : 0);
}

void pc_msgpack_bytes(pc_span *w, const uint8_t *data, size_t len)
{
    if (len <= 0xff)
    {
        put(w, 0xc4);
        put(w, (uint8_t)len);
    }
    else if (len <= 0xffff)
    {
        put(w, 0xc5);
        put_be(w, len, 2);
    }
    else
    {
        put(w, 0xc6);
        put_be(w, len, 4);
    }
    for (size_t i = 0; i < len; i++)
    {
        put(w, data[i]);
    }
}

void pc_msgpack_bool(pc_span *w, proto_bool b)
{
    put(w, b ? 0xc3 : 0xc2);
}

void pc_msgpack_null(pc_span *w)
{
    put(w, 0xc0);
}

void pc_msgpack_float(pc_span *w, float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    put(w, 0xca); // float32
    put_be(w, bits, 4);
}

void pc_msgpack_array(pc_span *w, size_t count)
{
    if (count <= 15)
    {
        put(w, (uint8_t)(0x90 | count)); // fixarray
    }
    else if (count <= 0xffff)
    {
        put(w, 0xdc);
        put_be(w, count, 2);
    }
    else
    {
        put(w, 0xdd);
        put_be(w, count, 4);
    }
}

void pc_msgpack_map(pc_span *w, size_t count)
{
    if (count <= 15)
    {
        put(w, (uint8_t)(0x80 | count)); // fixmap
    }
    else if (count <= 0xffff)
    {
        put(w, 0xde);
        put_be(w, count, 2);
    }
    else
    {
        put(w, 0xdf);
        put_be(w, count, 4);
    }
}

void pc_msgpack_label(pc_span *w, const char *name, int64_t num)
{
    (void)name; // the binary packs carry the integer label form, as CBOR does
    pc_msgpack_int(w, num);
}

// ---------------------------------------------------------------------------
// Decoder
// ---------------------------------------------------------------------------

// Consume the format byte at the cursor plus the @p nbytes big-endian argument that follows it.
//
// The format byte is MessagePack's framing, so stepping over it is this codec's step. It used to
// live inside pc_br_take_be(), which made the shared cursor unable to express a plain big-endian
// read; this function is where that knowledge belongs. A cursor already at the end advances to
// len + 1 and pc_br_take_be() rejects it, so the step needs no guard of its own.
static proto_bool take_be(pc_cspan *r, size_t nbytes, uint64_t *out)
{
    r->pos += 1;
    return pc_br_take_be(r, nbytes, out);
}

pc_codec_type pc_msgpack_peek(pc_cspan *r)
{
    if (r->err || r->pos >= r->len)
    {
        return PC_CODEC_INVALID;
    }
    uint8_t b = r->buf[r->pos];
    if (b <= 0x7f)
    {
        return PC_CODEC_UINT; // positive fixint
    }
    if (b >= 0xe0)
    {
        return PC_CODEC_INT; // negative fixint
    }
    // b is now in [0x80, 0xdf]; each fix* range's lower bound is already
    // established by the preceding checks, so test only the ascending upper bound.
    if (b <= 0x8f)
    {
        return PC_CODEC_MAP; // fixmap   (0x80-0x8f)
    }
    if (b <= 0x9f)
    {
        return PC_CODEC_ARRAY; // fixarray (0x90-0x9f)
    }
    if (b <= 0xbf)
    {
        return PC_CODEC_STR; // fixstr   (0xa0-0xbf)
    }
    switch (b)
    {
    case 0xc0:
        return PC_CODEC_NULL;
    case 0xc2:
    case 0xc3:
        return PC_CODEC_BOOL;
    case 0xc4:
    case 0xc5:
    case 0xc6:
        return PC_CODEC_BYTES;
    case 0xca:
    case 0xcb:
        return PC_CODEC_FLOAT;
    case 0xcc:
    case 0xcd:
    case 0xce:
    case 0xcf:
        return PC_CODEC_UINT;
    case 0xd0:
    case 0xd1:
    case 0xd2:
    case 0xd3:
        return PC_CODEC_INT;
    case 0xd9:
    case 0xda:
    case 0xdb:
        return PC_CODEC_STR;
    case 0xdc:
    case 0xdd:
        return PC_CODEC_ARRAY;
    case 0xde:
    case 0xdf:
        return PC_CODEC_MAP;
    default:
        return PC_CODEC_INVALID; // 0xc1, ext (0xc7-0xc9, 0xd4-0xd8)
    }
}

proto_bool pc_msgpack_read_uint(pc_cspan *r, uint64_t *out)
{
    if (r->err || r->pos >= r->len)
    {
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    uint8_t b = r->buf[r->pos];
    if (b <= 0x7f) // positive fixint
    {
        *out = b;
        r->pos += 1;
        return PROTO_TRUE;
    }
    uint64_t v;
    switch (b)
    {
    case 0xcc:
        if (!take_be(r, 1, &v))
        {
            return PROTO_FALSE;
        }
        break;
    case 0xcd:
        if (!take_be(r, 2, &v))
        {
            return PROTO_FALSE;
        }
        break;
    case 0xce:
        if (!take_be(r, 4, &v))
        {
            return PROTO_FALSE;
        }
        break;
    case 0xcf:
        if (!take_be(r, 8, &v))
        {
            return PROTO_FALSE;
        }
        break;
    default:
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    *out = v;
    return PROTO_TRUE;
}

proto_bool pc_msgpack_read_int(pc_cspan *r, int64_t *out)
{
    if (r->err || r->pos >= r->len)
    {
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    uint8_t b = r->buf[r->pos];
    if (b <= 0x7f) // positive fixint
    {
        *out = b;
        r->pos += 1;
        return PROTO_TRUE;
    }
    if (b >= 0xe0) // negative fixint (two's-complement byte)
    {
        *out = (int8_t)b;
        r->pos += 1;
        return PROTO_TRUE;
    }
    uint64_t v;
    switch (b)
    {
    case 0xcc: // uint8
        if (!take_be(r, 1, &v))
        {
            return PROTO_FALSE;
        }
        *out = (int64_t)v;
        return PROTO_TRUE;
    case 0xcd: // uint16
        if (!take_be(r, 2, &v))
        {
            return PROTO_FALSE;
        }
        *out = (int64_t)v;
        return PROTO_TRUE;
    case 0xce: // uint32
        if (!take_be(r, 4, &v))
        {
            return PROTO_FALSE;
        }
        *out = (int64_t)v;
        return PROTO_TRUE;
    case 0xcf: // uint64 (may exceed int64 range; wraps as two's-complement)
        if (!take_be(r, 8, &v))
        {
            return PROTO_FALSE;
        }
        *out = (int64_t)v;
        return PROTO_TRUE;
    case 0xd0: // int8
        if (!take_be(r, 1, &v))
        {
            return PROTO_FALSE;
        }
        *out = (int8_t)(uint8_t)v;
        return PROTO_TRUE;
    case 0xd1: // int16
        if (!take_be(r, 2, &v))
        {
            return PROTO_FALSE;
        }
        *out = (int16_t)(uint16_t)v;
        return PROTO_TRUE;
    case 0xd2: // int32
        if (!take_be(r, 4, &v))
        {
            return PROTO_FALSE;
        }
        *out = (int32_t)(uint32_t)v;
        return PROTO_TRUE;
    case 0xd3: // int64
        if (!take_be(r, 8, &v))
        {
            return PROTO_FALSE;
        }
        *out = (int64_t)v;
        return PROTO_TRUE;
    default:
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
}

proto_bool pc_msgpack_read_bool(pc_cspan *r, proto_bool *out)
{
    if (r->err || r->pos >= r->len)
    {
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    uint8_t b = r->buf[r->pos];
    if (b == 0xc2)
    {
        *out = PROTO_FALSE;
    }
    else if (b == 0xc3)
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

proto_bool pc_msgpack_read_null(pc_cspan *r)
{
    if (r->err || r->pos >= r->len || r->buf[r->pos] != 0xc0)
    {
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    r->pos += 1;
    return PROTO_TRUE;
}

proto_bool pc_msgpack_read_float(pc_cspan *r, float *out)
{
    if (r->err || r->pos >= r->len)
    {
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    uint8_t b = r->buf[r->pos];
    uint64_t v;
    if (b == 0xca) // float32
    {
        if (!take_be(r, 4, &v))
        {
            return PROTO_FALSE;
        }
        uint32_t bits = (uint32_t)v;
        memcpy(out, &bits, sizeof(*out));
        return PROTO_TRUE;
    }
    if (b == 0xcb) // float64 -> narrow to float
    {
        if (!take_be(r, 8, &v))
        {
            return PROTO_FALSE;
        }
        double d;
        memcpy(&d, &v, sizeof(d));
        *out = (float)d;
        return PROTO_TRUE;
    }
    r->err = PROTO_TRUE;
    return PROTO_FALSE;
}

// Shared body for the str family (fixstr / str8/16/32) and bin family (bin8/16/32).
static proto_bool read_blob(pc_cspan *r, proto_bool want_str, const uint8_t **out, size_t *len)
{
    if (r->err || r->pos >= r->len)
    {
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    uint8_t b = r->buf[r->pos];
    size_t n;
    uint64_t v;
    if (want_str && b >= 0xa0 && b <= 0xbf) // fixstr
    {
        n = (size_t)(b & 0x1f);
        r->pos += 1;
    }
    else
    {
        const uint8_t f8 = want_str ? 0xd9 : 0xc4;
        const uint8_t f16 = want_str ? 0xda : 0xc5;
        const uint8_t f32 = want_str ? 0xdb : 0xc6;
        if (b == f8)
        {
            if (!take_be(r, 1, &v))
            {
                return PROTO_FALSE;
            }
        }
        else if (b == f16)
        {
            if (!take_be(r, 2, &v))
            {
                return PROTO_FALSE;
            }
        }
        else if (b == f32)
        {
            if (!take_be(r, 4, &v))
            {
                return PROTO_FALSE;
            }
        }
        else
        {
            r->err = PROTO_TRUE;
            return PROTO_FALSE;
        }
        n = (size_t)v;
    }
    if (r->pos + n > r->len) // payload bounds
    {
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    *out = &r->buf[r->pos];
    *len = n;
    r->pos += n;
    return PROTO_TRUE;
}

proto_bool pc_msgpack_read_str(pc_cspan *r, const char **out, size_t *len)
{
    return read_blob(r, PROTO_TRUE, (const uint8_t **)out, len);
}

proto_bool pc_msgpack_read_bytes(pc_cspan *r, const uint8_t **out, size_t *len)
{
    return read_blob(r, PROTO_FALSE, out, len);
}

// Shared body for the array family (fixarray / array16/32) and map family.
static proto_bool read_count(pc_cspan *r, proto_bool want_map, size_t *count)
{
    if (r->err || r->pos >= r->len)
    {
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    uint8_t b = r->buf[r->pos];
    const uint8_t fix_lo = want_map ? 0x80 : 0x90;
    const uint8_t fix_hi = want_map ? 0x8f : 0x9f;
    const uint8_t f16 = want_map ? 0xde : 0xdc;
    const uint8_t f32 = want_map ? 0xdf : 0xdd;
    if (b >= fix_lo && b <= fix_hi)
    {
        *count = (size_t)(b & 0x0f);
        r->pos += 1;
        return PROTO_TRUE;
    }
    uint64_t v;
    if (b == f16)
    {
        if (!take_be(r, 2, &v))
        {
            return PROTO_FALSE;
        }
    }
    else if (b == f32)
    {
        if (!take_be(r, 4, &v))
        {
            return PROTO_FALSE;
        }
    }
    else
    {
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    *count = (size_t)v;
    return PROTO_TRUE;
}

proto_bool pc_msgpack_read_array(pc_cspan *r, size_t *count)
{
    return read_count(r, PROTO_FALSE, count);
}

proto_bool pc_msgpack_read_map(pc_cspan *r, size_t *count)
{
    return read_count(r, PROTO_TRUE, count);
}

#endif // PC_ENABLE_MSGPACK
