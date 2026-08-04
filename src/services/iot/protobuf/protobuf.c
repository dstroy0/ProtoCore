// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protobuf.c
 * @brief Protocol Buffers wire codec (pure, host-tested against spec vectors).
 */

#include "services/iot/protobuf/protobuf.h"

#if PC_NEED_PROTOBUF

#include <string.h>

void pc_pb_writer_init(PbWriter *w, uint8_t *buf, size_t cap)
{
    w->buf = buf;
    w->cap = cap;
    w->pos = 0;
    w->error = PROTO_FALSE;
}

proto_bool pc_pb_write_varint(PbWriter *w, uint64_t v)
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
    memcpy(w->buf + w->pos, tmp, n);
    w->pos += n;
    return PROTO_TRUE;
}

proto_bool pc_pb_write_tag(PbWriter *w, uint32_t field, uint8_t wire_type)
{
    return pc_pb_write_varint(w, ((uint64_t)field << 3) | (wire_type & 0x07)); // tag = field<<3 | wire(low 3 bits)
}

// Append @p n raw little-endian octets of @p v.
static proto_bool pc_pb_write_le(PbWriter *w, uint64_t v, size_t n)
{
    // right operand of `pc_pb_write_tag(...) && pc_pb_write_le(...)`; write_tag returns true only
    // when its inner write_varint succeeded, which itself guarantees w->error is still false at
    // that point. So w->error can never already be true on entry here.
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

proto_bool pc_pb_uint64(PbWriter *w, uint32_t field, uint64_t v)
{
    return pc_pb_write_tag(w, field, PB_WT_VARINT) && pc_pb_write_varint(w, v);
}

proto_bool pc_pb_int64(PbWriter *w, uint32_t field, int64_t v)
{
    return pc_pb_uint64(w, field, (uint64_t)v); // two's complement; negatives take 10 bytes
}

proto_bool pc_pb_sint64(PbWriter *w, uint32_t field, int64_t v)
{
    uint64_t zz = ((uint64_t)v << 1) ^ (uint64_t)(v >> 63); // ZigZag
    return pc_pb_uint64(w, field, zz);
}

proto_bool pc_pb_bool(PbWriter *w, uint32_t field, proto_bool v)
{
    return pc_pb_uint64(w, field, v ? 1 : 0);
}

proto_bool pc_pb_fixed32(PbWriter *w, uint32_t field, uint32_t v)
{
    return pc_pb_write_tag(w, field, PB_WT_I32) && pc_pb_write_le(w, v, 4);
}

proto_bool pc_pb_fixed64(PbWriter *w, uint32_t field, uint64_t v)
{
    return pc_pb_write_tag(w, field, PB_WT_I64) && pc_pb_write_le(w, v, 8);
}

proto_bool pc_pb_float(PbWriter *w, uint32_t field, float v)
{
    uint32_t bits;
    memcpy(&bits, &v, 4);
    return pc_pb_fixed32(w, field, bits);
}

proto_bool pc_pb_double(PbWriter *w, uint32_t field, double v)
{
    uint64_t bits;
    memcpy(&bits, &v, 8);
    return pc_pb_fixed64(w, field, bits);
}

proto_bool pc_pb_bytes(PbWriter *w, uint32_t field, const uint8_t *data, size_t len)
{
    if (!pc_pb_write_tag(w, field, PB_WT_LEN) || !pc_pb_write_varint(w, len))
    {
        return PROTO_FALSE;
    }
    // which (by the same argument as pc_pb_write_le) guarantees w->error is false here.
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
    memcpy(w->buf + w->pos, data, len);
    w->pos += len;
    return PROTO_TRUE;
}

proto_bool pc_pb_string(PbWriter *w, uint32_t field, const char *s)
{
    if (!s)
    {
        w->error = PROTO_TRUE;
        return PROTO_FALSE;
    }
    return pc_pb_bytes(w, field, (const uint8_t *)s, strnlen(s, w->cap + 1));
}

size_t pc_pb_writer_finish(PbWriter *w)
{
    return w->error ? 0 : w->pos;
}

proto_bool pc_pb_read_varint(const uint8_t *buf, size_t len, size_t *pos, uint64_t *out)
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

proto_bool pc_pb_read_field(const uint8_t *buf, size_t len, size_t *pos, PbField *out)
{
    if (!buf || !pos || !out || *pos >= len)
    {
        return PROTO_FALSE;
    }
    uint64_t tag;
    if (!pc_pb_read_varint(buf, len, pos, &tag))
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
        return pc_pb_read_varint(buf, len, pos, &out->value);
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
        if (!pc_pb_read_varint(buf, len, pos, &l))
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

int64_t pc_pb_zigzag64(uint64_t v)
{
    return (int64_t)(v >> 1) ^ -(int64_t)(v & 1);
}

int32_t pc_pb_zigzag32(uint32_t v)
{
    return (int32_t)(v >> 1) ^ -(int32_t)(v & 1);
}

float pc_pb_float_bits(uint32_t bits)
{
    float f;
    memcpy(&f, &bits, 4);
    return f;
}

double pc_pb_double_bits(uint64_t bits)
{
    double d;
    memcpy(&d, &bits, 8);
    return d;
}

#endif // PC_NEED_PROTOBUF
