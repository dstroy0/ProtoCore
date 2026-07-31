// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file scratch.h
 * @brief Shared per-dispatch scratch arena (Layer 5, session-scoped memory).
 *
 * Fixed BSS arenas that codec / protocol handlers borrow transient working
 * memory from, instead of each feature carrying its own dedicated scratch
 * buffer. Many such buffers are mutually exclusive in time - a connection is
 * doing HTTP *or* WebSocket *or* SSH at any instant, and a worker runs one
 * event to completion before the next - so overlapping them in one arena cuts
 * peak DRAM without weakening the zero-heap / deterministic guarantee (fixed
 * size, no runtime growth).
 *
 * There is one arena per slot (PC_SCRATCH_SLOTS): one per worker, plus one the
 * SSH client task owns when PC_ENABLE_SSH_CLIENT is set. scratch_alloc()
 * resolves the caller's slot with pc_worker_self(), so a borrow never crosses
 * workers.
 *
 * **Model - region reset per dispatch.** scratch_alloc() bump-allocates from the
 * caller's arena; scratch_reset() empties that one arena in O(1).
 * dispatch_event() calls scratch_reset() before handing an event to its
 * protocol handler, so a borrow is valid only until the handler returns. There
 * is no per-allocation free - the whole arena is reclaimed at once.
 *
 * **Race-safety.** Each arena has exactly one accessor - the worker that owns
 * its slot - so allocation is a plain bump with no lock. Work reaches a worker
 * through its queue, so a context that is not a worker never borrows: the lwIP
 * callbacks run in tcpip_thread and only fill the rx ring + enqueue events, and
 * an ISR posts a fixed-size item to a preempt-queue lane whose task does the
 * work. In debug builds an owner-task assertion (xTaskGetCurrentTaskHandle)
 * records the first task to touch each arena and fails loud if a second one
 * does, turning a future mistake into an immediate visible failure instead of a
 * silent cross-core race.
 *
 * **Exhaustion-safety.** Borrows live only within one dispatch and are
 * auto-reclaimed by the reset, so a forgotten free cannot accumulate (no
 * creeping exhaustion). An over-budget scratch_alloc() returns nullptr; every
 * caller must take a defined fail-closed path (drop the optional optimization,
 * close the connection, answer 503) and must never dereference a null borrow.
 *
 * **No implicit zeroing.** scratch_alloc() returns uninitialized memory and the
 * reset does not wipe. This pool is for plaintext: anything whose bytes are key
 * material belongs in the secure pool (server/mmgr/secure.h), which is the same
 * mechanism with one added control - reclaiming wipes, before the bytes become
 * available again.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SCRATCH_H
#define PROTOCORE_SCRATCH_H

#include "protocore_config.h"
#include "shared_primitives/span.h"
#include <stddef.h>

/**
 * @brief Borrow @p n bytes of scratch, aligned to @p align.
 *
 * The returned pointer is valid only until the next scratch_reset() (i.e. only
 * within the current session dispatch). Returns nullptr if the request does not
 * fit the remaining arena - callers MUST handle null and fail closed.
 *
 * @param n     bytes requested (0 yields a valid non-null pointer when space
 *              remains).
 * @param align required alignment in bytes, a power of two (0 selects the
 *              platform default).
 * @return pointer to @p n writable bytes, or nullptr if it does not fit.
 */
void *scratch_alloc(size_t n, size_t align);

/**
 * @brief Borrow @p n bytes as a span whose capacity is bound to the allocation.
 *
 * The preferred form. scratch_alloc() hands back a bare pointer, which leaves the caller to carry
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
pc_span pc_scratch_span(size_t n, size_t align);

/**
 * @brief Reclaim the whole arena (empties it).
 *
 * Called by server_tick() before each event dispatch. Invalidates every pointer
 * previously returned by scratch_alloc().
 */
void scratch_reset(void);

/**
 * @brief Capture the current arena offset (a savepoint for scratch_release()).
 * @return an opaque mark to pass to scratch_release().
 */
size_t scratch_mark(void);

/**
 * @brief Reclaim everything allocated since @p mark (LIFO).
 *
 * Restores the arena to a previous scratch_mark(), freeing every scratch_alloc()
 * made in between. Marks must be released in reverse order (nested scopes). Use
 * ScratchScope for return-safe scoping.
 *
 * @param mark a value previously returned by scratch_mark() (must be <= the
 *             current offset).
 */
void scratch_release(size_t mark);

/** @brief Bytes currently handed out (0 immediately after a reset). */
size_t scratch_used(void);

/** @brief Largest scratch_used() value seen since boot (for sizing the arena). */
size_t scratch_high_water(void);

/** @brief Total arena capacity in bytes (PC_SCRATCH_ARENA_SIZE). */
size_t scratch_capacity(void);

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
bool pc_scratch_owns(const void *p);

/**
 * @brief Which plaintext slot owns @p p, or -1 if @p p is not in the plaintext pool.
 *
 * A divide by a compile-time constant, so a multiply-and-shift rather than real division. Use to
 * assert that a borrow being handed back belongs to the calling worker: crossing slots is the one
 * way the lock-free single-accessor invariant can be violated, and this makes it checkable.
 */
int pc_scratch_slot_of(const void *p);

/**
 * @brief A scratch borrow whose acquire and release are one call each.
 *
 * ScratchScope + pc_scratch_span costs three crossings of this module's boundary: mark, alloc, then
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
class ScratchBorrow
{
  public:
    ScratchBorrow(size_t n, size_t align);
    ~ScratchBorrow();
    ScratchBorrow(const ScratchBorrow &) = delete;
    ScratchBorrow &operator=(const ScratchBorrow &) = delete;

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
 * @brief RAII scope guard for transient scratch borrows.
 *
 * Marks the arena on construction and restores it on destruction, so every
 * scratch_alloc() made within the scope is reclaimed on *every* exit path
 * (return, break, fall-through) - the safe way to borrow transient scratch in a
 * function with multiple returns. Scopes must nest (LIFO); the per-dispatch
 * scratch_reset() is the backstop if one is ever skipped.
 */
class ScratchScope
{
  public:
    ScratchScope() : m_mark(scratch_mark())
    {
    }
    ~ScratchScope()
    {
        scratch_release(m_mark);
    }
    ScratchScope(const ScratchScope &) = delete;
    ScratchScope &operator=(const ScratchScope &) = delete;

  private:
    size_t m_mark;
};

#endif // PROTOCORE_SCRATCH_H
