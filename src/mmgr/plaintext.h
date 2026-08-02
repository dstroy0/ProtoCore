// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file plaintext.h
 * @brief Plaintext pool accessor - transient borrows whose bytes are not secret.
 *
 * Fixed BSS arenas that codec / protocol handlers borrow transient working
 * memory from, instead of each feature carrying its own dedicated scratch
 * buffer. Many such buffers are mutually exclusive in time - a connection is
 * doing HTTP *or* WebSocket *or* SSH at any instant, and a worker runs one
 * event to completion before the next - so overlapping them in one arena cuts
 * peak RAM without weakening the zero-heap / deterministic guarantee (fixed
 * size, no runtime growth).
 *
 * There is one arena per slot (::PC_REG_POOL_SLOTS): one per server worker, plus
 * the ghost, which is the library's own. pc_plaintext_alloc() resolves the
 * caller's slot with pc_worker_self(), so a borrow never crosses workers.
 *
 * **Model - region reset per dispatch.** pc_plaintext_alloc() bump-allocates from the
 * caller's arena; pc_plaintext_reset() empties that one arena in O(1).
 * dispatch_event() calls pc_plaintext_reset() before handing an event to its
 * protocol handler, so a borrow is valid only until the handler returns. There
 * is no per-allocation free - the whole arena is reclaimed at once.
 *
 * **Race-safety.** Each arena has exactly one accessor - the worker that owns
 * its slot - so allocation is a plain bump with no lock. Work reaches a worker
 * through its queue, so a context that is not a worker never borrows: the lwIP
 * callbacks run in tcpip_thread and only fill the rx ring + enqueue events, and
 * an ISR posts a fixed-size item to a preempt-queue lane whose task does the
 * work. In debug builds an owner assertion (pc_platform_context_id()) records
 * the first context to touch each arena and fails loud if a second one does,
 * turning a future mistake into an immediate visible failure instead of a
 * silent cross-core race.
 *
 * **Exhaustion-safety.** Borrows live only within one dispatch and are
 * auto-reclaimed by the reset, so a forgotten free cannot accumulate (no
 * creeping exhaustion). An over-budget pc_plaintext_alloc() returns nullptr; every
 * caller must take a defined fail-closed path (drop the optional optimization,
 * close the connection, answer 503) and must never dereference a null borrow.
 *
 * **No implicit zeroing.** pc_plaintext_alloc() returns uninitialized memory and the
 * reset does not wipe. This pool is for plaintext: anything whose bytes are key
 * material belongs in the secure pool (mmgr/secure.h), which is the same
 * mechanism with one added control - reclaiming wipes, before the bytes become
 * available again.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_PLAINTEXT_H
#define PROTOCORE_PLAINTEXT_H

#include "protocore_config.h"
#include "shared_primitives/span.h"
#include <stddef.h>

/**
 * @brief Slots in the plaintext pool.
 *
 * Sized off the ghost rather than the worker count so the invariant is the definition: the pool
 * must reach the highest slot any caller can resolve to, and that is the ghost.
 *
 * The secure pool states its own count (::PC_SEC_POOL_SLOTS). They are equal today and neither is
 * derived from the other - one pool growing a slot is not a reason for the other to.
 */
#define PC_REG_POOL_SLOTS (PC_GHOST_WORKER_SLOT + 1)

/**
 * @brief Borrow @p n bytes of plaintext, aligned to @p align.
 *
 * The returned pointer is valid only until the next pc_plaintext_reset() (i.e. only
 * within the current session dispatch). Returns nullptr if the request does not
 * fit the remaining arena - callers MUST handle null and fail closed.
 *
 * @param n     bytes requested (0 yields a valid non-null pointer when space
 *              remains).
 * @param align required alignment in bytes, a power of two (0 selects the
 *              platform default).
 * @return pointer to @p n writable bytes, or nullptr if it does not fit.
 */
void *pc_plaintext_alloc(size_t n, size_t align);

/**
 * @brief Borrow @p n bytes as a span whose capacity is bound to the allocation.
 *
 * The preferred form. pc_plaintext_alloc() hands back a bare pointer, which leaves the caller to carry
 * the length separately and keep the two in agreement by hand at every call - the same severed
 * binding that makes `sizeof()` on a converted array read 4 bytes instead of the extent. Here one
 * argument sets both fields, so the run length is stated once and cannot drift.
 *
 * Fails closed: an over-budget request yields `{nullptr, 0}`, so a caller that omits the
 * pc_span_ok() check writes nothing rather than dereferencing null. Callers should still check and
 * take their defined fail-closed path.
 *
 * @param n     bytes requested.
 * @param align required alignment in bytes, a power of two (0 selects the platform default).
 * @return a span over @p n writable bytes, or an empty span if it does not fit.
 */
pc_span pc_plaintext_span(size_t n, size_t align);

/**
 * @brief Reclaim the whole arena (empties it).
 *
 * Called by server_tick() before each event dispatch. Invalidates every pointer
 * previously returned by pc_plaintext_alloc().
 */
void pc_plaintext_reset(void);

/**
 * @brief Capture the current arena offset (a savepoint for pc_plaintext_release()).
 * @return an opaque mark to pass to pc_plaintext_release().
 */
size_t pc_plaintext_mark(void);

/**
 * @brief Reclaim everything allocated since @p mark (LIFO).
 *
 * Restores the arena to a previous pc_plaintext_mark(), freeing every pc_plaintext_alloc()
 * made in between. Marks must be released in reverse order (nested scopes). Use
 * PlaintextScope for return-safe scoping.
 *
 * @param mark a value previously returned by pc_plaintext_mark() (must be <= the
 *             current offset).
 */
void pc_plaintext_release(size_t mark);

/** @brief Bytes currently handed out (0 immediately after a reset). */
size_t pc_plaintext_used(void);

/** @brief Largest pc_plaintext_used() value seen since boot (for sizing the arena). */
size_t pc_plaintext_high_water(void);

/** @brief Total arena capacity in bytes (PC_PLAINTEXT_ARENA_SIZE). */
size_t pc_plaintext_capacity(void);

/**
 * @brief True if @p p points inside the plaintext pool.
 *
 * Ownership is an address-range property, not bookkeeping. The slot count and slot size are both
 * compile-time, so the whole pool is ONE region of known extent and the test is a single unsigned
 * subtract and compare - no loop, no per-slot comparison, no per-allocation metadata. A pointer
 * below the base wraps to a huge offset and fails the same bound as one past the end, so a buffer
 * overrun cannot test as still-inside.
 *
 * This is the plaintext half of the control strategy. The secure pool is a disjoint region and
 * answers its own question, so a secure-pool pointer can never be accepted where a plaintext one
 * is required, or the reverse.
 */
bool pc_plaintext_owns(const void *p);

/**
 * @brief Which plaintext slot owns @p p, or -1 if @p p is not in the plaintext pool.
 *
 * A divide by a compile-time constant, so a multiply-and-shift rather than real division. Use to
 * assert that a borrow being handed back belongs to the calling worker: crossing slots is the one
 * way the lock-free single-accessor invariant can be violated, and this makes it checkable.
 */
int pc_plaintext_slot_of(const void *p);

/**
 * @brief A plaintext borrow whose acquire and release are one call each.
 *
 * PlaintextScope + pc_plaintext_span costs three crossings of this module's boundary: mark, alloc, then
 * release. Mark and alloc always happen together, so one of those is structure rather than work.
 * This does both in the constructor and the release in the destructor - two calls, and the slot is
 * resolved once instead of twice.
 *
 * The pool's state stays private to the .cpp; that is exactly why these cannot be inlined instead,
 * and why LTO would have been the alternative (it is unavailable here - the ESP32 core does not link
 * with -flto).
 *
 * Fails closed like every other borrow: an exhausted arena leaves span() empty, so a caller that
 * omits the check writes nothing rather than dereferencing null.
 */
class PlaintextBorrow
{
  public:
    PlaintextBorrow(size_t n, size_t align);
    ~PlaintextBorrow();
    PlaintextBorrow(const PlaintextBorrow &) = delete;
    PlaintextBorrow &operator=(const PlaintextBorrow &) = delete;

    /** @brief The borrowed region; empty if the arena could not satisfy it. */
    const pc_span &span() const
    {
        return m_span;
    }

  private:
    size_t m_mark;
    pc_span m_span;
};

/**
 * @brief RAII scope guard for transient plaintext borrows.
 *
 * Marks the arena on construction and restores it on destruction, so every
 * pc_plaintext_alloc() made within the scope is reclaimed on *every* exit path
 * (return, break, fall-through) - the safe way to borrow transient plaintext in a
 * function with multiple returns. Scopes must nest (LIFO); the per-dispatch
 * pc_plaintext_reset() is the backstop if one is ever skipped.
 */
class PlaintextScope
{
  public:
    PlaintextScope() : m_mark(pc_plaintext_mark())
    {
    }
    ~PlaintextScope()
    {
        pc_plaintext_release(m_mark);
    }
    PlaintextScope(const PlaintextScope &) = delete;
    PlaintextScope &operator=(const PlaintextScope &) = delete;

  private:
    size_t m_mark;
};

#endif // PROTOCORE_PLAINTEXT_H
