// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file arena.h
 * @brief Unified double-ended server arena.
 *
 * One contiguous region is shared by two allocators that grow toward each other, with
 * the free space floating in the middle:
 *
 *   [ persistent  --grows up-->        | free |        <--grows down--  scratch ]
 *     low addr                    (floating boundary)                   high addr
 *
 * - **Persistent** (bottom): a first-fit free-list. Long-lived objects that are freed
 *   individually, in arbitrary order (e.g. per-connection state). Grows up into the
 *   middle only as far as the scratch end allows; a freed top block shrinks it back.
 * - **Scratch** (top): a bump allocator reclaimed in bulk. Transient per-dispatch
 *   buffers. `pc_arena_scratch_reset()` empties it in O(1); `mark`/`release` give nested savepoints.
 *
 * Whichever side needs more room grows into the shared middle - that is the win over two
 * fixed pools. Both ends fail closed (return NULL) rather than crossing the boundary.
 *
 * All state lives in ::pc_arena (no globals), so it is unit-testable and can back several
 * arenas (a DRAM base and a PSRAM extension - see ::pc_arena_set). No heap; no stdlib.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_ARENA_H
#define PROTOCORE_ARENA_H

#include "protocore_config.h" // PC_WORKER_COUNT - how many slots the pools are cut into

// ---------------------------------------------------------------------------
// Slot identity - which arena is mine
// ---------------------------------------------------------------------------
//
// The pools are cut one slot per worker so a borrow never crosses workers, which means the
// allocator has to be able to answer "which slot is the caller" before it can hand anything back.
// That answer lives here, with the thing it indexes, rather than in the scheduler: mmgr arbitrates
// the memory, so it owns the identity the arbitration keys on. Starting and waking those workers is
// a separate concern and stays in the session layer.

/** @brief Number of server worker tasks (PC_WORKER_COUNT). */
int pc_worker_count(void);

/**
 * @brief Worker id [0, count) of the calling task; 0 by default / single-worker.
 *
 * With PC_WORKER_COUNT == 1 (the default) there is exactly one worker, so the answer is 0 by
 * construction and this is an inline constant - no lookup, no call. That matters because every pool
 * borrow asks: the multi-worker path reads a `_Thread_local`, which on FreeRTOS resolves through the
 * task's TLS block rather than a register, and it was being paid on operations that are otherwise a
 * single struct-field read.
 */
#if PC_WORKER_COUNT == 1
PC_INLINE int pc_worker_self(void)
{
    return 0;
}
#else
int pc_worker_self(void);
#endif

/** @brief Bind the calling task/thread to worker id @p id (worker entry / tests). */
void pc_worker_set_self(int id);

/** @brief Baseline alignment (bytes) applied to every allocation and to headers. */
#define PC_ARENA_ALIGN 8u

/** @brief Round @p n up to PC_ARENA_ALIGN. */
PC_INLINE size_t pc_arena_align_up(size_t n)
{
    return (n + (PC_ARENA_ALIGN - 1)) & ~(size_t)(PC_ARENA_ALIGN - 1);
}

/** @brief Strongest alignment a scratch borrow may request; the region base is aligned to it. */
#define PC_ARENA_MAX_ALIGN 16u

/**
 * @brief Double-ended arena over one region `[base, base+size)`.
 *
 * `persist_end` and `scratch_top` are byte offsets from `base`; the free middle is
 * `[persist_end, scratch_top)`. The persistent pool owns `[0, persist_end)` (a chain of
 * first-fit blocks); scratch owns `[scratch_top, size)` (bump). Initialize with
 * pc_arena_init(); do not touch the fields directly.
 */
typedef struct
{
    uint8_t *base;       ///< Region start.
    size_t size;         ///< Region length in bytes.
    size_t persist_end;  ///< Persistent pool occupies [0, persist_end).
    size_t scratch_top;  ///< Scratch occupies [scratch_top, size).
    size_t persist_used; ///< Bytes currently handed out by the persistent pool (payload).
    size_t persist_hw;   ///< High-water of persist_end (for sizing).
    size_t scratch_hw;   ///< High-water of scratch use (size - min scratch_top).
} pc_arena;

/**
 * @brief Initialize @p a over the region `[base, base+size)`.
 *
 * @p base should be PC_ARENA_ALIGN-aligned; @p size must be at least a few blocks.
 */
void pc_arena_init(pc_arena *a, void *base, size_t size);

// --- persistent end (first-fit, individual free, grows up) ------------------

/**
 * @brief Allocate @p n zero-initialized bytes of long-lived storage.
 *
 * First-fits an existing free block; otherwise carves a new block from the free middle
 * (growing the persistent end up) if it will not cross the scratch end.
 * @return aligned, zeroed pointer, or NULL if the arena cannot satisfy it.
 */
void *pc_arena_persist_alloc(pc_arena *a, size_t n);

/**
 * @brief Free a pointer previously returned by pc_arena_persist_alloc().
 *
 * Coalesces with adjacent free blocks; if the freed block sits at the top of the
 * persistent region it returns that space to the free middle (shrinks the boundary).
 * Passing NULL is a no-op.
 */
void pc_arena_persist_free(pc_arena *a, void *p);

// --- scratch end (bump, bulk reset, grows down) -----------------------------
//
// Inline: a borrow is a few loads, a mask and a store, so a call would cost more than the work.
// PC_INLINE, not bare inline - a bare C inline needs a second out-of-line definition to link.

/**
 * @brief Bump-allocate @p n bytes of transient storage, aligned to @p align.
 *
 * @param align power-of-two alignment for the returned pointer; clamped to
 *              `[PC_ARENA_ALIGN, PC_ARENA_MAX_ALIGN]`.
 * @return aligned pointer (NOT zeroed), or NULL if it would cross the persistent end.
 */
PC_INLINE void *pc_arena_scratch_alloc_aligned(pc_arena *a, size_t n, size_t align)
{
    if (align < PC_ARENA_ALIGN)
    {
        align = PC_ARENA_ALIGN;
    }
    if (align > PC_ARENA_MAX_ALIGN)
    {
        align = PC_ARENA_MAX_ALIGN; // the base only guarantees this much
    }
    n = pc_arena_align_up(n ? n : PC_ARENA_ALIGN);
    if (a->scratch_top < n)
    {
        return NULL;
    }
    // The base is PC_ARENA_MAX_ALIGN-aligned, so aligning the offset down aligns the pointer.
    size_t nt = (a->scratch_top - n) & ~(size_t)(align - 1);
    // The "nt > a->scratch_top" half is unreachable: the guard above already established
    // n <= a->scratch_top, so the subtraction cannot underflow, and masking off low bits can
    // only ever decrease the value further - nt <= a->scratch_top always holds.
    if (nt < a->persist_end || nt > a->scratch_top) // GCOVR_EXCL_BR_LINE
    {
        return NULL; // would cross the persistent end (or underflow)
    }
    a->scratch_top = nt;
    size_t used = a->size - a->scratch_top;
    if (used > a->scratch_hw)
    {
        a->scratch_hw = used;
    }
    return a->base + a->scratch_top;
}

/** @brief Bump-allocate @p n transient bytes at the baseline alignment (PC_ARENA_ALIGN). */
void *pc_arena_scratch_alloc(pc_arena *a, size_t n);

/** @brief Capture the current scratch position (a savepoint for pc_arena_scratch_release()). */
PC_INLINE size_t pc_arena_scratch_mark(const pc_arena *a)
{
    return a->scratch_top;
}

/** @brief Free every scratch allocation made since @p mark (a value from pc_arena_scratch_mark()). */
PC_INLINE void pc_arena_scratch_release(pc_arena *a, size_t mark)
{
    // A mark is an earlier (higher) scratch_top; releasing frees everything below it.
    if (mark >= a->scratch_top && mark <= a->size)
    {
        a->scratch_top = mark;
    }
}

/** @brief Free ALL scratch allocations in O(1). */
PC_INLINE void pc_arena_scratch_reset(pc_arena *a)
{
    a->scratch_top = a->size;
}

// --- ownership (the access control) -----------------------------------------

/**
 * @brief True if @p p points inside this pool's region.
 *
 * Ownership is an address-range property, not bookkeeping. The pools occupy disjoint regions, so a
 * pointer belongs to exactly one of them and the owner is recoverable from the address alone. That
 * is what stops a secret-pool pointer from being accepted where a plaintext one is expected, and
 * what makes a write that ran off the end land outside the range rather than in a neighbour.
 *
 * Two compares, because this arena's base and size are only known at run time. pc_plaintext_owns()
 * / pc_secure_owns() answer the same question against a compile-time bound in one subtract.
 */
PC_INLINE proto_bool pc_arena_owns(const pc_arena *a, const void *p)
{
    const uint8_t *q = (const uint8_t *)p;
    return a->base != NULL && q >= a->base && q < a->base + a->size;
}

// --- observability ----------------------------------------------------------

/** @brief Free bytes in the middle (max a single new allocation could take, minus a header). */
size_t pc_arena_free_bytes(const pc_arena *a);

/** @brief Persistent payload bytes currently allocated. */
size_t pc_arena_persist_used(const pc_arena *a);

/** @brief Scratch bytes currently allocated. */
PC_INLINE size_t pc_arena_scratch_used(const pc_arena *a)
{
    return a->size - a->scratch_top;
}

// ===========================================================================
// Multi-region extension: a DRAM base + an optional PSRAM extension.
// ===========================================================================
//
// A ::pc_arena_set chains a few ::pc_arena regions in preference order (add DRAM
// first, PSRAM second). Allocations try each region in turn and take the first
// that fits, so hot state stays in fast internal RAM and only the overflow
// spills into external RAM. Frees are routed to the owning region by address.
// This is how "arena extension" works: enable PSRAM by adding a second region;
// leave it out and the set is just the single DRAM arena.

/** @brief Max regions in a ::pc_arena_set (DRAM base + PSRAM extension). */
#ifndef PC_ARENA_MAX_REGIONS
#define PC_ARENA_MAX_REGIONS 2u
#endif

/** @brief A set of ::pc_arena regions searched in insertion (preference) order. */
typedef struct
{
    pc_arena region[PC_ARENA_MAX_REGIONS];
    size_t count; ///< Regions in use.
} pc_arena_set;

/** @brief A scratch savepoint across every region of a ::pc_arena_set. */
typedef struct
{
    size_t top[PC_ARENA_MAX_REGIONS];
    size_t count;
} pc_arena_mark;

/** @brief Initialize an empty set (no regions yet). */
void pc_arena_set_init(pc_arena_set *s);

/**
 * @brief Add a region `[base, base+size)`; regions are searched in the order added.
 * @return true if added, false if the set is full or the region is too small.
 */
proto_bool pc_arena_set_add(pc_arena_set *s, void *base, size_t size);

/** @brief Persistent alloc from the first region that fits (see pc_arena_persist_alloc()). */
void *pc_arena_set_persist_alloc(pc_arena_set *s, size_t n);

/** @brief Free a persistent pointer, routed to its owning region by address. */
void pc_arena_set_persist_free(pc_arena_set *s, void *p);

/** @brief Aligned scratch alloc from the first region that fits (see pc_arena_scratch_alloc_aligned()). */
void *pc_arena_set_scratch_alloc_aligned(pc_arena_set *s, size_t n, size_t align);

/** @brief Scratch alloc from the first region that fits (see pc_arena_scratch_alloc()). */
void *pc_arena_set_scratch_alloc(pc_arena_set *s, size_t n);

/** @brief Capture the scratch position of every region. */
pc_arena_mark pc_arena_set_scratch_mark(const pc_arena_set *s);

/** @brief Restore every region's scratch position to @p m (frees scratch made since). */
void pc_arena_set_scratch_release(pc_arena_set *s, const pc_arena_mark *m);

/** @brief Reset scratch in every region. */
void pc_arena_set_scratch_reset(pc_arena_set *s);

/** @brief Total free middle bytes summed over all regions. */
size_t pc_arena_set_free_bytes(const pc_arena_set *s);

/** @brief Persistent payload bytes allocated, summed over all regions. */
size_t pc_arena_set_persist_used(const pc_arena_set *s);

/** @brief Scratch bytes allocated, summed over all regions. */
size_t pc_arena_set_scratch_used(const pc_arena_set *s);

#endif // PROTOCORE_ARENA_H
