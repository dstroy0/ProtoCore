// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protobuf.h
 * @brief The Protocol Buffers wire format (PROTOCORE_ENABLE_PROTOBUF): a streaming encoder and a
 *        cursor decoder over caller buffers, zero heap.
 *
 * The governing specification is Google's Protocol Buffers "Encoding" document
 * (https://protobuf.dev/programming-guides/encoding/). It is not an IETF document and no RFC
 * number applies to it. Every normative term below is that document's.
 *
 * "Message Structure": a message is a sequence of records, and each record is "the field number, a
 * wire type and a payload". The tag "is encoded as a varint formed from the field number and the
 * wire type via the formula `(field_number << 3) | wire_type`".
 *
 * "Base 128 Varints": "Each byte in the varint has a continuation bit that indicates if the byte
 * that follows it is part of the varint. This is the most significant bit (MSB) of the byte." The
 * lower seven bits are payload, appended in little-endian order, and an unsigned 64-bit value takes
 * "anywhere between one and ten bytes".
 *
 * The wire types, from the same document's table:
 *
 *     ID  Name    Used For
 *     0   VARINT  int32, int64, uint32, uint64, sint32, sint64, bool, enum
 *     1   I64     fixed64, sfixed64, double
 *     2   LEN     string, bytes, embedded messages, packed repeated fields
 *     3   SGROUP  group start (deprecated)
 *     4   EGROUP  group end (deprecated)
 *     5   I32     fixed32, sfixed32, float
 *
 * "Length-Delimited Records": "The LEN wire type has a dynamic length, specified by a varint
 * immediately after the tag, which is followed by the payload as usual."
 *
 * "Groups": "Groups are a deprecated feature that should not be used." SGROUP and EGROUP records
 * are rejected by the decoder here, as are the two IDs the table does not name.
 *
 * sint32 and sint64 carry ZigZag: `(n << 1) ^ (n >> 31)` and `(n << 1) ^ (n >> 63)`.
 *
 * An encoder row appends one record at a time into a caller buffer and fails closed on overflow;
 * an embedded message is encoded into a second row's buffer and added with @ref ProtobufNs::write_bytes.
 * A decoder row is a cursor: it decodes the record at its own offset and reports where that offset
 * landed. Rows nest, which is what an embedded message needs, so @ref ProtobufNs::slot names one.
 *
 * The module exports one symbol, @ref Protobuf. Everything in protobuf.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PROTOBUF_H
#define PROTOCORE_PROTOBUF_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_NEED_PROTOBUF

// Wire type IDs, from the "Encoding" document's wire type table.
#define PROTOCORE_PROTOBUF_WT_VARINT 0 ///< int32, int64, uint32, uint64, sint32, sint64, bool, enum
#define PROTOCORE_PROTOBUF_WT_I64 1    ///< fixed64, sfixed64, double
#define PROTOCORE_PROTOBUF_WT_LEN 2    ///< string, bytes, embedded messages, packed repeated fields
#define PROTOCORE_PROTOBUF_WT_SGROUP 3 ///< group start, deprecated, rejected by a decode
#define PROTOCORE_PROTOBUF_WT_EGROUP 4 ///< group end, deprecated, rejected by a decode
#define PROTOCORE_PROTOBUF_WT_I32 5    ///< fixed32, sfixed32, float

/** @brief One varint holds at most ten octets, the width an unsigned 64-bit value reaches. */
#define PROTOCORE_PROTOBUF_VARINT_MAX 10

#ifndef PROTOCORE_PROTOBUF_SLOTS
/** @brief Encoder rows and decoder rows, each. An embedded message holds a second row open. */
#define PROTOCORE_PROTOBUF_SLOTS 4
#endif

/** @brief The caller buffer one encoder row appends into. */
typedef struct
{
    uint8_t *buf; ///< the octets an encode writes over
    size_t cap;   ///< how many of them there are
} ProtobufWriterArgs;
/** @brief The tag an encode stamps: `(field_number << 3) | wire_type`. */
typedef struct
{
    uint32_t field_number; ///< the record's field number, the tag's high bits
    uint8_t wire_type;     ///< the record's wire type, the tag's low three bits
} ProtobufTagArgs;
/** @brief The payload a record carries, one member per wire type the encoders and decoders take. */
typedef struct
{
    uint64_t u64;        ///< VARINT payload, I64 bits, or the ZigZag source a decode converts
    int64_t i64;         ///< signed VARINT payload: two's complement for int64, the ZigZag source for sint64
    uint32_t u32;        ///< I32 bits: a fixed32 payload, or the bits a decode converts
    float f32;           ///< float payload, encoded as I32
    double f64;          ///< double payload, encoded as I64
    proto_bool flag;     ///< bool payload, encoded as the VARINT 0 or 1
    const uint8_t *data; ///< LEN payload octets
    size_t len;          ///< how many of them there are
    const char *text;    ///< NUL-terminated LEN payload, bounded by the row's capacity
} ProtobufValueArgs;
/** @brief The encoded octets one decoder row walks. */
typedef struct
{
    const uint8_t *buf; ///< the message octets
    size_t len;         ///< how many are buffered
    size_t pos;         ///< the offset an open seats the cursor at, clamped to @c len
} ProtobufSourceArgs;
/** @brief One decoded record. For LEN, @c data points INTO the source octets and nothing is copied. */
typedef struct
{
    uint32_t field_number; ///< the tag's high bits
    uint8_t wire_type;     ///< the tag's low three bits
    uint64_t value;        ///< the VARINT payload, or the raw little-endian bits of an I32 / I64 payload
    const uint8_t *data;   ///< the LEN payload
    size_t len;            ///< the length prefix that preceded it
} ProtobufRecord;
/**
 * @brief The Protocol Buffers wire format: the record encoder and the record decoder.
 *
 * A caller sets the members a call takes, invokes it through ::Protobuf, and reads the outcome off
 * the same handle.
 *
 * @var ProtobufNs::slot          the row a call acts on; a @c write_ names an encoder row, a @c read_ a decoder row
 * @var ProtobufNs::writer        the caller buffer an encoder row appends into
 * @var ProtobufNs::tag           the field number and wire type a tag carries
 * @var ProtobufNs::value         the payload one record carries
 * @var ProtobufNs::source        the encoded octets a decoder row walks
 * @var ProtobufNs::ok            a call's true/false outcome
 * @var ProtobufNs::n             the encoded octet count a finish reports, or the offset a decode left its cursor at
 * @var ProtobufNs::u64           the varint a read decoded
 * @var ProtobufNs::i64           the sint64 a ZigZag decode produced
 * @var ProtobufNs::i32           the sint32 a ZigZag decode produced
 * @var ProtobufNs::f32           the float an I32 bit pattern names
 * @var ProtobufNs::f64           the double an I64 bit pattern names
 * @var ProtobufNs::record        the record a read decoded
 * @var ProtobufNs::writer_open   seat an encoder row on @c writer and empty it
 * @var ProtobufNs::write_varint  append @c value.u64 as a Base 128 varint, no tag
 * @var ProtobufNs::write_tag     append the tag `(tag.field_number << 3) | tag.wire_type`
 * @var ProtobufNs::write_uint64  append a VARINT record carrying @c value.u64
 * @var ProtobufNs::write_int64   append a VARINT record carrying @c value.i64 in two's complement
 * @var ProtobufNs::write_sint64  append a VARINT record carrying @c value.i64 in ZigZag
 * @var ProtobufNs::write_bool    append a VARINT record carrying @c value.flag as 0 or 1
 * @var ProtobufNs::write_fixed32 append an I32 record carrying @c value.u32
 * @var ProtobufNs::write_fixed64 append an I64 record carrying @c value.u64
 * @var ProtobufNs::write_float   append an I32 record carrying the bits of @c value.f32
 * @var ProtobufNs::write_double  append an I64 record carrying the bits of @c value.f64
 * @var ProtobufNs::write_bytes   append a LEN record carrying @c value.data for @c value.len
 * @var ProtobufNs::write_string  append a LEN record carrying @c value.text up to its NUL
 * @var ProtobufNs::writer_finish report the encoded octet count in @c n, or 0 if any append overflowed
 * @var ProtobufNs::reader_open   seat a decoder row on @c source at @c source.pos
 * @var ProtobufNs::read_varint   decode the Base 128 varint at the cursor into @c u64 and advance it
 * @var ProtobufNs::read_record   decode the record at the cursor into @c record and advance past it
 * @var ProtobufNs::zigzag64      convert the ZigZag varint @c value.u64 to the sint64 @c i64
 * @var ProtobufNs::zigzag32      convert the ZigZag varint @c value.u32 to the sint32 @c i32
 * @var ProtobufNs::float_bits    convert the I32 bit pattern @c value.u32 to the float @c f32
 * @var ProtobufNs::double_bits   convert the I64 bit pattern @c value.u64 to the double @c f64
 */
typedef struct
{
    uint8_t slot;              ///< the encoder or decoder row every call names
    ProtobufWriterArgs writer; ///< where an encode lands
    ProtobufTagArgs tag;       ///< what a tag says
    ProtobufValueArgs value;   ///< what a payload carries
    ProtobufSourceArgs source; ///< what a decode walks
    proto_bool ok;
    size_t n;
    uint64_t u64;
    int64_t i64;
    int32_t i32;
    float f32;
    double f64;
    ProtobufRecord record;
} ProtobufVars;

/** @brief The operands and the outcome. */
extern ProtobufVars ProtobufV;

/** @brief The entries. */
typedef struct
{
    void (*const writer_open)(uint8_t *restrict work);
    void (*const write_varint)(uint8_t *restrict work);
    void (*const write_tag)(uint8_t *restrict work);
    void (*const write_uint64)(uint8_t *restrict work);
    void (*const write_int64)(uint8_t *restrict work);
    void (*const write_sint64)(uint8_t *restrict work);
    void (*const write_bool)(uint8_t *restrict work);
    void (*const write_fixed32)(uint8_t *restrict work);
    void (*const write_fixed64)(uint8_t *restrict work);
    void (*const write_float)(uint8_t *restrict work);
    void (*const write_double)(uint8_t *restrict work);
    void (*const write_bytes)(uint8_t *restrict work);
    void (*const write_string)(uint8_t *restrict work);
    void (*const writer_finish)(uint8_t *restrict work);
    void (*const reader_open)(uint8_t *restrict work);
    void (*const read_varint)(uint8_t *restrict work);
    void (*const read_record)(uint8_t *restrict work);
    void (*const zigzag64)(uint8_t *restrict work);
    void (*const zigzag32)(uint8_t *restrict work);
    void (*const float_bits)(uint8_t *restrict work);
    void (*const double_bits)(uint8_t *restrict work);
} ProtobufNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in ProtobufV or a region of the borrow at a fixed offset.
void protocore_protobuf_writer_open(uint8_t *restrict work);
void protocore_protobuf_write_varint(uint8_t *restrict work);
void protocore_protobuf_write_tag(uint8_t *restrict work);
void protocore_protobuf_write_uint64(uint8_t *restrict work);
void protocore_protobuf_write_int64(uint8_t *restrict work);
void protocore_protobuf_write_sint64(uint8_t *restrict work);
void protocore_protobuf_write_bool(uint8_t *restrict work);
void protocore_protobuf_write_fixed32(uint8_t *restrict work);
void protocore_protobuf_write_fixed64(uint8_t *restrict work);
void protocore_protobuf_write_float(uint8_t *restrict work);
void protocore_protobuf_write_double(uint8_t *restrict work);
void protocore_protobuf_write_bytes(uint8_t *restrict work);
void protocore_protobuf_write_string(uint8_t *restrict work);
void protocore_protobuf_writer_finish(uint8_t *restrict work);
void protocore_protobuf_reader_open(uint8_t *restrict work);
void protocore_protobuf_read_varint(uint8_t *restrict work);
void protocore_protobuf_read_record(uint8_t *restrict work);
void protocore_protobuf_zigzag64(uint8_t *restrict work);
void protocore_protobuf_zigzag32(uint8_t *restrict work);
void protocore_protobuf_float_bits(uint8_t *restrict work);
void protocore_protobuf_double_bits(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Protobuf.writer_open(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const ProtobufNs Protobuf __attribute__((unused)) = {
    .writer_open = protocore_protobuf_writer_open,
    .write_varint = protocore_protobuf_write_varint,
    .write_tag = protocore_protobuf_write_tag,
    .write_uint64 = protocore_protobuf_write_uint64,
    .write_int64 = protocore_protobuf_write_int64,
    .write_sint64 = protocore_protobuf_write_sint64,
    .write_bool = protocore_protobuf_write_bool,
    .write_fixed32 = protocore_protobuf_write_fixed32,
    .write_fixed64 = protocore_protobuf_write_fixed64,
    .write_float = protocore_protobuf_write_float,
    .write_double = protocore_protobuf_write_double,
    .write_bytes = protocore_protobuf_write_bytes,
    .write_string = protocore_protobuf_write_string,
    .writer_finish = protocore_protobuf_writer_finish,
    .reader_open = protocore_protobuf_reader_open,
    .read_varint = protocore_protobuf_read_varint,
    .read_record = protocore_protobuf_read_record,
    .zigzag64 = protocore_protobuf_zigzag64,
    .zigzag32 = protocore_protobuf_zigzag32,
    .float_bits = protocore_protobuf_float_bits,
    .double_bits = protocore_protobuf_double_bits,
};

/**
 * @brief The PROTOCORE_PROTOBUF_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_protobuf_span(void);

#endif // PROTOCORE_NEED_PROTOBUF

PROTOCORE_END_DECLS

#endif // PROTOCORE_PROTOBUF_H
