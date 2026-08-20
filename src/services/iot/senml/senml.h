// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file senml.h
 * @brief Sensor Measurement Lists (SenML, RFC 8428): the Pack builders and the Record resolver.
 *
 * RFC 8428 sec 3 names the units: a SenML Record is "one measurement or configuration instance in
 * time presented using the SenML data model", and a SenML Pack is "one or more SenML Records in an
 * array structure". This module builds a Pack from a caller-owned Record array and resolves a Pack
 * in place of a consumer.
 *
 * A Record carries Base Fields (RFC 8428 sec 4.1) that apply to it and the Records after it, and
 * Regular Fields (sec 4.2) that apply to it alone. RFC 8428 sec 5.1.1:
 * @code
 *   [
 *     {"n":"urn:dev:ow:10e2073a01080063","u":"Cel","v":23.1}
 *   ]
 * @endcode
 *
 * The JSON representation (sec 5, application/senml+json) writes the labels as member names. The
 * CBOR representation (sec 6, application/senml+cbor) writes the Table 4 integer map keys instead;
 * that table is conclusive, so one walk feeds every binary encoding through @ref protocore_codec and
 * the encoding is an argument. A Number that is integral is emitted as an integer, so a Time keeps
 * full precision; otherwise it is emitted as a floating-point value.
 *
 * Both builders write into a caller buffer and report 0 bytes when it will not hold the whole Pack.
 * The resolver (sec 4.6) folds the Base Name and Base Time into each Record, so each output Record
 * carries a full Name and an absolute Time and stands alone.
 *
 * The module exports one symbol, @ref Senml. Everything in senml.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SENML_H
#define PROTOCORE_SENML_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_SENML

#include "network_drivers/presentation/codec/codec.h" // protocore_codec: the binary encoding is an argument

PROTOCORE_BEGIN_DECLS

/** @brief Longest resolved Name (Base Name concatenated with Name), the NUL included. */
#define PROTOCORE_SENML_RESOLVED_NAME_MAX 96

/** @brief Which value field a Record carries (RFC 8428 sec 4.2). */
typedef enum PROTO_ENUM_PACKED
{
    SENML_VALUE_NONE,    ///< no value field, as in a Base-Fields-only Record
    SENML_VALUE_NUMBER,  ///< Value (v), a Number; emitted as an integer when integral
    SENML_VALUE_STRING,  ///< String Value (vs)
    SENML_VALUE_BOOLEAN, ///< Boolean Value (vb)
} SenmlValueKind;

/**
 * @brief One SenML Record (RFC 8428 sec 3): its Base Fields (sec 4.1) and Regular Fields (sec 4.2).
 *
 * Every string is borrowed, not copied, and a null one leaves its label out of the Pack.
 */
typedef struct
{
    const char *base_name;     ///< Base Name (bn), prepended to the Names that follow
    proto_bool has_base_time;  ///< a Base Time is present
    double base_time;          ///< Base Time (bt), added to the Times that follow
    const char *name;          ///< Name (n)
    const char *unit;          ///< Unit (u)
    SenmlValueKind value_kind; ///< which of the three value fields below this Record carries
    double value;              ///< Value (v)
    const char *string_value;  ///< String Value (vs)
    proto_bool boolean_value;  ///< Boolean Value (vb)
    proto_bool has_time;       ///< a Time is present
    double time;               ///< Time (t)
} SenmlRecord;

/**
 * @brief One resolved SenML Record (RFC 8428 sec 4.6): no Base Fields left and no relative Time.
 */
typedef struct
{
    char name[PROTOCORE_SENML_RESOLVED_NAME_MAX]; ///< Name (n): the Base Name concatenated with the Name
    const char *unit;                             ///< Unit (u), borrowed from the input Record
    SenmlValueKind value_kind;                    ///< which value field this Record carries
    double value;                                 ///< Value (v)
    const char *string_value;                     ///< String Value (vs)
    proto_bool boolean_value;                     ///< Boolean Value (vb)
    proto_bool has_time;                          ///< a Time is present
    double time;                                  ///< Time (t): the Base Time added to the Time
} SenmlResolved;

/** @brief RFC 8428 sec 3: the Pack a call reads, one array of Records. */
typedef struct
{
    const SenmlRecord *records; ///< the Records the array holds
    size_t count;               ///< how many of them
} SenmlPackArgs;

/** @brief RFC 8428 sec 5: where the JSON representation (application/senml+json) lands. */
typedef struct
{
    char *buf;  ///< the buffer the text Pack is written into
    size_t cap; ///< how much room it has, the NUL included
} SenmlJsonArgs;

/** @brief RFC 8428 sec 6: the binary encoding a Pack takes, and where it lands. */
typedef struct
{
    const protocore_codec *codec; ///< the encoding the Table 4 integer labels are written through
    uint8_t *buf;                 ///< the buffer the encoded Pack is written into
    size_t cap;                   ///< how much room it has
} SenmlBinaryArgs;

/** @brief RFC 8428 sec 4.6: where the resolved Records land. */
typedef struct
{
    SenmlResolved *out; ///< the array a resolve fills
    size_t max;         ///< how many Records that array holds
} SenmlResolvedArgs;

/**
 * @brief The SenML Pack builders and the Record resolver.
 *
 * A caller sets the members a call takes, invokes it through ::Senml, and reads the outcome off the
 * same handle.
 *
 * No slot member: a Pack is the whole unit every call names, so no call names a row.
 *
 * @var SenmlNs::pack          the Records a build encodes or a resolve reads (RFC 8428 sec 3)
 * @var SenmlNs::json          where the JSON representation lands (RFC 8428 sec 5)
 * @var SenmlNs::binary        the encoding a Pack takes and where it lands (RFC 8428 sec 6)
 * @var SenmlNs::resolved      where the resolved Records land (RFC 8428 sec 4.6)
 * @var SenmlNs::ok            a call's true/false outcome
 * @var SenmlNs::n             the bytes a build wrote, excluding the NUL, or the Records a resolve
 *                             produced; 0 when the call failed
 * @var SenmlNs::json_build    encode @c pack into @c json as application/senml+json (sec 5)
 * @var SenmlNs::binary_build  encode @c pack into @c binary through its codec, with the sec 6
 *                             Table 4 integer labels
 * @var SenmlNs::resolve       carry the Base Name and Base Time across @c pack and fold them into
 *                             each Record, into @c resolved (sec 4.6)
 */
typedef struct
{
    SenmlPackArgs pack;         ///< what a call reads
    SenmlJsonArgs json;         ///< where the text Pack lands
    SenmlBinaryArgs binary;     ///< which encoding, and where the encoded Pack lands
    SenmlResolvedArgs resolved; ///< where the resolved Records land
    proto_bool ok;
    size_t n;
} SenmlVars;

/** @brief The operands and the outcome. */
extern SenmlVars SenmlV;

/** @brief The entries. */
typedef struct
{
    void (*const json_build)(uint8_t *restrict work);
    void (*const binary_build)(uint8_t *restrict work);
    void (*const resolve)(uint8_t *restrict work);
} SenmlNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in SenmlV or a region of the borrow at a fixed offset.
void protocore_senml_json_build(uint8_t *restrict work);
void protocore_senml_binary_build(uint8_t *restrict work);
void protocore_senml_resolve(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Senml.json_build(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const SenmlNs Senml __attribute__((unused)) = {
    .json_build = protocore_senml_json_build,
    .binary_build = protocore_senml_binary_build,
    .resolve = protocore_senml_resolve,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SENML

#endif // PROTOCORE_SENML_H
