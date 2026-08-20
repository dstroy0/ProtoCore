// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file lwm2m_tlv.h
 * @brief The OMA LightweightM2M TLV data format (PROTOCORE_ENABLE_LWM2M): a zero-heap writer and
 *        reader for `application/vnd.oma.lwm2m+tlv`.
 *
 * TLV is not an IETF format. It is specified by the Open Mobile Alliance in
 * OMA-TS-LightweightM2M_Core-V1_2-20201110-A sec 7.4.5. The octets travel as a CoAP payload
 * (RFC 7252), tagged by the Content-Format Option (RFC 7252 sec 5.10.3); LwM2M Core sec 7.4
 * Table 7.4.-1 pairs the media type `application/vnd.oma.lwm2m+tlv` with the numeric
 * Content-Format 11542.
 *
 * LwM2M Core sec 7.4.5 Table 7.4.5.-1 lays one entry out as
 * `Type(1) Identifier(1-2) Length(0-3) Value(n)`:
 *  - Type bits 7-6 indicate the type of Identifier: 00 Object Instance, whose Value holds one or
 *    more Resource TLVs; 01 Resource Instance with Value, for use within a multiple Resource TLV;
 *    10 multiple Resource, whose Value holds one or more Resource Instance TLVs; 11 Resource with
 *    Value.
 *  - Type bit 5 indicates the Length of the Identifier: 0 is 8 bits, 1 is 16 bits.
 *  - Type bits 4-3 indicate the type of Length: 00 is no Length field and the Value is the length
 *    bits 2-0 give, 01 an 8-bit Length field, 10 a 16-bit one, 11 a 24-bit one, and in those three
 *    "Bits 2-0 MUST be ignored".
 *  - Type bits 2-0 are a 3-bit unsigned integer holding the Length of the Value.
 *  Identifier and Length are unsigned integers in network byte order, so the maximum Value is
 *  16.7 MB.
 *
 * LwM2M Core Appendix C Table C.-2 gives the Value forms: Integer is a binary signed integer in
 * network byte order and two's complement representation, 1, 2, 4 or 8 octets; Float is binary32
 * or binary64, and this writer emits binary64; Boolean is an 8-bit unsigned integer 0 or 1 whose
 * "Length of a Boolean value MUST always be 1 byte"; String is UTF-8 of Length octets; Opaque is
 * Length octets as they stand.
 *
 * The writer emits entries into a caller buffer and fails closed: the first entry that does not fit
 * poisons the cursor, so a finish reports 0 rather than a truncated payload. The reader is a cursor
 * over a caller buffer that decodes the entry at its head and advances past it, pointing at the
 * Value where it lies rather than copying it.
 *
 * No slot member: the module keeps one writer cursor and one reader cursor, so no call names a row.
 *
 * The module exports one symbol, @ref Lwm2mTlv. Everything in lwm2m_tlv.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_LWM2M_TLV_H
#define PROTOCORE_LWM2M_TLV_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_LWM2M

PROTOCORE_BEGIN_DECLS

// Type byte bit-fields (LwM2M Core sec 7.4.5 Table 7.4.5.-1).
#define LWM2M_TLV_IDTYPE_MASK 0xC0     ///< bits 7-6: the type of Identifier
#define LWM2M_TLV_ID16_FLAG 0x20       ///< bit 5: the Identifier field is 16 bits, else 8
#define LWM2M_TLV_LENTYPE_SHIFT 3      ///< bits 4-3: the type of Length, at this position
#define LWM2M_TLV_LENTYPE_MASK 0x03    ///< its value: 0 none, 1 8-bit, 2 16-bit, 3 24-bit
#define LWM2M_TLV_INLINE_LEN_MASK 0x07 ///< bits 2-0: the Length of the Value when there is no Length field

/** @brief LwM2M Core sec 7.4.5 Table 7.4.5.-1, Type bits 7-6: what the Identifier names. */
typedef enum PROTO_ENUM_PACKED
{
    LWM2M_TLV_OBJECT_INSTANCE = 0x00,     ///< 00: the Value contains one or more Resource TLVs
    LWM2M_TLV_RESOURCE_INSTANCE = 0x40,   ///< 01: Resource Instance with Value, within a multiple Resource TLV
    LWM2M_TLV_MULTIPLE_RESOURCE = 0x80,   ///< 10: the Value contains one or more Resource Instance TLVs
    LWM2M_TLV_RESOURCE_WITH_VALUE = 0xC0, ///< 11: Resource with Value
} Lwm2mTlvIdType;

/** @brief Where a writer's octets land. */
typedef struct
{
    uint8_t *buf; ///< the caller buffer every entry is emitted into
    size_t cap;   ///< how many octets it holds
} Lwm2mTlvSinkArgs;

/** @brief The octets a reader walks. */
typedef struct
{
    const uint8_t *buf; ///< the TLV array a read decodes, left where it lies
    size_t len;         ///< how many octets of it are readable
} Lwm2mTlvSourceArgs;

/** @brief The Type and Identifier fields of one entry (LwM2M Core sec 7.4.5 Table 7.4.5.-1). */
typedef struct
{
    Lwm2mTlvIdType id_type; ///< Type bits 7-6: the type of Identifier
    uint16_t id;            ///< the Identifier field: the Object Instance, Resource or Resource Instance ID
} Lwm2mTlvHeaderArgs;

/** @brief The Value field of one entry, in the forms LwM2M Core Appendix C Table C.-2 gives. */
typedef struct
{
    const uint8_t *opaque;    ///< Opaque: the Value octets as they stand, and where a read points
    size_t len;               ///< the Length field: how many octets the Value is
    int64_t integer_value;    ///< Integer: staged into 1, 2, 4 or 8 octets, two's complement
    double float_value;       ///< Float: staged as binary64, 8 octets
    proto_bool boolean_value; ///< Boolean: staged as one octet, 0 for False and 1 for True
    const char *string_value; ///< String: UTF-8, measured to its NUL within the sink's capacity
} Lwm2mTlvValueArgs;

/**
 * @brief The OMA LwM2M TLV codec.
 *
 * A caller sets the members a call takes, invokes it through ::Lwm2mTlv, and reads the outcome off
 * the same handle. @c hdr and @c val are what a write emits and what a read fills, so an entry
 * decoded from one buffer is re-emitted into another with no field moved by hand.
 *
 * @var Lwm2mTlvNs::sink           the buffer a writer emits into, taken by an open
 * @var Lwm2mTlvNs::source         the buffer a reader walks, taken by a parse
 * @var Lwm2mTlvNs::hdr            the Type and Identifier fields: set for a write, filled by a next
 * @var Lwm2mTlvNs::val            the Value field: set for a write, filled by a next
 * @var Lwm2mTlvNs::ok             a call's true/false outcome
 * @var Lwm2mTlvNs::n              the octets a finish counts, 0 if any write did not fit
 * @var Lwm2mTlvNs::open           bind @c sink and clear the writer cursor
 * @var Lwm2mTlvNs::write          emit one entry carrying @c val.opaque for @c val.len octets
 * @var Lwm2mTlvNs::write_integer  stage @c val.integer_value as 1/2/4/8 octets and emit it
 * @var Lwm2mTlvNs::write_boolean  stage @c val.boolean_value as one octet and emit it
 * @var Lwm2mTlvNs::write_string   measure @c val.string_value and emit its UTF-8 octets
 * @var Lwm2mTlvNs::write_float    stage @c val.float_value as binary64 and emit it
 * @var Lwm2mTlvNs::finish         count the octets emitted into @c n
 * @var Lwm2mTlvNs::parse          bind @c source and clear the reader cursor
 * @var Lwm2mTlvNs::next           decode the entry at the cursor into @c hdr and @c val, and advance past it
 * @var Lwm2mTlvNs::value_integer  decode @c val.opaque for @c val.len octets into @c val.integer_value
 */
typedef struct
{
    Lwm2mTlvSinkArgs sink;     ///< where a writer's octets land
    Lwm2mTlvSourceArgs source; ///< the octets a reader walks
    Lwm2mTlvHeaderArgs hdr;    ///< one entry's Type and Identifier fields
    Lwm2mTlvValueArgs val;     ///< its Value field
    proto_bool ok;
    size_t n;
} Lwm2mTlvVars;

/** @brief The operands and the outcome. */
extern Lwm2mTlvVars Lwm2mTlvV;

/** @brief The entries. */
typedef struct
{
    void (*const open)(uint8_t *restrict work);
    void (*const write)(uint8_t *restrict work);
    void (*const write_integer)(uint8_t *restrict work);
    void (*const write_boolean)(uint8_t *restrict work);
    void (*const write_string)(uint8_t *restrict work);
    void (*const write_float)(uint8_t *restrict work);
    void (*const finish)(uint8_t *restrict work);
    void (*const parse)(uint8_t *restrict work);
    void (*const next)(uint8_t *restrict work);
    void (*const value_integer)(uint8_t *restrict work);
} Lwm2mTlvNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Lwm2mTlvV or a region of the borrow at a fixed offset.
void protocore_lwm2m_tlv_open(uint8_t *restrict work);
void protocore_lwm2m_tlv_write(uint8_t *restrict work);
void protocore_lwm2m_tlv_write_integer(uint8_t *restrict work);
void protocore_lwm2m_tlv_write_boolean(uint8_t *restrict work);
void protocore_lwm2m_tlv_write_string(uint8_t *restrict work);
void protocore_lwm2m_tlv_write_float(uint8_t *restrict work);
void protocore_lwm2m_tlv_finish(uint8_t *restrict work);
void protocore_lwm2m_tlv_parse(uint8_t *restrict work);
void protocore_lwm2m_tlv_next(uint8_t *restrict work);
void protocore_lwm2m_tlv_value_integer(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Lwm2mTlv.open(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Lwm2mTlvNs Lwm2mTlv __attribute__((unused)) = {
    .open = protocore_lwm2m_tlv_open,
    .write = protocore_lwm2m_tlv_write,
    .write_integer = protocore_lwm2m_tlv_write_integer,
    .write_boolean = protocore_lwm2m_tlv_write_boolean,
    .write_string = protocore_lwm2m_tlv_write_string,
    .write_float = protocore_lwm2m_tlv_write_float,
    .finish = protocore_lwm2m_tlv_finish,
    .parse = protocore_lwm2m_tlv_parse,
    .next = protocore_lwm2m_tlv_next,
    .value_integer = protocore_lwm2m_tlv_value_integer,
};

/**
 * @brief The PROTOCORE_LWM2M_TLV_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_lwm2m_tlv_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_LWM2M

#endif // PROTOCORE_LWM2M_TLV_H
