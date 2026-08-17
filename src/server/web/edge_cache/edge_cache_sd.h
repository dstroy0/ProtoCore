// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file edge_cache_sd.h
 * @brief CDN edge-cache tier - L2 SD persistence (PROTOCORE_ENABLE_EDGE_CACHE && PROTOCORE_ENABLE_DBM).
 *
 * The persistent second tier behind the bounded L1 RAM store (edge_cache): an evicted L1 entry is
 * written back to a dbm key-value store on the WAL (services/storage/dbm, SD-card backed on device, a RAM WalDev
 * for host tests), and an L1 miss is served by promoting the entry back from L2. Because the store is
 * log-structured on the WAL, the cached set survives a reboot (dbm rebuilds its index by replaying the
 * log on open).
 *
 * The L2 key is the entry's 32-byte SHA-256 digest (== PROTOCORE_DBM_KEY_MAX), so no key is re-derived. The
 * value is a compact, versioned, little-endian serialization of the entry's response metadata + body.
 *
 * These are pure functions over a caller-owned dbm handle and a caller-owned scratch buffer (no
 * file-scope state); the proxy glue (edge_cache_proxy) owns the dbm and the buffer and installs the
 * write-back / promote wiring.
 *
 * Reboot note: the monotonic insert time is meaningless across a reboot (no wall clock), so a promoted
 * entry is always treated as stale by the caller and revalidated - which is why only entries carrying a
 * validator (ETag / Last-Modified) are spilled: they are exactly the ones a cheap 304 can refresh.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_EDGE_CACHE_SD_H
#define PROTOCORE_EDGE_CACHE_SD_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_EDGE_CACHE

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief EdgeEntry, as the caller already knows it. */
struct EdgeEntry;

/** @brief What serialize takes: e, out, cap. */
typedef struct
{
    const struct EdgeEntry *e;
    uint8_t *out;
    size_t cap;
} EdgeCacheSdSerializeArgs;

/** @brief What deserialize takes: entry_buf, buf, len, e. */
typedef struct
{
    uint8_t *entry_buf; ///< scratch the entry is decoded into
    const uint8_t *buf;
    size_t len;
    struct EdgeEntry *e;
} EdgeCacheSdDeserializeArgs;

/** @brief What put takes: db, e, scratch, scratch_cap. */
typedef struct
{
    struct protocore_dbm *db;
    const struct EdgeEntry *e;
    uint8_t *scratch;
    size_t scratch_cap;
} EdgeCacheSdPutArgs;

/** @brief What get takes: entry_buf, db, digest, e, scratch, scratch_cap. */
typedef struct
{
    uint8_t *entry_buf; ///< scratch the entry is decoded into
    struct protocore_dbm *db;
    const uint8_t *digest; ///< 32 bytes.
    struct EdgeEntry *e;
    uint8_t *scratch;
    size_t scratch_cap;
} EdgeCacheSdGetArgs;

/** @brief What del takes: db, digest. */
typedef struct
{
    struct protocore_dbm *db;
    const uint8_t *digest; ///< 32 bytes.
} EdgeCacheSdDelArgs;

/** @brief What purge_prefix takes: db, path_prefix, scratch, ... */
typedef struct
{
    struct protocore_dbm *db;
    const char *path_prefix;
    uint8_t *scratch;
    size_t scratch_cap;
} EdgeCacheSdPurgePrefixArgs;

/** @brief What purge_all takes: db. */
typedef struct
{
    struct protocore_dbm *db;
} EdgeCacheSdPurgeAllArgs;

/**
 * @brief CDN edge-cache tier - L2 SD persistence (PROTOCORE_ENABLE_EDGE_CACHE && PROTOCORE_ENABLE_DBM). The persistent
 * ...
 *
 * A caller sets the members a call takes, invokes it through ::EdgeCacheSd with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   EdgeCacheSd.serialize_args.e = ...;
 *   EdgeCacheSd.serialize_args.out = ...;
 *   EdgeCacheSd.serialize_args.cap = ...;
 *   EdgeCacheSd.serialize(work);
 *   // EdgeCacheSd.n is what the call reports
 *
 * @var EdgeCacheSdNs::serialize_args  what serialize takes: e, out, cap
 * @var EdgeCacheSdNs::deserialize_args  what deserialize takes: entry_buf, buf, len, e
 * @var EdgeCacheSdNs::put_args  what put takes: db, e, scratch, scratch_cap
 * @var EdgeCacheSdNs::get_args  what get takes: entry_buf, db, digest, e, scratch, scratch_cap
 * @var EdgeCacheSdNs::del_args  what del takes: db, digest
 * @var EdgeCacheSdNs::purge_prefix_args  what purge_prefix takes: db, path_prefix, scratch,
 * @var EdgeCacheSdNs::purge_all_args  what purge_all takes: db
 * @var EdgeCacheSdNs::ok  false on a short/corrupt/oversized buffer or a version mismatch ...
 * @var EdgeCacheSdNs::n  the byte length written, or 0 if it would not fit cap. ...
 * @var EdgeCacheSdNs::count  what a call reports
 * @var EdgeCacheSdNs::serialize  serialize e's response metadata + body into out (little-endian, ...
 * @var EdgeCacheSdNs::deserialize  rehydrate an entry from buf (as produced by ::edge_sd_serialize) ...
 * @var EdgeCacheSdNs::put  write e back to the L2 store (keyed by its digest), using scratch ...
 * @var EdgeCacheSdNs::get  promote the entry stored under digest from L2 into e (via scratch)
 * @var EdgeCacheSdNs::del  drop the L2 entry stored under digest. true if one existed
 * @var EdgeCacheSdNs::purge_prefix  drop every L2 entry whose stored request path begins with prefix ...
 * @var EdgeCacheSdNs::purge_all  drop every L2 entry. the number purged
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    EdgeCacheSdSerializeArgs serialize_args;
    EdgeCacheSdDeserializeArgs deserialize_args;
    EdgeCacheSdPutArgs put_args;
    EdgeCacheSdGetArgs get_args;
    EdgeCacheSdDelArgs del_args;
    EdgeCacheSdPurgePrefixArgs purge_prefix_args;
    EdgeCacheSdPurgeAllArgs purge_all_args;

    proto_bool ok;
    size_t n;
    uint32_t count;

    void (*const serialize)(uint8_t *restrict work);
    void (*const deserialize)(uint8_t *restrict work);
    // The store operations are the only part that needs a key/value database behind it, so they
    // are the only part the flag gates. The codec above is pure and is always here.
#if PROTOCORE_ENABLE_DBM
    void (*const put)(uint8_t *restrict work);
    void (*const get)(uint8_t *restrict work);
    void (*const del)(uint8_t *restrict work);
    void (*const purge_prefix)(uint8_t *restrict work);
    void (*const purge_all)(uint8_t *restrict work);
#endif
} EdgeCacheSdNs;

/** @brief The one symbol this module exports. */
extern EdgeCacheSdNs EdgeCacheSd;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_EDGE_CACHE

#endif // PROTOCORE_EDGE_CACHE_SD_H
