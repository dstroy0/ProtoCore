// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
 * are derived from ::pc_swar_word rather than written out, so the carrier follows ::PROTO_SWAR_BITS
 * and every constant follows it. That knob defaults to the register width the die declares in
 * core_setup/board_profiles/, so nothing here infers a width from the toolchain.
 *
 * **This is the access layer.** Load a word, test its lanes, name the lane that fired. Nothing here
 * walks a buffer or takes a capacity, which is what keeps the byte-order claim under
 * ::pc_swar_zero_lane true: a lane count is the one place address order is decided, and stepping
 * across a word pair is not this file's business. The scans, compares, searches and the bounded copy
 * built on these live in shared_primitives/runops.h.
 *
 * **Constant time.** Every operation here is branchless and data-independent, which is why the
 * base64 decoder classifies characters with ::pc_swar_ge and ::pc_swar_le rather than a table: an
 * address derived from a secret byte leaks it through the cache, and an arithmetic mask cannot. The
 * operations in runops.h are NOT in that class - they stop at the byte they find, which is the whole
 * point of a bounded scan. Never run one over a secret.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SWAR_H
#define PROTOCORE_SWAR_H

#include "mmgr/rawmemcpy.h"   // proto_raw_load: the one owner of an unaligned wider load
#include "protocore_config.h" // PROTO_SWAR_BITS: the platform's lane-carrier width

/**
 * @brief The lane carrier, selected by the platform width knob.
 *
 * The type follows ::PROTO_SWAR_BITS rather than the width following the type, so the number that
 * every mask below is derived from is a preprocessor value the build can set (-DPROTO_SWAR_BITS) and
 * conditionals can test. A typedef alone could do neither.
 */
#if PROTO_SWAR_BITS == 64
typedef uint64_t pc_swar_word;
#elif PROTO_SWAR_BITS == 32
typedef uint32_t pc_swar_word;
#elif PROTO_SWAR_BITS == 16
typedef uint16_t pc_swar_word;
#elif PROTO_SWAR_BITS == 8
// One lane per word: the algebra is unchanged (ONES is 1, the guard bit is 0x80), it just answers
// for a single byte at a time. The honest setting for a machine with no wider register, and the
// floor the wider rungs are built from.
typedef uint8_t pc_swar_word;
#else
#error "PROTO_SWAR_BITS must be 8, 16, 32 or 64"
#endif

#define PC_SWAR_BYTES ((size_t)(PROTO_SWAR_BITS / 8u)) ///< lanes per word

// `static_assert`, not C11's `_Static_assert`: this header rides in through protocore.h to the
// sketches, which are C++, where the underscored spelling is not a keyword. See types.h.
static_assert(sizeof(pc_swar_word) * 8u == PROTO_SWAR_BITS, "the lane carrier must be exactly PROTO_SWAR_BITS wide");

// One bit per lane, derived from the width rather than written out: the all-ones word divided by
// 0xFF leaves exactly bit 0 of each lane (0xFFFFFFFF / 0xFF == 0x01010101), and the other two masks
// are that scaled. Spelling them as hex literals would pin the width in three more places.
#define PC_SWAR_ONES (((pc_swar_word) ~(pc_swar_word)0) / 0xFFu) ///< bit 0 of every lane
#define PC_SWAR_HIGH (PC_SWAR_ONES * 0x80u)                      ///< bit 7 (the guard bit) of every lane
#define PC_SWAR_LOW7 (PC_SWAR_ONES * 0x7Fu)                      ///< bits 0-6 of every lane

/** @brief Per lane: 0x80 where the lane is >= @p v, else 0. */
PC_INLINE pc_swar_word pc_swar_ge(pc_swar_word a, pc_swar_word v)
{
    return ((a | PC_SWAR_HIGH) - v * PC_SWAR_ONES) & PC_SWAR_HIGH;
}

/** @brief Per lane: 0x80 where the lane is <= @p v, else 0. */
PC_INLINE pc_swar_word pc_swar_le(pc_swar_word a, pc_swar_word v)
{
    return ((v * PC_SWAR_ONES | PC_SWAR_HIGH) - a) & PC_SWAR_HIGH;
}

/** @brief Widen a 0x80-per-lane mask to 0xFF per lane, without carrying between lanes. */
PC_INLINE pc_swar_word pc_swar_spread(pc_swar_word m)
{
    return m + (m - (m >> 7));
}

/** @brief Per lane: (lane - @p lo) in the low 7 bits, guard bit absorbing the borrow. */
PC_INLINE pc_swar_word pc_swar_sub7(pc_swar_word a, pc_swar_word lo)
{
    return ((a | PC_SWAR_HIGH) - lo * PC_SWAR_ONES) & PC_SWAR_LOW7;
}

/**
 * @brief 0x80 in every lane of @p w that is zero, and only those.
 *
 * Exact per lane, which is dearer than the usual spelling by one operation and worth it. The cheap
 * form is `(w - ONES) & ~w & HIGH`: a lane holding 0x00 borrows into its own high bit, which is the
 * answer, but that borrow does not stop at the lane boundary and goes on to mark lanes above it that
 * hold no zero at all. A caller reading only ::pc_swar_zero_lane never sees it, because the lowest
 * set bit is always a true one. A caller that ANDs two of these masks together, or reads any lane
 * but the first, gets a byte that is not there - `0x0100` reports both of its lanes zero.
 *
 * Adding LOW7 to the low seven bits of a lane carries into bit 7 for every value except 0x00, and
 * ORing @p w back in covers the lane that was exactly 0x80. So the guard bit ends up set on each
 * NONZERO lane, with nothing crossing between lanes, and the complement is the answer.
 */
PC_INLINE pc_swar_word pc_swar_has_zero(pc_swar_word w)
{
    return ~(((w & PC_SWAR_LOW7) + PC_SWAR_LOW7) | w) & PC_SWAR_HIGH;
}

/**
 * @brief Per lane: 0x80 where the lane equals @p c, else 0.
 *
 * XOR zeroes exactly the lanes that match, so the zero test finds them. That is what lets ONE load
 * answer for as many delimiters as a caller cares about: OR the masks together and the first set
 * lane is the first occurrence of any of them. A scan per delimiter would re-load the same word once
 * per byte it is looking for, and then have to reconcile which hit came first.
 *
 *     pc_swar_word w = pc_swar_load(p);
 *     pc_swar_word m = pc_swar_eq(w, '&') | pc_swar_eq(w, '=');   // one load, both delimiters
 */
PC_INLINE pc_swar_word pc_swar_eq(pc_swar_word w, uint8_t c)
{
    return pc_swar_has_zero(w ^ (PC_SWAR_ONES * (pc_swar_word)c));
}

/**
 * @brief Which lane of a ::pc_swar_has_zero mask is the first zero byte, in address order.
 *
 * The answer is one of 0..PC_SWAR_BYTES-1 and the mask already holds it - the set guard bit IS the
 * position. Re-walking the word's bytes to find out would spend a compare per byte to recover what
 * the mask states, which is the same waste the word test just avoided.
 *
 * Address order is where byte order enters, and only here: the lowest-addressed byte is the least
 * significant lane on a little-endian load and the most significant on a big-endian one, so the
 * count is taken from the matching end. Nothing else in this file depends on the layout.
 *
 * The count is taken at the CARRIER's width, which is why the builtin is selected below rather than
 * fixed. `unsigned` alone would truncate a carrier wider than 32 bits - a mask whose only set guard
 * bit sits above bit 31 becomes 0, and a zero count is undefined. `unsigned long long` alone asks a
 * 32-bit machine to count twice the bits it has, which is two registers to fill for a question that
 * fits in one.
 *
 * The builtin is also how the compiler reaches whatever bit-scan the die has.
 */
#if PROTO_SWAR_BITS <= 32
#define PC_SWAR_CTZ(v) __builtin_ctz((unsigned)(v))
#define PC_SWAR_CLZ(v) __builtin_clz((unsigned)(v))
#define PC_SWAR_CLZ_WIDTH 32u
#else
#define PC_SWAR_CTZ(v) __builtin_ctzll((unsigned long long)(v))
#define PC_SWAR_CLZ(v) __builtin_clzll((unsigned long long)(v))
#define PC_SWAR_CLZ_WIDTH 64u
#endif

PC_INLINE size_t pc_swar_zero_lane(pc_swar_word m)
{
#if PC_HW_BIG_ENDIAN
    // The count runs over the whole builtin width; the carrier sits in the low bits, so drop the pad.
    return (size_t)((PC_SWAR_CLZ(m) - (PC_SWAR_CLZ_WIDTH - PROTO_SWAR_BITS)) >> 3);
#else
    return (size_t)(PC_SWAR_CTZ(m) >> 3);
#endif
}

/**
 * @brief Load one word from @p p, whatever its alignment.
 *
 * Deferred to the raw load rather than spelled here: a `*(const pc_swar_word *)` cast is undefined
 * both for an unaligned address and for reading a `char` array as a wider type, and it traps on the
 * stricter targets. Taking the width from ::PC_SWAR_BYTES keeps this correct when the lane carrier
 * is retyped.
 */
PC_INLINE pc_swar_word pc_swar_load(const char *p)
{
    return (pc_swar_word)proto_raw_load(p, PC_SWAR_BYTES);
}

/**
 * @brief ::pc_swar_load for an address the caller has already walked to a lane boundary.
 *
 * Same value, one instruction instead of a synthesized word. ::pc_swar_load assumes nothing about
 * its address, and where PC_HW_UNALIGNED_LOAD is 0 - every xtensa in the target list - the compiler
 * answers that assumption by building each word out of byte loads and shifts. A caller that has
 * already paid a prologue to reach a boundary throws that away by asking for a load which disclaims
 * alignment.
 *
 * @p p must be ::PC_SWAR_BYTES-aligned. That is a real precondition, not a hint - a strict-alignment
 * part faults on it.
 */
PC_INLINE pc_swar_word pc_swar_load_al(const char *p)
{
    return (pc_swar_word)proto_al_load(p, PC_SWAR_BYTES);
}

/**
 * @brief ::pc_swar_eq's XOR, with a difference of ASCII case alone cancelled out of it.
 *
 * The syndrome is @p wa ^ @p wb and the allowed class is {0, 0x20} - identical, or differing in the
 * one bit that carries ASCII case - restricted to lanes where flipping that bit still names the same
 * letter. Clearing bit 5 of the syndrome on exactly those lanes IS the test: a lane falls to zero
 * when its syndrome was in the class, and a lane with any second bit set survives.
 *
 * So there is nothing to ask afterwards. Testing whether the syndrome WAS 0x20 re-derives what
 * clearing the bit decides, and widening a 0x80 lane mask to 0xFF to mask it back down to 0x20
 * spends three operations moving a bit two places that a shift moves in one without leaving its
 * lane. 13 operations per word.
 *
 * Branchless, like every other lane test here: a zero syndrome stays zero through every step, so an
 * early-out would spend a branch on parts where a mispredict costs more than it skipped.
 *
 * ORing 0x20 into one side puts both cases of a letter in `a`..`z`, so the letter test is one range
 * rather than two - and only one side needs testing, because a lane whose two sides disagree in any
 * bit but 5 fails on that bit regardless of what this decides.
 *
 * ASCII only, deliberately. Every field this serves is defined as ASCII by its own standard (an HTTP
 * field name, RFC 7230 3.2; a Connection or Upgrade token; an SMTP verb), so treating any other byte
 * as having a case would be inventing a rule no spec here states.
 *
 * `& ~lo` is what holds that line, and it is not decoration. ::pc_swar_ge and ::pc_swar_le answer
 * for 7-bit lanes: the guard bit they borrow into is already set on a lane above 0x7F, so the
 * comparison it was standing in for is gone and such a lane range-tests as a letter. 0xDB and 0xFB
 * differ by exactly the case bit and would have been accepted as a case pair - a Latin-1 or UTF-8
 * byte silently matching a different one. The mask keeps a lane out of the range test's answer
 * whenever bit 7 says the answer is not the test's to give.
 */
PC_INLINE pc_swar_word pc_swar_xor_ci(pc_swar_word wa, pc_swar_word wb)
{
    pc_swar_word x = wa ^ wb;
    pc_swar_word lo = wa | (PC_SWAR_ONES * 0x20u);
    pc_swar_word alpha = pc_swar_ge(lo, 'a') & pc_swar_le(lo, 'z') & ~lo;
    return x & ~(alpha >> 2); // 0x80 per letter lane, shifted onto the case bit it is allowed to eat
}

/**
 * @brief ::pc_swar_eq ignoring ASCII case: 0x80 per lane equal to @p c under case folding.
 *
 * The case bit cannot simply be masked out of both sides. Bit 5 is the case bit only for a letter;
 * clearing it elsewhere merges pairs that are not a case pair at all, and `'0'` (0x30) would report
 * equal to DLE (0x10). ::pc_swar_xor_ci is the syndrome that cancels bit 5 on letter lanes only, so
 * a zero lane of it is a case-insensitive match and the same zero test reads the answer.
 */
PC_INLINE pc_swar_word pc_swar_eq_ci(pc_swar_word w, uint8_t c)
{
    return pc_swar_has_zero(pc_swar_xor_ci(w, PC_SWAR_ONES * (pc_swar_word)c));
}

/// @name What one step of a walk concluded
/// @{
#define PC_SWAR_GO 0  ///< undecided: keep stepping
#define PC_SWAR_YES 1 ///< they agree, by whichever rule the caller asked for
#define PC_SWAR_NO 2  ///< they do not
/// @}

/** @brief The exact syndrome: zero in exactly the lanes where @p wa and @p wb match. */
PC_INLINE pc_swar_word pc_swar_xor(pc_swar_word wa, pc_swar_word wb)
{
    return wa ^ wb;
}

// Exact and case-folding are separate tests, so the bool names which one runs rather than travelling
// into it.
PC_INLINE pc_swar_word pc_swar_eq_sel(pc_swar_word w, uint8_t c, proto_bool ci)
{
    if (ci)
    {
        return pc_swar_eq_ci(w, c);
    }
    return pc_swar_eq(w, c);
}

PC_INLINE pc_swar_word pc_swar_xor_sel(pc_swar_word wa, pc_swar_word wb, proto_bool ci)
{
    if (ci)
    {
        return pc_swar_xor_ci(wa, wb);
    }
    return pc_swar_xor(wa, wb);
}

/**
 * @brief The lane-math module. One word in, a per-lane answer out, branchless throughout.
 *
 * @var SwarNs::ge         per lane: 0x80 where the lane is >= @c v
 * @var SwarNs::le         per lane: 0x80 where the lane is <= @c v
 * @var SwarNs::spread     widen a 0x80-per-lane mask to 0xFF per lane
 * @var SwarNs::sub7       per lane: (lane - @c lo) in the low 7 bits
 * @var SwarNs::has_zero   0x80 in every zero lane of @c w, and only those
 * @var SwarNs::eq         per lane: 0x80 where the lane equals @c c; @c ci folds ASCII case
 * @var SwarNs::xor_       the syndrome of @c wa against @c wb; @c ci cancels a case-only difference
 * @var SwarNs::zero_lane  which lane of a mask is the first one set, in address order
 * @var SwarNs::load       one word from @c p, whatever its alignment
 * @var SwarNs::load_al    the same, for an address already on a lane boundary
 *
 * The member is spelled `xor_` because `xor` is an alternative token in C++, and this header reaches
 * the sketches.
 *
 * No storage member: every operation works on the value or pointer handed to it and holds nothing.
 */
typedef struct
{
    pc_swar_word (*ge)(pc_swar_word a, pc_swar_word v);
    pc_swar_word (*le)(pc_swar_word a, pc_swar_word v);
    pc_swar_word (*spread)(pc_swar_word m);
    pc_swar_word (*sub7)(pc_swar_word a, pc_swar_word lo);
    pc_swar_word (*has_zero)(pc_swar_word w);
    pc_swar_word (*eq)(pc_swar_word w, uint8_t c, proto_bool ci);
    pc_swar_word (*xor_)(pc_swar_word wa, pc_swar_word wb, proto_bool ci);
    size_t (*zero_lane)(pc_swar_word m);
    pc_swar_word (*load)(const char *p);
    pc_swar_word (*load_al)(const char *p);
} SwarNs;

/**
 * @brief The names, aliased.
 *
 * `static const` and initialized here beside the bodies, not declared `extern` against a definition
 * in a .c: those are not the same object. A translation unit that can see this initializer knows
 * `swar.ge` IS ::pc_swar_ge, so the member read folds away, the body inlines, and the table is left
 * referenced by nothing for the linker to drop. One that can see only an `extern` declaration has to
 * load the pointer and call through it.
 *
 * `unused` because a header this wide is included by files that take none of it.
 */
static const SwarNs swar
    __attribute__((unused)) = {pc_swar_ge,     pc_swar_le,      pc_swar_spread,    pc_swar_sub7, pc_swar_has_zero,
                               pc_swar_eq_sel, pc_swar_xor_sel, pc_swar_zero_lane, pc_swar_load, pc_swar_load_al};

#endif // PROTOCORE_SWAR_H
