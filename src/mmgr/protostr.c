// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protostr.c
 * @brief The bounded-run operations and their one owner. See protostr.h.
 *
 * Built on mmgr/swar.h, which is the access layer: it loads a word, tests its lanes and
 * names the lane that fired, and nothing in it walks a buffer or takes a capacity. Everything here
 * does both.
 *
 * That split is what keeps swar.h's byte-order claim true. Address order decides which end a lane
 * count starts from, and in the access layer that question is asked once, in ::pc_swar_zero_lane.
 * Every other byte-order arm belongs to a walk across a word pair, which is a property of stepping
 * through a buffer rather than of reading one word, and lives here.
 *
 * Exact and case-folding are separate walks, not one walk carrying a flag: the syndrome, the byte
 * test and the lane identify are different operations in each. The public entry picks between them
 * once; below that point there is nothing to select and no flag to test.
 *
 * The one symbol this file exports is @ref str.
 */

#include "mmgr/protostr.h"
#include "mmgr/swar.h" // the access layer every operation below is built on

static inline size_t len(const char *s, size_t nul_cap)
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
        pc_swar_word m = swar.has_zero(swar.load_al(s + i));
        if (m != 0)
        {
            return i + swar.zero_lane(m); // the mask states the lane; no rescan
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
 * @brief One byte against one byte, folding ASCII case.
 *
 * The scalar edge of ::pc_swar_eq_ci, for the positions a whole word cannot cover. Folding bit 5 is
 * only valid when the folded value names a letter, which is the test the range check makes; without
 * it `'0'` (0x30) and DLE (0x10) fold together. The exact walk has no counterpart to this: there the
 * test is `==`.
 */
PC_INLINE int byte_same_ci(uint8_t a, uint8_t b)
{
    if (a == b)
    {
        return 1;
    }
    uint8_t la = (uint8_t)(a | 0x20u);
    uint8_t lb = (uint8_t)(b | 0x20u);
    if (la != lb)
    {
        return 0;
    }
    if (la < 'a')
    {
        return 0;
    }
    if (la > 'z')
    {
        return 0;
    }
    return 1;
}

static inline size_t diff_cs(const char *a, const char *b, size_t read_cap)
{
    size_t i = 0;
    while (i + PC_SWAR_BYTES <= read_cap)
    {
        pc_swar_word d = swar.load(a + i) ^ swar.load(b + i);
        if (d != 0)
        {
            // Guard bit set on every lane that differs, so the same lane reader that serves the NUL
            // scan states the position. `~has_zero` because has_zero marks the lanes that MATCH.
            return i + swar.zero_lane(PC_SWAR_HIGH & ~swar.has_zero(d));
        }
        i += PC_SWAR_BYTES;
    }
    while (i < read_cap && a[i] == b[i])
    {
        ++i;
    }
    return i;
}

static inline size_t diff_ci(const char *a, const char *b, size_t read_cap)
{
    size_t i = 0;
    while (i + PC_SWAR_BYTES <= read_cap)
    {
        pc_swar_word d = swar.xor_(swar.load(a + i), swar.load(b + i), PROTO_TRUE);
        if (d != 0)
        {
            return i + swar.zero_lane(PC_SWAR_HIGH & ~swar.has_zero(d));
        }
        i += PC_SWAR_BYTES;
    }
    // The tail runs the same lane math on a one-lane word, so the 0x20 rule is spelled once: the
    // empty lanes are 0x00 on both sides and cancel, leaving only the byte in lane 0 to answer.
    while (i < read_cap &&
           swar.xor_((pc_swar_word)(unsigned char)a[i], (pc_swar_word)(unsigned char)b[i], PROTO_TRUE) == 0)
    {
        ++i;
    }
    return i;
}

/**
 * @brief One word of the agreement test, given the two words already loaded.
 *
 * One rule for two load shapes, so both loops decide alike. Whichever lane fires lower settles it,
 * so nothing past this word is read: an end strictly below the difference means they agreed the
 * whole way, and the other side ends there too, because agreeing at the terminator means both hold
 * one. Neither mask firing is reported as one lane past the end, so the comparison reads the same
 * whether the word ran out of string, ran out of agreement, or neither.
 *
 * A walk built on this steps a whole word per decision, which is what a byte loop does not.
 */
PC_INLINE int step_word_cs(pc_swar_word wa, pc_swar_word wb, int end_wins)
{
    pc_swar_word x = wa ^ wb;
    pc_swar_word z = swar.has_zero(wa);
    if ((x | z) == 0)
    {
        return PC_SWAR_GO;
    }
    size_t dl = PC_SWAR_BYTES;
    if (x != 0)
    {
        dl = swar.zero_lane(PC_SWAR_HIGH & ~swar.has_zero(x));
    }
    size_t el = PC_SWAR_BYTES;
    if (z != 0)
    {
        el = swar.zero_lane(z);
    }
    if (end_wins)
    {
        if (el <= dl)
        {
            return PC_SWAR_YES;
        }
        return PC_SWAR_NO;
    }
    if (el < dl)
    {
        return PC_SWAR_YES;
    }
    return PC_SWAR_NO;
}

/** @brief ::step_word_cs over the case-folded syndrome. Same decision, a different comparison. */
PC_INLINE int step_word_ci(pc_swar_word wa, pc_swar_word wb, int end_wins)
{
    pc_swar_word x = swar.xor_(wa, wb, PROTO_TRUE);
    pc_swar_word z = swar.has_zero(wa);
    if ((x | z) == 0)
    {
        return PC_SWAR_GO;
    }
    size_t dl = PC_SWAR_BYTES;
    if (x != 0)
    {
        dl = swar.zero_lane(PC_SWAR_HIGH & ~swar.has_zero(x));
    }
    size_t el = PC_SWAR_BYTES;
    if (z != 0)
    {
        el = swar.zero_lane(z);
    }
    if (end_wins)
    {
        if (el <= dl)
        {
            return PC_SWAR_YES;
        }
        return PC_SWAR_NO;
    }
    if (el < dl)
    {
        return PC_SWAR_YES;
    }
    return PC_SWAR_NO;
}

/** @brief ::step_word_cs for one byte, serving the head and the tail. Same rule, one lane. */
PC_INLINE int step_byte_cs(unsigned char ca, unsigned char cb, int end_wins)
{
    if (ca == 0)
    {
        if (ca == cb)
        {
            return PC_SWAR_YES;
        }
        if (end_wins != 0)
        {
            return PC_SWAR_YES;
        }
        return PC_SWAR_NO;
    }
    if (ca != cb)
    {
        return PC_SWAR_NO;
    }
    return PC_SWAR_GO;
}

/** @brief ::step_byte_cs over the case-folded syndrome. */
PC_INLINE int step_byte_ci(unsigned char ca, unsigned char cb, int end_wins)
{
    pc_swar_word d = swar.xor_((pc_swar_word)ca, (pc_swar_word)cb, PROTO_TRUE);
    if (ca == 0)
    {
        if (d == 0)
        {
            return PC_SWAR_YES;
        }
        if (end_wins != 0)
        {
            return PC_SWAR_YES;
        }
        return PC_SWAR_NO;
    }
    if (d != 0)
    {
        return PC_SWAR_NO;
    }
    return PC_SWAR_GO;
}

/**
 * @brief Do @p a and @p b agree up to where the deciding one ends? The core of the exact compares.
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
 * it passes when the lanes tie.
 */
static inline proto_bool agree_cs(const char *a, const char *b, size_t read_cap, int end_wins)
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
            pc_swar_word wa = swar.load_al(a + i);
            pc_swar_word wb = swar.load(b + i);
            pc_swar_word x = wa ^ wb;
            pc_swar_word z = swar.has_zero(wa);
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
                pc_swar_word xm = PC_SWAR_HIGH & ~swar.has_zero(x); // lanes that differ, guard bits
                pc_swar_word zl = (z - (pc_swar_word)1) & ~z;
                pc_swar_word xl = (xm - (pc_swar_word)1) & ~xm;
#if PC_HW_BIG_ENDIAN
                // Lowest address is the HIGHEST bit here, so first-to-fire is the LARGER value.
                if (end_wins)
                {
                    return zl >= xl;
                }
                return zl > xl;
#else
                if (end_wins)
                {
                    return zl <= xl;
                }
                return zl < xl;
#endif
            }
            i += PC_SWAR_BYTES;
            continue;
        }
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        if (ca == 0)
        {
            return (ca == cb) || (end_wins != 0);
        }
        if (ca != cb)
        {
            return PROTO_FALSE;
        }
        ++i;
    }
    // The bound ran out with no terminator and no disagreement. Equality cannot be claimed for a
    // string this never saw the end of; a prefix that filled the whole bound has matched.
    return end_wins != 0;
}

/** @brief ::agree_cs over the case-folded syndrome. Same decision, a different comparison. */
static inline proto_bool agree_ci(const char *a, const char *b, size_t read_cap, int end_wins)
{
    size_t i = 0;
    while (i < read_cap)
    {
        if (((uintptr_t)(a + i) & (PC_SWAR_BYTES - 1u)) == 0u && i + PC_SWAR_BYTES <= read_cap)
        {
            pc_swar_word wa = swar.load_al(a + i);
            pc_swar_word wb = swar.load(b + i);
            pc_swar_word x = swar.xor_(wa, wb, PROTO_TRUE);
            pc_swar_word z = swar.has_zero(wa);
            if ((x | z) != 0)
            {
                pc_swar_word xm = PC_SWAR_HIGH & ~swar.has_zero(x);
                pc_swar_word zl = (z - (pc_swar_word)1) & ~z;
                pc_swar_word xl = (xm - (pc_swar_word)1) & ~xm;
#if PC_HW_BIG_ENDIAN
                if (end_wins)
                {
                    return zl >= xl;
                }
                return zl > xl;
#else
                if (end_wins)
                {
                    return zl <= xl;
                }
                return zl < xl;
#endif
            }
            i += PC_SWAR_BYTES;
            continue;
        }
        unsigned char ca = (unsigned char)a[i];
        unsigned char cb = (unsigned char)b[i];
        pc_swar_word d = swar.xor_((pc_swar_word)ca, (pc_swar_word)cb, PROTO_TRUE);
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
    return end_wins != 0;
}

static inline proto_bool eq_cs(const char *a, const char *b, size_t read_cap)
{
    return agree_cs(a, b, read_cap, 0);
}
static inline proto_bool eq_ci(const char *a, const char *b, size_t read_cap)
{
    return agree_ci(a, b, read_cap, 0);
}
static inline proto_bool starts_cs(const char *s, const char *pre, size_t read_cap)
{
    return agree_cs(pre, s, read_cap, 1); // the pattern's terminator is what ends the compare
}
static inline proto_bool starts_ci(const char *s, const char *pre, size_t read_cap)
{
    return agree_ci(pre, s, read_cap, 1);
}

/**
 * @brief First occurrence of @p needle in @p hay within @p read_cap bytes, or NULL. Exact.
 *
 * **Not fixed-work.** Its cost depends on how many candidate lanes the haystack happens to hold, so
 * the worst case is a property of the input rather than a number that can be stated before flashing.
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
 * The walk branches on the needle's length, because the cheapest shape is a function of it: one byte
 * needs no funnel, no lookahead word and no extent mask; a pair is settled for every start position
 * in a word at once; past that the anchor-plus-verify walk costs less than a mask chain whose length
 * grows with the needle.
 *
 * @p read_cap is ::diff_cs's promise: that many bytes of @p hay are readable. Every wide read below
 * is guarded against it, and a candidate too close to the bound for one falls back to bytes.
 *
 * An empty needle matches at the start, which is what a search for nothing means.
 */
static const char *find_cs(const char *hay, size_t read_cap, const char *needle, size_t needle_cap)
{
    // The load width is the largest power of two at or under `needle_cap`, which the caller states as
    // the count of readable bytes, so the load is inside the object by that promise alone.
    size_t w = 1u;
    if (needle_cap >= PC_SWAR_BYTES)
    {
        w = PC_SWAR_BYTES;
    }
    else if (needle_cap >= 4u)
    {
        w = 4u;
    }
    else if (needle_cap >= 2u)
    {
        w = 2u;
    }

    // The ingest is the first operation, and it answers the length as well as supplying the bytes:
    // a zero lane at index j < w means needle[j] is the terminator, so nlen is j. The mask only
    // decides the length when the terminator lies inside the loaded window; at j == w the needle may
    // continue past it and the bound is read the long way.
    const pc_swar_word n_raw = (pc_swar_word)proto_raw_load(needle, w);
    const pc_swar_word nz = swar.has_zero(n_raw);
    // With no terminator in the window, what is left is [w, needle_cap). A single byte there can only
    // be the terminator, so nlen is w. Anything longer is read the long way.
    size_t j0 = PC_SWAR_BYTES;
    if (nz != 0)
    {
        j0 = swar.zero_lane(nz);
    }
    size_t nlen = j0;
    if (j0 >= w)
    {
        if (needle_cap - w == 1u)
        {
            nlen = w;
        }
        else
        {
            nlen = len(needle, needle_cap);
        }
    }
    if (nlen == 0)
    {
        return hay;
    }

    // needle bytes the masked compare settles
    size_t take = w;
    if (nlen < w)
    {
        take = nlen;
    }

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
    // first byte is dense, and pays an extra load per word everywhere else.
    const uint8_t c_first = (uint8_t)needle[0];

    // The walk's anchor. A funnel by `ka` can only reach lane j+ka out of the loaded pair while
    // ka stays under one word, which is the clamp; within that the middle byte is taken.
    size_t ka = PC_SWAR_BYTES - 1u;
    if ((nlen / 2u) < PC_SWAR_BYTES)
    {
        ka = nlen / 2u;
    }
    const uint8_t c_anchor = (uint8_t)needle[ka];

    size_t i = 0;

    // Head: positions before the first word boundary, one byte at a time.
    while (i < read_cap && ((uintptr_t)(hay + i) & (PC_SWAR_BYTES - 1u)) != 0u)
    {
        if (hay[i] == '\0')
        {
            return NULL;
        }
        if ((uint8_t)hay[i] == c_first)
        {
            size_t j = 0;
            while (j < nlen && i + j < read_cap && hay[i + j] == needle[j])
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
            pc_swar_word w0 = swar.load_al(hay + i);
            pc_swar_word z = swar.has_zero(w0);
            pc_swar_word m = swar.eq(w0, c_first, PROTO_FALSE);
            if ((m | z) != 0)
            {
                // Both cannot name one lane: a needle byte of 0 would have made nlen 0 above.
                size_t km = PC_SWAR_BYTES;
                if (m != 0)
                {
                    km = swar.zero_lane(m);
                }
                size_t kz = PC_SWAR_BYTES;
                if (z != 0)
                {
                    kz = swar.zero_lane(z);
                }
                if (km < kz)
                {
                    return hay + i + km;
                }
                return NULL;
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
        pc_swar_word w0 = swar.load_al(hay + i);
        pc_swar_word w1 = swar.load_al(hay + i + PC_SWAR_BYTES);
        pc_swar_word m = swar.eq(w0, c_first, PROTO_FALSE);
        for (size_t k = 1u; k < nlen; ++k)
        {
#if PC_HW_BIG_ENDIAN
            pc_swar_word fk = (pc_swar_word)((w0 << (8u * k)) | (w1 >> (PROTO_SWAR_BITS - 8u * k)));
#else
            pc_swar_word fk = (pc_swar_word)((w0 >> (8u * k)) | (w1 << (PROTO_SWAR_BITS - 8u * k)));
#endif
            m &= swar.eq(fk, (uint8_t)needle[k], PROTO_FALSE);
        }
        pc_swar_word z = swar.has_zero(w0);
#if PC_HW_BIG_ENDIAN
        size_t zend = PC_SWAR_BYTES;
        if (z != 0)
        {
            zend = swar.zero_lane(z);
        }
        if (zend != PC_SWAR_BYTES)
        {
            m &= (pc_swar_word)(all << (PROTO_SWAR_BITS - 8u * zend));
        }
#else
        m &= (pc_swar_word)((z - 1u) & ~z);
#endif
        if (m != 0)
        {
            return hay + i + swar.zero_lane(m);
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
        pc_swar_word w0 = swar.load_al(hay + i);
        pc_swar_word w1 = swar.load_al(hay + i + PC_SWAR_BYTES);
        pc_swar_word z = swar.has_zero(w0);
        // eq against the anchor's own funnel sets lane j when hay[i+j+ka] is needle[ka], which is
        // the needle STARTING at j whatever ka is. So which byte anchors is free, and it decides how
        // many candidates the verify below runs on. The first byte of a pattern is where a delimiter
        // or a capital sits, and both are dense; the middle byte is the one carrying content.
        pc_swar_word wa = w0;
        if (ka != 0u)
        {
#if PC_HW_BIG_ENDIAN
            wa = (pc_swar_word)((w0 << (8u * ka)) | (w1 >> (PROTO_SWAR_BITS - 8u * ka)));
#else
            wa = (pc_swar_word)((w0 >> (8u * ka)) | (w1 << (PROTO_SWAR_BITS - 8u * ka)));
#endif
        }
        pc_swar_word m = swar.eq(wa, c_anchor, PROTO_FALSE);
        size_t end = PC_SWAR_BYTES;
        if (z != 0)
        {
            end = swar.zero_lane(z);
        }

        while (m != 0)
        {
            size_t k = swar.zero_lane(m);
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
            pc_swar_word syn = (pc_swar_word)(wk ^ nw);
            size_t rest = nlen - take;
            if ((syn & nm) == 0 && (take == nlen || (i + k + nlen <= read_cap &&
                                                     diff_cs(hay + i + k + take, needle + take, rest) == rest)))
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
        pc_swar_word w0 = swar.load_al(hay + i);
        pc_swar_word z = swar.has_zero(w0);
        pc_swar_word m = swar.eq(w0, c_first, PROTO_FALSE);
        for (size_t k = 1u; k < nlen; ++k)
        {
#if PC_HW_BIG_ENDIAN
            pc_swar_word fk = (pc_swar_word)(w0 << (8u * k));
#else
            pc_swar_word fk = (pc_swar_word)(w0 >> (8u * k));
#endif
            m &= swar.eq(fk, (uint8_t)needle[k], PROTO_FALSE);
        }
#if PC_HW_BIG_ENDIAN
        size_t zend = PC_SWAR_BYTES;
        if (z != 0)
        {
            zend = swar.zero_lane(z);
        }
        if (zend != PC_SWAR_BYTES)
        {
            m &= (pc_swar_word)(all << (PROTO_SWAR_BITS - 8u * zend));
        }
#else
        m &= (pc_swar_word)((z - 1u) & ~z);
#endif
        if (m != 0)
        {
            return hay + i + swar.zero_lane(m);
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
        if ((uint8_t)hay[i] == c_first)
        {
            size_t j = 1u;
            while (j < nlen && i + j < read_cap && hay[i + j] == needle[j])
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
 * @brief ::find_cs over the case-folded comparison.
 *
 * The same walk and the same length dispatch, built on the folding lane tests: ::pc_swar_eq_ci for
 * the candidate mask, ::pc_swar_xor_ci for the syndrome, ::byte_same_ci for the edges. Folding is
 * only sound on letters, which is why the byte test is a function here and an `==` in ::find_cs.
 */
static const char *find_ci(const char *hay, size_t read_cap, const char *needle, size_t needle_cap)
{
    size_t w = 1u;
    if (needle_cap >= PC_SWAR_BYTES)
    {
        w = PC_SWAR_BYTES;
    }
    else if (needle_cap >= 4u)
    {
        w = 4u;
    }
    else if (needle_cap >= 2u)
    {
        w = 2u;
    }

    const pc_swar_word n_raw = (pc_swar_word)proto_raw_load(needle, w);
    const pc_swar_word nz = swar.has_zero(n_raw);
    size_t j0 = PC_SWAR_BYTES;
    if (nz != 0)
    {
        j0 = swar.zero_lane(nz);
    }
    size_t nlen = j0;
    if (j0 >= w)
    {
        if (needle_cap - w == 1u)
        {
            nlen = w;
        }
        else
        {
            nlen = len(needle, needle_cap);
        }
    }
    if (nlen == 0)
    {
        return hay;
    }

    size_t take = w;
    if (nlen < w)
    {
        take = nlen;
    }

    const pc_swar_word all = (pc_swar_word) ~(pc_swar_word)0;
#if PC_HW_BIG_ENDIAN
    const pc_swar_word nm = (pc_swar_word)((all >> (PC_SWAR_BYTES * 8u - take * 8u)) << ((w - take) * 8u));
#else
    const pc_swar_word nm = (pc_swar_word)(all >> (PC_SWAR_BYTES * 8u - take * 8u));
#endif
    const pc_swar_word nw = n_raw & nm;

    const uint8_t c_first = (uint8_t)needle[0];
    size_t ka = PC_SWAR_BYTES - 1u;
    if ((nlen / 2u) < PC_SWAR_BYTES)
    {
        ka = nlen / 2u;
    }
    const uint8_t c_anchor = (uint8_t)needle[ka];

    size_t i = 0;

    while (i < read_cap && ((uintptr_t)(hay + i) & (PC_SWAR_BYTES - 1u)) != 0u)
    {
        if (hay[i] == '\0')
        {
            return NULL;
        }
        if (byte_same_ci((uint8_t)hay[i], c_first))
        {
            size_t j = 0;
            while (j < nlen && i + j < read_cap && byte_same_ci((uint8_t)hay[i + j], (uint8_t)needle[j]))
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

    if (nlen == 1u)
    {
        while (i + PC_SWAR_BYTES <= read_cap)
        {
            pc_swar_word w0 = swar.load_al(hay + i);
            pc_swar_word z = swar.has_zero(w0);
            pc_swar_word m = swar.eq(w0, c_first, PROTO_TRUE);
            if ((m | z) != 0)
            {
                size_t km = PC_SWAR_BYTES;
                if (m != 0)
                {
                    km = swar.zero_lane(m);
                }
                size_t kz = PC_SWAR_BYTES;
                if (z != 0)
                {
                    kz = swar.zero_lane(z);
                }
                if (km < kz)
                {
                    return hay + i + km;
                }
                return NULL;
            }
            i += PC_SWAR_BYTES;
        }
    }

    while (nlen >= 2u && nlen <= 3u && nlen <= PC_SWAR_BYTES && i + (2u * PC_SWAR_BYTES) <= read_cap)
    {
        pc_swar_word w0 = swar.load_al(hay + i);
        pc_swar_word w1 = swar.load_al(hay + i + PC_SWAR_BYTES);
        pc_swar_word m = swar.eq(w0, c_first, PROTO_TRUE);
        for (size_t k = 1u; k < nlen; ++k)
        {
#if PC_HW_BIG_ENDIAN
            pc_swar_word fk = (pc_swar_word)((w0 << (8u * k)) | (w1 >> (PROTO_SWAR_BITS - 8u * k)));
#else
            pc_swar_word fk = (pc_swar_word)((w0 >> (8u * k)) | (w1 << (PROTO_SWAR_BITS - 8u * k)));
#endif
            m &= swar.eq(fk, (uint8_t)needle[k], PROTO_TRUE);
        }
        pc_swar_word z = swar.has_zero(w0);
#if PC_HW_BIG_ENDIAN
        size_t zend = PC_SWAR_BYTES;
        if (z != 0)
        {
            zend = swar.zero_lane(z);
        }
        if (zend != PC_SWAR_BYTES)
        {
            m &= (pc_swar_word)(all << (PROTO_SWAR_BITS - 8u * zend));
        }
#else
        m &= (pc_swar_word)((z - 1u) & ~z);
#endif
        if (m != 0)
        {
            return hay + i + swar.zero_lane(m);
        }
        if (z != 0)
        {
            return NULL;
        }
        i += PC_SWAR_BYTES;
    }

    while ((nlen > 3u || nlen > PC_SWAR_BYTES) && nlen >= 2u && i + (2u * PC_SWAR_BYTES) <= read_cap)
    {
        pc_swar_word w0 = swar.load_al(hay + i);
        pc_swar_word w1 = swar.load_al(hay + i + PC_SWAR_BYTES);
        pc_swar_word z = swar.has_zero(w0);
        pc_swar_word wa = w0;
        if (ka != 0u)
        {
#if PC_HW_BIG_ENDIAN
            wa = (pc_swar_word)((w0 << (8u * ka)) | (w1 >> (PROTO_SWAR_BITS - 8u * ka)));
#else
            wa = (pc_swar_word)((w0 >> (8u * ka)) | (w1 << (PROTO_SWAR_BITS - 8u * ka)));
#endif
        }
        pc_swar_word m = swar.eq(wa, c_anchor, PROTO_TRUE);
        size_t end = PC_SWAR_BYTES;
        if (z != 0)
        {
            end = swar.zero_lane(z);
        }

        while (m != 0)
        {
            size_t k = swar.zero_lane(m);
            if (k >= end)
            {
                break;
            }
            pc_swar_word wk = w0;
            if (k != 0)
            {
#if PC_HW_BIG_ENDIAN
                wk = (pc_swar_word)((w0 << (8u * k)) | (w1 >> (PROTO_SWAR_BITS - 8u * k)));
#else
                wk = (pc_swar_word)((w0 >> (8u * k)) | (w1 << (PROTO_SWAR_BITS - 8u * k)));
#endif
            }
            pc_swar_word syn = swar.xor_(wk, nw, PROTO_TRUE);
            size_t rest = nlen - take;
            if ((syn & nm) == 0 && (take == nlen || (i + k + nlen <= read_cap &&
                                                     diff_ci(hay + i + k + take, needle + take, rest) == rest)))
            {
                return hay + i + k;
            }
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

    if (nlen <= PC_SWAR_BYTES && i + PC_SWAR_BYTES <= read_cap)
    {
        pc_swar_word w0 = swar.load_al(hay + i);
        pc_swar_word z = swar.has_zero(w0);
        pc_swar_word m = swar.eq(w0, c_first, PROTO_TRUE);
        for (size_t k = 1u; k < nlen; ++k)
        {
#if PC_HW_BIG_ENDIAN
            pc_swar_word fk = (pc_swar_word)(w0 << (8u * k));
#else
            pc_swar_word fk = (pc_swar_word)(w0 >> (8u * k));
#endif
            m &= swar.eq(fk, (uint8_t)needle[k], PROTO_TRUE);
        }
#if PC_HW_BIG_ENDIAN
        size_t zend = PC_SWAR_BYTES;
        if (z != 0)
        {
            zend = swar.zero_lane(z);
        }
        if (zend != PC_SWAR_BYTES)
        {
            m &= (pc_swar_word)(all << (PROTO_SWAR_BITS - 8u * zend));
        }
#else
        m &= (pc_swar_word)((z - 1u) & ~z);
#endif
        if (m != 0)
        {
            return hay + i + swar.zero_lane(m);
        }
        if (z != 0)
        {
            return NULL;
        }
        i += PC_SWAR_BYTES - nlen + 1u;
    }

    while (i < read_cap && hay[i] != '\0')
    {
        if (byte_same_ci((uint8_t)hay[i], c_first))
        {
            size_t j = 1u;
            while (j < nlen && i + j < read_cap && byte_same_ci((uint8_t)hay[i + j], (uint8_t)needle[j]))
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

static inline size_t copy(char *dst, const char *src, size_t dst_cap)
{
    if (dst_cap == 0)
    {
        return 0;
    }
    // The bound reaches the scan as a `nul_cap`, a willingness to look that far, so a shorter source
    // is copied whole.
    size_t n = len(src, dst_cap - 1);
    proto_raw_read(dst, src, n);
    dst[n] = '\0';
    return n;
}

// The dispatch. One branch, at the door, naming which of the two walks runs. Nothing below this
// point carries a flag or tests one.
static size_t diff(const char *a, const char *b, size_t read_cap, proto_bool ci)
{
    if (ci)
    {
        return diff_ci(a, b, read_cap);
    }
    return diff_cs(a, b, read_cap);
}

static proto_bool eq(const char *a, const char *b, size_t read_cap, proto_bool ci)
{
    if (ci)
    {
        return eq_ci(a, b, read_cap);
    }
    return eq_cs(a, b, read_cap);
}

static proto_bool starts(const char *s, const char *pre, size_t read_cap, proto_bool ci)
{
    if (ci)
    {
        return starts_ci(s, pre, read_cap);
    }
    return starts_cs(s, pre, read_cap);
}

static const char *find(const char *hay, size_t read_cap, const char *needle, size_t needle_cap, proto_bool ci)
{
    if (ci)
    {
        return find_ci(hay, read_cap, needle, needle_cap);
    }
    return find_cs(hay, read_cap, needle, needle_cap);
}

static proto_bool has(const char *hay, size_t read_cap, const char *needle, size_t needle_cap, proto_bool ci)
{
    if (ci)
    {
        return find_ci(hay, read_cap, needle, needle_cap) != NULL;
    }
    return find_cs(hay, read_cap, needle, needle_cap) != NULL;
}

static int step_word(pc_swar_word wa, pc_swar_word wb, proto_bool ci, int end_wins)
{
    if (ci)
    {
        return step_word_ci(wa, wb, end_wins);
    }
    return step_word_cs(wa, wb, end_wins);
}

static int step_byte(unsigned char ca, unsigned char cb, proto_bool ci, int end_wins)
{
    if (ci)
    {
        return step_byte_ci(ca, cb, end_wins);
    }
    return step_byte_cs(ca, cb, end_wins);
}

static proto_bool ws(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

static proto_bool digit(char c)
{
    return c >= '0' && c <= '9';
}

static long to_long(const char *s, const char **end)
{
    const char *p = s;
    while (ws(*p))
    {
        p++;
    }
    proto_bool neg = PROTO_FALSE;
    if (*p == '+' || *p == '-')
    {
        neg = (*p++ == '-');
    }
    const char *ds = p;
    unsigned long v = 0; // accumulate unsigned: signed overflow on a huge digit run is undefined
    while (digit(*p))
    {
        v = v * 10UL + (unsigned long)(*p++ - '0');
    }
    if (end)
    {
        *end = s;
        if (p != ds)
        {
            *end = p;
        }
    }
    if (neg)
    {
        return (long)(0UL - v); // two's-complement reinterpret, so no negation overflow
    }
    return (long)v;
}

static unsigned long to_ulong(const char *s, const char **end)
{
    const char *p = s;
    while (ws(*p))
    {
        p++;
    }
    if (*p == '+')
    {
        p++;
    }
    const char *ds = p;
    unsigned long v = 0;
    while (digit(*p))
    {
        v = v * 10UL + (unsigned long)(*p++ - '0');
    }
    if (end)
    {
        *end = s;
        if (p != ds)
        {
            *end = p;
        }
    }
    return v;
}

// The fractional part after a '.', advancing p and accumulating into val.
static void frac(const char **p, double *val, proto_bool *any)
{
    (*p)++; // consume '.'
    double scale = 1.0;
    while (digit(**p))
    {
        scale *= 10.0;
        *val += (double)(*(*p)++ - '0') / scale;
        *any = PROTO_TRUE;
    }
}

// A trailing exponent (e[+/-]NNN) applied to val, advancing p past it.
static void expo(const char **p, double *val)
{
    (*p)++; // consume 'e'/'E'
    proto_bool eneg = PROTO_FALSE;
    if (**p == '+' || **p == '-')
    {
        eneg = (*(*p)++ == '-');
    }
    int ex = 0;
    while (digit(**p))
    {
        if (ex < 400) // clamp: 10^400 overflows the double to inf, so more digits change nothing
        {
            ex = ex * 10 + (**p - '0');
        }
        (*p)++;
    }
    double m = 1.0;
    for (int k = 0; k < ex; k++)
    {
        m *= 10.0;
    }
    if (eneg)
    {
        *val = *val / m;
        return;
    }
    *val = *val * m;
}

static double to_double(const char *s, const char **end)
{
    const char *p = s;
    while (ws(*p))
    {
        p++;
    }
    proto_bool neg = PROTO_FALSE;
    if (*p == '+' || *p == '-')
    {
        neg = (*p++ == '-');
    }
    proto_bool any = PROTO_FALSE;
    double val = 0.0;
    while (digit(*p))
    {
        val = val * 10.0 + (*p++ - '0');
        any = PROTO_TRUE;
    }
    if (*p == '.')
    {
        frac(&p, &val, &any);
    }
    if (any && (*p == 'e' || *p == 'E'))
    {
        expo(&p, &val);
    }
    if (end)
    {
        *end = s;
        if (any)
        {
            *end = p;
        }
    }
    if (neg)
    {
        return -val;
    }
    return val;
}

// Through the double, because sub-meter values (a GGA latitude) need its precision to land right.
static float to_float(const char *s, const char **end)
{
    return (float)to_double(s, end);
}

const StrNs str = {len,       diff, eq,    starts,  find,     has,       copy,    step_word,
                   step_byte, ws,   digit, to_long, to_ulong, to_double, to_float};
