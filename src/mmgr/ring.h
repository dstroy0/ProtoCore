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

#include "mmgr/rawmemcpy.h" // proto_raw_read: the producer span move
#include "mmgr/span.h"      // pc_cspan: the region a held slot keeps out
#include <stdatomic.h>      // _Atomic, atomic_load_explicit, atomic_store_explicit, memory_order_*

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

/**
 * @brief A ring capacity is a power of two, so an index wraps with an AND.
 *
 * Every owner asserts this over its own capacity. Not every part in the target list has a hardware
 * divide, and where it is missing the compiler emits a call into a software routine, which a
 * per-byte wrap would take on every byte.
 */
#define PC_RING_POW2(cap) (((cap) & ((cap) - 1)) == 0)

/** @brief Wrap @p i into a ring of @p cap bytes. */
#define PC_RING_WRAP(i, cap) ((i) & ((cap) - 1))

/** @brief Bytes available to read (head - tail, wrapped). */
static inline size_t pc_ring_available(const _Atomic size_t *head, const _Atomic size_t *tail, size_t cap)
{
    return PC_RING_WRAP(PROTO_ATOMIC_LOAD(head) - PROTO_ATOMIC_LOAD(tail), cap);
}

/** @brief Pop one byte into @p out; false if empty. */
static inline proto_bool pc_ring_read_byte(const uint8_t *buf, size_t cap, const _Atomic size_t *head,
                                           _Atomic size_t *tail, uint8_t *out)
{
    size_t t = PROTO_ATOMIC_LOAD(tail);
    if (t == PROTO_ATOMIC_LOAD(head))
    {
        return PROTO_FALSE;
    }
    *out = buf[t];
    PROTO_ATOMIC_STORE(tail, PC_RING_WRAP(t + 1, cap));
    return PROTO_TRUE;
}

/** @brief Pop up to @p maxn bytes into @p dst; returns the count read. */
static inline size_t pc_ring_read(const uint8_t *buf, size_t cap, const _Atomic size_t *head, _Atomic size_t *tail,
                                  uint8_t *dst, size_t maxn)
{
    size_t h = PROTO_ATOMIC_LOAD(head);
    size_t t = PROTO_ATOMIC_LOAD(tail);
    size_t n = 0;
    while (n < maxn && t != h)
    {
        dst[n] = buf[t];
        n++;
        t = PC_RING_WRAP(t + 1, cap);
    }
    PROTO_ATOMIC_STORE(tail, t);
    return n;
}

/** @brief Copy @p n bytes at @p off ahead of the tail into @p dst WITHOUT consuming. */
static inline void pc_ring_peek(const uint8_t *buf, size_t cap, const _Atomic size_t *tail, size_t off, uint8_t *dst,
                                size_t n)
{
    size_t idx = PC_RING_WRAP(PROTO_ATOMIC_LOAD(tail) + off, cap);
    for (size_t i = 0; i < n; i++)
    {
        dst[i] = buf[idx];
        idx = PC_RING_WRAP(idx + 1, cap);
    }
}

/** @brief Drop @p n bytes from the tail (advance past already-peeked data). */
static inline void pc_ring_consume(_Atomic size_t *tail, size_t cap, size_t n)
{
    PROTO_ATOMIC_STORE(tail, PC_RING_WRAP(PROTO_ATOMIC_LOAD(tail) + n, cap));
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
    size_t used = PC_RING_WRAP(PROTO_ATOMIC_LOAD(head) - PROTO_ATOMIC_LOAD(tail), cap);
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
        head = PC_RING_WRAP(head + chunk, cap);
        src += chunk;
        len -= chunk;
    }
    return head;
}

// ---------------------------------------------------------------------------
// Segment ring: one message per segment
// ---------------------------------------------------------------------------
// The ring is `nsegs` segments of `seg_size` bytes and one entry fills one segment, so an
// entry is contiguous and a consumer reads it where it was written. The segments from
// `rel` up to `claim` are filled and not yet released. The producer advances `claim` and
// the consumer advances `rel`, one side each, the same rule as the byte ring above.
// Segments release in order. `nsegs` is a power of two, so the index is a mask.

/** @brief Segments filled and not yet released. */
static inline size_t pc_seg_inflight(const _Atomic size_t *claim, const _Atomic size_t *rel)
{
    return PROTO_ATOMIC_LOAD(claim) - PROTO_ATOMIC_LOAD(rel);
}

/**
 * @brief Index of the segment the producer fills next.
 *
 * Publishing is separate, so a half-filled segment is never visible to the consumer.
 * @return false when every segment is in flight.
 */
static inline proto_bool pc_seg_next(const _Atomic size_t *claim, const _Atomic size_t *rel, size_t nsegs, size_t *idx)
{
    size_t c = PROTO_ATOMIC_LOAD(claim);
    if ((c - PROTO_ATOMIC_LOAD(rel)) >= nsegs)
    {
        return PROTO_FALSE;
    }
    *idx = c & (nsegs - 1);
    return PROTO_TRUE;
}

/** @brief Make the filled segment visible to the consumer. */
static inline void pc_seg_publish(_Atomic size_t *claim)
{
    PROTO_ATOMIC_STORE(claim, PROTO_ATOMIC_LOAD(claim) + 1);
}

/**
 * @brief Index of the segment the consumer sends next.
 * @return false when none is in flight.
 */
static inline proto_bool pc_seg_front(const _Atomic size_t *claim, const _Atomic size_t *rel, size_t nsegs, size_t *idx)
{
    size_t r = PROTO_ATOMIC_LOAD(rel);
    if (PROTO_ATOMIC_LOAD(claim) == r)
    {
        return PROTO_FALSE;
    }
    *idx = r & (nsegs - 1);
    return PROTO_TRUE;
}

/** @brief Free the front segment: the wire has taken those bytes. */
static inline void pc_seg_release(_Atomic size_t *rel)
{
    PROTO_ATOMIC_STORE(rel, PROTO_ATOMIC_LOAD(rel) + 1);
}

/** @brief The contiguous span of segment @p idx, @p seg_size bytes. */
static inline uint8_t *pc_seg_at(uint8_t *buf, size_t seg_size, size_t idx)
{
    return &buf[idx * seg_size];
}

// ---------------------------------------------------------------------------
// Slot view: a mask per meaning, and the region a hold keeps out
// ---------------------------------------------------------------------------
// The views above index a slot the caller already chose; this one answers which slot. A
// slot count that fits a word makes that a ctz rather than a scan, and a hold one fetch_or
// whose return says whether it was won, so a loser moves on instead of retrying.
//
// A held slot carries the region the wire is still reading. A forward hands the egress
// that pointer and walks its length rather than copying, so the slot cannot be reused
// until the transmit completes and drops it.

/** @brief Slots a mask can address. A wider pool falls back to the head/tail view. */
#define PC_RING_SLOTS_MAX 32

// A shift past the word is undefined, and every index below comes from a ctz that cannot produce
// one - so the bound is here rather than at each call site, the way the pool's state setter carries
// its own. An out-of-range slot names nothing, so it reads as held and is never handed out.
static inline uint32_t pc_slot_bit(size_t idx)
{
    if (idx >= PC_RING_SLOTS_MAX)
    {
        return 0u;
    }
    return 1u << idx;
}

/** @brief Every slot below @p count, as a mask. */
static inline uint32_t pc_slot_all(size_t count)
{
    if (count >= PC_RING_SLOTS_MAX)
    {
        return 0xFFFFFFFFu;
    }
    return (1u << count) - 1u;
}

/**
 * @brief Take slot @p idx if no one holds it.
 * @return true when this caller took it; false when another already had it.
 */
static inline proto_bool pc_slot_take(_Atomic uint32_t *held, size_t idx)
{
    const uint32_t bit = pc_slot_bit(idx);
    if (bit == 0u)
    {
        return PROTO_FALSE;
    }
    uint32_t prev = atomic_fetch_or_explicit(held, bit, memory_order_acquire);
    return (prev & bit) == 0u;
}

/**
 * @brief Take slot @p idx and record the @p len bytes at @p ptr that the wire will read.
 *
 * The recorded region is the keepout: it stays valid until pc_slot_drop(), so an egress
 * handed @p ptr walks it in place.
 * @return true when this caller took it; false when another already had it.
 */
static inline proto_bool pc_slot_hold(_Atomic uint32_t *held, pc_cspan *keepout, size_t idx, const uint8_t *ptr,
                                      size_t len)
{
    if (!pc_slot_take(held, idx))
    {
        return PROTO_FALSE;
    }
    keepout[idx].buf = ptr;
    keepout[idx].len = len;
    keepout[idx].pos = 0;
    keepout[idx].err = PROTO_FALSE;
    return PROTO_TRUE;
}

/** @brief The region slot @p idx is keeping out, for the egress to walk. */
static inline const pc_cspan *pc_slot_keepout(const pc_cspan *keepout, size_t idx)
{
    return &keepout[idx];
}

/** @brief Give slot @p idx back: the wire has taken its bytes. */
static inline void pc_slot_drop(_Atomic uint32_t *held, size_t idx)
{
    atomic_fetch_and_explicit(held, ~pc_slot_bit(idx), memory_order_release);
}

/** @brief Mark slot @p idx in @p mask, published after the slot is written. */
static inline void pc_slot_mark(_Atomic uint32_t *mask, size_t idx)
{
    atomic_fetch_or_explicit(mask, pc_slot_bit(idx), memory_order_release);
}

/** @brief Clear slot @p idx in @p mask, before the slot is written. */
static inline void pc_slot_clear(_Atomic uint32_t *mask, size_t idx)
{
    atomic_fetch_and_explicit(mask, ~pc_slot_bit(idx), memory_order_release);
}

/** @brief The slots @p mask names, minus the held ones, within @p count. */
static inline uint32_t pc_slot_ready(const _Atomic uint32_t *mask, const _Atomic uint32_t *held, size_t count)
{
    return PROTO_ATOMIC_LOAD(mask) & ~PROTO_ATOMIC_LOAD(held) & pc_slot_all(count);
}

/**
 * @brief Lowest slot set in @p m.
 * @return its index, or -1 when @p m is empty.
 */
static inline int32_t pc_slot_next(uint32_t m)
{
    if (m == 0u)
    {
        return -1;
    }
    return (int32_t)__builtin_ctz(m);
}

#endif // PROTOCORE_RING_H
