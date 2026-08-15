// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file swar.c
 * @brief The lane math - see swar.h.
 *
 * One machine word treated as its byte lanes. Every body here sets the guard bit of each lane before
 * subtracting, so a borrow cannot cross out of the lane that raised it, and reads the high bits back
 * as the per-lane answer. Nothing branches and nothing walks a buffer.
 *
 * The table @ref swar names ten of these. The rest are the halves the `_sel` pair chooses between.
 */

#include "mmgr/swar.h"

/** @brief Per lane: 0x80 where the lane is >= @p v, else 0. */
protocore_swar_word protocore_swar_ge(protocore_swar_word a, protocore_swar_word v)
{
    return ((a | PROTOCORE_SWAR_HIGH) - v * PROTOCORE_SWAR_ONES) & PROTOCORE_SWAR_HIGH;
}

/** @brief Per lane: 0x80 where the lane is <= @p v, else 0. */
protocore_swar_word protocore_swar_le(protocore_swar_word a, protocore_swar_word v)
{
    return ((v * PROTOCORE_SWAR_ONES | PROTOCORE_SWAR_HIGH) - a) & PROTOCORE_SWAR_HIGH;
}

/** @brief Widen a 0x80-per-lane mask to 0xFF per lane, without carrying between lanes. */
protocore_swar_word protocore_swar_spread(protocore_swar_word m)
{
    return m + (m - (m >> 7));
}

/** @brief Per lane: (lane - @p lo) in the low 7 bits, guard bit absorbing the borrow. */
protocore_swar_word protocore_swar_sub7(protocore_swar_word a, protocore_swar_word lo)
{
    return ((a | PROTOCORE_SWAR_HIGH) - lo * PROTOCORE_SWAR_ONES) & PROTOCORE_SWAR_LOW7;
}

/**
 * @brief 0x80 in every lane of @p w that is zero, and only those.
 *
 * Exact per lane, which is dearer than the usual spelling by one operation and worth it. The cheap
 * form is `(w - ONES) & ~w & HIGH`: a lane holding 0x00 borrows into its own high bit, which is the
 * answer, but that borrow does not stop at the lane boundary and goes on to mark lanes above it that
 * hold no zero at all. A caller reading only ::protocore_swar_zero_lane never sees it, because the lowest
 * set bit is always a true one. A caller that ANDs two of these masks together, or reads any lane
 * but the first, gets a byte that is not there - `0x0100` reports both of its lanes zero.
 *
 * Adding LOW7 to the low seven bits of a lane carries into bit 7 for every value except 0x00, and
 * ORing @p w back in covers the lane that was exactly 0x80. So the guard bit ends up set on each
 * NONZERO lane, with nothing crossing between lanes, and the complement is the answer.
 */
protocore_swar_word protocore_swar_has_zero(protocore_swar_word w)
{
    return ~(((w & PROTOCORE_SWAR_LOW7) + PROTOCORE_SWAR_LOW7) | w) & PROTOCORE_SWAR_HIGH;
}

/**
 * @brief Per lane: 0x80 where the lane equals @p c, else 0.
 *
 * XOR zeroes exactly the lanes that match, so the zero test finds them. That is what lets ONE load
 * answer for as many delimiters as a caller cares about: OR the masks together and the first set
 * lane is the first occurrence of any of them. A scan per delimiter would re-load the same word once
 * per byte it is looking for, and then have to reconcile which hit came first.
 *
 *     protocore_swar_word w = swar.load(p);
 *     protocore_swar_word m = swar.eq(w, '&', PROTO_FALSE) | swar.eq(w, '=', PROTO_FALSE);
 */
protocore_swar_word protocore_swar_eq(protocore_swar_word w, uint8_t c)
{
    return protocore_swar_has_zero(w ^ (PROTOCORE_SWAR_ONES * (protocore_swar_word)c));
}

/**
 * @brief Which lane of a ::protocore_swar_has_zero mask is the first zero byte, in address order.
 *
 * The answer is one of 0..PROTOCORE_SWAR_BYTES-1 and the mask already holds it - the set guard bit IS the
 * position. Re-walking the word's bytes to find out would spend a compare per byte to recover what
 * the mask states, which is the same waste the word test just avoided.
 *
 * Address order is where byte order enters, and only here: the lowest-addressed byte is the least
 * significant lane on a little-endian load and the most significant on a big-endian one, so the
 * count is taken from the matching end. Nothing else in this module depends on the layout.
 */
size_t protocore_swar_zero_lane(protocore_swar_word m)
{
#if PROTOCORE_HW_BIG_ENDIAN
    // The count runs over the whole builtin width; the carrier sits in the low bits, so drop the pad.
    return (size_t)((PROTOCORE_SWAR_CLZ(m) - (PROTOCORE_SWAR_CLZ_WIDTH - PROTO_SWAR_BITS)) >> 3);
#else
    return (size_t)(PROTOCORE_SWAR_CTZ(m) >> 3);
#endif
}

/**
 * @brief Load one word from @p p, whatever its alignment.
 *
 * Deferred to the raw load rather than spelled here: a `*(const protocore_swar_word *)` cast is undefined
 * both for an unaligned address and for reading a `char` array as a wider type, and it traps on the
 * stricter targets. Taking the width from ::PROTOCORE_SWAR_BYTES keeps this correct when the lane carrier
 * is retyped.
 */
protocore_swar_word protocore_swar_load(const char *p)
{
    return (protocore_swar_word)proto_raw_load(p, PROTOCORE_SWAR_BYTES);
}

/**
 * @brief ::protocore_swar_load for an address the caller has already walked to a lane boundary.
 *
 * Same value, one instruction instead of a synthesized word. ::protocore_swar_load assumes nothing about
 * its address, and where PROTOCORE_HW_UNALIGNED_LOAD is 0 the compiler answers that assumption by building
 * each word out of byte loads and shifts. A caller that has already paid a prologue to reach a
 * boundary throws that away by asking for a load which disclaims alignment.
 *
 * @p p must be ::PROTOCORE_SWAR_BYTES-aligned. That is a real precondition, not a hint - a strict-alignment
 * part faults on it.
 */
protocore_swar_word protocore_swar_load_al(const char *p)
{
    return (protocore_swar_word)proto_al_load(p, PROTOCORE_SWAR_BYTES);
}

/** @brief The exact syndrome: zero in exactly the lanes where @p wa and @p wb match. */
protocore_swar_word protocore_swar_xor(protocore_swar_word wa, protocore_swar_word wb)
{
    return wa ^ wb;
}

/**
 * @brief ::protocore_swar_xor with a difference of ASCII case alone cancelled out of it.
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
 * `& ~lo` is what holds that line, and it is not decoration. ::protocore_swar_ge and ::protocore_swar_le answer
 * for 7-bit lanes: the guard bit they borrow into is already set on a lane above 0x7F, so the
 * comparison it was standing in for is gone and such a lane range-tests as a letter. 0xDB and 0xFB
 * differ by exactly the case bit and would have been accepted as a case pair - a Latin-1 or UTF-8
 * byte silently matching a different one. The mask keeps a lane out of the range test's answer
 * whenever bit 7 says the answer is not the test's to give.
 */
protocore_swar_word protocore_swar_xor_ci(protocore_swar_word wa, protocore_swar_word wb)
{
    protocore_swar_word x = wa ^ wb;
    protocore_swar_word lo = wa | (PROTOCORE_SWAR_ONES * 0x20u);
    protocore_swar_word alpha = protocore_swar_ge(lo, 'a') & protocore_swar_le(lo, 'z') & ~lo;
    return x & ~(alpha >> 2); // 0x80 per letter lane, shifted onto the case bit it is allowed to eat
}

/**
 * @brief ::protocore_swar_eq ignoring ASCII case: 0x80 per lane equal to @p c under case folding.
 *
 * The case bit cannot simply be masked out of both sides. Bit 5 is the case bit only for a letter;
 * clearing it elsewhere merges pairs that are not a case pair at all, and `'0'` (0x30) would report
 * equal to DLE (0x10). ::protocore_swar_xor_ci is the syndrome that cancels bit 5 on letter lanes only, so
 * a zero lane of it is a case-insensitive match and the same zero test reads the answer.
 */
protocore_swar_word protocore_swar_eq_ci(protocore_swar_word w, uint8_t c)
{
    return protocore_swar_has_zero(protocore_swar_xor_ci(w, PROTOCORE_SWAR_ONES * (protocore_swar_word)c));
}

// Exact and case-folding are separate tests, so the bool names which one runs rather than travelling
// into it.
protocore_swar_word protocore_swar_eq_sel(protocore_swar_word w, uint8_t c, proto_bool ci)
{
    if (ci)
    {
        return protocore_swar_eq_ci(w, c);
    }
    return protocore_swar_eq(w, c);
}

protocore_swar_word protocore_swar_xor_sel(protocore_swar_word wa, protocore_swar_word wb, proto_bool ci)
{
    if (ci)
    {
        return protocore_swar_xor_ci(wa, wb);
    }
    return protocore_swar_xor(wa, wb);
}
