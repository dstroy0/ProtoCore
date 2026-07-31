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
 * client (pc_client_* over its pool). Sharing it is deliberate - it keeps the ring
 * invariants in a single place so a consumer in any layer drains identically and
 * cannot reimplement (and subtly break) the wrap/ordering by hand.
 *
 * Ownership rule: exactly one producer advances `head`, exactly one consumer
 * advances `tail`; indices are pc_atomic so a producer's buffer writes are visible
 * before the consumer observes the advanced index (acquire/release), correct across
 * the tcpip_thread <-> worker/caller boundary on either core. No locks, no RMW.
 */

#include <atomic>
#include <stddef.h>
#include <stdint.h>
#include <string.h> // memcpy (producer span copy)

// ---------------------------------------------------------------------------
// Cross-thread field wrapper
// ---------------------------------------------------------------------------
//
// Fields shared across a producer/consumer thread boundary (ring head/tail, slot
// state) are wrapped in pc_atomic so the ordering is enforced by construction
// rather than by convention: every read is an acquire load and every write a
// release store, so a producer's buffer writes are visible before the consumer
// observes the advanced index (single-producer / single-consumer, so no
// read-modify-write atomicity is needed - only ordering). On Xtensa/x86 an aligned
// acquire/release load/store is a plain load/store plus a compiler barrier, so this
// adds no lock and no measurable cost - determinism (bounded latency) is preserved.
// The conversion operators keep every ==/=/arithmetic call site unchanged; the copy
// ctor/assignment let a containing aggregate still be value-reset (`x = {}`).
template <typename T> struct pc_atomic
{
    std::atomic<T> v;
    pc_atomic() noexcept : v(T())
    {
    }
    pc_atomic(T x) noexcept : v(x)
    {
    }
    pc_atomic(const pc_atomic &o) noexcept : v(o.v.load(std::memory_order_acquire))
    {
    }
    pc_atomic &operator=(const pc_atomic &o) noexcept
    {
        v.store(o.v.load(std::memory_order_acquire), std::memory_order_release);
        return *this;
    }
    pc_atomic &operator=(T x) noexcept
    {
        v.store(x, std::memory_order_release); // GCOVR_EXCL_BR_LINE - the only branches here come from
        // ThreadSanitizer's atomic-store instrumentation (fixed release memory-order dispatch);
        // the residual arm is unreachable in a correct SPSC program - see test_spsc_ring_no_race.
        return *this;
    }
    operator T() const noexcept
    {
        return v.load(std::memory_order_acquire); // GCOVR_EXCL_BR_LINE - the only branches here come
        // from ThreadSanitizer's atomic-load instrumentation (fixed acquire memory-order dispatch);
        // the residual arm is unreachable in a correct SPSC program - see test_spsc_ring_no_race.
    }
};

// ---------------------------------------------------------------------------
// SPSC ring drain math (consumer side)
// ---------------------------------------------------------------------------
// The caller owns the storage (`buf` of `cap` bytes) and the indices; these advance
// `tail` only (the producer owns `head`). A read reads `tail` once and publishes it
// once at the end (one release store), not per byte.

/** @brief Bytes available to read (head - tail, modulo cap). */
static inline size_t pc_ring_available(const pc_atomic<size_t> &head, const pc_atomic<size_t> &tail, size_t cap)
{
    return ((size_t)head + cap - (size_t)tail) % cap;
}

/** @brief Pop one byte into @p out; false if empty. */
static inline bool pc_ring_read_byte(const uint8_t *buf, size_t cap, const pc_atomic<size_t> &head,
                                     pc_atomic<size_t> &tail, uint8_t *out)
{
    size_t t = tail;
    if (t == (size_t)head)
    {
        return false;
    }
    *out = buf[t];
    tail = (t + 1) % cap;
    return true;
}

/** @brief Pop up to @p maxn bytes into @p dst; returns the count read. */
static inline size_t pc_ring_read(const uint8_t *buf, size_t cap, const pc_atomic<size_t> &head,
                                  pc_atomic<size_t> &tail, uint8_t *dst, size_t maxn)
{
    size_t h = head;
    size_t t = tail;
    size_t n = 0;
    while (n < maxn && t != h)
    {
        dst[n++] = buf[t];
        t = (t + 1) % cap;
    }
    tail = t;
    return n;
}

/** @brief Copy @p n bytes at @p off ahead of the tail into @p dst WITHOUT consuming. */
static inline void pc_ring_peek(const uint8_t *buf, size_t cap, const pc_atomic<size_t> &tail, size_t off, uint8_t *dst,
                                size_t n)
{
    size_t idx = ((size_t)tail + off) % cap;
    for (size_t i = 0; i < n; i++)
    {
        dst[i] = buf[idx];
        idx = (idx + 1) % cap;
    }
}

/** @brief Drop @p n bytes from the tail (advance past already-peeked data). */
static inline void pc_ring_consume(pc_atomic<size_t> &tail, size_t cap, size_t n)
{
    tail = ((size_t)tail + n) % cap;
}

// ---------------------------------------------------------------------------
// SPSC ring fill (producer side)
// ---------------------------------------------------------------------------
// The producer owns `head`. The recv callback checks pc_ring_free() against the
// whole inbound segment (refuse it for lossless backpressure if it will not fit),
// then copies each source span with pc_ring_write_span() advancing a LOCAL head,
// and publishes that head once at the end (one release store, not per byte).

/** @brief Free space to write: (cap-1) - used, one slot reserved to tell full from empty. */
static inline size_t pc_ring_free(const pc_atomic<size_t> &head, const pc_atomic<size_t> &tail, size_t cap)
{
    size_t used = ((size_t)head + cap - (size_t)tail) % cap;
    return (cap - 1) - used;
}

/**
 * @brief Copy @p len bytes from @p src into @p buf at local index @p head, wrap-aware
 * (a contiguous memcpy, at most two spans across the wrap), returning the advanced
 * local head. Caller checks pc_ring_free() first and publishes the returned head
 * once. Bulk memcpy, not a per-byte loop.
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
        memcpy(&buf[head], src, chunk);
        head = (head + chunk) % cap;
        src += chunk;
        len -= chunk;
    }
    return head;
}

#endif // PROTOCORE_RING_H
