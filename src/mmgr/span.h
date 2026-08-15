// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
 * capacity, so a caller that skips span.ok() writes nothing instead of dereferencing null.
 *
 * The reading accessors take the region by value and span.reset() takes a pointer, so a caller can
 * see at the call site which one writes.
 *
 * The bodies live in span.c and are reached through @ref span.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SPAN_H
#define PROTOCORE_SPAN_H

#include "protocore_config.h" // the entry point: protocore_types.h for proto_bool / size_t / uint8_t

PROTOCORE_BEGIN_DECLS

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
} protocore_span;

/**
 * @brief A read-only byte region.
 *
 * Bind with span.cfrom() and check with span.cok(). A read is bounded by the region itself, which is
 * why nothing here takes a separate length.
 */
typedef struct
{
    const uint8_t *buf; ///< first byte, or NULL when there is nothing to read
    size_t len;         ///< readable bytes at @ref buf (0 whenever @ref buf is NULL)
    size_t pos;         ///< read cursor
    proto_bool err;     ///< sticky: a read ran past @ref len
} protocore_cspan;

/**
 * @brief The bounded-region module. Every constructor normalizes the empty case; every sub-region
 *        clamps to what its parent holds.
 *
 * @var SpanNs::from
 * Bind a span to memory whose extent is only known at run time. Storage without capacity, or capacity
 * without storage, normalizes to `{NULL, 0}`.
 *
 * @var SpanNs::ok
 * True when the span refers to real storage and every write so far has fit.
 *
 * @var SpanNs::has_storage
 * True when the span refers to real storage, whether or not a write has overflowed.
 *
 * @var SpanNs::len
 * Bytes the payload needs - the backward direction. Equals the bytes written while everything fit.
 * After an overflow it keeps counting, so a value greater than @ref protocore_span::cap is exactly the
 * capacity the run-length constant should have had.
 *
 * @var SpanNs::room
 * Writable bytes remaining (0 once the region is full or has overflowed).
 *
 * @var SpanNs::reset
 * Rewind to empty, keeping the same storage and clearing the overflow flag. Takes a pointer, so the
 * call site shows that this one writes.
 *
 * @var SpanNs::after
 * The sub-span starting @c off bytes into @c s, a fresh empty cursor over that tail. Clamps rather
 * than trapping: an @c off past the end yields an empty span, which writes nothing.
 *
 * @var SpanNs::first
 * The first @c n bytes of @c s as a fresh span, clamped to what @c s actually has.
 *
 * @var SpanNs::produced
 * A read-only view of what has been produced into @c s: the length comes from the span's own cursor,
 * so a reader is never handed a length that disagrees with the bytes. Yields an empty view when the
 * span overflowed.
 *
 * @var SpanNs::read
 * A read-only view of the first @c len bytes of @c s, clamped to its capacity.
 *
 * @var SpanNs::cfrom
 * Bind a read-only span to memory whose extent is only known at run time. Normalizes the empty case
 * the way @ref SpanNs::from does.
 *
 * @var SpanNs::cok
 * True when the read-only span refers to real bytes and no read has run past the end.
 *
 * No storage member: every accessor works on the region the caller passes and holds nothing of its
 * own.
 */
typedef struct
{
    protocore_span (*from)(uint8_t *p, size_t cap);
    proto_bool (*ok)(protocore_span s);
    proto_bool (*has_storage)(protocore_span s);
    size_t (*len)(protocore_span s);
    size_t (*room)(protocore_span s);
    void (*reset)(protocore_span *s);
    protocore_span (*after)(protocore_span s, size_t off);
    protocore_span (*first)(protocore_span s, size_t n);
    protocore_cspan (*produced)(protocore_span s);
    protocore_cspan (*read)(protocore_span s, size_t len);
    protocore_cspan (*cfrom)(const uint8_t *p, size_t len);
    proto_bool (*cok)(protocore_cspan s);
} SpanNs;

// The accessors, in span.c. Named here because the table below has to name them, and prefixed
// because that puts them in the linker's namespace.
protocore_span protocore_span_from(uint8_t *p, size_t cap);
proto_bool protocore_span_ok(protocore_span s);
proto_bool protocore_span_has_storage(protocore_span s);
size_t protocore_span_len(protocore_span s);
size_t protocore_span_room(protocore_span s);
void protocore_span_reset(protocore_span *s);
protocore_span protocore_span_after(protocore_span s, size_t off);
protocore_span protocore_span_first(protocore_span s, size_t n);
protocore_cspan protocore_span_produced(protocore_span s);
protocore_cspan protocore_span_read(protocore_span s, size_t len);
protocore_cspan protocore_cspan_from(const uint8_t *p, size_t len);
proto_bool protocore_cspan_ok(protocore_cspan s);

/**
 * @brief The names, aliased.
 *
 * `static const` and initialized here, not declared `extern` against a definition in the .c: a
 * translation unit that can see this initializer knows which function each member holds, so a member
 * read folds away and the call to the body in span.c is direct, leaving the table referenced by
 * nothing for the linker to drop.
 *
 * `unused` because a header this wide is included by files that take none of it.
 */
// Designated, so a member's position in the struct does not decide what it binds to.
static const SpanNs span __attribute__((unused)) = {.from = protocore_span_from,
                                                    .ok = protocore_span_ok,
                                                    .has_storage = protocore_span_has_storage,
                                                    .len = protocore_span_len,
                                                    .room = protocore_span_room,
                                                    .reset = protocore_span_reset,
                                                    .after = protocore_span_after,
                                                    .first = protocore_span_first,
                                                    .produced = protocore_span_produced,
                                                    .read = protocore_span_read,
                                                    .cfrom = protocore_cspan_from,
                                                    .cok = protocore_cspan_ok};

PROTOCORE_END_DECLS

#endif // PROTOCORE_SPAN_H
