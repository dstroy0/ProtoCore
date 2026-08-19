// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file secure.c
 * @brief Secure pool accessor - implementation.
 *
 * An access layer, not a second allocator: the pool mechanism is ::protocore_arena and there is exactly one
 * of it. This module owns a second set of instances over its own compile-time-sized storage, and
 * adds the one control the plaintext side does not have - reclaiming wipes.
 */

#include "mmgr/secure/secure.h"
#include "config/platform/platform.h" // protocore_platform_context_id()
#include "mmgr/arena/arena.h"
#include <assert.h>

/** @brief The pool's compile-time backing bytes: one arena's worth per slot, in one block. */
struct SecureStorage
{
    _Alignas(32) uint8_t mem[PROTOCORE_SEC_POOL_SLOTS][PROTOCORE_SECURE_ARENA_SIZE];
};

/**
 * @brief The pool's state: the per-slot arenas, the block they hand out of, and the debug owner
 *        table.
 *
 * @var SecureInternal::store  the backing block, NULL until the first borrow binds it
 * @var SecureInternal::pool   one arena per slot, indexed by the calling worker's slot
 * @var SecureInternal::owner  the execution context that first touched each slot, debug builds only
 */
struct SecureInternal
{
    struct SecureStorage *store;
    protocore_arena pool[PROTOCORE_SEC_POOL_SLOTS];
#if PROTOCORE_DEBUG_CHECKS
    uintptr_t owner[PROTOCORE_SEC_POOL_SLOTS];
#endif
};

// Backing storage in its OWN linker symbol, named only from bind() below and therefore only from the
// allocation path. A firmware that never borrows secure storage has the allocator garbage-collected and
// this storage with it. protocore_secure_reset() must NOT bind, for the same reason protocore_plaintext_reset() must
// not: --gc-sections is per-symbol and one always-live reference would anchor the whole block.
static struct SecureStorage s_store;

// Every mutable this module has, in one object. Zero at boot: bind() fills in the block pointer and
// the slot's arena on that slot's first borrow, so the state holds a reference to the block only
// once something has borrowed from it.
struct SecureInternal protocore_secure_state;

// The state, under one name.
static inline struct SecureInternal *secure_ctx(void)
{
    return &protocore_secure_state;
}

// Byte offset of @p p within the whole secure block. The slot count and slot size are compile-time,
// so the pool is ONE region of known extent: no loop, no per-slot compare, just an unsigned
// subtract. A pointer below the base wraps to a huge value and fails the same bound as one past the
// end, so NULL needs no special case and an overrun cannot read as still-inside.
//
// No power-of-two requirement, matching the plaintext pool: the per-die profiles size these to what
// each part can afford (s3 12288, c6 10240), and subtract-and-compare does not care.
static inline uintptr_t secure_offset(const struct SecureInternal *ctx, const void *p)
{
    return (uintptr_t)p - (uintptr_t)ctx->store;
}

// The clamp guarantees a legal index, and only that: a caller that is not a server worker lands on
// the ghost instead of worker 0. Two such callers still share the ghost, which the tripwire catches.
static inline int cur_worker(void)
{
    int w = protocore_worker_self();
    return (w >= 0 && w < PROTOCORE_SEC_POOL_SLOTS) ? w : PROTOCORE_GHOST_WORKER_SLOT;
}

// Debug tripwire: one execution context per slot, as on the plaintext side.
static inline void assert_single_owner(struct SecureInternal *ctx, int w)
{
#if PROTOCORE_DEBUG_CHECKS
    // Off by default; see PROTOCORE_DEBUG_CHECKS. The identity comes from test/core_setup/ - the core does
    // not name an RTOS.
    const uintptr_t cur = protocore_platform_context_id();
    if (ctx->owner[w] == 0)
    {
        ctx->owner[w] = cur;
    }
    else
    {
        assert(ctx->owner[w] == cur && "secure pool borrowed from a foreign task");
    }
#else
    (void)ctx;
    (void)w;
#endif
}

// Bind slot @p w to its storage on first use. The ONLY reference to s_store.
static inline protocore_arena *bind(struct SecureInternal *ctx, int w)
{
    protocore_arena *a = &ctx->pool[w];
    if (a->base == NULL)
    {
        ctx->store = &s_store;
        protocore_arena_init(a, ctx->store->mem[w], PROTOCORE_SECURE_ARENA_SIZE);
    }
    return a;
}

// The slot WITHOUT binding it, for observers and the reset. An unbound slot has never allocated, so
// it holds nothing to wipe.
static inline protocore_arena *peek(struct SecureInternal *ctx, int w)
{
    protocore_arena *a = &ctx->pool[w];
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
static inline void wipe_down_to(protocore_arena *a, size_t mark)
{
    const size_t top = protocore_arena_scratch_mark(a); // current position (an offset from the base)
    if (mark > top && mark <= a->size)
    {
        protocore_secure_wipe(a->base + top, mark - top); // volatile: the compiler may not elide it
    }
    protocore_arena_scratch_release(a, mark);
}

void *protocore_secure_alloc(size_t n, size_t align)
{
    struct SecureInternal *ctx = secure_ctx();
    int w = cur_worker();
    assert_single_owner(ctx, w);
    assert((align & (align - 1)) == 0 && "secure alignment must be a power of two");
    return protocore_arena_scratch_alloc_aligned(bind(ctx, w), n, align);
}

protocore_span protocore_secure_span(size_t n, size_t align)
{
    return protocore_span_from((uint8_t *)protocore_secure_alloc(n, align), n);
}

protocore_span protocore_secure_persist_span(size_t n)
{
    struct SecureInternal *ctx = secure_ctx();
    int w = cur_worker();
    assert_single_owner(ctx, w);
    // The persistent end grows up from the base and the scratch end bumps down from the top, so a
    // mark taken on the scratch end never reaches this and no release reclaims it. The arena hands
    // these bytes back zeroed.
    return protocore_span_from((uint8_t *)protocore_arena_persist_alloc(bind(ctx, w), n), n);
}

size_t protocore_secure_mark(void)
{
    struct SecureInternal *ctx = secure_ctx();
    int w = cur_worker();
    assert_single_owner(ctx, w);
    return protocore_arena_scratch_mark(bind(ctx, w));
}

void protocore_secure_release(size_t mark)
{
    struct SecureInternal *ctx = secure_ctx();
    int w = cur_worker();
    assert_single_owner(ctx, w);
    wipe_down_to(bind(ctx, w), mark);
}

void protocore_secure_reset(void)
{
    struct SecureInternal *ctx = secure_ctx();
    int w = cur_worker();
    assert_single_owner(ctx, w);
    protocore_arena *a = peek(ctx, w); // must not bind: would anchor the storage into builds that never borrow
    if (a != NULL)
    {
        wipe_down_to(a, a->size); // the empty position: wipes everything live
    }
}

size_t protocore_secure_used(void)
{
    const protocore_arena *a = peek(secure_ctx(), cur_worker());
    return (a != NULL) ? protocore_arena_scratch_used(a) : 0;
}

size_t protocore_secure_high_water(void)
{
    struct SecureInternal *ctx = secure_ctx();
    size_t peak = 0;
    for (int w = 0; w < PROTOCORE_SEC_POOL_SLOTS; w++)
    {
        const protocore_arena *a = peek(ctx, w);
        if (a != NULL && a->scratch_hw > peak)
        {
            peak = a->scratch_hw;
        }
    }
    return peak;
}

size_t protocore_secure_capacity(void)
{
    return PROTOCORE_SECURE_ARENA_SIZE;
}

proto_bool protocore_secure_owns(const void *p)
{
    const struct SecureInternal *ctx = secure_ctx();
    // An unbound block has handed out nothing, so no pointer is inside it and the subtract has no
    // base to run against.
    return ctx->store != NULL && secure_offset(ctx, p) < (uintptr_t)sizeof(ctx->store->mem);
}

int protocore_secure_slot_of(const void *p)
{
    const struct SecureInternal *ctx = secure_ctx();
    if (ctx->store == NULL)
    {
        return -1;
    }
    const uintptr_t off = secure_offset(ctx, p);
    if (off >= (uintptr_t)sizeof(ctx->store->mem))
    {
        return -1;
    }
    // A divide by a compile-time constant, which the compiler emits as a multiply-and-shift.
    return (int)(off / PROTOCORE_SECURE_ARENA_SIZE);
}
