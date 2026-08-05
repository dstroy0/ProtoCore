// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sockpool.h
 * @brief Dynamic socket recycling: an LRU connection-slot pool (PC_ENABLE_SOCKPOOL).
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

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_SOCKPOOL

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

/** @brief Acquire outcome (the sole return of pc_sockpool_acquire). */
typedef enum PROTO_ENUM_PACKED
{
    SOCK_ACQ_FREE = 0,     ///< a free slot was used.
    SOCK_ACQ_RECYCLED = 1, ///< the pool was full; the LRU slot was recycled (see evicted_id).
    SOCK_ACQ_FAIL = 2      ///< the pool has zero slots / bad args.
} SockAcq;

/** @brief Initialize a pool over caller storage; all slots start free. */
void pc_sockpool_init(SockPool *p, SockSlot *slots, size_t n);

/**
 * @brief Acquire a slot for connection @p id at tick @p now.
 *
 * Uses a free slot if one exists, otherwise recycles the least-recently-used in-use slot. On a recycle,
 * @p evicted_id (may be null) receives the id that was kicked out so the caller can close that socket.
 * @param idx  (may be null) receives the chosen slot index.
 * @return SOCK_ACQ_FREE / SOCK_ACQ_RECYCLED / SOCK_ACQ_FAIL.
 */
SockAcq pc_sockpool_acquire(SockPool *p, uint32_t id, uint32_t now, size_t *idx, uint32_t *evicted_id);

/** @brief Mark slot @p idx active at tick @p now (refreshes its LRU position). */
void pc_sockpool_touch(SockPool *p, size_t idx, uint32_t now);

/** @brief Free slot @p idx. @return true if it was a valid, in-use slot. */
proto_bool pc_sockpool_release(SockPool *p, size_t idx);

/** @brief Find the slot holding connection @p id. @p idx (may be null) gets the index. @return found. */
proto_bool pc_sockpool_find(const SockPool *p, uint32_t id, size_t *idx);

/** @brief Count of in-use slots. */
size_t pc_sockpool_in_use(const SockPool *p);

#endif // PC_ENABLE_SOCKPOOL

PROTO_END_DECLS

#endif // PROTOCORE_SOCKPOOL_H
