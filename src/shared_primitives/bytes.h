// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file bytes.h
 * @brief Shared byte-cursor mechanics for the binary codecs (one source of truth).
 *
 * CBOR and MessagePack (and any future binary codec) kept their own copies of the
 * exact same write cursor (`{buf, cap, pos, overflow}` with bounds-checked put +
 * big-endian put) and read cursor (`{buf, len, pos, err}` with a big-endian take).
 * The subtle invariants - keep counting `pos` past `cap` on overflow so the caller
 * can size the buffer; sticky `err`; network (big-endian) byte order - now live
 * here once, so a bug is fixed in one place and every codec inherits it.
 *
 * Header-only and templated on the cursor type, so each codec keeps its own public
 * struct (CborWriter, MsgpackReader, ...) - these helpers just require the matching
 * field names. No .cpp to wire into the per-env test src filters.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_BYTES_H
#define PROTOCORE_BYTES_H

#include <stddef.h>
#include <stdint.h>

// --- write cursor: requires fields { uint8_t *buf; size_t cap; size_t pos; bool overflow; } ---

/** @brief Bind a write cursor to @p buf (capacity @p cap) and reset it. */
template <typename W> inline void pc_bw_init(W *w, uint8_t *buf, size_t cap)
{
    w->buf = buf;
    w->cap = cap;
    w->pos = 0;
    w->overflow = false;
}

/** @brief Bytes the payload needs so far (keeps counting past @p cap on overflow). */
template <typename W> inline size_t pc_bw_len(const W *w)
{
    return w->pos;
}

/** @brief True while every write has fit in the buffer. */
template <typename W> inline bool pc_bw_ok(const W *w)
{
    return !w->overflow;
}

/** @brief Append one byte; on overflow set the flag but keep counting @p pos. */
template <typename W> inline void pc_bw_put(W *w, uint8_t b)
{
    if (w->pos < w->cap)
    {
        w->buf[w->pos] = b;
    }
    else
    {
        w->overflow = true;
    }
    w->pos++; // keep counting so pc_bw_len() reports the size the payload needs
}

/** @brief Append the low @p nbytes of @p val, big-endian (network order). */
template <typename W> inline void pc_bw_put_be(W *w, uint64_t val, int nbytes)
{
    for (int s = (nbytes - 1) * 8; s >= 0; s -= 8)
    {
        pc_bw_put(w, (uint8_t)(val >> s));
    }
}

// --- read cursor: requires fields { const uint8_t *buf; size_t len; size_t pos; bool err; } ---

/** @brief Bind a read cursor to @p buf (length @p len) at offset 0. */
template <typename R> inline void pc_br_init(R *r, const uint8_t *buf, size_t len)
{
    r->buf = buf;
    r->len = len;
    r->pos = 0;
    r->err = false;
}

/** @brief True while no malformed / out-of-bounds read has occurred. */
template <typename R> inline bool pc_br_ok(const R *r)
{
    return !r->err;
}

/**
 * @brief Read @p nbytes big-endian immediately after the tag byte at @p pos,
 *        advancing past the tag and the argument (pos += 1 + nbytes).
 *
 * Both CBOR heads and MessagePack format bytes are a 1-byte tag followed by a
 * big-endian argument, so this consumes the tag + argument in one step. Sets the
 * sticky err and returns false if the read would run past the buffer.
 */
template <typename R> inline bool pc_br_take_be(R *r, size_t nbytes, uint64_t *out)
{
    if (r->pos + 1 + nbytes > r->len)
    {
        r->err = true;
        return false;
    }
    uint64_t v = 0;
    for (size_t i = 0; i < nbytes; i++)
    {
        v = (v << 8) | r->buf[r->pos + 1 + i];
    }
    *out = v;
    r->pos += 1 + nbytes;
    return true;
}

#endif // PROTOCORE_BYTES_H
