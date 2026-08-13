// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file membuild.h
 * @brief Bounded no-heap builder that writes into memory the caller already owns.
 *
 * Under mmgr because building into a buffer is a memory operation: the builder never allocates, it
 * is handed a region and fills it. It bump-appends into a caller-owned `char[]` and latches @c ok to
 * false the first time something would not fit, so every later append is a no-op and the caller
 * tests one flag at the end rather than a return value per call.
 *
 * The module exports one symbol, @ref Sb. The appends it names are in membuild.c, and everything
 * else in that file has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MEMBUILD_H
#define PROTOCORE_MEMBUILD_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

/**
 * @brief Append a string literal, taking the length from the array type.
 *
 * `sizeof(s) - 1` is the extent the array type already states, folded at compile time. A macro
 * because only the array type carries that extent: passing a literal to a function decays it to a
 * pointer and loses the length.
 *
 * @warning @p s must be a string literal or a `char[]`. A `const char *` compiles here and yields the
 *          pointer size, so it appends 3 or 7 bytes. ::SbNs::put takes a runtime string.
 */
#define protocore_sb_lit(b, s) protocore_sb_put_n((b), (s), sizeof(s) - 1)

/** @brief Bump-append target; @c ok latches false once an append would overflow @c cap. */
typedef struct
{
    char *p;
    size_t cap;
    size_t len;
    proto_bool ok;
} protocore_sb;

/**
 * @brief The builder module. Every append measures its field, tests the one bound, then writes.
 *
 * @var SbNs::put_n
 * Append @c sl bytes of @c s. The primitive the others are built on: it takes the length rather than
 * scanning for a NUL. Appending a literal goes through ::protocore_sb_lit, which deduces the length.
 *
 * @var SbNs::put
 * Append the NUL-terminated @c s, measured within @c cap. Leaves the buffer untouched and clears
 * @c ok when the whole string would not fit.
 *
 * @var SbNs::put_clip
 * Append as much of @c s as fits and stop, WITHOUT latching @c ok. A NULL @c s appends nothing.
 *
 * Display text only - an `ls -l` line, a log message. A protocol field takes ::SbNs::put: a clipped
 * header or frame has no terminator and desynchronizes the peer, which the latch is what stops.
 *
 * @var SbNs::u64_clip
 * Append @c v as decimal, right-aligned in at least @c columns with leading spaces, if the whole
 * field fits - else append nothing, without latching @c ok. @c columns of 0 asks for the natural
 * width. All-or-nothing where ::SbNs::put_clip is byte-wise, since half a number reads as a
 * different number. Space padding, not the zero padding ::SbNs::uint writes.
 *
 * @var SbNs::xml
 * Append @c s XML-escaped (&amp; &lt; &gt; &quot;); a NULL @c s appends nothing.
 *
 * @var SbNs::ch
 * Append one character.
 *
 * @var SbNs::uint
 * Append @c v in @c base (10 or 16), left-padded with '0' to at least @c min_digits. The one engine
 * behind the decimal and hex appenders: measure the field, bounds-check once, then fill it
 * back-to-front in place, so neither needs a scratch array. A power-of-two base takes each digit
 * with a shift and a mask; base 10 divides, in 32-bit arithmetic when the value fits 32 bits.
 * @c min_digits is what carries a printf width like %08lx or %02d.
 *
 * @var SbNs::u32w
 * Append @c v as decimal, zero-padded to at least @c min_digits (printf "%0Nu").
 *
 * @var SbNs::hex
 * Append @c v as lowercase hex, zero-padded to at least @c min_digits (printf "%0Nx").
 *
 * @var SbNs::u32
 * Append @c v as decimal, no leading zeros ("0" for zero).
 *
 * @var SbNs::u64
 * Append @c v as decimal, 64-bit.
 *
 * @var SbNs::i64
 * Append @c v as signed decimal, 64-bit, with a leading '-' when negative. The magnitude is taken
 * through unsigned arithmetic, so the most negative value does not overflow on the way.
 *
 * @var SbNs::sign_bit
 * True when @c v carries the IEEE-754 sign bit, including for -0.0. A mask on the encoding, not a
 * divide.
 *
 * @var SbNs::is_inf
 * True when @c v is an infinity: all exponent bits set, zero significand.
 *
 * @var SbNs::is_nan
 * True when @c v is a NaN: all exponent bits set, nonzero significand.
 *
 * @var SbNs::g
 * Append @c v with @c sig significant digits, choosing fixed or scientific form - the printf
 * "%.<sig>g" rendering, including trailing-zero removal. Byte-identical to printf for @c sig up to
 * 10. Above that the scaling is done in double, which runs out of precision around 16 significant
 * digits, so @c sig of 15 or more can differ from libc in the last digit.
 *
 * @var SbNs::fixed
 * Append @c v with exactly @c decimals digits after the point (printf "%.<decimals>f"), @c decimals
 * clamped to 18. Byte-identical to printf for a magnitude below 2^64; at or above it the value falls
 * back to ::SbNs::g, whose exact expansion needs no big-integer arithmetic.
 *
 * @var SbNs::json
 * Append @c s as a JSON string literal: double-quoted, with `"` and `\` backslash-escaped. A NULL
 * @c s appends `""`. Control characters pass through unescaped.
 *
 * @var SbNs::finish
 * NUL-terminate and return the built length, or 0 when the build overflowed or @c cap is 0.
 *
 * No storage member: every call works on the caller's @ref protocore_sb and the module holds nothing
 * of its own.
 */
typedef struct
{
    void (*put_n)(protocore_sb *b, const char *s, size_t sl);
    void (*put)(protocore_sb *b, const char *s);
    void (*put_clip)(protocore_sb *b, const char *s);
    void (*u64_clip)(protocore_sb *b, uint64_t v, uint8_t columns);
    void (*xml)(protocore_sb *b, const char *s);
    void (*ch)(protocore_sb *b, char c);
    void (*uint)(protocore_sb *b, uint64_t v, unsigned base, unsigned min_digits);
    void (*u32w)(protocore_sb *b, uint32_t v, unsigned min_digits);
    void (*hex)(protocore_sb *b, uint64_t v, unsigned min_digits);
    void (*u32)(protocore_sb *b, uint32_t v);
    void (*u64)(protocore_sb *b, uint64_t v);
    void (*i64)(protocore_sb *b, int64_t v);
    proto_bool (*sign_bit)(double v);
    proto_bool (*is_inf)(double v);
    proto_bool (*is_nan)(double v);
    void (*g)(protocore_sb *b, double v, unsigned sig);
    void (*fixed)(protocore_sb *b, double v, unsigned decimals);
    void (*json)(protocore_sb *b, const char *s);
    size_t (*finish)(protocore_sb *b);
} SbNs;

// The appends, in membuild.c. Named here because the table below has to name them, and prefixed
// because that puts them in the linker's namespace.
void protocore_sb_put_n(protocore_sb *b, const char *s, size_t sl);
void protocore_sb_put(protocore_sb *b, const char *s);
void protocore_sb_put_clip(protocore_sb *b, const char *s);
void protocore_sb_u64_clip(protocore_sb *b, uint64_t v, uint8_t columns);
void protocore_sb_xml(protocore_sb *b, const char *s);
void protocore_sb_ch(protocore_sb *b, char c);
void protocore_sb_uint(protocore_sb *b, uint64_t v, unsigned base, unsigned min_digits);
void protocore_sb_u32w(protocore_sb *b, uint32_t v, unsigned min_digits);
void protocore_sb_hex(protocore_sb *b, uint64_t v, unsigned min_digits);
void protocore_sb_u32(protocore_sb *b, uint32_t v);
void protocore_sb_u64(protocore_sb *b, uint64_t v);
void protocore_sb_i64(protocore_sb *b, int64_t v);
proto_bool protocore_signbit(double v);
proto_bool protocore_isinf(double v);
proto_bool protocore_isnan(double v);
void protocore_sb_g(protocore_sb *b, double v, unsigned sig);
void protocore_sb_fixed(protocore_sb *b, double v, unsigned decimals);
void protocore_sb_json(protocore_sb *b, const char *s);
size_t protocore_sb_finish(protocore_sb *b);

/**
 * @brief The names, aliased.
 *
 * `static const` and initialized here, not declared `extern` against a definition in the .c: a
 * translation unit that can see this initializer knows which function each member holds, so a member
 * read folds away and the call to the append in membuild.c is direct, leaving the table referenced
 * by nothing for the linker to drop.
 *
 * `unused` because a header this wide is included by files that take none of it.
 */
// Designated, so a member's position in the struct does not decide what it binds to.
static const SbNs Sb __attribute__((unused)) = {.put_n = protocore_sb_put_n,
                                                .put = protocore_sb_put,
                                                .put_clip = protocore_sb_put_clip,
                                                .u64_clip = protocore_sb_u64_clip,
                                                .xml = protocore_sb_xml,
                                                .ch = protocore_sb_ch,
                                                .uint = protocore_sb_uint,
                                                .u32w = protocore_sb_u32w,
                                                .hex = protocore_sb_hex,
                                                .u32 = protocore_sb_u32,
                                                .u64 = protocore_sb_u64,
                                                .i64 = protocore_sb_i64,
                                                .sign_bit = protocore_signbit,
                                                .is_inf = protocore_isinf,
                                                .is_nan = protocore_isnan,
                                                .g = protocore_sb_g,
                                                .fixed = protocore_sb_fixed,
                                                .json = protocore_sb_json,
                                                .finish = protocore_sb_finish};

PROTOCORE_END_DECLS

#endif // PROTOCORE_MEMBUILD_H
