// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protomem.h
 * @brief The byte-span operations: copy, move, compare, fill.
 *
 * In mmgr because they move what mmgr hands out. Every allocation leaves the arena rounded up to
 * PC_ARENA_ALIGN and starting on it, so a span's trailing lanes belong to that same allocation.
 *
 * The module exports one symbol, @ref mem. Everything in protomem.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PROTOMEM_H
#define PROTOCORE_PROTOMEM_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

/**
 * @brief The byte-span module. Every access is one register-width load or store.
 *
 * @var MemNs::cpy
 * Copy @c n bytes from @c src to @c dst, which must not overlap. A source that is not co-aligned with
 * the destination is funnelled: two shifts and an OR assemble the wanted word from the two aligned
 * words holding it. A partial word at either end is one masked store, so the lanes past the span go
 * to zero rather than costing a byte walk.
 *
 * @var MemNs::move
 * Copy @c n bytes from @c src to @c dst, correct when the two overlap. Every direction but one reads
 * each byte before the copy reaches it and goes through @ref MemNs::cpy; a destination ahead of the
 * source inside it walks down, still a word per step.
 *
 * @var MemNs::cmp
 * Order @c n bytes at @c a against @c b: negative, zero or positive on the first byte that differs,
 * compared as unsigned. Both operands must hold @c n readable bytes.
 *
 * **Not constant time.** It stops at the first difference, so how long it runs says how many leading
 * bytes matched. A secret comparison uses pc_ct_eq (crypto/ct_eq.h) instead.
 *
 * @var MemNs::set
 * Write @c v into @c n bytes at @c dst.
 *
 * @var MemNs::zero
 * Write zero into @c n bytes at @c dst.
 *
 * No storage member: every operation works on the caller's pointers and holds nothing of its own.
 */
typedef struct
{
    void (*cpy)(void *dst, const void *src, size_t n);
    void (*move)(void *dst, const void *src, size_t n);
    int (*cmp)(const void *a, const void *b, size_t n);
    void (*set)(void *dst, unsigned char v, size_t n);
    void (*zero)(void *dst, size_t n);
} MemNs;

/** @brief The one symbol this module exports. */
extern const MemNs mem;

PROTO_END_DECLS

#endif // PROTOCORE_PROTOMEM_H
