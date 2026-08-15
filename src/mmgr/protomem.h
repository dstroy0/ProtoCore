// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protomem.h
 * @brief The byte-span operations: copy, move, compare, fill.
 *
 * In mmgr because they move what mmgr hands out. Every allocation leaves the arena rounded up to
 * PROTOCORE_ARENA_ALIGN and starting on it, so a span's trailing lanes belong to that same allocation.
 *
 * The module exports one symbol, @ref mem. Everything in protomem.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PROTOMEM_H
#define PROTOCORE_PROTOMEM_H

#include "mmgr/rawmemcpy.h" // the loads and stores every walk below steps with
#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

/**
 * @brief The byte-span module. Every access is one register-width load or store.
 *
 * @var MemNs::cpy
 * Copy @c n bytes from @c src to @c dst, which must not overlap. A source that is not co-aligned with
 * the destination is funnelled: two shifts and an OR assemble the wanted word from the two aligned
 * words holding it.
 *
 * A partial word at the end is one merged store rather than a byte walk: the lane mask takes the
 * span's bytes from the source and the destination's own word supplies the rest, so the store is a
 * whole word and nothing past @c n changes. @c dst is any span, whole borrow or offset into one.
 *
 * @var MemNs::move
 * Copy @c n bytes from @c src to @c dst, correct when the two overlap. Every direction but one reads
 * each byte before the copy reaches it and goes through @ref MemNs::cpy; a destination ahead of the
 * source inside it walks down, still a word per step.
 *
 * @var MemNs::cmp
 * Order @c n bytes at @c a against @c b: negative, zero or positive on the first byte that differs,
 * compared as unsigned. Both operands must hold @c n readable bytes; nothing stops at a terminator,
 * so this is a span comparison and not a string one.
 *
 * **Not constant time.** It stops at the first difference, so how long it runs says how many leading
 * bytes matched. A secret comparison uses protocore_ct_eq (crypto/ct_eq.h) instead.
 *
 * @var MemNs::chr
 * The first byte equal to @c c in @c n bytes at @c p, or NULL. @c n is the whole bound: a span has
 * no terminator, so nothing stops early.
 *
 * That is the difference from StrNs::find, which searches a STRING and stops at its NUL. Reach for
 * this one whenever the buffer legitimately carries NULs and its extent is already known - a decoded
 * credential, a wire frame - and for find when the run really does end at a terminator.
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
    const void *(*chr)(const void *p, size_t n, uint8_t c);
    void (*set)(void *dst, unsigned char v, size_t n);
    void (*zero)(void *dst, size_t n);
} MemNs;

// The span walks, in protomem.c. Named here because the table below has to name them, and prefixed
// because that puts them in the linker's namespace.
void protocore_mem_cpy(void *dst, const void *src, size_t n);
void protocore_mem_move(void *dst, const void *src, size_t n);
int protocore_mem_cmp(const void *a, const void *b, size_t n);
const void *protocore_mem_chr(const void *p, size_t n, uint8_t c);
void protocore_mem_set(void *dst, unsigned char v, size_t n);
void protocore_mem_zero(void *dst, size_t n);

/**
 * @brief The names, aliased.
 *
 * `static const` and initialized here, not declared `extern` against a definition in the .c: a
 * translation unit that can see this initializer knows which function each member holds, so a member
 * read folds away and the call to the walk in protomem.c is direct, leaving the table referenced by
 * nothing for the linker to drop.
 *
 * `unused` because a header this wide is included by files that take none of it.
 */
static const MemNs mem __attribute__((unused)) = {protocore_mem_cpy, protocore_mem_move, protocore_mem_cmp,
                                                  protocore_mem_chr, protocore_mem_set,  protocore_mem_zero};

PROTOCORE_END_DECLS

#endif // PROTOCORE_PROTOMEM_H
