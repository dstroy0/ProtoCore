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
#include "mmgr/plaintext.h" // the persistent end this module's state is taken from

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

// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define PROTOBUF_OFF_CTX 0u
static_assert(PROTOBUF_OFF_CTX + sizeof(struct ProtobufStorage) <= PROTOCORE_PROTOBUF_BORROW,
              "PROTOCORE_PROTOBUF_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define PROTOBUF_CTX(w) ((struct ProtobufStorage *)(void *)((w) + PROTOBUF_OFF_CTX))

// The encoder row ns->slot names, or NULL when the slot is past the pool.
static ProtobufWriterRow *writer_row(uint8_t *restrict work)
{
    if (Protobuf.slot >= PROTOCORE_PROTOBUF_SLOTS)
    {
        return NULL;
    }
    return &PROTOBUF_CTX(work)->writers[Protobuf.slot];
}

// The decoder row ns->slot names, or NULL when the slot is past the pool.
static ProtobufReaderRow *reader_row(uint8_t *restrict work)
{
    if (Protobuf.slot >= PROTOCORE_PROTOBUF_SLOTS)
    {
        return NULL;
    }
    return &PROTOBUF_CTX(work)->readers[Protobuf.slot];
}

// Append v as a Base 128 varint: seven payload bits per octet, little-endian, the continuation bit
// (MSB) set on every octet but the last. Sets the row's sticky error when it does not fit.
static proto_bool writer_varint(uint8_t *restrict work, uint64_t v)
{
    ProtobufWriterRow *w = writer_row(work);
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
static proto_bool writer_le(uint8_t *restrict work, uint64_t v, size_t n)
{
    ProtobufWriterRow *w = writer_row(work);
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
static proto_bool writer_tag(uint8_t *restrict work, uint8_t wire_type)
{
    return writer_varint(work, ((uint64_t)Protobuf.tag.field_number << 3) | (uint64_t)(wire_type & 0x07u));
}

// Append a LEN record: the tag, the length prefix varint, then len payload octets.
static proto_bool writer_len(uint8_t *restrict work, const uint8_t *data, size_t len)
{
    if (!writer_tag(work, PROTOCORE_PROTOBUF_WT_LEN) || !writer_varint(work, (uint64_t)len))
    {
        return PROTO_FALSE;
    }
    ProtobufWriterRow *w = writer_row(work);
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
static proto_bool reader_varint(uint8_t *restrict work, uint64_t *out)
{
    ProtobufReaderRow *r = reader_row(work);
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
static proto_bool reader_le(uint8_t *restrict work, size_t n, uint64_t *out)
{
    ProtobufReaderRow *r = reader_row(work);
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

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_PROTOBUF_BORROW persistent bytes, or null while the pool was short
} ProtobufOwnCtx;
static ProtobufOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_protobuf_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_plaintext_persist_span(PROTOCORE_PROTOBUF_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

// Seat the named encoder row on ns->writer and empty it.
static void protobuf_writer_open(uint8_t *restrict work)
{
    ProtobufWriterRow *w = writer_row(work);
    Protobuf.ok = PROTO_FALSE;
    if (!w)
    {
        return;
    }
    w->buf = Protobuf.writer.buf;
    w->cap = Protobuf.writer.cap;
    w->pos = 0;
    w->error = (w->buf == NULL);
    Protobuf.ok = !w->error;
}

// Append ns->value.u64 as a bare Base 128 varint, no tag.
static void protobuf_write_varint(uint8_t *restrict work)
{
    Protobuf.ok = writer_varint(work, Protobuf.value.u64);
}

// Append the tag ns->tag names.
static void protobuf_write_tag(uint8_t *restrict work)
{
    Protobuf.ok = writer_tag(work, Protobuf.tag.wire_type);
}

// Append a VARINT record carrying ns->value.u64.
static void protobuf_write_uint64(uint8_t *restrict work)
{
    Protobuf.ok = writer_tag(work, PROTOCORE_PROTOBUF_WT_VARINT) && writer_varint(work, Protobuf.value.u64);
}

// Append a VARINT record carrying ns->value.i64 in two's complement, ten octets when negative.
static void protobuf_write_int64(uint8_t *restrict work)
{
    Protobuf.ok = writer_tag(work, PROTOCORE_PROTOBUF_WT_VARINT) && writer_varint(work, (uint64_t)Protobuf.value.i64);
}

// Append a VARINT record carrying ns->value.i64 as ZigZag: (n << 1) ^ (n >> 63).
static void protobuf_write_sint64(uint8_t *restrict work)
{
    const int64_t v = Protobuf.value.i64;
    const uint64_t zz = ((uint64_t)v << 1) ^ (uint64_t)(v >> 63);
    Protobuf.ok = writer_tag(work, PROTOCORE_PROTOBUF_WT_VARINT) && writer_varint(work, zz);
}

// Append a VARINT record carrying ns->value.flag as 0 or 1.
static void protobuf_write_bool(uint8_t *restrict work)
{
    Protobuf.ok = writer_tag(work, PROTOCORE_PROTOBUF_WT_VARINT) && writer_varint(work, Protobuf.value.flag ? 1u : 0u);
}

// Append an I32 record carrying ns->value.u32 in four little-endian octets.
static void protobuf_write_fixed32(uint8_t *restrict work)
{
    Protobuf.ok = writer_tag(work, PROTOCORE_PROTOBUF_WT_I32) && writer_le(work, (uint64_t)Protobuf.value.u32, 4);
}

// Append an I64 record carrying ns->value.u64 in eight little-endian octets.
static void protobuf_write_fixed64(uint8_t *restrict work)
{
    Protobuf.ok = writer_tag(work, PROTOCORE_PROTOBUF_WT_I64) && writer_le(work, Protobuf.value.u64, 8);
}

// Append an I32 record carrying the four bits-as-octets of ns->value.f32.
static void protobuf_write_float(uint8_t *restrict work)
{
    uint32_t bits = 0;
    const float v = Protobuf.value.f32;
    mem.cpy(&bits, &v, 4);
    Protobuf.ok = writer_tag(work, PROTOCORE_PROTOBUF_WT_I32) && writer_le(work, (uint64_t)bits, 4);
}

// Append an I64 record carrying the eight bits-as-octets of ns->value.f64.
static void protobuf_write_double(uint8_t *restrict work)
{
    uint64_t bits = 0;
    const double v = Protobuf.value.f64;
    mem.cpy(&bits, &v, 8);
    Protobuf.ok = writer_tag(work, PROTOCORE_PROTOBUF_WT_I64) && writer_le(work, bits, 8);
}

// Append a LEN record carrying ns->value.data for ns->value.len octets.
static void protobuf_write_bytes(uint8_t *restrict work)
{
    Protobuf.ok = writer_len(work, Protobuf.value.data, Protobuf.value.len);
}

// Append a LEN record carrying ns->value.text up to its NUL, bounded by the row's capacity.
static void protobuf_write_string(uint8_t *restrict work)
{
    ProtobufWriterRow *w = writer_row(work);
    Protobuf.ok = PROTO_FALSE;
    if (!w)
    {
        return;
    }
    const char *s = Protobuf.value.text;
    if (!s)
    {
        w->error = PROTO_TRUE;
        return;
    }
    Protobuf.ok = writer_len(work, (const uint8_t *)s, str.len(s, w->cap + 1));
}

// Report the encoded octet count in ns->n, or 0 when any append overflowed.
static void protobuf_writer_finish(uint8_t *restrict work)
{
    ProtobufWriterRow *w = writer_row(work);
    Protobuf.ok = (w != NULL) && !w->error;
    Protobuf.n = Protobuf.ok ? w->pos : 0;
}

// Seat the named decoder row on ns->source, its cursor at ns->source.pos clamped to the length.
static void protobuf_reader_open(uint8_t *restrict work)
{
    ProtobufReaderRow *r = reader_row(work);
    Protobuf.ok = PROTO_FALSE;
    Protobuf.n = 0;
    if (!r)
    {
        return;
    }
    r->buf = Protobuf.source.buf;
    r->len = Protobuf.source.len;
    r->pos = (Protobuf.source.pos < Protobuf.source.len) ? Protobuf.source.pos : Protobuf.source.len;
    Protobuf.n = r->pos;
    Protobuf.ok = (r->buf != NULL);
}

// Decode the Base 128 varint at the cursor into ns->u64 and report the offset it landed at.
static void protobuf_read_varint(uint8_t *restrict work)
{
    uint64_t v = 0;
    Protobuf.ok = reader_varint(work, &v);
    Protobuf.u64 = Protobuf.ok ? v : 0;
    ProtobufReaderRow *r = reader_row(work);
    Protobuf.n = r ? r->pos : 0;
}

// Decode the record at the cursor into ns->record and leave the cursor past its payload. False at
// end of buffer, on a truncated payload, and on the deprecated group IDs.
static void protobuf_read_record(uint8_t *restrict work)
{
    ProtobufReaderRow *r = reader_row(work);
    Protobuf.ok = PROTO_FALSE;
    Protobuf.record.field_number = 0;
    Protobuf.record.wire_type = 0;
    Protobuf.record.value = 0;
    Protobuf.record.data = NULL;
    Protobuf.record.len = 0;
    Protobuf.n = r ? r->pos : 0;
    if (!r || !r->buf || r->pos >= r->len)
    {
        return;
    }
    uint64_t tag = 0;
    if (!reader_varint(work, &tag))
    {
        Protobuf.n = r->pos;
        return;
    }
    Protobuf.record.field_number = (uint32_t)(tag >> 3);
    Protobuf.record.wire_type = (uint8_t)(tag & 0x07u);

    switch (Protobuf.record.wire_type)
    {
    case PROTOCORE_PROTOBUF_WT_VARINT: {
        uint64_t v = 0;
        if (reader_varint(work, &v))
        {
            Protobuf.record.value = v;
            Protobuf.ok = PROTO_TRUE;
        }
        break;
    }
    case PROTOCORE_PROTOBUF_WT_I64: {
        uint64_t v = 0;
        if (reader_le(work, 8, &v))
        {
            Protobuf.record.value = v;
            Protobuf.ok = PROTO_TRUE;
        }
        break;
    }
    case PROTOCORE_PROTOBUF_WT_I32: {
        uint64_t v = 0;
        if (reader_le(work, 4, &v))
        {
            Protobuf.record.value = v;
            Protobuf.ok = PROTO_TRUE;
        }
        break;
    }
    case PROTOCORE_PROTOBUF_WT_LEN: {
        uint64_t l = 0;
        if (reader_varint(work, &l) && l <= (uint64_t)(r->len - r->pos))
        {
            Protobuf.record.data = r->buf + r->pos;
            Protobuf.record.len = (size_t)l;
            r->pos += (size_t)l;
            Protobuf.ok = PROTO_TRUE;
        }
        break;
    }
    default:
        break; // SGROUP and EGROUP are deprecated; the two remaining IDs name nothing
    }
    Protobuf.n = r->pos;
}

// Convert the ZigZag varint ns->value.u64 to the sint64 ns->i64: even maps to positive, odd to negative.
static void protobuf_zigzag64(uint8_t *restrict work)
{
    (void)work;
    const uint64_t v = Protobuf.value.u64;
    Protobuf.i64 = (int64_t)(v >> 1) ^ -(int64_t)(v & 1u);
    Protobuf.ok = PROTO_TRUE;
}

// Convert the ZigZag varint ns->value.u32 to the sint32 ns->i32: even maps to positive, odd to negative.
static void protobuf_zigzag32(uint8_t *restrict work)
{
    (void)work;
    const uint32_t v = Protobuf.value.u32;
    Protobuf.i32 = (int32_t)(v >> 1) ^ -(int32_t)(v & 1u);
    Protobuf.ok = PROTO_TRUE;
}

// Read the I32 bit pattern ns->value.u32 as the float ns->f32.
static void protobuf_float_bits(uint8_t *restrict work)
{
    (void)work;
    const uint32_t bits = Protobuf.value.u32;
    float f = 0.0f;
    mem.cpy(&f, &bits, 4);
    Protobuf.f32 = f;
    Protobuf.ok = PROTO_TRUE;
}

// Read the I64 bit pattern ns->value.u64 as the double ns->f64.
static void protobuf_double_bits(uint8_t *restrict work)
{
    (void)work;
    const uint64_t bits = Protobuf.value.u64;
    double d = 0.0;
    mem.cpy(&d, &bits, 8);
    Protobuf.f64 = d;
    Protobuf.ok = PROTO_TRUE;
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
                       .double_bits = protobuf_double_bits};

#endif // PROTOCORE_NEED_PROTOBUF
