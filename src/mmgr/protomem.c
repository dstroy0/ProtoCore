// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protomem.c
 * @brief The byte-span operations - see protomem.h.
 *
 * Every access is one register-width load or store. A span that does not end on a boundary is not
 * walked a byte at a time: the partial word is masked and stored whole, and the lanes past the span
 * go to zero, which is allocation padding because the arena rounds every size up to PC_ARENA_ALIGN.
 * A source that is not co-aligned with the destination is funnelled, the way the raw mover above it
 * does it.
 *
 * The one symbol this file exports is @ref mem.
 */

#include "mmgr/protomem.h"
#include "mmgr/swar.h" // the lane math the ordering walk reads its answer out of

#define PC_MEM_MASK ((uintptr_t)(PROTO_RAW_WORD - 1u))

// The low @p nbytes lanes of a word, as bits. A count at or past the width is the whole word, which
// is the case a shift by the full width would leave undefined.
static proto_mv_word lo_lanes(size_t nbytes)
{
    if (nbytes >= PROTO_RAW_WORD)
    {
        return (proto_mv_word) ~(proto_mv_word)0;
    }
    return (proto_mv_word)(((proto_mv_word)1 << (nbytes * 8u)) - (proto_mv_word)1);
}

// Bits covering byte lanes [from, to) of a word in ADDRESS order. Address order is where byte order
// enters: the lowest-addressed lane sits in the low bits on a little-endian load and the high bits on
// a big-endian one, so the two ends are named from opposite sides.
static proto_mv_word span_lanes(size_t from, size_t to)
{
#if PC_HW_BIG_ENDIAN
    return (proto_mv_word)(~lo_lanes(PROTO_RAW_WORD - to) & lo_lanes(PROTO_RAW_WORD - from));
#else
    return (proto_mv_word)(lo_lanes(to) & ~lo_lanes(from));
#endif
}

// The word of source bytes beginning at @p p, assembled from the aligned words that hold it. @p avail
// is what the span still has past @p p, so the second load is taken only when the wanted bytes reach
// into the next word and never past the span's own allocation.
static proto_mv_word src_word(const unsigned char *p, size_t avail)
{
    const size_t off = (size_t)((uintptr_t)p & PC_MEM_MASK);
    const unsigned char *sa = p - off;
    const proto_mv_word w0 = proto_mv_load(sa);

    if (off == 0u)
    {
        return w0;
    }

    const unsigned lo = (unsigned)(off * 8u);
    const unsigned hi = (unsigned)(PROTO_MV_BITS - lo);
    proto_mv_word w1 = 0;
    if (avail > PROTO_RAW_WORD - off)
    {
        w1 = proto_mv_load(sa + PROTO_RAW_WORD);
    }
#if PC_HW_BIG_ENDIAN
    return (proto_mv_word)((w0 << lo) | (w1 >> hi));
#else
    return (proto_mv_word)((w0 >> lo) | (w1 << hi));
#endif
}

void pc_mem_cpy(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    size_t i = 0;

    while (i + PROTO_RAW_WORD <= n)
    {
        proto_mv_put(d + i, src_word(s + i, n - i));
        i += PROTO_RAW_WORD;
    }
    if (i < n)
    {
        proto_mv_put(d + i, (proto_mv_word)(src_word(s + i, n - i) & span_lanes(0u, n - i)));
    }
}

void pc_mem_move(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    if (d == s || n == 0u)
    {
        return;
    }
    if (d < s || d >= s + n)
    {
        pc_mem_cpy(dst, src, n);
        return;
    }

    // Destination ahead of the source and inside it: walk down so a word is read before the store
    // that would overwrite it. The trailing partial goes first, for the same reason.
    size_t i = n & ~(size_t)PC_MEM_MASK;
    if (i < n)
    {
        proto_mv_put(d + i, (proto_mv_word)(src_word(s + i, n - i) & span_lanes(0u, n - i)));
    }
    while (i >= PROTO_RAW_WORD)
    {
        i -= PROTO_RAW_WORD;
        proto_mv_put(d + i, src_word(s + i, n - i));
    }
}

/**
 * @brief Order @p n bytes at @p a against @p b, as unsigned.
 *
 * A word per step: XOR zeroes every lane that matches, so a nonzero syndrome means the two part
 * inside this word and the lane math names which byte. That byte is the whole answer, because the
 * ordering of two spans is decided where they first disagree and nothing after it is read.
 *
 * The tail runs the same rule a byte at a time. A partial word cannot be loaded whole without
 * reading past what the caller offered: @p n is a promise about both operands, not a hint.
 */
int pc_mem_cmp(const void *a, const void *b, size_t n)
{
    const char *x = (const char *)a;
    const char *y = (const char *)b;
    size_t i = 0;

    while (i + PC_SWAR_BYTES <= n)
    {
        pc_swar_word d = swar.load(x + i) ^ swar.load(y + i);
        if (d != 0)
        {
            // The guard bit stands on every lane that differs, and the lowest one is where they part.
            i += swar.zero_lane(PC_SWAR_HIGH & ~swar.has_zero(d));
            return (int)(unsigned char)x[i] - (int)(unsigned char)y[i];
        }
        i += PC_SWAR_BYTES;
    }
    while (i < n)
    {
        if (x[i] != y[i])
        {
            return (int)(unsigned char)x[i] - (int)(unsigned char)y[i];
        }
        ++i;
    }
    return 0;
}

/**
 * @brief The first byte equal to @p c in @p n bytes at @p p, or NULL.
 *
 * A word per step: ::SwarNs::eq marks every lane holding @p c, and the lane math names the lowest
 * one. @p n is the whole bound - there is no terminator test, because a span has no terminator, and
 * the caller has already stated how far the bytes go.
 *
 * That is what separates this from ::StrNs::find, which stops at a NUL because it searches a string.
 * A buffer that deliberately carries NULs - a decoded credential, a wire frame - needs this one.
 */
const void *pc_mem_chr(const void *p, size_t n, uint8_t c)
{
    const char *s = (const char *)p;
    size_t i = 0;

    while (i < n && ((uintptr_t)(s + i) & (PC_SWAR_BYTES - 1u)) != 0u)
    {
        if ((uint8_t)s[i] == c)
        {
            return s + i;
        }
        ++i;
    }
    while (i + PC_SWAR_BYTES <= n)
    {
        pc_swar_word m = swar.eq(swar.load_al(s + i), c, PROTO_FALSE);
        if (m != 0)
        {
            return s + i + swar.zero_lane(m);
        }
        i += PC_SWAR_BYTES;
    }
    // The final partial word, a byte at a time: a whole load would read past what @p n offers.
    while (i < n)
    {
        if ((uint8_t)s[i] == c)
        {
            return s + i;
        }
        ++i;
    }
    return NULL;
}

void pc_mem_set(void *dst, unsigned char v, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    size_t i = 0;

    // The splat comes from the identity swar.h derives its lane masks with: the all-ones word over
    // 0xFF leaves bit 0 of every lane, so multiplying by the byte puts it in every lane at once.
    const proto_mv_word ones = (proto_mv_word)((proto_mv_word) ~(proto_mv_word)0 / 0xFFu);
    const proto_mv_word w = (proto_mv_word)(ones * (proto_mv_word)v);

    while (i + PROTO_RAW_WORD <= n)
    {
        proto_mv_put(d + i, w);
        i += PROTO_RAW_WORD;
    }
    if (i < n)
    {
        proto_mv_put(d + i, (proto_mv_word)(w & span_lanes(0u, n - i)));
    }
}

void pc_mem_zero(void *dst, size_t n)
{
    pc_mem_set(dst, 0u, n);
}
