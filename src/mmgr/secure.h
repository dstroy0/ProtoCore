// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file secure.h
 * @brief Secure pool accessor - borrows that hold key material.
 *
 * The same pool mechanism as the plaintext side (::protocore_arena, mmgr/arena), instantiated a second
 * time. The resource and its mechanics are identical - one instance per worker slot, compile-time
 * sized, double-ended, fail-closed, high-water reported. What differs is the access and control
 * layer, and the difference is security:
 *
 *   - **Release wipes.** protocore_secure_release() and protocore_secure_reset() zero the region being reclaimed
 *     before it becomes available again. On the plaintext side reclaiming is just an offset move; a
 *     secret must not outlive its borrow, so here the wipe IS the release. That makes the rule
 *     structural instead of a discipline every caller has to remember on every return path - the
 *     form that had already been missed on two of the SSH key-exchange error paths.
 *
 *   - **Disjoint region.** The two pools occupy different addresses, so protocore_secure_owns() and
 *     protocore_plaintext_owns() are mutually exclusive by construction. A secure borrow can never be
 *     accepted where a plaintext one is expected, or the reverse, with no tagging and no metadata.
 *
 * **What belongs here.** Anything whose bytes are key material: shared secrets, private scalars,
 * derived keys, and the working state of an operation over them. Public wire values (a peer's
 * public point, a ciphertext about to be transmitted, a staging buffer for an outbound frame) belong
 * in the plaintext pool - putting them here only shrinks the room left for real secrets.
 *
 * **Lifetime is not the axis.** Both pools carry long-lived and ephemeral allocations; the pool a
 * borrow comes from is decided by whether its contents are secret, nothing else.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SECURE_H
#define PROTOCORE_SECURE_H

#include "mmgr/protomem.h"
#include "mmgr/span.h"
#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

/**
 * @brief Slots in the secure pool.
 *
 * Sized off the ghost rather than the worker count so the invariant is the definition: the pool
 * must reach the highest slot any caller can resolve to, and that is the ghost.
 *
 * The plaintext pool states its own count (::PROTOCORE_REG_POOL_SLOTS). They are equal today and neither
 * is derived from the other - one pool growing a slot is not a reason for the other to.
 */
#define PROTOCORE_SEC_POOL_SLOTS (PROTOCORE_GHOST_WORKER_SLOT + 1)

/**
 * @brief The pool's own state: the per-slot arenas and the block they hand out of. Defined in
 *        secure.c.
 *
 * Incomplete here, so nothing outside secure.c can name a field, size the block or reach a slot
 * around the accessors. The calls below take no handle - each resolves the calling worker's slot
 * itself - so this is named only to give the state one owner.
 */
struct SecureInternal;

/**
 * @brief The secure pool: the same mechanism as @ref plain, with reclaiming that wipes.
 *
 * @var SecureNs::alloc         borrow @c n bytes aligned to @c align, or NULL if it does not fit
 * @var SecureNs::span          the same borrow as a span, so the length travels with the pointer
 * @var SecureNs::persist_span  borrow from the end no mark walks, zeroed, for the life of the program
 * @var SecureNs::reset       wipe and empty the calling worker's arena
 * @var SecureNs::mark        capture the arena offset, to release back to
 * @var SecureNs::release     wipe everything borrowed since a mark, then reclaim it, LIFO
 * @var SecureNs::used        bytes currently handed out
 * @var SecureNs::high_water  the largest @c used any slot has reached, for sizing the arena
 * @var SecureNs::capacity    one arena's total extent
 * @var SecureNs::owns        whether a pointer lies inside the pool
 * @var SecureNs::slot_of     which slot holds a pointer, or -1
 * @var SecureNs::internal    the per-slot arenas and their backing block, described only in secure.c
 *
 * @ref SecureNs::owns and protocore_plaintext_owns() are mutually exclusive: the two pools are disjoint
 * regions, so a secret can never be handed back where plaintext is expected, or the reverse.
 *
 * The arenas and their backing bytes belong to secure.c, and a caller reaches its own by calling -
 * the slot is resolved from the worker, never passed in. @ref SecureNs::internal names that state
 * without describing it.
 */
typedef struct
{
    void *(*alloc)(size_t n, size_t align);
    protocore_span (*span)(size_t n, size_t align);
    protocore_span (*persist_span)(size_t n);
    void (*reset)(void);
    size_t (*mark)(void);
    void (*release)(size_t mark);
    size_t (*used)(void);
    size_t (*high_water)(void);
    size_t (*capacity)(void);
    proto_bool (*owns)(const void *p);
    int (*slot_of)(const void *p);

    struct SecureInternal *internal;
} SecureNs;

/**
 * @brief The pool's state, the one object every call in secure.c reaches its slot through.
 *
 * Zero at boot: the arenas and the block they hand out of bind on the first borrow, so a build that
 * never borrows a secret carries neither.
 */
extern struct SecureInternal protocore_secure_state;

/**
 * @brief Securely zero @p len bytes at @p ptr with a volatile store the compiler cannot elide.
 *
 * The canonical wipe. Use this, never mem.zero(), for any buffer that held key material: a plain
 * store whose result is never observed (the buffer dies at return) is a dead store and may be
 * optimized away, leaving the bytes in memory. The volatile write forces it even when the memory is
 * never read again.
 *
 * It lives here because wiping is a memory-manager operation, not a cryptographic one. It is the
 * secure pool's own reclaim primitive, and it is equally what any owner needs for storage that was
 * never in a pool at all - session key material, a caller's own buffer. Crypto is a consumer of it,
 * not its home.
 *
 * @param ptr  Buffer to wipe.
 * @param len  Number of bytes to zero.
 */
static inline void protocore_secure_wipe(void *ptr, size_t len)
{
    // Machine-width stores, with byte head/tail only for unaligned edges. Both edges are normally
    // empty - pool borrows are aligned and their lengths rounded up - so this is the word loop.
    // volatile is per-access, so a volatile word store is exactly as un-elidable as a volatile byte
    // store; the guarantee is unchanged and the store count drops by the width.
    volatile uint8_t *b = (volatile uint8_t *)ptr;
    while (len != 0 && (((uintptr_t)b & (sizeof(uintptr_t) - 1)) != 0))
    {
        *b++ = 0;
        len--;
    }
    volatile uintptr_t *w = (volatile uintptr_t *)b;
    while (len >= sizeof(uintptr_t))
    {
        *w++ = 0;
        len -= sizeof(uintptr_t);
    }
    b = (volatile uint8_t *)w;
    while (len != 0)
    {
        *b++ = 0;
        len--;
    }
}

/**
 * @brief Borrow @p n bytes of secure storage, aligned to @p align.
 *
 * Returns uninitialized memory (the pool wipes on release, not on hand-out). Returns NULL if the
 * request does not fit - callers MUST handle null and fail closed.
 *
 * @param n     bytes requested.
 * @param align required alignment in bytes, a power of two (0 selects the platform default).
 */
void *protocore_secure_alloc(size_t n, size_t align);

/**
 * @brief Borrow @p n secure bytes as a span whose capacity is bound to the allocation.
 *
 * The preferred form: one argument sets both fields, so the capacity cannot drift from what was
 * reserved. An over-budget request yields an empty span, so an omitted protocore_span_ok() check writes
 * nothing rather than dereferencing null.
 */
protocore_span protocore_secure_span(size_t n, size_t align);

/**
 * @brief Borrow @p n secure bytes that outlive every mark and release.
 *
 * The arena is double ended (mmgr/arena.h): this takes the persistent end, which grows up from the
 * base, while protocore_secure_span() bumps down from the top. A mark walks the top end only, so a table
 * taken here is not reclaimed by any release, nor by protocore_secure_reset(). The bytes come back zeroed.
 *
 * For storage a module holds for the life of the program: a credential table, a key schedule bound
 * once at setup. A working set borrowed and returned within one call takes protocore_secure_span().
 */
protocore_span protocore_secure_persist_span(size_t n);

/** @brief Capture the current position, to be handed to protocore_secure_release(). */
size_t protocore_secure_mark(void);

/**
 * @brief Wipe and reclaim everything borrowed since @p mark.
 *
 * The wipe happens BEFORE the position moves, so the bytes are already zero at the instant they
 * become available again - there is no window in which a subsequent borrow could be handed memory
 * still holding the previous tenant's key material. Every return path out of a borrow must reach
 * this, including the early ones taken when a peer sends something malformed.
 */
void protocore_secure_release(size_t mark);

/** @brief Wipe and reclaim the whole slot. */
void protocore_secure_reset(void);

/** @brief Bytes currently handed out. */
size_t protocore_secure_used(void);

/** @brief Peak bytes ever handed out, for sizing PROTOCORE_SECURE_ARENA_SIZE. */
size_t protocore_secure_high_water(void);

/** @brief Total per-slot capacity in bytes (PROTOCORE_SECURE_ARENA_SIZE). */
size_t protocore_secure_capacity(void);

/**
 * @brief True if @p p points inside the secure pool.
 *
 * One unsigned subtract and compare against the block the first borrow bound: the slot count and
 * slot size are compile-time, so the pool is one region of known extent. Mutually exclusive with
 * protocore_plaintext_owns() because the regions are disjoint - which is the whole access control, with no
 * per-allocation bookkeeping. False while no slot has borrowed, when no pointer into the pool
 * exists to ask about.
 */
proto_bool protocore_secure_owns(const void *p);

/** @brief Which secure slot owns @p p, or -1 if @p p is not in the secure pool. */
int protocore_secure_slot_of(const void *p);

/**
 * @brief The names, aliased.
 *
 * `static const` and initialized here rather than defined in the .c, for the reason secure.c states
 * about its storage: a table object in the .c names every member, so a build that only ever calls
 * @ref SecureNs::reset would still reference @ref SecureNs::alloc, and through it `bind()` and the
 * pool's backing bytes. Initialized here, the member read resolves in the reading translation unit
 * and the table is dropped, so `--gc-sections` still reclaims the storage from firmware that never
 * borrows a secret.
 *
 * `unused` because this header reaches files that take none of it.
 */
// Designated, so a member's position in the struct does not decide what it binds to.
static const SecureNs secure __attribute__((unused)) = {.alloc = protocore_secure_alloc,
                                                        .span = protocore_secure_span,
                                                        .persist_span = protocore_secure_persist_span,
                                                        .reset = protocore_secure_reset,
                                                        .mark = protocore_secure_mark,
                                                        .release = protocore_secure_release,
                                                        .used = protocore_secure_used,
                                                        .high_water = protocore_secure_high_water,
                                                        .capacity = protocore_secure_capacity,
                                                        .owns = protocore_secure_owns,
                                                        .slot_of = protocore_secure_slot_of,
                                                        .internal = &protocore_secure_state};

PROTOCORE_END_DECLS

#endif // PROTOCORE_SECURE_H
