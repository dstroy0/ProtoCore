// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file secure.h
 * @brief Secure pool accessor - borrows that hold key material.
 *
 * The same pool mechanism as the plaintext side (::pc_arena, mmgr/arena), instantiated a second
 * time. The resource and its mechanics are identical - one instance per worker slot, compile-time
 * sized, double-ended, fail-closed, high-water reported. What differs is the access and control
 * layer, and the difference is security:
 *
 *   - **Release wipes.** pc_secure_release() and pc_secure_reset() zero the region being reclaimed
 *     before it becomes available again. On the plaintext side reclaiming is just an offset move; a
 *     secret must not outlive its borrow, so here the wipe IS the release. That makes the rule
 *     structural instead of a discipline every caller has to remember on every return path - the
 *     form that had already been missed on two of the SSH key-exchange error paths.
 *
 *   - **Disjoint region.** The two pools occupy different addresses, so pc_secure_owns() and
 *     pc_plaintext_owns() are mutually exclusive by construction. A secure borrow can never be
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

#include "protocore_config.h"
#include "server/mmgr/span.h"
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Slots in the secure pool.
 *
 * Sized off the ghost rather than the worker count so the invariant is the definition: the pool
 * must reach the highest slot any caller can resolve to, and that is the ghost.
 *
 * The plaintext pool states its own count (::PC_REG_POOL_SLOTS). They are equal today and neither
 * is derived from the other - one pool growing a slot is not a reason for the other to.
 */
#define PC_SEC_POOL_SLOTS (PC_GHOST_WORKER_SLOT + 1)

/**
 * @brief Securely zero @p len bytes at @p ptr with a volatile store the compiler cannot elide.
 *
 * The canonical wipe. Use this, never memset(), for any buffer that held key material: a plain
 * memset() whose result is never observed (the buffer dies at return) is a dead store and may be
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
static inline void pc_secure_wipe(void *ptr, size_t len)
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
 * Returns uninitialized memory (the pool wipes on release, not on hand-out). Returns nullptr if the
 * request does not fit - callers MUST handle null and fail closed.
 *
 * @param n     bytes requested.
 * @param align required alignment in bytes, a power of two (0 selects the platform default).
 */
void *pc_secure_alloc(size_t n, size_t align);

/**
 * @brief Borrow @p n secure bytes as a span whose capacity is bound to the allocation.
 *
 * The preferred form: one argument sets both fields, so the capacity cannot drift from what was
 * reserved. An over-budget request yields an empty span, so an omitted pc_span_ok() check writes
 * nothing rather than dereferencing null.
 */
pc_span pc_secure_span(size_t n, size_t align);

/** @brief Capture the current position, to be handed to pc_secure_release(). */
size_t pc_secure_mark(void);

/**
 * @brief Wipe and reclaim everything borrowed since @p mark.
 *
 * The wipe happens BEFORE the position moves, so the bytes are already zero at the instant they
 * become available again - there is no window in which a subsequent borrow could be handed memory
 * still holding the previous tenant's key material. Use SecureScope rather than calling this by
 * hand, so no return path can skip it.
 */
void pc_secure_release(size_t mark);

/** @brief Wipe and reclaim the whole slot. */
void pc_secure_reset(void);

/** @brief Bytes currently handed out. */
size_t pc_secure_used(void);

/** @brief Peak bytes ever handed out, for sizing PC_SECURE_ARENA_SIZE. */
size_t pc_secure_high_water(void);

/** @brief Total per-slot capacity in bytes (PC_SECURE_ARENA_SIZE). */
size_t pc_secure_capacity(void);

/**
 * @brief True if @p p points inside the secure pool.
 *
 * One unsigned subtract and compare: the slot count and slot size are compile-time, so the pool is
 * one region of known extent. Mutually exclusive with pc_plaintext_owns() because the regions are
 * disjoint - which is the whole access control, with no per-allocation bookkeeping.
 */
bool pc_secure_owns(const void *p);

/** @brief Which secure slot owns @p p, or -1 if @p p is not in the secure pool. */
int pc_secure_slot_of(const void *p);

/**
 * @brief A secure borrow whose acquire and release are one call each.
 *
 * SecureScope + pc_secure_span crosses this module's boundary three times (mark, alloc, release);
 * mark and alloc always happen together, so one of those is structure rather than work. Measured on
 * an ESP32-S3: 129 cycles against 172 for the three-call form.
 *
 * Releases exactly like SecureScope - the region is wiped BEFORE the position moves, so no later
 * borrow can be handed memory still holding this one's key material.
 */
class SecureBorrow
{
  public:
    SecureBorrow(size_t n, size_t align);
    ~SecureBorrow();
    SecureBorrow(const SecureBorrow &) = delete;
    SecureBorrow &operator=(const SecureBorrow &) = delete;

    /** @brief The borrowed region; empty if the pool could not satisfy it. */
    const pc_span &span() const
    {
        return m_span;
    }

  private:
    size_t m_mark;
    pc_span m_span;
};

/**
 * @brief RAII scope guard for secure borrows - marks on entry, wipes and reclaims on every exit.
 *
 * The reason to prefer this over pc_secure_release(): a secret's wipe must happen on *every* path,
 * including the early returns taken when a peer sends something malformed, and those are exactly the
 * paths a hand-written wipe gets forgotten on.
 */
class SecureScope
{
  public:
    SecureScope() : m_mark(pc_secure_mark())
    {
    }
    ~SecureScope()
    {
        pc_secure_release(m_mark);
    }
    SecureScope(const SecureScope &) = delete;
    SecureScope &operator=(const SecureScope &) = delete;

  private:
    size_t m_mark;
};

#endif // PROTOCORE_SECURE_H
