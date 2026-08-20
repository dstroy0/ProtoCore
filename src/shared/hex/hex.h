// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hex.h
 * @brief Base-16 conversion between raw bytes and their ASCII digits.
 *
 * Four operations cover every hex site in the library: one nibble out, one digit in, a machine
 * word out, and a byte run in either direction. Each writes into a caller-owned buffer, takes no
 * heap and no `<stdlib.h>`, and is inline so an unused one costs nothing.
 *
 * The decoders report failure through a negative return rather than a sentinel digit, so a
 * malformed byte can never be mistaken for a valid zero.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HEX_H
#define PROTOCORE_HEX_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

/**
 * @brief The two digit tables: the only thing this module owns.
 *
 * One definition for the whole library rather than a copy per translation unit. Immutable, so a
 * site that only needs a digit indexes it directly instead of going through a call.
 */
typedef struct
{
    const char *lower; ///< the 16 hex digits, lowercase
    const char *upper; ///< the 16 hex digits, uppercase - for the protocols that specify capitals
} HexStorage;
/** @brief The digit tables every hex site reads. */
extern const HexStorage PROTOCORE_HEX;
/** @brief What a digit conversion names: one nibble, or one character. */
typedef struct
{
    uint8_t nibble;   ///< the nibble a digit lookup renders
    char ch;          ///< the character a value lookup reads
    uint32_t v;       ///< the value a u32 render writes
    proto_bool upper; ///< render A-F rather than a-f
} HexArgs;
/** @brief The buffers a run conversion moves between. */
typedef struct
{
    const uint8_t *in; ///< the bytes an encode reads
    const char *text;  ///< the characters a decode reads
    uint32_t n;        ///< how many bytes to encode, or how many characters to decode
    char *out;         ///< where an encode or a u32 render writes
    uint8_t *bytes;    ///< where a decode writes
    uint32_t cap;      ///< how much room that has
} HexIoArgs;
/**
 * @brief Hex digits, and the conversions both directions.
 *
 * A caller sets the members a call takes, invokes it through ::Hex, and reads the outcome off the
 * same handle. The digit tables are behind @ref internal.
 *
 * @var HexNs::args      one nibble, or one character
 * @var HexNs::io        the buffers a run conversion moves between
 * @var HexNs::ch        the digit a lookup rendered
 * @var HexNs::i8        the value a digit lookup read, or -1 when it is not a hex digit
 * @var HexNs::u8        digits a u32 render wrote (1..8)
 * @var HexNs::i32       bytes a decode wrote, or -1 on a refusal
 * @var HexNs::digit     the hex character for a nibble
 * @var HexNs::val       the value of a hex character
 * @var HexNs::u32       render a value as lowercase hex, most significant digit first
 * @var HexNs::encode    render a byte run as hex characters plus a NUL
 * @var HexNs::decode    read a hex run back into bytes
 *
 * u32 writes no `0x` prefix, no NUL, and no leading zeros, which is the form the HTTP/1.1 chunked
 * size line takes; zero renders as a single "0" and @c io.out needs room for 8 characters.
 *
 * encode needs @c io.out to hold 2 * @c io.n + 1; the caller owns that bound, since a byte run has
 * no self-describing end. decode refuses an odd length or a result larger than @c io.cap without
 * writing anything; a bad digit stops the run where it is found.
 */
typedef struct
{
    HexArgs args;
    HexIoArgs io;
    char ch;
    int8_t i8;
    uint8_t u8;
    int32_t i32;
} HexVars;

/** @brief The operands and the outcome. */
extern HexVars HexV;

/** @brief The entries. */
typedef struct
{
    void (*const digit)(uint8_t *restrict work);
    void (*const val)(uint8_t *restrict work);
    void (*const u32)(uint8_t *restrict work);
    void (*const encode)(uint8_t *restrict work);
    void (*const decode)(uint8_t *restrict work);
} HexNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in HexV or a region of the borrow at a fixed offset.
void protocore_hex_digit(uint8_t *restrict work);
void protocore_hex_val(uint8_t *restrict work);
void protocore_hex_u32(uint8_t *restrict work);
void protocore_hex_encode(uint8_t *restrict work);
void protocore_hex_decode(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Hex.digit(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const HexNs Hex __attribute__((unused)) = {
    .digit = protocore_hex_digit,
    .val = protocore_hex_val,
    .u32 = protocore_hex_u32,
    .encode = protocore_hex_encode,
    .decode = protocore_hex_decode,
};

#endif // PROTOCORE_HEX_H
