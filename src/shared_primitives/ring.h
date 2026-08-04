// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
#ifndef PROTOCORE_RING_H
#define PROTOCORE_RING_H

/**
 * @file ring.h
 * @brief Shared single-producer / single-consumer byte-ring primitive.
 *
 * The one implementation of the receive-ring drain math, used by BOTH transports:
 * the server (pc_conn_* in tcp.h, over conn_pool slots) and the outbound
 * client (pc_client_* over its pool). The wrap and ordering invariants are stated
 * here once, so a consumer in any layer drains identically.
 *
 * Ownership rule: exactly one producer advances `head`, exactly one consumer
 * advances `tail`; both indices are `_Atomic` so a producer's buffer writes are visible
 * before the consumer observes the advanced index (acquire/release), correct across
 * the tcpip_thread <-> worker/caller boundary on either core. No locks, no RMW.
 */

#include "shared_primitives/rawmemcpy.h" // proto_raw_read: the producer span move
#include <stdatomic.h>                   // _Atomic, atomic_load_explicit, atomic_store_explicit, memory_order_*

// ---------------------------------------------------------------------------
// Cross-thread field access
// ---------------------------------------------------------------------------
//
// A field shared across a producer/consumer thread boundary (a ring head/tail, a slot
// state) is declared `_Atomic` by its owner and reached only through these two, so the
// ordering is stated at every access rather than left to the default. Every read is an
// acquire load and every write a release store, which is what makes a producer's buffer
// writes visible before the consumer observes the advanced index. Single-producer /
// single-consumer, so only ordering is needed and never read-modify-write atomicity.
//
// Naming the pair here rather than per width is what lets a slot-state enum and a
// size_t index carry the identical rule; `atomic_load_explicit` is generic over both.
//
// `_Atomic` is native on every part in the target list: the emitted code is the plain
// load or store plus whatever that ISA needs to order it, and never a lock or a call into
// __atomic. Checked in the generated assembly - x86 emits nothing extra (its store order
// already gives acquire/release), Xtensa brackets the access with `memw`, RISC-V with
// `fence`, Cortex-M4 with `dmb`. So the cost is a counted instruction or two, not a wait,
// and bounded latency is preserved.
//
// Under ThreadSanitizer these expand to instrumented calls that branch on the
// memory-order argument. That order is a compile-time constant here, so only one arm is
// ever reachable in a correct SPSC program (see test_spsc_ring_no_race) and the lines
// below carry GCOVR_EXCL_BR_LINE for the residual arm.

/** @brief Acquire-load the atomic at @p p. */
#define PROTO_ATOMIC_LOAD(p) atomic_load_explicit((p), memory_order_acquire)

/** @brief Release-store @p v into the atomic at @p p. */
#define PROTO_ATOMIC_STORE(p, v) atomic_store_explicit((p), (v), memory_order_release)

// ---------------------------------------------------------------------------
// SPSC ring drain math (consumer side)
// ---------------------------------------------------------------------------
// The caller owns the storage (`buf` of `cap` bytes) and the indices; these advance
// `tail` only (the producer owns `head`). A read reads `tail` once and publishes it
// once at the end (one release store), not per byte.

/** @brief Bytes available to read (head - tail, modulo cap). */
static inline size_t pc_ring_available(const _Atomic size_t *head, const _Atomic size_t *tail, size_t cap)
{
    return (PROTO_ATOMIC_LOAD(head) + cap - PROTO_ATOMIC_LOAD(tail)) % cap; // GCOVR_EXCL_BR_LINE
}

/** @brief Pop one byte into @p out; false if empty. */
static inline proto_bool pc_ring_read_byte(const uint8_t *buf, size_t cap, const _Atomic size_t *head,
                                           _Atomic size_t *tail, uint8_t *out)
{
    size_t t = PROTO_ATOMIC_LOAD(tail); // GCOVR_EXCL_BR_LINE
    if (t == PROTO_ATOMIC_LOAD(head))   // GCOVR_EXCL_BR_LINE
    {
        return PROTO_FALSE;
    }
    *out = buf[t];
    PROTO_ATOMIC_STORE(tail, (t + 1) % cap); // GCOVR_EXCL_BR_LINE
    return PROTO_TRUE;
}

/** @brief Pop up to @p maxn bytes into @p dst; returns the count read. */
static inline size_t pc_ring_read(const uint8_t *buf, size_t cap, const _Atomic size_t *head, _Atomic size_t *tail,
                                  uint8_t *dst, size_t maxn)
{
    size_t h = PROTO_ATOMIC_LOAD(head); // GCOVR_EXCL_BR_LINE
    size_t t = PROTO_ATOMIC_LOAD(tail); // GCOVR_EXCL_BR_LINE
    size_t n = 0;
    while (n < maxn && t != h)
    {
        dst[n] = buf[t];
        n++;
        t = (t + 1) % cap;
    }
    PROTO_ATOMIC_STORE(tail, t); // GCOVR_EXCL_BR_LINE
    return n;
}

/** @brief Copy @p n bytes at @p off ahead of the tail into @p dst WITHOUT consuming. */
static inline void pc_ring_peek(const uint8_t *buf, size_t cap, const _Atomic size_t *tail, size_t off, uint8_t *dst,
                                size_t n)
{
    size_t idx = (PROTO_ATOMIC_LOAD(tail) + off) % cap; // GCOVR_EXCL_BR_LINE
    for (size_t i = 0; i < n; i++)
    {
        dst[i] = buf[idx];
        idx = (idx + 1) % cap;
    }
}

/** @brief Drop @p n bytes from the tail (advance past already-peeked data). */
static inline void pc_ring_consume(_Atomic size_t *tail, size_t cap, size_t n)
{
    PROTO_ATOMIC_STORE(tail, (PROTO_ATOMIC_LOAD(tail) + n) % cap); // GCOVR_EXCL_BR_LINE
}

// ---------------------------------------------------------------------------
// SPSC ring fill (producer side)
// ---------------------------------------------------------------------------
// The producer owns `head`. The recv callback checks pc_ring_free() against the
// whole inbound segment (refuse it for lossless backpressure if it will not fit),
// then copies each source span with pc_ring_write_span() advancing a LOCAL head,
// and publishes that head once at the end (one release store, not per byte).

/** @brief Free space to write: (cap-1) - used, one slot reserved to tell full from empty. */
static inline size_t pc_ring_free(const _Atomic size_t *head, const _Atomic size_t *tail, size_t cap)
{
    size_t used = (PROTO_ATOMIC_LOAD(head) + cap - PROTO_ATOMIC_LOAD(tail)) % cap; // GCOVR_EXCL_BR_LINE
    return (cap - 1) - used;
}

/**
 * @brief Copy @p len bytes from @p src into @p buf at local index @p head, wrap-aware
 * (at most two spans across the wrap), returning the advanced local head.
 *
 * The head is local and unpublished for the whole call: the caller checks pc_ring_free()
 * first and publishes the returned head once, so the consumer never sees a partially
 * filled span.
 *
 * A span here is a whole inbound segment, up to an MTU, and lands wherever the ring's fill
 * left off, so the move goes through proto_raw_read: it steps the machine word rather than
 * the byte, and it is the one owner of an access whose address carries no alignment.
 */
static inline size_t pc_ring_write_span(uint8_t *buf, size_t cap, size_t head, const uint8_t *src, size_t len)
{
    while (len > 0)
    {
        size_t chunk = cap - head; // bytes until the buffer end (wrap point)
        if (chunk > len)
        {
            chunk = len;
        }
        proto_raw_read(&buf[head], src, chunk);
        head = (head + chunk) % cap;
        src += chunk;
        len -= chunk;
    }
    return head;
}

#endif // PROTOCORE_RING_H
