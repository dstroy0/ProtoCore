// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file runops.h
 * @brief Operations over a bounded run of bytes: where it ends, where two part company,
 *        whether one occurs inside another, and moving one into a bounded destination.
 *
 * Separate from shared_primitives/swar.h because that file is the access layer and this is
 * built on it. swar.h loads a word, tests its lanes and names the lane that fired; nothing in
 * it walks a buffer or takes a capacity. Everything here does both.
 *
 * The split is what keeps swar.h's byte-order claim true. Address order decides which end a
 * lane count starts from, and in the access layer that question is asked once, in
 * ::pc_swar_zero_lane. Every other byte-order arm belongs to a walk across a word pair, which
 * is a property of stepping through a buffer rather than of reading one word, and lives here.
 *
 * Every entry point takes an explicit capacity. A bound is the caller's statement about the
 * object, not a hint: `nul_cap` is how many bytes may be read looking for a terminator,
 * `read_cap` is a promise that many bytes ARE readable, and `dst_cap` is what a write may fill.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_RUNOPS_H
#define PROTOCORE_RUNOPS_H

#include "shared_primitives/swar.h" // the access layer every operation below is built on

/**
 * @brief Index of the first NUL in @p s within @p nul_cap bytes, or @p nul_cap if there is none.
 *
 * The bounded strnlen this library actually wants, a word per test instead of a byte.
 *
 * **@p nul_cap is a willingness, not a promise.** It says how far this is allowed to keep looking
 * for a terminator, and nothing about how much of @p s exists: `proto_scan_nul(payload, 0xFFFF)`
 * over a five-byte literal is the intended use. That is the opposite of the `read_cap` every
 * compare in this file takes, and the two are spelled differently for exactly that reason.
 *
 * Per word: one load, the zero test, and - when it hits - the lane straight out of the mask. The
 * only per-byte work left is the two partial words at the ends, which cannot be loaded whole
 * without reading outside the string.
 */
static inline size_t proto_scan_nul(const char *s, size_t nul_cap)
{
    // Walk to the boundary a byte at a time. A masked aligned load of the partial first word does
    // the same job, but it is a load, a shift, a NOT, an OR, a zero test and a lane extract against
    // at most PC_SWAR_BYTES-1 byte compares that often do not run at all.
    //
    // Alignment is what makes the word loop safe to read past the terminator: an aligned load lies
    // wholly inside one machine word, so it only touches bytes the string's own page already covers.
    size_t i = 0;
    while (i < nul_cap && ((uintptr_t)(s + i) & (PC_SWAR_BYTES - 1u)) != 0u)
    {
        if (s[i] == '\0')
        {
            return i;
        }
        ++i;
    }
    while (i + PC_SWAR_BYTES <= nul_cap)
    {
        pc_swar_word m = pc_swar_has_zero(pc_swar_load_al(s + i));
        if (m != 0)
        {
            return i + pc_swar_zero_lane(m); // the mask states the lane; no rescan
        }
        i += PC_SWAR_BYTES;
    }
    // The final partial word, walked a byte at a time so the search cannot end past the bound. A
    // whole aligned load here would never fault, since an aligned word cannot straddle a page, but it
    // reads bytes the caller did not offer: `nul_cap` is the extent of the object, not a hint about
    // where the answer is, and a needle passed by value is exactly as long as it says.
    while (i < nul_cap && s[i] != '\0')
    {
        ++i;
    }
    return i;
}
/**
 * @brief Index of the first byte where @p a and @p b differ, or @p read_cap if they agree throughout.
 *
 * The other half of the scan: ::proto_scan_nul answers "where does this string end", this answers
 * "where do these two part company". XOR zeroes every lane that matches, so the lanes that differ are
 * exactly the nonzero ones and the first of them is the answer - one load pair and one test per word
 * rather than a compare per byte.
 *
 * **@p read_cap is a promise, not a willingness.** Both @p a and @p b must hold that many readable
 * bytes, because this never looks for a terminator and so has nothing else to stop it: a caller
 * compares a fixed field, a prefix, or a whole string by choosing the bound. Handing it a bound
 * longer than either operand is an out-of-bounds read, which is the mistake ::proto_scan_nul's
 * `nul_cap` is specifically allowed to make and this is not.
 *
 * Stops at the first difference, so it is not constant time - never compare a secret with it (see
 * ::pc_swar_eq for the branchless lane tests that are).
 */
static inline size_t proto_diff(const char *a, const char *b, size_t read_cap)
{
    size_t i = 0;
    while (i + PC_SWAR_BYTES <= read_cap)
    {
        pc_swar_word d = pc_swar_load(a + i) ^ pc_swar_load(b + i);
        if (d != 0)
        {
            // Guard bit set on every lane that differs, so the same lane reader that serves the NUL
            // scan states the position. `~has_zero` because has_zero marks the lanes that MATCH.
            return i + pc_swar_zero_lane(PC_SWAR_HIGH & ~pc_swar_has_zero(d));
        }
        i += PC_SWAR_BYTES;
    }
    while (i < read_cap && a[i] == b[i])
    {
        ++i;
    }
    return i;
}
/**
 * @brief ::proto_diff ignoring ASCII case: the first byte where they differ, or @p read_cap.
 *
 * @p read_cap carries ::proto_diff's contract unchanged: both operands must hold that many
 * readable bytes. Not constant time, for the same reason ::proto_diff is not.
 */
static inline size_t proto_diff_ci(const char *a, const char *b, size_t read_cap)
{
    size_t i = 0;
    while (i + PC_SWAR_BYTES <= read_cap)
    {
        pc_swar_word d = pc_swar_xor_ci(pc_swar_load(a + i), pc_swar_load(b + i));
        if (d != 0)
        {
            return i + pc_swar_zero_lane(PC_SWAR_HIGH & ~pc_swar_has_zero(d));
        }
        i += PC_SWAR_BYTES;
    }
    // The tail runs the same lane math on a one-lane word, so the 0x20 rule is spelled once: the
    // empty lanes are 0x00 on both sides and cancel, leaving only the byte in lane 0 to answer.
    while (i < read_cap && pc_swar_xor_ci((pc_swar_word)(unsigned char)a[i], (pc_swar_word)(unsigned char)b[i]) == 0)
    {
        ++i;
    }
    return i;
}
/**
 * @brief One word of ::proto_agree's comparison, given the two words already loaded.
 *
 * One rule for two load shapes, so both loops decide alike. Whichever lane fires lower settles it,
 * so nothing past this word is read: an end strictly below the difference means they agreed the
 * whole way, and the other side ends there too, because agreeing at the terminator means both hold
 * one.
 */
PC_INLINE int proto_step_word(pc_swar_word wa, pc_swar_word wb, int ci, int end_wins)
{
    pc_swar_word x = ci ? pc_swar_xor_ci(wa, wb) : (wa ^ wb);
    pc_swar_word z = pc_swar_has_zero(wa);
    if ((x | z) == 0)
    {
        return PC_SWAR_GO;
    }
    // Neither mask firing is reported as one lane past the end, so the comparison reads the same
    // whether the word ran out of string, ran out of agreement, or neither.
    size_t dl = (x != 0) ? pc_swar_zero_lane(PC_SWAR_HIGH & ~pc_swar_has_zero(x)) : PC_SWAR_BYTES;
    size_t el = (z != 0) ? pc_swar_zero_lane(z) : PC_SWAR_BYTES;
    return (end_wins ? (el <= dl) : (el < dl)) ? PC_SWAR_YES : PC_SWAR_NO;
}
/** @brief ::proto_step_word for one byte, serving the head and the tail. Same rule, one lane. */
PC_INLINE int proto_step_byte(unsigned char ca, unsigned char cb, int ci, int end_wins)
{
    pc_swar_word d = ci ? pc_swar_xor_ci((pc_swar_word)ca, (pc_swar_word)cb) : (pc_swar_word)(ca ^ cb);
    if (ca == 0)
    {
        return ((d == 0) || (end_wins != 0)) ? PC_SWAR_YES : PC_SWAR_NO;
    }
    if (d != 0)
    {
        return PC_SWAR_NO;
    }
    return PC_SWAR_GO;
}
/**
 * @brief Do @p a and @p b agree up to where the deciding one ends? The core of all four compares.
 *
 * One pass, which is the whole point of it being one function. Measuring @p a, then measuring @p b,
 * then comparing them is three walks to answer what one walk decides, and it is worst on the case
 * these serve most: a field name that is NOT the one being sought disagrees at byte 0, so the two
 * measuring walks are spent in full to learn nothing.
 *
 * Per word there are two questions and both come out of the same load pair: where do they part
 * (the syndrome), and where does the string end (the zero test). Whichever fires in a lower lane
 * settles it, so nothing beyond that word is read:
 *
 *   - end lane strictly below the difference lane -> they agreed the whole way, and the other side
 *     ends there too, because agreeing at the terminator means both hold one.
 *   - difference lane at or below the end lane -> they part before either ran out.
 *
 * @p end_wins is the one bit of behavior that separates the two shapes, and it is the `<` above.
 * Equality needs the terminator strictly first: a difference AT the terminator means one string
 * carries a byte the other does not, so it is longer and they are not equal. A prefix test wants
 * the opposite reading of that same lane - the pattern ended, whatever the subject does next - so
 * it passes when the lanes tie. Both are compiled with a literal, so neither test survives inlining.
 *
 * @p ci selects the syndrome, exact or ::pc_swar_xor_ci, and is a literal for the same reason.
 *
 * @p read_cap is ::proto_diff's promise, not ::proto_scan_nul's willingness: both operands must
 * hold that many readable bytes. Comparing a fixed field against a literal means passing
 * `sizeof(the literal)`, not the field's capacity.
 */
static inline proto_bool proto_agree(const char *a, const char *b, size_t read_cap, int ci, int end_wins)
{
    // `a` is the side whose terminator ends the comparison, and the side that gets aligned - a
    // register holds one pointer, so only one of the two can be walked to a boundary. At nearly
    // every call site `b` is a literal the compiler answers with immediates, so `a` is the only
    // real pointer in the loop.
    //
    // One loop, deciding in place: a separate prologue would hold the same byte compare twice.
    size_t i = 0;
    while (i < read_cap)
    {
        if (((uintptr_t)(a + i) & (PC_SWAR_BYTES - 1u)) == 0u && i + PC_SWAR_BYTES <= read_cap)
        {
            pc_swar_word wa = pc_swar_load_al(a + i);
            pc_swar_word wb = pc_swar_load(b + i);
            pc_swar_word x = ci ? pc_swar_xor_ci(wa, wb) : (wa ^ wb);
            pc_swar_word z = pc_swar_has_zero(wa);
            if ((x | z) != 0)
            {
                // Which mask fires FIRST. That is not a question about positions, so nothing here
                // computes one: a lane's guard bit IS its position, and `(v - 1) & ~v` leaves every
                // bit BELOW the lowest set one - a value that grows with the position it describes.
                // Comparing those two values compares the two positions.
                //
                // A mask that never fires falls out of the same expression rather than being tested
                // for: v == 0 gives all ones, the largest value there is, which is exactly "fires
                // after everything". So there is no case for "no terminator in this word", no case
                // for "no difference", and no branch for either.
                pc_swar_word xm = PC_SWAR_HIGH & ~pc_swar_has_zero(x); // lanes that differ, guard bits
                pc_swar_word zl = (z - (pc_swar_word)1) & ~z;
                pc_swar_word xl = (xm - (pc_swar_word)1) & ~xm;
#if PC_HW_BIG_ENDIAN
                // Lowest address is the HIGHEST bit here, so first-to-fire is the LARGER value.
                return end_wins ? (zl >= xl) : (zl > xl);
#else
                return end_wins ? (zl <= xl) : (zl < xl);
#endif
            }
            i += PC_SWAR_BYTES;
            continue;
        }
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        pc_swar_word d = ci ? pc_swar_xor_ci((pc_swar_word)ca, (pc_swar_word)cb) : (pc_swar_word)(ca ^ cb);
        if (ca == 0)
        {
            return (d == 0) || (end_wins != 0);
        }
        if (d != 0)
        {
            return PROTO_FALSE;
        }
        ++i;
    }
    // The bound ran out with no terminator and no disagreement. Equality cannot be claimed for a
    // string this never saw the end of; a prefix that filled the whole bound has matched.
    return end_wins != 0;
}
/**
 * @brief True if @p a and @p b are the same NUL-terminated string within @p read_cap bytes.
 *
 * The bounded strcmp-for-equality this library wants. An unbounded compare walks until it finds a
 * terminator, so a buffer that never got one is read past its end; @p read_cap is the bound instead,
 * and both sides must hold that many readable bytes. A prefix can never pass as the whole, because
 * the terminator has to arrive strictly before the first disagreement.
 */
static inline proto_bool proto_eq_str(const char *a, const char *b, size_t read_cap)
{
    return proto_agree(a, b, read_cap, 0, 0);
}
/** @brief True if @p s begins with @p pre, both holding @p read_cap readable bytes. */
static inline proto_bool proto_starts(const char *s, const char *pre, size_t read_cap)
{
    return proto_agree(pre, s, read_cap, 0, 1); // the pattern's terminator is what ends the compare
}
/** @brief ::proto_eq_str, ignoring ASCII case. */
static inline proto_bool proto_eq_str_ci(const char *a, const char *b, size_t read_cap)
{
    return proto_agree(a, b, read_cap, 1, 0);
}
/** @brief ::proto_starts, ignoring ASCII case. */
static inline proto_bool proto_starts_ci(const char *s, const char *pre, size_t read_cap)
{
    return proto_agree(pre, s, read_cap, 1, 1);
}

/**
 * @brief One byte against one byte, optionally ignoring ASCII case.
 *
 * The scalar edge of ::pc_swar_eq_ci, for the positions a whole word cannot cover. Folding bit 5 is
 * only valid when the folded value names a letter, which is the test the range check makes; without
 * it `'0'` (0x30) and DLE (0x10) fold together.
 */
PC_INLINE int proto_byte_same(uint8_t a, uint8_t b, int ci)
{
    if (a == b)
    {
        return 1;
    }
    if (ci == 0)
    {
        return 0;
    }
    uint8_t la = (uint8_t)(a | 0x20u);
    uint8_t lb = (uint8_t)(b | 0x20u);
    return (la == lb && la >= 'a' && la <= 'z') ? 1 : 0;
}

/**
 * @brief First occurrence of @p needle in @p hay within @p read_cap bytes, or NULL.
 *
 * **Not fixed-work.** Its cost depends on how many candidate lanes the haystack happens to hold, so
 * the worst case is a property of the input rather than a number that can be stated before flashing.
 * ::proto_has asks the same scan for a bool and carries the same caveat.
 *
 * The haystack is walked once. One load answers both questions a step has: ::pc_swar_eq marks every
 * lane holding the needle's first byte, ::pc_swar_has_zero marks the lane the haystack ends on, and
 * a word with neither costs that single load before the search steps a whole word past it. Measuring
 * the haystack first, to know where to stop, is a second full walk over the same bytes to learn what
 * this load already reports.
 *
 * A candidate is then settled by one load and a masked difference: XOR the haystack word against the
 * needle word and AND a mask covering only the needle's own bytes, so zero means the whole needle
 * matched. That test cannot run off the end of the haystack, which is why no length is needed to
 * bound it: a needle byte is never NUL (the needle stops at its own first one), so a lane where the
 * haystack has terminated always differs and always fails the match.
 *
 * The mask is one shift. Every lane it has to cover is contiguous from the needle's first byte, so
 * sliding a word of all-ones until only that run survives states the whole thing in a single
 * operation, with nothing in rodata to load and nothing to index. The load width is the largest
 * power of two the needle is known to cover, which is what keeps the read inside a needle shorter
 * than a word; a longer needle has its head settled by the mask and its tail by the ordinary
 * compare.
 *
 * @p read_cap is ::proto_diff's promise: that many bytes of @p hay are readable. Every wide read
 * below is guarded against it, and a candidate too close to the bound for one falls back to bytes.
 *
 * An empty needle matches at the start, which is what a search for nothing means.
 */
PC_INLINE const char *proto_find_impl(const char *hay, size_t read_cap, const char *needle, size_t needle_cap, int ci)
{
    // The load width is the largest power of two at or under `needle_cap`, which the caller states as
    // the count of readable bytes, so the load is inside the object by that promise alone.
    const size_t w = (needle_cap >= PC_SWAR_BYTES) ? PC_SWAR_BYTES
                     : (needle_cap >= 4u)          ? 4u
                     : (needle_cap >= 2u)          ? 2u
                                                   : 1u;

    // The ingest is the first operation, and it answers the length as well as supplying the bytes:
    // a zero lane at index j < w means needle[j] is the terminator, so nlen is j. The mask only
    // decides the length when the terminator lies inside the loaded window; at j == w the needle may
    // continue past it and the bound is read the long way.
    const pc_swar_word n_raw = (pc_swar_word)proto_raw_load(needle, w);
    const pc_swar_word nz = pc_swar_has_zero(n_raw);
    // With no terminator in the window, what is left is [w, needle_cap). A single byte there can only
    // be the terminator, so nlen is w. Anything longer is read the long way.
    const size_t j0 = (nz != 0) ? pc_swar_zero_lane(nz) : PC_SWAR_BYTES;
    const size_t nlen = (j0 < w) ? j0 : ((needle_cap - w == 1u) ? w : proto_scan_nul(needle, needle_cap));
    if (nlen == 0)
    {
        return hay;
    }

    const size_t take = (nlen < w) ? nlen : w; // needle bytes the masked compare settles

    // Slide a word of all-ones down until only `take` bytes are left standing. Which end the run is
    // anchored at is the one thing a load's byte order decides: the first byte of a w-byte load is
    // the low byte of the value on a little-endian machine and the top byte of the w-byte field on a
    // big-endian one, so the big-endian arm shifts the surviving run back up to meet it.
    // Built at the carrier's own width, because a 64-bit shift by a runtime amount is a call into
    // libgcc on every target narrower than 64, for a mask that never leaves one word. The complement
    // is truncated back to the carrier BEFORE the shift: a carrier narrower than `int` promotes, and
    // an all-ones `int` is -1, whose right shift is arithmetic and refills the bits just cleared.
    const pc_swar_word all = (pc_swar_word) ~(pc_swar_word)0;
#if PC_HW_BIG_ENDIAN
    const pc_swar_word nm = (pc_swar_word)((all >> (PC_SWAR_BYTES * 8u - take * 8u)) << ((w - take) * 8u));
#else
    const pc_swar_word nm = (pc_swar_word)(all >> (PC_SWAR_BYTES * 8u - take * 8u));
#endif
    const pc_swar_word nw = n_raw & nm;

    // One splatted byte. Folding the needle's last byte in too thins the candidates only where the
    // first byte is dense, and pays an extra load per word everywhere else; the dense case wants no
    // position back and is ::proto_has's.
    const uint8_t c_first = (uint8_t)needle[0];

    // The walk's anchor. A funnel by `ka` can only reach lane j+ka out of the loaded pair while
    // ka stays under one word, which is the clamp; within that the middle byte is taken.
    const size_t ka = ((nlen / 2u) < PC_SWAR_BYTES) ? (nlen / 2u) : (PC_SWAR_BYTES - 1u);
    const uint8_t c_anchor = (uint8_t)needle[ka];

    size_t i = 0;

    // Head: positions before the first word boundary, one byte at a time.
    while (i < read_cap && ((uintptr_t)(hay + i) & (PC_SWAR_BYTES - 1u)) != 0u)
    {
        if (hay[i] == '\0')
        {
            return NULL;
        }
        if (proto_byte_same((uint8_t)hay[i], c_first, ci))
        {
            size_t j = 0;
            while (j < nlen && i + j < read_cap && proto_byte_same((uint8_t)hay[i + j], (uint8_t)needle[j], ci))
            {
                ++j;
            }
            if (j == nlen)
            {
                return hay + i;
            }
        }
        ++i;
    }

    // A start at lane j needs needle[k] at lane j+k, and eq(w, needle[k]) shifted down k lanes has
    // lane j set exactly when that holds. ANDing those shifted masks over k leaves a lane set at
    // every start the whole needle lands on, so one chain of small operations decides all
    // PC_SWAR_BYTES positions at once and there is no candidate to walk.
    //
    // One needle byte needs no funnel, no lookahead word and no extent mask: the match mask and the
    // terminator mask are the same shape, so whichever names the lower lane settles the answer.
    if (nlen == 1u)
    {
        while (i + PC_SWAR_BYTES <= read_cap)
        {
            pc_swar_word w0 = pc_swar_load_al(hay + i);
            pc_swar_word z = pc_swar_has_zero(w0);
            pc_swar_word m = ci ? pc_swar_eq_ci(w0, c_first) : pc_swar_eq(w0, c_first);
            if ((m | z) != 0)
            {
                // Both cannot name one lane: a needle byte of 0 would have made nlen 0 above.
                size_t km = (m != 0) ? pc_swar_zero_lane(m) : PC_SWAR_BYTES;
                size_t kz = (z != 0) ? pc_swar_zero_lane(z) : PC_SWAR_BYTES;
                return (km < kz) ? (hay + i + km) : NULL;
            }
            i += PC_SWAR_BYTES;
        }
    }

    // Two needle bytes, decided for every lane at once: a start at lane j needs needle[1] at lane
    // j+1, and eq(w, needle[1]) shifted down one lane has lane j set exactly when that holds, so
    // ANDing it into the first mask leaves a lane set at every start the pair lands on.
    //
    // Then mask for the extent out of the word already loaded: lanes at or past the terminator are
    // not start positions. On a little-endian load lane order is bit order, so (z-1) & ~z is exactly
    // the lanes below the first zero, and all-ones when there is none.
    //
    // Only at two. This chain costs nlen identifies and nlen-1 funnels per word whatever the input
    // holds; the walk below pays per candidate lane instead, which is the cheaper of the two once the
    // needle is longer than a pair.
    while (nlen >= 2u && nlen <= 3u && nlen <= PC_SWAR_BYTES && i + (2u * PC_SWAR_BYTES) <= read_cap)
    {
        pc_swar_word w0 = pc_swar_load_al(hay + i);
        pc_swar_word w1 = pc_swar_load_al(hay + i + PC_SWAR_BYTES);
        pc_swar_word m = ci ? pc_swar_eq_ci(w0, c_first) : pc_swar_eq(w0, c_first);
        for (size_t k = 1u; k < nlen; ++k)
        {
#if PC_HW_BIG_ENDIAN
            pc_swar_word fk = (pc_swar_word)((w0 << (8u * k)) | (w1 >> (PROTO_SWAR_BITS - 8u * k)));
            m &= ci ? pc_swar_eq_ci(fk, (uint8_t)needle[k]) : pc_swar_eq(fk, (uint8_t)needle[k]);
#else
            pc_swar_word fk = (pc_swar_word)((w0 >> (8u * k)) | (w1 << (PROTO_SWAR_BITS - 8u * k)));
            m &= ci ? pc_swar_eq_ci(fk, (uint8_t)needle[k]) : pc_swar_eq(fk, (uint8_t)needle[k]);
#endif
        }
        pc_swar_word z = pc_swar_has_zero(w0);
#if PC_HW_BIG_ENDIAN
        const size_t zend = (z != 0) ? pc_swar_zero_lane(z) : PC_SWAR_BYTES;
        m &= (zend == PC_SWAR_BYTES) ? all : (pc_swar_word)(all << (PROTO_SWAR_BITS - 8u * zend));
#else
        m &= (pc_swar_word)((z - 1u) & ~z);
#endif
        if (m != 0)
        {
            return hay + i + pc_swar_zero_lane(m);
        }
        if (z != 0)
        {
            return NULL;
        }
        i += PC_SWAR_BYTES;
    }

    // Body: `i` only ever moves a whole word, so every load is aligned. Candidates inside a word are
    // walked out of the mask rather than by stepping the pointer to each one, and a candidate is
    // verified against a funnel of the two loaded words instead of a fresh unaligned read.
    while ((nlen > 3u || nlen > PC_SWAR_BYTES) && nlen >= 2u && i + (2u * PC_SWAR_BYTES) <= read_cap)
    {
        pc_swar_word w0 = pc_swar_load_al(hay + i);
        pc_swar_word w1 = pc_swar_load_al(hay + i + PC_SWAR_BYTES);
        pc_swar_word z = pc_swar_has_zero(w0);
        // eq against the anchor's own funnel sets lane j when hay[i+j+ka] is needle[ka], which is
        // the needle STARTING at j whatever ka is. So which byte anchors is free, and it decides how
        // many candidates the verify below runs on. The first byte of a pattern is where a delimiter
        // or a capital sits, and both are dense; the middle byte is the one carrying content.
#if PC_HW_BIG_ENDIAN
        pc_swar_word wa = (ka == 0u) ? w0 : (pc_swar_word)((w0 << (8u * ka)) | (w1 >> (PROTO_SWAR_BITS - 8u * ka)));
#else
        pc_swar_word wa = (ka == 0u) ? w0 : (pc_swar_word)((w0 >> (8u * ka)) | (w1 << (PROTO_SWAR_BITS - 8u * ka)));
#endif
        pc_swar_word m = ci ? pc_swar_eq_ci(wa, c_anchor) : pc_swar_eq(wa, c_anchor);
        size_t end = (z != 0) ? pc_swar_zero_lane(z) : PC_SWAR_BYTES;

        while (m != 0)
        {
            size_t k = pc_swar_zero_lane(m);
            if (k >= end)
            {
                break; // this candidate is at or past the terminator
            }
            // The word whose lane 0 is hay[i+k], built from the pair already in registers.
            pc_swar_word wk = w0;
            if (k != 0)
            {
#if PC_HW_BIG_ENDIAN
                wk = (pc_swar_word)((w0 << (8u * k)) | (w1 >> (PROTO_SWAR_BITS - 8u * k)));
#else
                wk = (pc_swar_word)((w0 >> (8u * k)) | (w1 << (PROTO_SWAR_BITS - 8u * k)));
#endif
            }
            pc_swar_word syn = ci ? pc_swar_xor_ci(wk, nw) : (pc_swar_word)(wk ^ nw);
            size_t rest = nlen - take;
            if ((syn & nm) == 0 &&
                (take == nlen ||
                 (i + k + nlen <= read_cap && (ci ? proto_diff_ci(hay + i + k + take, needle + take, rest)
                                                  : proto_diff(hay + i + k + take, needle + take, rest)) == rest)))
            {
                return hay + i + k;
            }
            // Drop this lane and look at the next one in address order.
#if PC_HW_BIG_ENDIAN
            m &= (pc_swar_word) ~((pc_swar_word)1 << (PC_SWAR_CLZ_WIDTH - 1u - (unsigned)PC_SWAR_CLZ(m)));
#else
            m &= (pc_swar_word)(m - 1u);
#endif
        }

        if (z != 0)
        {
            return NULL;
        }
        i += PC_SWAR_BYTES;
    }

    // The body stops with up to 2*PC_SWAR_BYTES-1 bytes left, because it needs a lookahead word it
    // can no longer reach. A whole aligned load is still in bounds while PC_SWAR_BYTES of them
    // remain, and `i` is on a boundary, so the start positions whose needle lies entirely inside
    // that one word take the same shape the body used. Shifting the word down brings zeros in above
    // it, and a needle byte is never zero, so the positions that would straddle into the next word
    // fail here and are left to the byte loop. That is PC_SWAR_BYTES - nlen + 1 positions settled by
    // one load instead of by nlen byte compares each.
    if (nlen <= PC_SWAR_BYTES && i + PC_SWAR_BYTES <= read_cap)
    {
        pc_swar_word w0 = pc_swar_load_al(hay + i);
        pc_swar_word z = pc_swar_has_zero(w0);
        pc_swar_word m = ci ? pc_swar_eq_ci(w0, c_first) : pc_swar_eq(w0, c_first);
        for (size_t k = 1u; k < nlen; ++k)
        {
#if PC_HW_BIG_ENDIAN
            pc_swar_word fk = (pc_swar_word)(w0 << (8u * k));
#else
            pc_swar_word fk = (pc_swar_word)(w0 >> (8u * k));
#endif
            m &= ci ? pc_swar_eq_ci(fk, (uint8_t)needle[k]) : pc_swar_eq(fk, (uint8_t)needle[k]);
        }
#if PC_HW_BIG_ENDIAN
        const size_t zend = (z != 0) ? pc_swar_zero_lane(z) : PC_SWAR_BYTES;
        m &= (zend == PC_SWAR_BYTES) ? all : (pc_swar_word)(all << (PROTO_SWAR_BITS - 8u * zend));
#else
        m &= (pc_swar_word)((z - 1u) & ~z);
#endif
        if (m != 0)
        {
            return hay + i + pc_swar_zero_lane(m);
        }
        if (z != 0)
        {
            return NULL;
        }
        i += PC_SWAR_BYTES - nlen + 1u;
    }

    // What is left is shorter than a word, or straddles into one that is not there.
    while (i < read_cap && hay[i] != '\0')
    {
        if (proto_byte_same((uint8_t)hay[i], c_first, ci))
        {
            size_t j = 1u;
            while (j < nlen && i + j < read_cap && proto_byte_same((uint8_t)hay[i + j], (uint8_t)needle[j], ci))
            {
                ++j;
            }
            if (j == nlen)
            {
                return hay + i;
            }
        }
        ++i;
    }
    return NULL;
}
/**
 * @brief First occurrence of @p needle in @p hay within @p read_cap bytes, or NULL.
 *
 * The search above branches on the needle's length, because the cheapest shape is a function of it:
 * one byte needs no funnel, no lookahead word and no extent mask; a pair is settled for every start
 * position in a word at once; past that the anchor-plus-verify walk costs less than a mask chain
 * whose length grows with the needle. @p needle_cap is a literal at the call site, so the arms the
 * caller does not take are dead.
 */
PC_INLINE const char *proto_find(const char *hay, size_t read_cap, const char *needle, size_t needle_cap)
{
    return proto_find_impl(hay, read_cap, needle, needle_cap, 0);
}
/** @brief ::proto_find ignoring ASCII case. Same dispatch; the byte test folds bit 5 on letters. */
PC_INLINE const char *proto_find_ci(const char *hay, size_t read_cap, const char *needle, size_t needle_cap)
{
    return proto_find_impl(hay, read_cap, needle, needle_cap, 1);
}
/**
 * @brief True if @p needle occurs anywhere in @p hay within @p read_cap bytes.
 *
 * The same scan ::proto_find runs, asked for less: whether a needle is present and where it is are
 * one question over one pass, so both come from one loop.
 *
 * NOT constant time, for ::proto_scan_nul's reason: it stops at the match. Never use it on a secret.
 */
static inline proto_bool proto_has(const char *hay, size_t read_cap, const char *needle, size_t needle_cap)
{
    return proto_find_impl(hay, read_cap, needle, needle_cap, 0) != NULL;
}
/** @brief ::proto_has ignoring ASCII case. */
static inline proto_bool proto_has_ci(const char *hay, size_t read_cap, const char *needle, size_t needle_cap)
{
    return proto_find_impl(hay, read_cap, needle, needle_cap, 1) != NULL;
}
/**
 * @brief Copy the NUL-terminated @p src into @p dst, which holds @p dst_cap bytes. Returns the length.
 *
 * The bounded copy without strncpy's two failure modes: it always terminates (strncpy leaves the
 * destination unterminated when the source fills it, so the next read runs off the end), and it
 * writes only the bytes it copies (strncpy pads the remainder with NULs, which costs the whole
 * capacity on every short copy). The move itself goes through the raw mover, which steps the bus
 * width. A @p dst_cap of 0 writes nothing.
 *
 * The bound belongs to the DESTINATION, which is why it is neither of the other two names in this
 * file: nothing is claimed about how long @p src is. It reaches the scan as a `nul_cap`, a
 * willingness to look that far, so a shorter source is simply copied whole.
 */
static inline size_t proto_copy(char *dst, const char *src, size_t dst_cap)
{
    if (dst_cap == 0)
    {
        return 0;
    }
    size_t n = proto_scan_nul(src, dst_cap - 1);
    proto_raw_read(dst, src, n);
    dst[n] = '\0';
    return n;
}

#endif // PROTOCORE_RUNOPS_H
