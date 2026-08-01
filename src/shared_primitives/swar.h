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
 * are derived from ::pc_swar_word rather than written out, so retyping it to the environment's
 * efficient width is a one-line change and every constant follows. It is `uint32_t` until the sweep
 * that binds types to the environment.
 *
 * **Constant time.** ::pc_swar_ge, ::pc_swar_le, ::pc_swar_spread and ::pc_swar_sub7 are branchless
 * and data-independent, which is why the base64 decoder classifies characters with them: a decoder
 * that branched on a secret's bytes would leak it through timing. ::pc_swar_scan_nul is NOT in that
 * class - it stops at the byte it finds, which is the whole point of a length scan. Never use it on
 * a secret.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SWAR_H
#define PROTOCORE_SWAR_H

#include <stddef.h>
#include <stdint.h>
#include <string.h> // memcpy: the aligned-safe word load

/** @brief The lane carrier. Retyped to the environment's efficient width by a later sweep. */
typedef uint32_t pc_swar_word;

#define PC_SWAR_BYTES ((size_t)sizeof(pc_swar_word)) ///< lanes per word

// One bit per lane, derived from the width rather than written out: the all-ones word divided by
// 0xFF leaves exactly bit 0 of each lane (0xFFFFFFFF / 0xFF == 0x01010101), and the other two masks
// are that scaled. Spelling them as hex literals would pin the width in three more places.
#define PC_SWAR_ONES (((pc_swar_word) ~(pc_swar_word)0) / 0xFFu) ///< bit 0 of every lane
#define PC_SWAR_HIGH (PC_SWAR_ONES * 0x80u)                      ///< bit 7 (the guard bit) of every lane
#define PC_SWAR_LOW7 (PC_SWAR_ONES * 0x7Fu)                      ///< bits 0-6 of every lane

/** @brief Per lane: 0x80 where the lane is >= @p v, else 0. */
inline pc_swar_word pc_swar_ge(pc_swar_word a, pc_swar_word v)
{
    return ((a | PC_SWAR_HIGH) - v * PC_SWAR_ONES) & PC_SWAR_HIGH;
}

/** @brief Per lane: 0x80 where the lane is <= @p v, else 0. */
inline pc_swar_word pc_swar_le(pc_swar_word a, pc_swar_word v)
{
    return ((v * PC_SWAR_ONES | PC_SWAR_HIGH) - a) & PC_SWAR_HIGH;
}

/** @brief Widen a 0x80-per-lane mask to 0xFF per lane, without carrying between lanes. */
inline pc_swar_word pc_swar_spread(pc_swar_word m)
{
    return m + (m - (m >> 7));
}

/** @brief Per lane: (lane - @p lo) in the low 7 bits, guard bit absorbing the borrow. */
inline pc_swar_word pc_swar_sub7(pc_swar_word a, pc_swar_word lo)
{
    return ((a | PC_SWAR_HIGH) - lo * PC_SWAR_ONES) & PC_SWAR_LOW7;
}

/**
 * @brief Nonzero if any lane of @p w is zero.
 *
 * `w - ONES` borrows into a lane's high bit exactly when that lane was 0x00; `& ~w` discards the
 * lanes that merely had their high bit already set, so only a true zero lane survives.
 */
inline pc_swar_word pc_swar_has_zero(pc_swar_word w)
{
    return (w - PC_SWAR_ONES) & ~w & PC_SWAR_HIGH;
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
inline pc_swar_word pc_swar_eq(pc_swar_word w, uint8_t c)
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
 */
inline size_t pc_swar_zero_lane(pc_swar_word m)
{
#if defined(__BYTE_ORDER__) && defined(__ORDER_BIG_ENDIAN__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    return (size_t)(__builtin_clz((unsigned)m) >> 3);
#else
    return (size_t)(__builtin_ctz((unsigned)m) >> 3);
#endif
}

/**
 * @brief Load one word from @p p, whatever its alignment.
 *
 * The spelling is a fixed-size copy into a word because that is the only form defined for both an
 * unaligned address and a `char` array read as a wider type; a `*(const pc_swar_word *)` cast is
 * undefined on each count and traps on the stricter targets. Nothing is actually copied - at the
 * framework's own -Os this is one `mov`, then the lane math, with no call and no stack traffic.
 */
inline pc_swar_word pc_swar_load(const char *p)
{
    pc_swar_word w = 0;
    memcpy(&w, p, PC_SWAR_BYTES);
    return w;
}

/**
 * @brief Index of the first NUL in @p s within @p cap bytes, or @p cap if there is none.
 *
 * The bounded strnlen this library actually wants, a word per test instead of a byte. The caller
 * always knows the width it is willing to look at, so that width is the bound rather than a
 * sentinel search with no end.
 *
 * Per word: one load, the zero test, and - when it hits - the lane straight out of the mask. The
 * only per-byte work left is the final partial word, which cannot be loaded whole without reading
 * past @p cap.
 */
inline size_t pc_swar_scan_nul(const char *s, size_t cap)
{
    size_t i = 0;
    while (i + PC_SWAR_BYTES <= cap)
    {
        pc_swar_word m = pc_swar_has_zero(pc_swar_load(s + i));
        if (m != 0)
        {
            return i + pc_swar_zero_lane(m); // the mask states the lane; no rescan
        }
        i += PC_SWAR_BYTES;
    }
    // The final partial word only. It cannot be read as a word without touching bytes past the
    // caller's cap, which is the one bound this must never cross, so it costs at most
    // PC_SWAR_BYTES-1 compares.
    while (i < cap && s[i] != '\0')
    {
        i++;
    }
    return i;
}

#endif // PROTOCORE_SWAR_H
