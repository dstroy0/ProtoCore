// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file plaintext.cpp
 * @brief Plaintext pool accessor - implementation.
 *
 * This file is an access layer, not a second allocator. The pool mechanism is ::pc_arena
 * (mmgr/arena) and there is exactly one of it; this module owns one instance per worker slot over
 * compile-time-sized storage and decides who may reach it. The secret pool is the same mechanism
 * instantiated again behind its own accessor - identical resource, identical mechanics, different
 * access and control.
 *
 * Each instance has exactly one accessor (its worker), so the lock-free guarantees in plaintext.h hold
 * per slot. With PC_WORKER_COUNT == 1 this is byte-for-byte the original single arena.
 */

#include "plaintext.h"
#include "board_drivers/board_profiles/pc_platform.h" // pc_platform_context_id()
#include "network_drivers/session/worker.h"
#include "server/mmgr/arena.h"
#include <assert.h>
#include <stdint.h>

namespace
{
// Per-slot pool instances (offsets + high-water live inside pc_arena), owned by one instance with
// internal linkage.
struct PlainPoolCtx
{
    pc_arena pool[PC_REG_POOL_SLOTS];
};
static PlainPoolCtx s_plain;

// The backing storage, in its OWN owned instance so it is a distinct linker symbol.
//
// Nothing outside bind() below names this symbol, and bind() is reachable only from the allocation
// path. A firmware that never allocates plaintext scratch (a plain TLS/HTTP server - no SSH /
// WebSocket / OIDC) therefore has the allocator garbage-collected and, with it, this storage,
// reclaiming PC_REG_POOL_SLOTS * PC_PLAINTEXT_ARENA_SIZE bytes of DRAM. That is why pc_plaintext_reset() -
// which runs every dispatch and so is always live - must NOT bind: --gc-sections is per-symbol, and
// one always-live reference would anchor the multi-KB storage into builds that never touch it.
struct PlainPoolStorageCtx
{
    // The pool aligns allocation offsets, so the base must itself satisfy the strictest alignment a
    // caller can request. As a struct member it would only inherit 8-byte alignment; force 32.
    alignas(32) uint8_t mem[PC_REG_POOL_SLOTS][PC_PLAINTEXT_ARENA_SIZE];
};
static PlainPoolStorageCtx s_plain_storage;

// Byte offset of @p p within the whole plaintext block. The slot count is compile time, so the block
// is ONE region of known extent and the test needs no loop and no per-slot compare: a single
// unsigned subtract. A pointer below the base wraps to a huge value and so fails the same bound as
// one past the end, which is why nullptr needs no special case.
//
// Deliberately NOT keyed on a power-of-two slot size. The board profiles tune this per die and most
// are not powers of two (s3 12288, c6 10240), so a masking form would either reject those builds or
// round them up and waste kilobytes on the smallest parts. Subtract-and-compare works for any size.
inline uintptr_t plain_offset(const void *p)
{
    return (uintptr_t)p - (uintptr_t)s_plain_storage.mem;
}

// Resolve the calling worker. The clamp guarantees a legal index, and only that: a caller that is
// not a server worker lands on the ghost instead of worker 0. Two such callers still share the
// ghost, which is what the tripwire below is for.
inline int cur_worker()
{
    int w = pc_worker_self();
    return (w >= 0 && w < PC_REG_POOL_SLOTS) ? w : PC_GHOST_WORKER_SLOT;
}

// Debug tripwire: each pool instance must only ever be touched from one execution context (its
// worker). Record the first caller per slot and assert every later call matches. The structural
// single-accessor-per-slot invariant is what makes this lock-free; the assert just turns a future
// violation into an immediate failure instead of a silent cross-core race.
inline void assert_single_owner(int w)
{
#if PC_DEBUG_CHECKS
    // ~52 cycles per call on the S3, three per mark+alloc+release. Off by default and turned on
    // deliberately; see PC_DEBUG_CHECKS. The identity comes from board_drivers/ - the core does not
    // name an RTOS.
    static uintptr_t s_owner[PC_REG_POOL_SLOTS] = {0};
    const uintptr_t cur = pc_platform_context_id();
    if (s_owner[w] == 0)
    {
        s_owner[w] = cur;
    }
    else
    {
        assert(s_owner[w] == cur && "plaintext pool borrowed from a foreign task");
    }
#else
    (void)w;
#endif
}

// Bind slot @p w's pool to its storage on first use. The ONLY reference to s_plain_storage - see
// the note on PlainPoolStorageCtx for why it must stay confined to the allocation path.
inline pc_arena *bind(int w)
{
    pc_arena *a = &s_plain.pool[w];
    if (a->base == nullptr)
    {
        pc_arena_init(a, s_plain_storage.mem[w], PC_PLAINTEXT_ARENA_SIZE);
    }
    return a;
}

// The pool for slot @p w WITHOUT binding it: for the observers and the reset, which must not anchor
// the storage. An unbound slot has never allocated, so it is empty by definition.
inline pc_arena *peek(int w)
{
    pc_arena *a = &s_plain.pool[w];
    return (a->base != nullptr) ? a : nullptr;
}
} // namespace

PlaintextBorrow::PlaintextBorrow(size_t n, size_t align)
{
    // One boundary crossing for mark + alloc, and bind() resolved once for both.
    int w = cur_worker();
    assert_single_owner(w);
    pc_arena *a = bind(w);
    m_mark = pc_arena_scratch_mark(a);
    m_span = pc_span_from((uint8_t *)pc_arena_scratch_alloc_aligned(a, n, align), n);
}

PlaintextBorrow::~PlaintextBorrow()
{
    int w = cur_worker();
    assert_single_owner(w);
    pc_arena_scratch_release(bind(w), m_mark);
}

void *pc_plaintext_alloc(size_t n, size_t align)
{
    int w = cur_worker();
    assert_single_owner(w);
    // The false half is a caller-contract violation (align documented as a power of two) and aborts
    // the process, so it cannot be exercised from an in-process host test without killing the whole
    // test binary mid-run.
    assert((align & (align - 1)) == 0 && "plaintext alignment must be a power of two"); // GCOVR_EXCL_BR_LINE
    return pc_arena_scratch_alloc_aligned(bind(w), n, align);
}

pc_span pc_plaintext_span(size_t n, size_t align)
{
    // One argument sets both fields, so the capacity can never disagree with the allocation. On
    // exhaustion pc_span_from() normalizes to {nullptr, 0} rather than a null with a live capacity,
    // which is what makes an omitted pc_span_ok() check drop bytes instead of dereference null.
    return pc_span_from((uint8_t *)pc_plaintext_alloc(n, align), n);
}

void pc_plaintext_reset(void)
{
    int w = cur_worker();
    assert_single_owner(w);
    pc_arena *a = peek(w); // must not bind: this runs every dispatch and would anchor the storage
    if (a != nullptr)
    {
        pc_arena_scratch_reset(a);
    }
}

size_t pc_plaintext_mark(void)
{
    int w = cur_worker();
    assert_single_owner(w);
    return pc_arena_scratch_mark(bind(w));
}

void pc_plaintext_release(size_t mark)
{
    int w = cur_worker();
    assert_single_owner(w);
    pc_arena_scratch_release(bind(w), mark);
}

size_t pc_plaintext_used(void)
{
    const pc_arena *a = peek(cur_worker());
    return (a != nullptr) ? pc_arena_scratch_used(a) : 0;
}

size_t pc_plaintext_high_water(void)
{
    // Peak any single slot reached - the value to size PC_PLAINTEXT_ARENA_SIZE by. This is the
    // backward direction of the run-length constant: the pool reports what was actually needed.
    size_t peak = 0;
    for (int w = 0; w < PC_REG_POOL_SLOTS; w++)
    {
        const pc_arena *a = peek(w);
        if (a != nullptr && a->scratch_hw > peak)
        {
            peak = a->scratch_hw;
        }
    }
    return peak;
}

size_t pc_plaintext_capacity(void)
{
    return PC_PLAINTEXT_ARENA_SIZE;
}

bool pc_plaintext_owns(const void *p)
{
    return plain_offset(p) < (uintptr_t)sizeof(s_plain_storage.mem);
}

int pc_plaintext_slot_of(const void *p)
{
    const uintptr_t off = plain_offset(p);
    if (off >= (uintptr_t)sizeof(s_plain_storage.mem))
    {
        return -1;
    }
    // A divide by a compile-time constant, which the compiler emits as a multiply-and-shift. Not a
    // hot path either way: this exists so a borrow handed back can be asserted to belong to the
    // calling worker.
    return (int)(off / PC_PLAINTEXT_ARENA_SIZE);
}
