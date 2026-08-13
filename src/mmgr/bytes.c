// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file bytes.c
 * @brief The byte verbs - see bytes.h.
 *
 * Every append tests the room that remains before it stores, latches the region's overflow flag when
 * the store does not fit, and advances the cursor either way, so the cursor states the capacity the
 * payload needs.
 *
 * Every bound is a subtraction against the space that remains, taken after the offset is established
 * to be within the length, so the subtraction cannot wrap.
 *
 * The surface this file stands behind is @ref bytes.
 */

#include "mmgr/bytes.h"

// --- append into a protocore_span ---

void protocore_bw_put(protocore_span *w, uint8_t b)
{
    if (w->pos < w->cap)
    {
        w->buf[w->pos] = b;
    }
    else
    {
        w->overflow = PROTO_TRUE;
    }
    w->pos++; // keep counting so protocore_span_len() reports the size the payload needs
}

void protocore_bw_put_be(protocore_span *w, uint64_t val, int32_t nbytes)
{
    for (int32_t s = (nbytes - 1) * 8; s >= 0; s -= 8)
    {
        protocore_bw_put(w, (uint8_t)(val >> s));
    }
}

void protocore_bw_bytes(protocore_span *w, const void *src, size_t n)
{
    if (w->buf != NULL && w->pos <= w->cap && w->cap - w->pos >= n)
    {
        mem.cpy(w->buf + w->pos, src, n);
    }
    else if (n > 0)
    {
        w->overflow = PROTO_TRUE;
    }
    w->pos += n; // keep counting so protocore_span_len() reports the size the payload needs
}

// --- take out of a protocore_cspan ---

proto_bool protocore_br_take_be(protocore_cspan *r, size_t nbytes, uint64_t *out)
{
    if (r->pos > r->len || r->len - r->pos < nbytes)
    {
        r->err = PROTO_TRUE;
        return PROTO_FALSE;
    }
    uint64_t v = 0;
    for (size_t i = 0; i < nbytes; i++)
    {
        v = (v << 8) | r->buf[r->pos + i];
    }
    *out = v;
    r->pos += nbytes;
    return PROTO_TRUE;
}

// --- offset-passing reads over a caller-owned buffer (no region object needed) ---

proto_bool protocore_rd_u32(const uint8_t *p, size_t len, size_t *off, uint32_t *out)
{
    if (*off > len || len - *off < 4)
    {
        return PROTO_FALSE;
    }
    *out = protocore_rd32be(p + *off);
    *off += 4;
    return PROTO_TRUE;
}

proto_bool protocore_rd_str(const uint8_t *p, size_t len, size_t *off, const uint8_t **out, uint32_t *slen)
{
    size_t start = *off;
    uint32_t n = 0;
    if (!protocore_rd_u32(p, len, off, &n))
    {
        return PROTO_FALSE;
    }
    if (n > len - *off) // protocore_rd_u32 succeeding established *off <= len, so this cannot wrap
    {
        *off = start;
        return PROTO_FALSE;
    }
    *out = p + *off;
    *slen = n;
    *off += n;
    return PROTO_TRUE;
}

proto_bool protocore_mpint_to_fixed(const uint8_t *m, uint32_t mlen, uint8_t *out, size_t outlen)
{
    uint32_t off = 0;
    while (off < mlen && m[off] == 0)
    {
        off++;
    }
    uint32_t vlen = mlen - off;
    if (vlen > outlen)
    {
        return PROTO_FALSE;
    }
    mem.set(out, 0, outlen);
    mem.cpy(out + (outlen - vlen), m + off, vlen);
    return PROTO_TRUE;
}
