// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sockpool.h
 * @brief Dynamic socket recycling: an LRU connection-slot pool (PROTOCORE_ENABLE_SOCKPOOL).
 *
 * A device serves a bounded number of concurrent connections. When the pool saturates, the right move is
 * not to drop the new connection but to *recycle* the least-recently-active slot (the one most likely to
 * be a dead / idle keep-alive) and hand it to the new peer, returning the evicted id so the transport can
 * close it cleanly. This is the transport-pool half left open by `services/netadapt`.
 *
 * This is that pure policy: a fixed table of connection slots (each an id + last-used tick), with acquire
 * (free slot, else LRU-recycle), touch (mark active), release, and find. The app owns the real sockets;
 * this owns *which* slot a connection lives in and which to reclaim under pressure. No heap, no stdlib,
 * host-testable.
 */

#ifndef PROTOCORE_SOCKPOOL_H
#define PROTOCORE_SOCKPOOL_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_SOCKPOOL

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief One connection slot. */
typedef struct
{
    proto_bool in_use;
    uint32_t id;        ///< application connection id (e.g. a socket fd / handle).
    uint32_t last_used; ///< tick of the last acquire/touch (for LRU).
} SockSlot;

/** @brief A fixed pool of connection slots (storage is caller-owned). */
typedef struct
{
    SockSlot *slots;
    size_t n;
} SockPool;

/** @brief Acquire outcome (the sole return of protocore_sockpool_acquire). */
typedef enum PROTO_ENUM_PACKED
{
    SOCK_ACQ_FREE = 0,     ///< a free slot was used.
    SOCK_ACQ_RECYCLED = 1, ///< the pool was full; the LRU slot was recycled (see evicted_id).
    SOCK_ACQ_FAIL = 2      ///< the pool has zero slots / bad args.
} SockAcq;

/** @brief What init takes: p, slots, n. */
typedef struct
{
    SockPool *p;
    SockSlot *slots;
    size_t n;
} SockpoolInitArgs;

/** @brief What acquire takes: p, id, now, idx, evicted_id. */
typedef struct
{
    SockPool *p;
    uint32_t id;
    uint32_t now;
    size_t *idx; ///< (may be null) receives the chosen slot index
    uint32_t *evicted_id;
} SockpoolAcquireArgs;

/** @brief What touch takes: p, idx, now. */
typedef struct
{
    SockPool *p;
    size_t idx;
    uint32_t now;
} SockpoolTouchArgs;

/** @brief What release takes: p, idx. */
typedef struct
{
    SockPool *p;
    size_t idx;
} SockpoolReleaseArgs;

/** @brief What find takes: p, id, idx. */
typedef struct
{
    const SockPool *p;
    uint32_t id;
    size_t *idx;
} SockpoolFindArgs;

/** @brief What in_use takes: p. */
typedef struct
{
    const SockPool *p;
} SockpoolInUseArgs;

/**
 * @brief Dynamic socket recycling: an LRU connection-slot pool (PROTOCORE_ENABLE_SOCKPOOL). A device serves a bounded
 * ...
 *
 * A caller sets the members a call takes, invokes it through ::Sockpool with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Sockpool.init_args.p = ...;
 *   Sockpool.init_args.slots = ...;
 *   Sockpool.init_args.n = ...;
 *   Sockpool.init(work);
 *
 * @var SockpoolNs::init_args  what init takes: p, slots, n
 * @var SockpoolNs::acquire_args  what acquire takes: p, id, now, idx, evicted_id
 * @var SockpoolNs::touch_args  what touch takes: p, idx, now
 * @var SockpoolNs::release_args  what release takes: p, idx
 * @var SockpoolNs::find_args  what find takes: p, id, idx
 * @var SockpoolNs::in_use_args  what in_use takes: p
 * @var SockpoolNs::ok  a call's true/false outcome
 * @var SockpoolNs::acq  SOCK_ACQ_FREE / SOCK_ACQ_RECYCLED / SOCK_ACQ_FAIL
 * @var SockpoolNs::n  the count a call reports
 * @var SockpoolNs::init  initialize a pool over caller storage; all slots start free
 * @var SockpoolNs::acquire  acquire a slot for connection id at tick now. Uses a free slot if ...
 * @var SockpoolNs::touch  mark slot idx active at tick now (refreshes its LRU position)
 * @var SockpoolNs::release  free slot idx. true if it was a valid, in-use slot
 * @var SockpoolNs::find  find the slot holding connection id. idx (may be null) gets the ...
 * @var SockpoolNs::in_use  count of in-use slots
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    SockpoolInitArgs init_args;
    SockpoolAcquireArgs acquire_args;
    SockpoolTouchArgs touch_args;
    SockpoolReleaseArgs release_args;
    SockpoolFindArgs find_args;
    SockpoolInUseArgs in_use_args;

    proto_bool ok;
    SockAcq acq;
    size_t n;

    void (*const init)(uint8_t *restrict work);
    void (*const acquire)(uint8_t *restrict work);
    void (*const touch)(uint8_t *restrict work);
    void (*const release)(uint8_t *restrict work);
    void (*const find)(uint8_t *restrict work);
    void (*const in_use)(uint8_t *restrict work);
} SockpoolNs;

/** @brief The one symbol this module exports. */
extern SockpoolNs Sockpool;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SOCKPOOL

#endif // PROTOCORE_SOCKPOOL_H
