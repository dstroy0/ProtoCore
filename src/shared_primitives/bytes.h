// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file bytes.h
 * @brief The byte verbs - append into a pc_span, take out of a pc_cspan.
 *
 * A bounded byte region is one thing with two accessors. span.h is the region: where the storage
 * came from, how big it is, how much has been produced, and whether anything overran. This file is
 * what you do to it. The two halves are split that way so a region can be passed somewhere that only
 * reads it without carrying an append API along.
 *
 * The subtle invariants live here once, so a bug is fixed in one place and every codec inherits it:
 * keep counting `pos` past `cap` on overflow so the caller can size the buffer, sticky fault flags,
 * and network (big-endian) byte order.
 *
 * These take pc_span / pc_cspan directly rather than being templated on a per-codec cursor struct.
 * CBOR and MessagePack each declared their own writer and reader, all four field-identical to the
 * spans, and the templates existed only to bind them by field name. One concrete pair replaces four
 * near-duplicate structs and removes the deduction along with them.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_BYTES_H
#define PROTOCORE_BYTES_H

#include "shared_primitives/endian.h" // pc_rd32be - the fixed-width serializers live there
#include "shared_primitives/span.h"   // pc_span / pc_cspan - the region these verbs act on
#include <stddef.h>
#include <stdint.h>

// --- append into a pc_span ---

/** @brief Append one byte; on overflow set the flag but keep counting @p pos. */
inline void pc_bw_put(pc_span *w, uint8_t b)
{
    if (w->pos < w->cap)
    {
        w->buf[w->pos] = b;
    }
    else
    {
        w->overflow = true;
    }
    w->pos++; // keep counting so pc_span_len() reports the size the payload needs
}

/** @brief Append the low @p nbytes of @p val, big-endian (network order). */
inline void pc_bw_put_be(pc_span *w, uint64_t val, int32_t nbytes)
{
    for (int32_t s = (nbytes - 1) * 8; s >= 0; s -= 8)
    {
        pc_bw_put(w, static_cast<uint8_t>(val >> s));
    }
}

// --- take out of a pc_cspan ---

/**
 * @brief Read @p nbytes big-endian immediately after the tag byte at @p pos,
 *        advancing past the tag and the argument (pos += 1 + nbytes).
 *
 * Both CBOR heads and MessagePack format bytes are a 1-byte tag followed by a big-endian argument,
 * so this consumes the tag + argument in one step. Sets the sticky err and returns false if the read
 * would run past the buffer.
 */
inline bool pc_br_take_be(pc_cspan *r, size_t nbytes, uint64_t *out)
{
    if (r->pos + 1 + nbytes > r->len)
    {
        r->err = true;
        return false;
    }
    uint64_t v{0};
    for (size_t i = 0; i < nbytes; i++)
    {
        v = (v << 8) | r->buf[r->pos + 1 + i];
    }
    *out = v;
    r->pos += 1 + nbytes;
    return true;
}

// --- offset-passing reads over a caller-owned buffer (no region object needed) ---
//
// A length-prefixed field is the same shape in every protocol: a big-endian u32 count, then that
// many bytes. SSH calls it a "string" (RFC 4251 sec 5) and had it written four separate times.
// These bounds-check and advance an offset the caller owns, for parsers that walk a raw payload.

// Every bound here is written as a subtraction against the space that remains, never as a sum
// compared to the length. A sum overflows: size_t is 32 bits on esp32 and c2000, the length prefix
// on the wire is a full u32, and `*off + n > len` with n = 0xFFFFFFFF wraps to a small number that
// passes the check. The peer picks n, so the sum form hands out a length larger than the buffer.
// Subtracting cannot wrap once *off <= len is established, which each check does first.

/** @brief Read a big-endian u32 at @p *off, advancing it by 4. False if it would run past @p len. */
inline bool pc_rd_u32(const uint8_t *p, size_t len, size_t *off, uint32_t *out)
{
    if (*off > len || len - *off < 4)
    {
        return false;
    }
    *out = pc_rd32be(p + *off);
    *off += 4;
    return true;
}

/**
 * @brief Read a u32-length-prefixed blob: @p out points into @p p, @p slen is its length.
 *
 * Nothing is copied, so the result must not outlive @p p. On a length that would run past the end,
 * @p *off is left where it started so the caller can report which field failed.
 */
inline bool pc_rd_str(const uint8_t *p, size_t len, size_t *off, const uint8_t **out, uint32_t *slen)
{
    size_t start{*off};
    uint32_t n{0};
    if (!pc_rd_u32(p, len, off, &n))
    {
        return false;
    }
    if (n > len - *off) // pc_rd_u32 succeeding established *off <= len, so this cannot wrap
    {
        *off = start;
        return false;
    }
    *out = p + *off;
    *slen = n;
    *off += n;
    return true;
}

#endif // PROTOCORE_BYTES_H
