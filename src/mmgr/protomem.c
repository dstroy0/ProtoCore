// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protomem.c
 * @brief The byte-span operations - see protomem.h.
 *
 * Every access is one register-width load or store. A span that does not end on a boundary is not
 * walked a byte at a time: the partial word is masked and stored whole, and the lanes past the span
 * go to zero, which is allocation padding because the arena rounds every size up to PC_ARENA_ALIGN.
 * A source that is not co-aligned with the destination is funnelled, the way rawmemcpy.h's mover
 * does it.
 *
 * The one symbol this file exports is @ref mem.
 */

#include "mmgr/protomem.h"
#include "shared_primitives/rawmemcpy.h" // the aligned load/store rung and its width
#include "shared_primitives/runops.h"    // proto_diff: the lane-tested first-difference scan

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

static void cpy(void *dst, const void *src, size_t n)
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

static void move(void *dst, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    if (d == s || n == 0u)
    {
        return;
    }
    if (d < s || d >= s + n)
    {
        cpy(dst, src, n);
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

static int cmp(const void *a, const void *b, size_t n)
{
    const char *x = (const char *)a;
    const char *y = (const char *)b;
    const size_t k = proto_diff(x, y, n);

    if (k == n)
    {
        return 0;
    }
    return (int)(unsigned char)x[k] - (int)(unsigned char)y[k];
}

static void set(void *dst, unsigned char v, size_t n)
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

static void zero(void *dst, size_t n)
{
    set(dst, 0u, n);
}

const MemNs mem = {cpy, move, cmp, set, zero};
