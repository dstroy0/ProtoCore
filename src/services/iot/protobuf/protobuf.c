// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protobuf.c
 * @brief The Protocol Buffers wire format: the record encoder and the record cursor decoder.
 *
 * Google's "Encoding" document (https://protobuf.dev/programming-guides/encoding/) governs every
 * layout here; it is not an IETF document and no RFC number applies. An encode appends a tag varint
 * then a payload sized by the tag's wire type. A decode reads the tag at the cursor, takes the
 * payload the wire type calls for, and leaves the cursor past it.
 */

#include "services/iot/protobuf/protobuf.h"

#if PROTOCORE_NEED_PROTOBUF

#include "mmgr/protomem.h" // mem.cpy: the payload octets and the float bit patterns
#include "mmgr/protostr.h" // str.len: the bounded length of a NUL-terminated LEN payload

// One encoder row: the caller buffer it appends into, how far it has appended, and the sticky flag
// an overflow sets.
typedef struct
{
    uint8_t *buf;
    size_t cap;
    size_t pos;
    proto_bool error;
} ProtobufWriterRow;

// One decoder row: the encoded octets it walks and the offset it has walked to.
typedef struct
{
    const uint8_t *buf;
    size_t len;
    size_t pos;
} ProtobufReaderRow;

/**
 * @brief The codec's compile-time storage: the encoder rows and the decoder rows.
 *
 * All of it BSS, so an embedded message costs no heap and lands on no task stack.
 */
struct ProtobufStorage
{
    ProtobufWriterRow writers[PROTOCORE_PROTOBUF_SLOTS]; ///< the rows an encode appends into
    ProtobufReaderRow readers[PROTOCORE_PROTOBUF_SLOTS]; ///< the rows a decode walks
};

/**
 * @brief The codec's rows and the calls that reach them - what ProtobufNs points at.
 *
 * @var ProtobufInternal::store  the encoder rows and the decoder rows
 * @var ProtobufInternal::ns     the handle a caller sets a call's members on
 */
struct ProtobufInternal
{
    struct ProtobufStorage *store;
    ProtobufNs *ns;
};

static struct ProtobufStorage s_store;

static struct ProtobufInternal s_protobuf = {.store = &s_store, .ns = &Protobuf};

// The encoder row ns->slot names, or NULL when the slot is past the pool.
static ProtobufWriterRow *writer_row(struct ProtobufInternal *restrict ctx)
{
    if (ctx->ns->slot >= PROTOCORE_PROTOBUF_SLOTS)
    {
        return NULL;
    }
    return &ctx->store->writers[ctx->ns->slot];
}

// The decoder row ns->slot names, or NULL when the slot is past the pool.
static ProtobufReaderRow *reader_row(struct ProtobufInternal *restrict ctx)
{
    if (ctx->ns->slot >= PROTOCORE_PROTOBUF_SLOTS)
    {
        return NULL;
    }
    return &ctx->store->readers[ctx->ns->slot];
}

// Append v as a Base 128 varint: seven payload bits per octet, little-endian, the continuation bit
// (MSB) set on every octet but the last. Sets the row's sticky error when it does not fit.
static proto_bool writer_varint(struct ProtobufInternal *restrict ctx, uint64_t v)
{
    ProtobufWriterRow *w = writer_row(ctx);
    if (!w || w->error)
    {
        return PROTO_FALSE;
    }
    uint8_t tmp[PROTOCORE_PROTOBUF_VARINT_MAX];
    size_t n = 0;
    do
    {
        uint8_t b = (uint8_t)(v & 0x7Fu);
        v >>= 7;
        if (v)
        {
            b |= 0x80u;
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

// Append the low n octets of v, least significant first, the layout I32 and I64 payloads take.
static proto_bool writer_le(struct ProtobufInternal *restrict ctx, uint64_t v, size_t n)
{
    ProtobufWriterRow *w = writer_row(ctx);
    if (!w || w->error)
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
        w->buf[w->pos++] = (uint8_t)(v >> (8u * i));
    }
    return PROTO_TRUE;
}

// Append the tag (field_number << 3) | wire_type, the field number coming off ns->tag.
static proto_bool writer_tag(struct ProtobufInternal *restrict ctx, uint8_t wire_type)
{
    return writer_varint(ctx, ((uint64_t)ctx->ns->tag.field_number << 3) | (uint64_t)(wire_type & 0x07u));
}

// Append a LEN record: the tag, the length prefix varint, then len payload octets.
static proto_bool writer_len(struct ProtobufInternal *restrict ctx, const uint8_t *data, size_t len)
{
    if (!writer_tag(ctx, PROTOCORE_PROTOBUF_WT_LEN) || !writer_varint(ctx, (uint64_t)len))
    {
        return PROTO_FALSE;
    }
    ProtobufWriterRow *w = writer_row(ctx);
    if (!w || w->error)
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

// Decode the Base 128 varint at the row's cursor into *out and advance the cursor past it. False on
// a truncated varint or one that runs past ten octets.
static proto_bool reader_varint(struct ProtobufInternal *restrict ctx, uint64_t *out)
{
    ProtobufReaderRow *r = reader_row(ctx);
    if (!r || !r->buf)
    {
        return PROTO_FALSE;
    }
    uint64_t v = 0;
    size_t shift = 0;
    size_t i = r->pos;
    for (size_t b = 0; b < PROTOCORE_PROTOBUF_VARINT_MAX; b++)
    {
        if (i >= r->len)
        {
            return PROTO_FALSE;
        }
        uint8_t c = r->buf[i++];
        v |= (uint64_t)(c & 0x7Fu) << shift;
        if (!(c & 0x80u))
        {
            *out = v;
            r->pos = i;
            return PROTO_TRUE;
        }
        shift += 7;
    }
    return PROTO_FALSE;
}

// Take n little-endian octets at the row's cursor into *out and advance the cursor past them.
static proto_bool reader_le(struct ProtobufInternal *restrict ctx, size_t n, uint64_t *out)
{
    ProtobufReaderRow *r = reader_row(ctx);
    if (!r || !r->buf || n > r->len - r->pos)
    {
        return PROTO_FALSE;
    }
    uint64_t v = 0;
    for (size_t i = 0; i < n; i++)
    {
        v |= (uint64_t)r->buf[r->pos + i] << (8u * i);
    }
    r->pos += n;
    *out = v;
    return PROTO_TRUE;
}

// Seat the named encoder row on ns->writer and empty it.
static void protobuf_writer_open(struct ProtobufInternal *restrict ctx)
{
    ProtobufWriterRow *w = writer_row(ctx);
    ctx->ns->ok = PROTO_FALSE;
    if (!w)
    {
        return;
    }
    w->buf = ctx->ns->writer.buf;
    w->cap = ctx->ns->writer.cap;
    w->pos = 0;
    w->error = (w->buf == NULL);
    ctx->ns->ok = !w->error;
}

// Append ns->value.u64 as a bare Base 128 varint, no tag.
static void protobuf_write_varint(struct ProtobufInternal *restrict ctx)
{
    ctx->ns->ok = writer_varint(ctx, ctx->ns->value.u64);
}

// Append the tag ns->tag names.
static void protobuf_write_tag(struct ProtobufInternal *restrict ctx)
{
    ctx->ns->ok = writer_tag(ctx, ctx->ns->tag.wire_type);
}

// Append a VARINT record carrying ns->value.u64.
static void protobuf_write_uint64(struct ProtobufInternal *restrict ctx)
{
    ctx->ns->ok = writer_tag(ctx, PROTOCORE_PROTOBUF_WT_VARINT) && writer_varint(ctx, ctx->ns->value.u64);
}

// Append a VARINT record carrying ns->value.i64 in two's complement, ten octets when negative.
static void protobuf_write_int64(struct ProtobufInternal *restrict ctx)
{
    ctx->ns->ok = writer_tag(ctx, PROTOCORE_PROTOBUF_WT_VARINT) && writer_varint(ctx, (uint64_t)ctx->ns->value.i64);
}

// Append a VARINT record carrying ns->value.i64 as ZigZag: (n << 1) ^ (n >> 63).
static void protobuf_write_sint64(struct ProtobufInternal *restrict ctx)
{
    const int64_t v = ctx->ns->value.i64;
    const uint64_t zz = ((uint64_t)v << 1) ^ (uint64_t)(v >> 63);
    ctx->ns->ok = writer_tag(ctx, PROTOCORE_PROTOBUF_WT_VARINT) && writer_varint(ctx, zz);
}

// Append a VARINT record carrying ns->value.flag as 0 or 1.
static void protobuf_write_bool(struct ProtobufInternal *restrict ctx)
{
    ctx->ns->ok = writer_tag(ctx, PROTOCORE_PROTOBUF_WT_VARINT) && writer_varint(ctx, ctx->ns->value.flag ? 1u : 0u);
}

// Append an I32 record carrying ns->value.u32 in four little-endian octets.
static void protobuf_write_fixed32(struct ProtobufInternal *restrict ctx)
{
    ctx->ns->ok = writer_tag(ctx, PROTOCORE_PROTOBUF_WT_I32) && writer_le(ctx, (uint64_t)ctx->ns->value.u32, 4);
}

// Append an I64 record carrying ns->value.u64 in eight little-endian octets.
static void protobuf_write_fixed64(struct ProtobufInternal *restrict ctx)
{
    ctx->ns->ok = writer_tag(ctx, PROTOCORE_PROTOBUF_WT_I64) && writer_le(ctx, ctx->ns->value.u64, 8);
}

// Append an I32 record carrying the four bits-as-octets of ns->value.f32.
static void protobuf_write_float(struct ProtobufInternal *restrict ctx)
{
    uint32_t bits = 0;
    const float v = ctx->ns->value.f32;
    mem.cpy(&bits, &v, 4);
    ctx->ns->ok = writer_tag(ctx, PROTOCORE_PROTOBUF_WT_I32) && writer_le(ctx, (uint64_t)bits, 4);
}

// Append an I64 record carrying the eight bits-as-octets of ns->value.f64.
static void protobuf_write_double(struct ProtobufInternal *restrict ctx)
{
    uint64_t bits = 0;
    const double v = ctx->ns->value.f64;
    mem.cpy(&bits, &v, 8);
    ctx->ns->ok = writer_tag(ctx, PROTOCORE_PROTOBUF_WT_I64) && writer_le(ctx, bits, 8);
}

// Append a LEN record carrying ns->value.data for ns->value.len octets.
static void protobuf_write_bytes(struct ProtobufInternal *restrict ctx)
{
    ctx->ns->ok = writer_len(ctx, ctx->ns->value.data, ctx->ns->value.len);
}

// Append a LEN record carrying ns->value.text up to its NUL, bounded by the row's capacity.
static void protobuf_write_string(struct ProtobufInternal *restrict ctx)
{
    ProtobufWriterRow *w = writer_row(ctx);
    ctx->ns->ok = PROTO_FALSE;
    if (!w)
    {
        return;
    }
    const char *s = ctx->ns->value.text;
    if (!s)
    {
        w->error = PROTO_TRUE;
        return;
    }
    ctx->ns->ok = writer_len(ctx, (const uint8_t *)s, str.len(s, w->cap + 1));
}

// Report the encoded octet count in ns->n, or 0 when any append overflowed.
static void protobuf_writer_finish(struct ProtobufInternal *restrict ctx)
{
    ProtobufWriterRow *w = writer_row(ctx);
    ctx->ns->ok = (w != NULL) && !w->error;
    ctx->ns->n = ctx->ns->ok ? w->pos : 0;
}

// Seat the named decoder row on ns->source, its cursor at ns->source.pos clamped to the length.
static void protobuf_reader_open(struct ProtobufInternal *restrict ctx)
{
    ProtobufReaderRow *r = reader_row(ctx);
    ctx->ns->ok = PROTO_FALSE;
    ctx->ns->n = 0;
    if (!r)
    {
        return;
    }
    r->buf = ctx->ns->source.buf;
    r->len = ctx->ns->source.len;
    r->pos = (ctx->ns->source.pos < ctx->ns->source.len) ? ctx->ns->source.pos : ctx->ns->source.len;
    ctx->ns->n = r->pos;
    ctx->ns->ok = (r->buf != NULL);
}

// Decode the Base 128 varint at the cursor into ns->u64 and report the offset it landed at.
static void protobuf_read_varint(struct ProtobufInternal *restrict ctx)
{
    uint64_t v = 0;
    ctx->ns->ok = reader_varint(ctx, &v);
    ctx->ns->u64 = ctx->ns->ok ? v : 0;
    ProtobufReaderRow *r = reader_row(ctx);
    ctx->ns->n = r ? r->pos : 0;
}

// Decode the record at the cursor into ns->record and leave the cursor past its payload. False at
// end of buffer, on a truncated payload, and on the deprecated group IDs.
static void protobuf_read_record(struct ProtobufInternal *restrict ctx)
{
    ProtobufReaderRow *r = reader_row(ctx);
    ctx->ns->ok = PROTO_FALSE;
    ctx->ns->record.field_number = 0;
    ctx->ns->record.wire_type = 0;
    ctx->ns->record.value = 0;
    ctx->ns->record.data = NULL;
    ctx->ns->record.len = 0;
    ctx->ns->n = r ? r->pos : 0;
    if (!r || !r->buf || r->pos >= r->len)
    {
        return;
    }
    uint64_t tag = 0;
    if (!reader_varint(ctx, &tag))
    {
        ctx->ns->n = r->pos;
        return;
    }
    ctx->ns->record.field_number = (uint32_t)(tag >> 3);
    ctx->ns->record.wire_type = (uint8_t)(tag & 0x07u);

    switch (ctx->ns->record.wire_type)
    {
    case PROTOCORE_PROTOBUF_WT_VARINT: {
        uint64_t v = 0;
        if (reader_varint(ctx, &v))
        {
            ctx->ns->record.value = v;
            ctx->ns->ok = PROTO_TRUE;
        }
        break;
    }
    case PROTOCORE_PROTOBUF_WT_I64: {
        uint64_t v = 0;
        if (reader_le(ctx, 8, &v))
        {
            ctx->ns->record.value = v;
            ctx->ns->ok = PROTO_TRUE;
        }
        break;
    }
    case PROTOCORE_PROTOBUF_WT_I32: {
        uint64_t v = 0;
        if (reader_le(ctx, 4, &v))
        {
            ctx->ns->record.value = v;
            ctx->ns->ok = PROTO_TRUE;
        }
        break;
    }
    case PROTOCORE_PROTOBUF_WT_LEN: {
        uint64_t l = 0;
        if (reader_varint(ctx, &l) && l <= (uint64_t)(r->len - r->pos))
        {
            ctx->ns->record.data = r->buf + r->pos;
            ctx->ns->record.len = (size_t)l;
            r->pos += (size_t)l;
            ctx->ns->ok = PROTO_TRUE;
        }
        break;
    }
    default:
        break; // SGROUP and EGROUP are deprecated; the two remaining IDs name nothing
    }
    ctx->ns->n = r->pos;
}

// Convert the ZigZag varint ns->value.u64 to the sint64 ns->i64: even maps to positive, odd to negative.
static void protobuf_zigzag64(struct ProtobufInternal *restrict ctx)
{
    const uint64_t v = ctx->ns->value.u64;
    ctx->ns->i64 = (int64_t)(v >> 1) ^ -(int64_t)(v & 1u);
    ctx->ns->ok = PROTO_TRUE;
}

// Convert the ZigZag varint ns->value.u32 to the sint32 ns->i32: even maps to positive, odd to negative.
static void protobuf_zigzag32(struct ProtobufInternal *restrict ctx)
{
    const uint32_t v = ctx->ns->value.u32;
    ctx->ns->i32 = (int32_t)(v >> 1) ^ -(int32_t)(v & 1u);
    ctx->ns->ok = PROTO_TRUE;
}

// Read the I32 bit pattern ns->value.u32 as the float ns->f32.
static void protobuf_float_bits(struct ProtobufInternal *restrict ctx)
{
    const uint32_t bits = ctx->ns->value.u32;
    float f = 0.0f;
    mem.cpy(&f, &bits, 4);
    ctx->ns->f32 = f;
    ctx->ns->ok = PROTO_TRUE;
}

// Read the I64 bit pattern ns->value.u64 as the double ns->f64.
static void protobuf_double_bits(struct ProtobufInternal *restrict ctx)
{
    const uint64_t bits = ctx->ns->value.u64;
    double d = 0.0;
    mem.cpy(&d, &bits, 8);
    ctx->ns->f64 = d;
    ctx->ns->ok = PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
ProtobufNs Protobuf = {.writer_open = protobuf_writer_open,
                       .write_varint = protobuf_write_varint,
                       .write_tag = protobuf_write_tag,
                       .write_uint64 = protobuf_write_uint64,
                       .write_int64 = protobuf_write_int64,
                       .write_sint64 = protobuf_write_sint64,
                       .write_bool = protobuf_write_bool,
                       .write_fixed32 = protobuf_write_fixed32,
                       .write_fixed64 = protobuf_write_fixed64,
                       .write_float = protobuf_write_float,
                       .write_double = protobuf_write_double,
                       .write_bytes = protobuf_write_bytes,
                       .write_string = protobuf_write_string,
                       .writer_finish = protobuf_writer_finish,
                       .reader_open = protobuf_reader_open,
                       .read_varint = protobuf_read_varint,
                       .read_record = protobuf_read_record,
                       .zigzag64 = protobuf_zigzag64,
                       .zigzag32 = protobuf_zigzag32,
                       .float_bits = protobuf_float_bits,
                       .double_bits = protobuf_double_bits,
                       .internal = &s_protobuf};

#endif // PROTOCORE_NEED_PROTOBUF
