// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protomem.h
 * @brief The byte-span operations: copy, move, compare, fill.
 *
 * The four things a caller reaches for when it has two pointers and a length. They are one module
 * because they are one question asked four ways, and because a library that owns its own footprint
 * owns the operations that move it.
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
 * @brief The byte-span module.
 *
 * @var MemNs::cpy
 * Copy @c n bytes from @c src to @c dst. The two must not overlap; @ref MemNs::move is the one that
 * handles that. Steps the machine word.
 *
 * @var MemNs::move
 * Copy @c n bytes from @c src to @c dst, correct when the two overlap. Forwards to @ref MemNs::cpy
 * whenever the direction allows a word-at-a-time step, and walks downward when it does not.
 *
 * @var MemNs::cmp
 * Order @c n bytes at @c a against @c b: negative, zero or positive on the first byte that differs,
 * compared as unsigned. Word steps while both sides sit on a lane boundary, and the lane math names
 * the differing byte rather than searching for it.
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
