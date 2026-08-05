// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file secure.c
 * @brief Secure pool accessor - implementation.
 *
 * An access layer, not a second allocator: the pool mechanism is ::pc_arena and there is exactly one
 * of it. This module owns a second set of instances over its own compile-time-sized storage, and
 * adds the one control the plaintext side does not have - reclaiming wipes.
 */

#include "secure.h"
#include "board_drivers/board_profiles/pc_platform.h" // pc_platform_context_id()
#include "mmgr/arena.h"
#include <assert.h>

// Per-slot pool instances, owned by one instance with internal linkage.
typedef struct
{
    pc_arena pool[PC_SEC_POOL_SLOTS];
} SecurePoolCtx;
static SecurePoolCtx s_secure;

// Backing storage in its OWN linker symbol, named only from bind() below and therefore only from the
// allocation path. A firmware that never borrows secure storage has the allocator garbage-collected and
// this storage with it. pc_secure_reset() must NOT bind, for the same reason pc_plaintext_reset() must
// not: --gc-sections is per-symbol and one always-live reference would anchor the whole block.
typedef struct
{
    _Alignas(32) uint8_t mem[PC_SEC_POOL_SLOTS][PC_SECURE_ARENA_SIZE];
} SecurePoolStorageCtx;
static SecurePoolStorageCtx s_secure_storage;

// Byte offset of @p p within the whole secure block. The slot count and slot size are compile-time,
// so the pool is ONE region of known extent: no loop, no per-slot compare, just an unsigned
// subtract. A pointer below the base wraps to a huge value and fails the same bound as one past the
// end, so NULL needs no special case and an overrun cannot read as still-inside.
//
// No power-of-two requirement, matching the plaintext pool: the per-die profiles size these to what
// each part can afford (s3 12288, c6 10240), and subtract-and-compare does not care.
static inline uintptr_t secure_offset(const void *p)
{
    return (uintptr_t)p - (uintptr_t)s_secure_storage.mem;
}

// The clamp guarantees a legal index, and only that: a caller that is not a server worker lands on
// the ghost instead of worker 0. Two such callers still share the ghost, which the tripwire catches.
static inline int cur_worker(void)
{
    int w = pc_worker_self();
    return (w >= 0 && w < PC_SEC_POOL_SLOTS) ? w : PC_GHOST_WORKER_SLOT;
}

// Debug tripwire: one execution context per slot, as on the plaintext side.
static inline void assert_single_owner(int w)
{
#if PC_DEBUG_CHECKS
    // Off by default; see PC_DEBUG_CHECKS. The identity comes from board_drivers/ - the core does
    // not name an RTOS.
    static uintptr_t s_owner[PC_SEC_POOL_SLOTS] = {0};
    const uintptr_t cur = pc_platform_context_id();
    if (s_owner[w] == 0)
    {
        s_owner[w] = cur;
    }
    else
    {
        assert(s_owner[w] == cur && "secure pool borrowed from a foreign task");
    }
#else
    (void)w;
#endif
}

// Bind slot @p w to its storage on first use. The ONLY reference to s_secure_storage.
static inline pc_arena *bind(int w)
{
    pc_arena *a = &s_secure.pool[w];
    if (a->base == NULL)
    {
        pc_arena_init(a, s_secure_storage.mem[w], PC_SECURE_ARENA_SIZE);
    }
    return a;
}

// The slot WITHOUT binding it, for observers and the reset. An unbound slot has never allocated, so
// it holds nothing to wipe.
static inline pc_arena *peek(int w)
{
    pc_arena *a = &s_secure.pool[w];
    return (a->base != NULL) ? a : NULL;
}

// Wipe the live extent down to @p mark, then move the position.
//
// The order is the whole point. The scratch end grows DOWN, so the live bytes are
// [scratch_top, mark) and reclaiming means raising scratch_top back to mark. Wiping first means the
// region is already zero at the instant it becomes available, so there is no window - not to a
// preempting handler, not to the very next borrow - in which memory still holding the previous
// tenant's key material can be handed out. Reclaiming first and wiping after would leave exactly
// that window.
static inline void wipe_down_to(pc_arena *a, size_t mark)
{
    const size_t top = pc_arena_scratch_mark(a); // current position (an offset from the base)
    if (mark > top && mark <= a->size)
    {
        pc_secure_wipe(a->base + top, mark - top); // volatile: the compiler may not elide it
    }
    pc_arena_scratch_release(a, mark);
}

void *pc_secure_alloc(size_t n, size_t align)
{
    int w = cur_worker();
    assert_single_owner(w);
    assert((align & (align - 1)) == 0 && "secure alignment must be a power of two");
    return pc_arena_scratch_alloc_aligned(bind(w), n, align);
}

pc_span pc_secure_span(size_t n, size_t align)
{
    return pc_span_from((uint8_t *)pc_secure_alloc(n, align), n);
}

size_t pc_secure_mark(void)
{
    int w = cur_worker();
    assert_single_owner(w);
    return pc_arena_scratch_mark(bind(w));
}

void pc_secure_release(size_t mark)
{
    int w = cur_worker();
    assert_single_owner(w);
    wipe_down_to(bind(w), mark);
}

void pc_secure_reset(void)
{
    int w = cur_worker();
    assert_single_owner(w);
    pc_arena *a = peek(w); // must not bind: would anchor the storage into builds that never borrow
    if (a != NULL)
    {
        wipe_down_to(a, a->size); // the empty position: wipes everything live
    }
}

size_t pc_secure_used(void)
{
    const pc_arena *a = peek(cur_worker());
    return (a != NULL) ? pc_arena_scratch_used(a) : 0;
}

size_t pc_secure_high_water(void)
{
    size_t peak = 0;
    for (int w = 0; w < PC_SEC_POOL_SLOTS; w++)
    {
        const pc_arena *a = peek(w);
        if (a != NULL && a->scratch_hw > peak)
        {
            peak = a->scratch_hw;
        }
    }
    return peak;
}

size_t pc_secure_capacity(void)
{
    return PC_SECURE_ARENA_SIZE;
}

proto_bool pc_secure_owns(const void *p)
{
    return secure_offset(p) < (uintptr_t)sizeof(s_secure_storage.mem);
}

int pc_secure_slot_of(const void *p)
{
    const uintptr_t off = secure_offset(p);
    if (off >= (uintptr_t)sizeof(s_secure_storage.mem))
    {
        return -1;
    }
    // A divide by a compile-time constant, which the compiler emits as a multiply-and-shift.
    return (int)(off / PC_SECURE_ARENA_SIZE);
}
