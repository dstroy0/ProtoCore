// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file rawmemcpy.c
 * @brief The arbitrary-width span move - see rawmemcpy.h.
 *
 * The scalar rungs stay in the header, where each one folds to a single load or store at its call
 * site. This file holds the one body with loops in it: the ladder that aligns the destination, steps
 * PROTO_RAW_WORD bytes at a time, and funnels a source that does not share the destination's
 * boundary.
 *
 * The one symbol this file defines is ::proto_raw_read.
 */

#include "mmgr/rawmemcpy/rawmemcpy.h"

void proto_raw_read(void *dst, const void *p, size_t sz)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *u = (const unsigned char *)p;
    const uintptr_t mask = (uintptr_t)(PROTO_RAW_WORD - 1u);
    size_t i = 0;

    // Head: bytes until the DESTINATION is on a boundary. Fewer than one word, once. The
    // destination is the side that gets aligned because every step below writes it, and a store
    // that has to be assembled from bytes costs the same as a load that does.
    while (i < sz && ((uintptr_t)(d + i) & mask) != 0u)
    {
        d[i] = u[i];
        i++;
    }

    const size_t off = (size_t)((uintptr_t)(u + i) & mask);
    if (off == 0u)
    {
        // Both on a boundary: the machine's own load and its own store, nothing in between.
        while (sz - i >= PROTO_RAW_WORD)
        {
            proto_mv_put(d + i, proto_mv_load(u + i));
            i += PROTO_RAW_WORD;
        }
    }
    else if (sz - i >= PROTO_RAW_WORD)
    {
        // Not co-aligned, and the answer is NOT to fall back to bytes. Read the source at its own
        // boundary and funnel each adjacent PAIR of aligned words into the word the destination
        // wants: one shifts down by the misalignment, the next shifts up by the rest, and the OR
        // joins them. Both shifts move every lane at once, so the misalignment costs two shifts and
        // an OR per word instead of a byte loop - and every access stays a real load and a real
        // store, which on a part with no unaligned instruction is the whole difference.
        //
        // Address order decides which way each shift goes: on a little-endian load the lowest byte
        // sits in the low bits, so the earlier word shifts DOWN; big-endian is the mirror.
        //
        // The priming load reads a whole word, so it is spent only once there is a whole word of
        // work: with fewer bytes left than that the loop below never runs, and the load would reach
        // past a source that short for a value nothing reads. The tail takes those bytes instead.
        const unsigned char *sa = (u + i) - off;
        const unsigned lo = (unsigned)(off * 8u);
        const unsigned hi = (unsigned)(PROTO_MV_BITS - (off * 8u));
        proto_mv_word prev = proto_mv_load(sa);
        while (sz - i >= PROTO_RAW_WORD)
        {
            sa += PROTO_RAW_WORD;
            proto_mv_word cur = proto_mv_load(sa);
#if PROTOCORE_HW_BIG_ENDIAN
            proto_mv_put(d + i, (proto_mv_word)((prev << lo) | (cur >> hi)));
#else
            proto_mv_put(d + i, (proto_mv_word)((prev >> lo) | (cur << hi)));
#endif
            prev = cur;
            i += PROTO_RAW_WORD;
        }
    }

    // Tail: fewer than a word left.
    while (i < sz)
    {
        d[i] = u[i];
        i++;
    }
}
