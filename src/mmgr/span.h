// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file span.h
 * @brief A byte region that carries its own bound, and the accessors that move bytes through it.
 *
 * `cap` bounds every write. `pos` keeps counting past `cap` on overflow, so an undersized region
 * reports the capacity it should have had instead of only failing.
 *
 * Accessors take the span first. A pointer, a length, and an offset passed separately are this struct
 * with its invariant dropped; passing the region keeps the bound attached to the bytes.
 *
 * A failed allocation yields a null pointer with zero capacity, never a null pointer with a live
 * capacity, so a caller that skips pc_span_ok() writes nothing instead of dereferencing null.
 *
 * The reading accessors take the region by value and pc_span_reset() takes a pointer, so a caller
 * can see at the call site which one writes.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SPAN_H
#define PROTOCORE_SPAN_H

#include "protocore_config.h" // the entry point: PC_INLINE, and types.h for proto_bool / size_t

/**
 * @brief A writable byte region: the storage, the capacity that belongs to it, and what has been
 *        produced into it.
 *
 */
typedef struct
{
    uint8_t *buf;        ///< first byte, or NULL when the region could not be obtained
    size_t cap;          ///< bytes writable at @ref buf (0 whenever @ref buf is NULL)
    size_t pos;          ///< bytes the payload needs so far; keeps counting past @ref cap on overflow
    proto_bool overflow; ///< set once a write did not fit; @ref pos then reports the size required
} pc_span;

/**
 * @brief A read-only byte region.
 *
 * Bind with pc_cspan_from() and check with pc_cspan_ok(). A read is bounded by the region itself,
 * which is why nothing here takes a separate length.
 */
typedef struct
{
    const uint8_t *buf; ///< first byte, or NULL when there is nothing to read
    size_t len;         ///< readable bytes at @ref buf (0 whenever @ref buf is NULL)
    size_t pos;         ///< read cursor
    proto_bool err;     ///< sticky: a read ran past @ref len
} pc_cspan;

/**
 * @brief Bind a span to memory whose extent is only known at run time.
 *
 * Normalizes the empty case so a span with storage but no capacity - or capacity but no storage -
 * cannot be constructed.
 */
PC_INLINE pc_span pc_span_from(uint8_t *p, size_t cap)
{
    pc_span s;
    s.buf = (p != NULL && cap != 0) ? p : NULL;
    s.cap = (s.buf != NULL) ? cap : 0;
    s.pos = 0;
    s.overflow = PROTO_FALSE;
    return s;
}

/** @brief Bind a read-only span to memory whose extent is only known at run time. */
PC_INLINE pc_cspan pc_cspan_from(const uint8_t *p, size_t len)
{
    pc_cspan s;
    s.buf = (p != NULL && len != 0) ? p : NULL;
    s.len = (s.buf != NULL) ? len : 0;
    s.pos = 0;
    s.err = PROTO_FALSE;
    return s;
}

/** @brief True when the span refers to real storage and every write so far has fit. */
PC_INLINE proto_bool pc_span_ok(pc_span s)
{
    return s.buf != NULL && !s.overflow;
}

/** @brief True when the span refers to real storage, whether or not a write has overflowed. */
PC_INLINE proto_bool pc_span_has_storage(pc_span s)
{
    return s.buf != NULL;
}

/** @brief True when the read-only span refers to real bytes and no read has run past the end. */
PC_INLINE proto_bool pc_cspan_ok(pc_cspan s)
{
    return s.buf != NULL && !s.err;
}

/**
 * @brief Bytes the payload needs - the backward direction.
 *
 * Equals the bytes written while everything fit. After an overflow it keeps counting, so a value
 * greater than @ref pc_span::cap is exactly the capacity the run-length constant should have had.
 */
PC_INLINE size_t pc_span_len(pc_span s)
{
    return s.pos;
}

/** @brief Writable bytes remaining (0 once the region is full or has overflowed). */
PC_INLINE size_t pc_span_room(pc_span s)
{
    return (s.pos < s.cap) ? (s.cap - s.pos) : 0;
}

/** @brief Rewind to empty, keeping the same storage and clearing the overflow flag. */
PC_INLINE void pc_span_reset(pc_span *s)
{
    s->pos = 0;
    s->overflow = PROTO_FALSE;
}

/**
 * @brief The sub-span starting @p off bytes into @p s (a fresh, empty cursor over that tail).
 *
 * Clamps rather than trapping: an @p off past the end yields an empty span, so a truncated frame
 * produces a zero-capacity region that writes nothing instead of a pointer past the allocation.
 */
PC_INLINE pc_span pc_span_after(pc_span s, size_t off)
{
    if (!pc_span_has_storage(s) || off >= s.cap)
    {
        return pc_span_from(NULL, 0);
    }
    return pc_span_from(s.buf + off, s.cap - off);
}

/** @brief The first @p n bytes of @p s as a fresh span, clamped to what @p s actually has. */
PC_INLINE pc_span pc_span_first(pc_span s, size_t n)
{
    if (!pc_span_has_storage(s))
    {
        return pc_span_from(NULL, 0);
    }
    return pc_span_from(s.buf, (n < s.cap) ? n : s.cap);
}

/**
 * @brief A read-only view of what has been produced into @p s.
 *
 * The natural handoff once a frame is built: the length comes from the span's own cursor, so a
 * reader cannot be handed a length that disagrees with the bytes. Yields an empty view if the span
 * overflowed - a partially written frame must not be transmitted as though it were whole.
 */
PC_INLINE pc_cspan pc_span_produced(pc_span s)
{
    if (!pc_span_ok(s))
    {
        return pc_cspan_from(NULL, 0);
    }
    return pc_cspan_from(s.buf, s.pos);
}

/** @brief A read-only view of the first @p len bytes of @p s, clamped to its capacity. */
PC_INLINE pc_cspan pc_span_read(pc_span s, size_t len)
{
    if (!pc_span_has_storage(s))
    {
        return pc_cspan_from(NULL, 0);
    }
    return pc_cspan_from(s.buf, (len < s.cap) ? len : s.cap);
}

#endif // PROTOCORE_SPAN_H
