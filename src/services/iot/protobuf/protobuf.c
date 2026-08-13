// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protobuf.c
 * @brief Protocol Buffers wire codec (pure, host-tested against spec vectors).
 */

#include "services/iot/protobuf/protobuf.h"
#include "mmgr/protomem.h"

#if PROTOCORE_NEED_PROTOBUF

void protocore_pb_writer_init(PbWriter *w, uint8_t *buf, size_t cap)
{
    w->buf = buf;
    w->cap = cap;
    w->pos = 0;
    w->error = PROTO_FALSE;
}

proto_bool protocore_pb_write_varint(PbWriter *w, uint64_t v)
{
    if (w->error)
    {
        return PROTO_FALSE;
    }
    uint8_t tmp[10];
    size_t n = 0;
    do
    {
        uint8_t b = (uint8_t)(v & 0x7F); // low 7 bits of payload
        v >>= 7;
        if (v)
        {
            b |= 0x80; // high bit = "more bytes follow" (continuation)
        }
        tmp[n++] = b;
    } while (v);
    if (w->pos + n > w->cap)
    {
        w->error = PROTO_TRUE;
        return PROTO_FALSE;
    }
    mem.cpy(w->buf + w->pos, tmp, n);
    w->pos += n;
    return PROTO_TRUE;
}

proto_bool protocore_pb_write_tag(PbWriter *w, uint32_t field, uint8_t wire_type)
{
    return protocore_pb_write_varint(w,
                                     ((uint64_t)field << 3) | (wire_type & 0x07)); // tag = field<<3 | wire(low 3 bits)
}

// Append @p n raw little-endian octets of @p v.
static proto_bool protocore_pb_write_le(PbWriter *w, uint64_t v, size_t n)
{
    if (w->error)
    {
        return PROTO_FALSE;
    }
    if (w->pos + n > w->cap)
    {
        w->error = PROTO_TRUE;
        return PROTO_FALSE;
    }
    for (size_t i = 0; i < n; i++)
    {
        w->buf[w->pos++] = (uint8_t)(v >> (8 * i));
    }
    return PROTO_TRUE;
}

proto_bool protocore_pb_uint64(PbWriter *w, uint32_t field, uint64_t v)
{
    return protocore_pb_write_tag(w, field, PB_WT_VARINT) && protocore_pb_write_varint(w, v);
}

proto_bool protocore_pb_int64(PbWriter *w, uint32_t field, int64_t v)
{
    return protocore_pb_uint64(w, field, (uint64_t)v); // two's complement; negatives take 10 bytes
}

proto_bool protocore_pb_sint64(PbWriter *w, uint32_t field, int64_t v)
{
    uint64_t zz = ((uint64_t)v << 1) ^ (uint64_t)(v >> 63); // ZigZag
    return protocore_pb_uint64(w, field, zz);
}

proto_bool protocore_pb_bool(PbWriter *w, uint32_t field, proto_bool v)
{
    return protocore_pb_uint64(w, field, v ? 1 : 0);
}

proto_bool protocore_pb_fixed32(PbWriter *w, uint32_t field, uint32_t v)
{
    return protocore_pb_write_tag(w, field, PB_WT_I32) && protocore_pb_write_le(w, v, 4);
}

proto_bool protocore_pb_fixed64(PbWriter *w, uint32_t field, uint64_t v)
{
    return protocore_pb_write_tag(w, field, PB_WT_I64) && protocore_pb_write_le(w, v, 8);
}

proto_bool protocore_pb_float(PbWriter *w, uint32_t field, float v)
{
    uint32_t bits;
    mem.cpy(&bits, &v, 4);
    return protocore_pb_fixed32(w, field, bits);
}

proto_bool protocore_pb_double(PbWriter *w, uint32_t field, double v)
{
    uint64_t bits;
    mem.cpy(&bits, &v, 8);
    return protocore_pb_fixed64(w, field, bits);
}

proto_bool protocore_pb_bytes(PbWriter *w, uint32_t field, const uint8_t *data, size_t len)
{
    if (!protocore_pb_write_tag(w, field, PB_WT_LEN) || !protocore_pb_write_varint(w, len))
    {
        return PROTO_FALSE;
    }
    if (w->error)
    {
        return PROTO_FALSE;
    }
    if (len == 0)
    {
        return PROTO_TRUE;
    }
    if (!data || w->pos + len > w->cap)
    {
        w->error = PROTO_TRUE;
        return PROTO_FALSE;
    }
    mem.cpy(w->buf + w->pos, data, len);
    w->pos += len;
    return PROTO_TRUE;
}

proto_bool protocore_pb_string(PbWriter *w, uint32_t field, const char *s)
{
    if (!s)
    {
        w->error = PROTO_TRUE;
        return PROTO_FALSE;
    }
    return protocore_pb_bytes(w, field, (const uint8_t *)s, strnlen(s, w->cap + 1));
}

size_t protocore_pb_writer_finish(PbWriter *w)
{
    return w->error ? 0 : w->pos;
}

proto_bool protocore_pb_read_varint(const uint8_t *buf, size_t len, size_t *pos, uint64_t *out)
{
    if (!buf || !pos || !out)
    {
        return PROTO_FALSE;
    }
    uint64_t v = 0;
    size_t shift = 0;
    size_t i = *pos;
    for (size_t b = 0; b < 10; b++) // a 64-bit varint is at most 10 bytes
    {
        if (i >= len)
        {
            return PROTO_FALSE; // truncated
        }
        uint8_t c = buf[i++];
        v |= (uint64_t)(c & 0x7F) << shift; // accumulate 7 payload bits, little-endian
        if (!(c & 0x80))                    // continuation bit clear -> last byte
        {
            *out = v;
            *pos = i;
            return PROTO_TRUE;
        }
        shift += 7;
    }
    return PROTO_FALSE; // overlong / unterminated
}

proto_bool protocore_pb_read_field(const uint8_t *buf, size_t len, size_t *pos, PbField *out)
{
    if (!buf || !pos || !out || *pos >= len)
    {
        return PROTO_FALSE;
    }
    uint64_t tag;
    if (!protocore_pb_read_varint(buf, len, pos, &tag))
    {
        return PROTO_FALSE;
    }
    out->field_number = (uint32_t)(tag >> 3); // tag high bits = field number
    out->wire_type = (uint8_t)(tag & 0x07);   // tag low 3 bits = wire type
    out->value = 0;
    out->data = NULL;
    out->len = 0;

    switch (out->wire_type)
    {
    case PB_WT_VARINT:
        return protocore_pb_read_varint(buf, len, pos, &out->value);
    case PB_WT_I64: {
        if (*pos + 8 > len)
        {
            return PROTO_FALSE;
        }
        uint64_t v = 0;
        for (size_t i = 0; i < 8; i++)
        {
            v |= (uint64_t)buf[*pos + i] << (8 * i);
        }
        *pos += 8;
        out->value = v;
        return PROTO_TRUE;
    }
    case PB_WT_I32: {
        if (*pos + 4 > len)
        {
            return PROTO_FALSE;
        }
        uint32_t v = 0;
        for (size_t i = 0; i < 4; i++)
        {
            v |= (uint32_t)buf[*pos + i] << (8 * i);
        }
        *pos += 4;
        out->value = v;
        return PROTO_TRUE;
    }
    case PB_WT_LEN: {
        uint64_t l;
        if (!protocore_pb_read_varint(buf, len, pos, &l))
        {
            return PROTO_FALSE;
        }
        if (*pos + l > len)
        {
            return PROTO_FALSE; // payload not fully buffered
        }
        out->data = buf + *pos;
        out->len = (size_t)l;
        *pos += (size_t)l;
        return PROTO_TRUE;
    }
    default:
        return PROTO_FALSE; // groups (3/4) / reserved (6/7) are not supported
    }
}

int64_t protocore_pb_zigzag64(uint64_t v)
{
    return (int64_t)(v >> 1) ^ -(int64_t)(v & 1);
}

int32_t protocore_pb_zigzag32(uint32_t v)
{
    return (int32_t)(v >> 1) ^ -(int32_t)(v & 1);
}

float protocore_pb_float_bits(uint32_t bits)
{
    float f;
    mem.cpy(&f, &bits, 4);
    return f;
}

double protocore_pb_double_bits(uint64_t bits)
{
    double d;
    mem.cpy(&d, &bits, 8);
    return d;
}

#endif // PROTOCORE_NEED_PROTOBUF
