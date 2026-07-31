// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file span.h
 * @brief A byte region whose run length is bound in both directions.
 *
 * Every buffer in this library has a declared, compile-time run length - SSH_PKT_BUF_SIZE,
 * PC_RSA_KEY_BYTES, and so on. The problem rule 19 exposes is not that the size is unknown; it is
 * that once an array becomes a borrowed region, the size gets *restated* at each use and the
 * restatements can disagree:
 *
 *     uint8_t *buf = (uint8_t *)scratch_alloc(SSH_PKT_BUF_SIZE, 16);
 *     handler(buf, &n, SSH_PKT_BUF_SIZE);   // nothing forces these two constants to match
 *
 * and `sizeof(buf)` silently becomes `sizeof(uint8_t *)` - 4 bytes on a 32-bit target - with no
 * diagnostic, because -Wsizeof-pointer-memaccess only fires for the memcpy / strcpy family.
 *
 * A span is stated once, from the constant that already names the size, and then carried. Nothing
 * here re-derives a length the code already knows: there is no deduction and no `sizeof` on a
 * borrow, only the one constant travelling with its storage.
 *
 * **Both directions, one object.** A fixed-capacity region in this library is never one-way. The
 * preempting queue is the reference: one depth constant sizes the lane, items go in at either end,
 * and pc_pq_high_water_lane() reports the peak actually reached so the constant can be right-sized.
 * A span carries the same pair:
 *
 *   - forward   `cap`  - reserves the storage and bounds every write
 *   - backward  `pos`  - what the payload actually needed, which **keeps counting past `cap`** on
 *                        overflow, so an undersized run-length constant reports the size it should
 *                        have been instead of merely failing
 *
 * That retires the `size_t *out_len` out-parameter that used to ride beside the buffer: capacity in
 * and produced length out are one object, and neither can be paired with the wrong buffer.
 *
 * **One byte-cursor API, not two.** This type is the storage-binding half - where the region came
 * from and how big it is - and shared_primitives/bytes.h is the verbs, pc_bw_put() and
 * pc_bw_put_be(). Length and status are this file's own pc_span_len() / pc_span_ok(); bytes.h no
 * longer restates them. There is no second byte-append API.
 *
 * **Fail-closed by construction.** A failed allocation yields a null pointer with zero capacity,
 * never a null pointer with a live capacity. A caller that forgets the check writes nothing into a
 * zero-capacity region instead of dereferencing null, so an omitted check drops a message rather
 * than crashing. Callers should still test pc_span_ok() and take their defined fail-closed path
 * (drop the optional work, close the connection, answer 503).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SPAN_H
#define PROTOCORE_SPAN_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief A writable byte region: the storage, the capacity that belongs to it, and what has been
 *        produced into it.
 *
 * Field names match the bytes.h write-cursor convention on purpose - see the file comment.
 */
struct pc_span
{
    uint8_t *buf;  ///< first byte, or nullptr when the region could not be obtained
    size_t cap;    ///< bytes writable at @ref buf (0 whenever @ref buf is nullptr)
    size_t pos;    ///< bytes the payload needs so far; keeps counting past @ref cap on overflow
    bool overflow; ///< set once a write did not fit; @ref pos then reports the size required
};

/**
 * @brief A read-only byte region.
 *
 * Bind with pc_cspan_from() and check with pc_cspan_ok(); pc_br_take_be() in bytes.h reads through
 * it. A read is bounded by the region itself, which is why nothing here takes a separate length.
 */
struct pc_cspan
{
    const uint8_t *buf; ///< first byte, or nullptr when there is nothing to read
    size_t len;         ///< readable bytes at @ref buf (0 whenever @ref buf is nullptr)
    size_t pos;         ///< read cursor
    bool err;           ///< sticky: a read ran past @ref len
};

/**
 * @brief Bind a span to memory whose extent is only known at run time.
 *
 * Normalizes the empty case so a span with storage but no capacity - or capacity but no storage -
 * cannot be constructed.
 */
inline pc_span pc_span_from(uint8_t *p, size_t cap)
{
    pc_span s;
    s.buf = (p != nullptr && cap != 0) ? p : nullptr;
    s.cap = (s.buf != nullptr) ? cap : 0;
    s.pos = 0;
    s.overflow = false;
    return s;
}

/** @brief Bind a read-only span to memory whose extent is only known at run time. */
inline pc_cspan pc_cspan_from(const uint8_t *p, size_t len)
{
    pc_cspan s;
    s.buf = (p != nullptr && len != 0) ? p : nullptr;
    s.len = (s.buf != nullptr) ? len : 0;
    s.pos = 0;
    s.err = false;
    return s;
}

/** @brief True when the span refers to real storage and every write so far has fit. */
inline bool pc_span_ok(const pc_span &s)
{
    return s.buf != nullptr && !s.overflow;
}

/** @brief True when the span refers to real storage, whether or not a write has overflowed. */
inline bool pc_span_has_storage(const pc_span &s)
{
    return s.buf != nullptr;
}

/** @brief True when the read-only span refers to real bytes and no read has run past the end. */
inline bool pc_cspan_ok(const pc_cspan &s)
{
    return s.buf != nullptr && !s.err;
}

/**
 * @brief Bytes the payload needs - the backward direction.
 *
 * Equals the bytes written while everything fit. After an overflow it keeps counting, so a value
 * greater than @ref pc_span::cap is exactly the capacity the run-length constant should have had.
 */
inline size_t pc_span_len(const pc_span &s)
{
    return s.pos;
}

/** @brief Writable bytes remaining (0 once the region is full or has overflowed). */
inline size_t pc_span_room(const pc_span &s)
{
    return (s.pos < s.cap) ? (s.cap - s.pos) : 0;
}

/** @brief Rewind to empty, keeping the same storage and clearing the overflow flag. */
inline void pc_span_reset(pc_span *s)
{
    s->pos = 0;
    s->overflow = false;
}

/**
 * @brief The sub-span starting @p off bytes into @p s (a fresh, empty cursor over that tail).
 *
 * Clamps rather than trapping: an @p off past the end yields an empty span, so a truncated frame
 * produces a zero-capacity region that writes nothing instead of a pointer past the allocation.
 */
inline pc_span pc_span_after(const pc_span &s, size_t off)
{
    if (!pc_span_has_storage(s) || off >= s.cap)
    {
        return pc_span_from(nullptr, 0);
    }
    return pc_span_from(s.buf + off, s.cap - off);
}

/** @brief The first @p n bytes of @p s as a fresh span, clamped to what @p s actually has. */
inline pc_span pc_span_first(const pc_span &s, size_t n)
{
    if (!pc_span_has_storage(s))
    {
        return pc_span_from(nullptr, 0);
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
inline pc_cspan pc_span_produced(const pc_span &s)
{
    if (!pc_span_ok(s))
    {
        return pc_cspan_from(nullptr, 0);
    }
    return pc_cspan_from(s.buf, s.pos);
}

/** @brief A read-only view of the first @p len bytes of @p s, clamped to its capacity. */
inline pc_cspan pc_span_read(const pc_span &s, size_t len)
{
    if (!pc_span_has_storage(s))
    {
        return pc_cspan_from(nullptr, 0);
    }
    return pc_cspan_from(s.buf, (len < s.cap) ? len : s.cap);
}

#endif // PROTOCORE_SPAN_H
