// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file edge_cache_proxy.h
 * @brief CDN edge-cache tier - server glue (PROTOCORE_ENABLE_EDGE_CACHE).
 *
 * Wires the pure engine (edge_cache) + async fetch (edge_fetch) into a PC: registers the
 * cache as a middleware and installs the async-fetch poll hook, maps request path prefixes to upstream
 * origins (fetched over protocore_client), and serves hits with the constant-memory send-pump. A miss or a
 * stale-entry revalidation suspends the client request and drives the origin fetch from the slot's poll,
 * so the worker never stalls; every failure path fails open. Purge + stats round it out.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_EDGE_CACHE_PROXY_H
#define PROTOCORE_EDGE_CACHE_PROXY_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_EDGE_CACHE

PROTOCORE_BEGIN_DECLS

// PROTOCORE_EDGE_PROXY_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

/** @brief EdgeCacheStats, as the caller already knows it. */
struct EdgeCacheStats;

/** @brief What map takes: path_prefix, origin_base_url. */
typedef struct
{
    const char *path_prefix;
    const char *origin_base_url;
} EdgeProxyMapArgs;

#if PROTOCORE_ENABLE_DBM
/** @brief What bind_sd takes: dbm. */
typedef struct
{
    struct protocore_dbm *dbm;
} EdgeProxyBindSdArgs;
#endif

#if PROTOCORE_ENABLE_EDGE_MESH
/** @brief What add_peer takes: host, port. */
typedef struct
{
    const char *host;
    uint16_t port;
} EdgeProxyAddPeerArgs;
#endif

/** @brief What purge takes: canonical_key. */
typedef struct
{
    const char *canonical_key;
} EdgeProxyPurgeArgs;

/** @brief What purge_prefix takes: path_prefix. */
typedef struct
{
    const char *path_prefix;
} EdgeProxyPurgePrefixArgs;

/** @brief What stats takes: out. */
typedef struct
{
    struct EdgeCacheStats *out;
} EdgeProxyStatsArgs;

/**
 * @brief CDN edge-cache tier - server glue (PROTOCORE_ENABLE_EDGE_CACHE).
 *
 * A caller sets the members a call takes, invokes it through ::EdgeProxy with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   EdgeProxy.enable(work);
 *
 * @var EdgeProxyNs::map_args  what map takes: path_prefix, origin_base_url
 * @var EdgeProxyNs::bind_sd_args  what bind_sd takes: dbm
 * @var EdgeProxyNs::add_peer_args  what add_peer takes: host, port
 * @var EdgeProxyNs::purge_args  what purge takes: canonical_key
 * @var EdgeProxyNs::purge_prefix_args  what purge_prefix takes: path_prefix
 * @var EdgeProxyNs::stats_args  what stats takes: out
 * @var EdgeProxyNs::ok  false if the map table is full, an argument overflows, or the ...
 * @var EdgeProxyNs::n  the count a call reports
 * @var EdgeProxyNs::enable  enable the edge cache on server: register the cache middleware + ...
 * @var EdgeProxyNs::map  map a request path prefix to an upstream origin (e.g. "/cdn/" -> ...
 * @var EdgeProxyNs::bind_sd  bind an L2 persistent tier: an opened dbm handle (on a mounted WAL ...
 * @var EdgeProxyNs::add_peer  add a sibling peer to query on a full local miss before hitting the ...
 * @var EdgeProxyNs::mesh_serve  serve sibling queries: register the PROTO_MESH handler so this node ...
 * @var EdgeProxyNs::reset  clear the L1 store, the L2 store (if bound), all route maps, and ...
 * @var EdgeProxyNs::purge  invalidate a single canonical key. true if an entry was purged
 * @var EdgeProxyNs::purge_prefix  invalidate every entry whose request path begins with prefix. the ...
 * @var EdgeProxyNs::stats  snapshot the cache counters
 *
 * @c work is PROTOCORE_EDGE_PROXY_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    EdgeProxyMapArgs map_args;
#if PROTOCORE_ENABLE_DBM
    EdgeProxyBindSdArgs bind_sd_args;
#endif
#if PROTOCORE_ENABLE_EDGE_MESH
    EdgeProxyAddPeerArgs add_peer_args;
#endif
    EdgeProxyPurgeArgs purge_args;
    EdgeProxyPurgePrefixArgs purge_prefix_args;
    EdgeProxyStatsArgs stats_args;
    proto_bool ok;
    uint32_t n;
#if PROTOCORE_ENABLE_DBM
#endif
#if PROTOCORE_ENABLE_EDGE_MESH
#endif
#if PROTOCORE_ENABLE_EDGE_MESH
#endif
} EdgeProxyVars;

/** @brief The operands and the outcome. */
extern EdgeProxyVars EdgeProxyV;

/** @brief The entries. */
typedef struct
{
    void (*const enable)(uint8_t *restrict work);
    void (*const map)(uint8_t *restrict work);
    void (*const bind_sd)(uint8_t *restrict work);
    void (*const add_peer)(uint8_t *restrict work);
    void (*const mesh_serve)(uint8_t *restrict work);
    void (*const reset)(uint8_t *restrict work);
    void (*const purge)(uint8_t *restrict work);
    void (*const purge_prefix)(uint8_t *restrict work);
    void (*const stats)(uint8_t *restrict work);
} EdgeProxyNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in EdgeProxyV or a region of the borrow at a fixed offset.
void protocore_edge_proxy_enable(uint8_t *restrict work);
void protocore_edge_proxy_map(uint8_t *restrict work);
#if PROTOCORE_ENABLE_DBM
void protocore_edge_proxy_bind_sd(uint8_t *restrict work);
#endif
#if PROTOCORE_ENABLE_EDGE_MESH
void protocore_edge_proxy_add_peer(uint8_t *restrict work);
#endif
#if PROTOCORE_ENABLE_EDGE_MESH
void protocore_edge_proxy_mesh_serve(uint8_t *restrict work);
#endif
void protocore_edge_proxy_reset(uint8_t *restrict work);
void protocore_edge_proxy_purge(uint8_t *restrict work);
void protocore_edge_proxy_purge_prefix(uint8_t *restrict work);
void protocore_edge_proxy_stats(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `EdgeProxy.enable(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const EdgeProxyNs EdgeProxy __attribute__((unused)) = {
    .enable = protocore_edge_proxy_enable,
    .map = protocore_edge_proxy_map,
#if PROTOCORE_ENABLE_DBM
    .bind_sd = protocore_edge_proxy_bind_sd,
#endif
#if PROTOCORE_ENABLE_EDGE_MESH
    .add_peer = protocore_edge_proxy_add_peer,
#endif
#if PROTOCORE_ENABLE_EDGE_MESH
    .mesh_serve = protocore_edge_proxy_mesh_serve,
#endif
    .reset = protocore_edge_proxy_reset,
    .purge = protocore_edge_proxy_purge,
    .purge_prefix = protocore_edge_proxy_purge_prefix,
    .stats = protocore_edge_proxy_stats,
};

/**
 * @brief The PROTOCORE_EDGE_PROXY_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_edge_cache_proxy_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_EDGE_CACHE

#endif // PROTOCORE_EDGE_CACHE_PROXY_H
