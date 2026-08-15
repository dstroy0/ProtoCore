// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file swar.h
 * @brief Lane math: one machine word treated as its byte lanes, tested in parallel.
 *
 * SWAR is "SIMD within a register" - a plain word holds several bytes, and an arithmetic trick
 * answers a question about all of them at once. The trick is the guard bit: set every lane's high
 * bit before subtracting so a borrow cannot cross out of its lane, then read the high bits back as
 * the per-lane answer. What would be one compare and one branch per byte becomes two arithmetic
 * operations and no branches at all.
 *
 * **The width is a typedef, not a decision.** The algebra is identical at any width - the lane masks
 * are derived from ::protocore_swar_word rather than written out, so the carrier follows ::PROTO_SWAR_BITS
 * and every constant follows it. That knob defaults to the register width the die declares in
 * core_setup/board_profiles/, so nothing here infers a width from the toolchain.
 *
 * **This is the access layer.** Load a word, test its lanes, name the lane that fired. Nothing here
 * walks a buffer or takes a capacity, which is what keeps the byte-order claim under
 * ::SwarNs::zero_lane true: a lane count is the one place address order is decided, and stepping
 * across a word pair is not this file's business. The scans, compares, searches and the bounded copy
 * built on these live in mmgr/protostr.h, behind @ref str.
 *
 * **Constant time.** Every operation here is branchless and data-independent, which is why the
 * base64 decoder classifies characters with ::SwarNs::ge and ::SwarNs::le rather than a table: an
 * address derived from a secret byte leaks it through the cache, and an arithmetic mask cannot. The
 * walks behind @ref str are NOT in that class - they stop at the byte they find, which is the whole
 * point of a bounded scan. Never run one over a secret.
 *
 * The lane constants are macros because they are constant expressions a caller puts in an array
 * bound, a `#if` or a static_assert. The lane tests are functions and their bodies are in swar.c.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SWAR_H
#define PROTOCORE_SWAR_H

#include "mmgr/rawmemcpy.h"   // proto_raw_load: the one owner of an unaligned wider load
#include "protocore_config.h" // PROTO_SWAR_BITS: the platform's lane-carrier width

PROTOCORE_BEGIN_DECLS

/**
 * @brief The lane carrier, selected by the platform width knob.
 *
 * The type follows ::PROTO_SWAR_BITS rather than the width following the type, so the number that
 * every mask below is derived from is a preprocessor value the build can set (-DPROTO_SWAR_BITS) and
 * conditionals can test. A typedef alone could do neither.
 */
#if PROTO_SWAR_BITS == 64
typedef uint64_t protocore_swar_word;
#elif PROTO_SWAR_BITS == 32
typedef uint32_t protocore_swar_word;
#elif PROTO_SWAR_BITS == 16
typedef uint16_t protocore_swar_word;
#elif PROTO_SWAR_BITS == 8
// One lane per word: the algebra is unchanged (ONES is 1, the guard bit is 0x80), it just answers
// for a single byte at a time. The honest setting for a machine with no wider register, and the
// floor the wider rungs are built from.
typedef uint8_t protocore_swar_word;
#else
#error "PROTO_SWAR_BITS must be 8, 16, 32 or 64"
#endif

#define PROTOCORE_SWAR_BYTES ((size_t)(PROTO_SWAR_BITS / 8u)) ///< lanes per word

// `static_assert`, not C11's `_Static_assert`: this header rides in through protocore.h to the C++
// translation units that call this library, where the underscored spelling is not a keyword. See
// protocore_types.h.
static_assert(sizeof(protocore_swar_word) * 8u == PROTO_SWAR_BITS,
              "the lane carrier must be exactly PROTO_SWAR_BITS wide");

// One bit per lane, derived from the width rather than written out: the all-ones word divided by
// 0xFF leaves exactly bit 0 of each lane (0xFFFFFFFF / 0xFF == 0x01010101), and the other two masks
// are that scaled. Spelling them as hex literals would pin the width in three more places.
#define PROTOCORE_SWAR_ONES (((protocore_swar_word) ~(protocore_swar_word)0) / 0xFFu) ///< bit 0 of every lane
#define PROTOCORE_SWAR_HIGH (PROTOCORE_SWAR_ONES * 0x80u) ///< bit 7 (the guard bit) of every lane
#define PROTOCORE_SWAR_LOW7 (PROTOCORE_SWAR_ONES * 0x7Fu) ///< bits 0-6 of every lane

/**
 * @brief The bit-scan pair ::SwarNs::zero_lane reads a lane position out of, at the carrier's width.
 *
 * The count is taken at the CARRIER's width, which is why the builtin is selected here rather than
 * fixed. `unsigned` alone would truncate a carrier wider than 32 bits - a mask whose only set guard
 * bit sits above bit 31 becomes 0, and a zero count is undefined. `unsigned long long` alone asks a
 * 32-bit machine to count twice the bits it has, which is two registers to fill for a question that
 * fits in one.
 *
 * The builtin is also how the compiler reaches whatever bit-scan the die has. These stay macros: a
 * builtin is not a callable object, and ::PROTOCORE_SWAR_CLZ_WIDTH is a constant expression callers
 * put in shift counts.
 */
#if PROTO_SWAR_BITS <= 32
#define PROTOCORE_SWAR_CTZ(v) __builtin_ctz((unsigned)(v))
#define PROTOCORE_SWAR_CLZ(v) __builtin_clz((unsigned)(v))
#define PROTOCORE_SWAR_CLZ_WIDTH 32u
#else
#define PROTOCORE_SWAR_CTZ(v) __builtin_ctzll((unsigned long long)(v))
#define PROTOCORE_SWAR_CLZ(v) __builtin_clzll((unsigned long long)(v))
#define PROTOCORE_SWAR_CLZ_WIDTH 64u
#endif

/// @name What one step of a walk concluded
/// @{
#define PROTOCORE_SWAR_GO 0  ///< undecided: keep stepping
#define PROTOCORE_SWAR_YES 1 ///< they agree, by whichever rule the caller asked for
#define PROTOCORE_SWAR_NO 2  ///< they do not
/// @}

/**
 * @brief The lane-math module. One word in, a per-lane answer out, branchless throughout.
 *
 * @var SwarNs::ge
 * Per lane: 0x80 where the lane is >= @c v, else 0. The guard bit is set on every lane before the
 * subtraction, so a borrow stays inside the lane that raised it and the bit that survives is that
 * lane's own answer.
 *
 * @var SwarNs::le
 * Per lane: 0x80 where the lane is <= @c v, else 0. The same subtraction with the operands the other
 * way round.
 *
 * @var SwarNs::spread
 * Widen a 0x80-per-lane mask to 0xFF per lane, without carrying between lanes.
 *
 * @var SwarNs::sub7
 * Per lane: (lane - @c lo) in the low 7 bits, the guard bit absorbing the borrow.
 *
 * @var SwarNs::has_zero
 * 0x80 in every lane of @c w that is zero, and only those. Exact per lane, so two of these masks may
 * be ANDed and any lane of one may be read, not just the lowest.
 *
 * @var SwarNs::eq
 * Per lane: 0x80 where the lane equals @c c, else 0. @c ci folds ASCII case, which merges a letter
 * with its other case and nothing else. ORing several of these masks answers for several delimiters
 * off one load.
 *
 * @var SwarNs::xor_
 * The syndrome of @c wa against @c wb: zero in exactly the lanes that match. @c ci additionally
 * cancels a difference of ASCII case alone, on the lanes where flipping that bit still names the
 * same letter.
 *
 * The member is spelled `xor_` because `xor` is an alternative token in C++, and this header reaches
 * the C++ translation units.
 *
 * @var SwarNs::zero_lane
 * Which lane of a mask is the first one set, in address order: 0..PROTOCORE_SWAR_BYTES-1. Address
 * order is where byte order enters, and only here.
 *
 * @var SwarNs::load
 * One word from @c p, whatever its alignment.
 *
 * @var SwarNs::load_al
 * The same, for an address the caller has already walked to a lane boundary. @c p must be
 * ::PROTOCORE_SWAR_BYTES-aligned; a strict-alignment part faults otherwise.
 *
 * No storage member: every operation works on the word or pointer handed to it and holds nothing of
 * its own. There is no pool, no cursor and no configuration to keep.
 */
typedef struct
{
    protocore_swar_word (*ge)(protocore_swar_word a, protocore_swar_word v);
    protocore_swar_word (*le)(protocore_swar_word a, protocore_swar_word v);
    protocore_swar_word (*spread)(protocore_swar_word m);
    protocore_swar_word (*sub7)(protocore_swar_word a, protocore_swar_word lo);
    protocore_swar_word (*has_zero)(protocore_swar_word w);
    protocore_swar_word (*eq)(protocore_swar_word w, uint8_t c, proto_bool ci);
    protocore_swar_word (*xor_)(protocore_swar_word wa, protocore_swar_word wb, proto_bool ci);
    size_t (*zero_lane)(protocore_swar_word m);
    protocore_swar_word (*load)(const char *p);
    protocore_swar_word (*load_al)(const char *p);
} SwarNs;

// The lane tests, in swar.c. Named here because the table below has to name them, and prefixed
// because that puts them in the linker's namespace. The `_sel` pair is what the table's `eq` and
// `xor_` members hold: exact and case-folding are separate tests, so the bool names which one runs
// rather than travelling into it.
protocore_swar_word protocore_swar_ge(protocore_swar_word a, protocore_swar_word v);
protocore_swar_word protocore_swar_le(protocore_swar_word a, protocore_swar_word v);
protocore_swar_word protocore_swar_spread(protocore_swar_word m);
protocore_swar_word protocore_swar_sub7(protocore_swar_word a, protocore_swar_word lo);
protocore_swar_word protocore_swar_has_zero(protocore_swar_word w);
protocore_swar_word protocore_swar_eq(protocore_swar_word w, uint8_t c);
protocore_swar_word protocore_swar_eq_ci(protocore_swar_word w, uint8_t c);
protocore_swar_word protocore_swar_eq_sel(protocore_swar_word w, uint8_t c, proto_bool ci);
protocore_swar_word protocore_swar_xor(protocore_swar_word wa, protocore_swar_word wb);
protocore_swar_word protocore_swar_xor_ci(protocore_swar_word wa, protocore_swar_word wb);
protocore_swar_word protocore_swar_xor_sel(protocore_swar_word wa, protocore_swar_word wb, proto_bool ci);
size_t protocore_swar_zero_lane(protocore_swar_word m);
protocore_swar_word protocore_swar_load(const char *p);
protocore_swar_word protocore_swar_load_al(const char *p);

/**
 * @brief The names, aliased.
 *
 * `static const` and initialized here, not declared `extern` against a definition in the .c: a
 * translation unit that can see this initializer knows which function each member holds, so a member
 * read folds away and the call to the lane test in swar.c is direct, leaving the table referenced by
 * nothing for the linker to drop.
 *
 * `unused` because a header this wide is included by files that take none of it.
 */
static const SwarNs swar __attribute__((unused)) = {
    protocore_swar_ge,       protocore_swar_le,     protocore_swar_spread,  protocore_swar_sub7,
    protocore_swar_has_zero, protocore_swar_eq_sel, protocore_swar_xor_sel, protocore_swar_zero_lane,
    protocore_swar_load,     protocore_swar_load_al};

PROTOCORE_END_DECLS

#endif // PROTOCORE_SWAR_H
