// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_EDGE_CACHE_PROXY_H
#define PROTOCORE_EDGE_CACHE_PROXY_H

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_EDGE_CACHE

PROTOCORE_BEGIN_DECLS

struct protocore_dbm;  // bind_sd: the SD-backed store, by pointer only
struct EdgeCacheStats; // stats: filled by the engine, by pointer only

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
 * @c work is PROTOCORE_EDGE_PROXY_BORROW bytes the CALLER took, at an address it knows. It is not held past the call,
 * so nothing here aliases it. How those bytes are carved is this module's and is never named here.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    void (*enable)(uint8_t *);
    proto_bool (*map)(uint8_t *, const char *, const char *);
    void (*bind_sd)(uint8_t *, struct protocore_dbm *);
    proto_bool (*add_peer)(uint8_t *, const char *, uint16_t);
    void (*mesh_serve)(uint8_t *);
    void (*reset)(uint8_t *);
    proto_bool (*purge)(uint8_t *, const char *);
    uint32_t (*purge_prefix)(uint8_t *, const char *);
    void (*stats)(uint8_t *, struct EdgeCacheStats *);
} EdgeProxyNs;
PROTOCORE_NS_LAYOUT(EdgeProxyNs, enable, map, bind_sd, add_peer, mesh_serve, reset, purge, purge_prefix, stats);

/**
 * @brief Enable the edge cache on server: register the cache middleware + .
 * @param work PROTOCORE_EDGE_PROXY_BORROW bytes the caller took. Not held past the call.
 */
void protocore_edge_proxy_enable(uint8_t *work);
/**
 * @brief Map a request path prefix to an upstream origin (e.g. "/cdn/" -> .
 * @param work PROTOCORE_EDGE_PROXY_BORROW bytes the caller took. Not held past the call.
 * @param path_prefix Path prefix
 * @param origin_base_url Origin base url
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_edge_proxy_map(uint8_t *work, const char *path_prefix, const char *origin_base_url);
/**
 * @brief Bind an L2 persistent tier: an opened dbm handle (on a mounted WAL .
 * @param work PROTOCORE_EDGE_PROXY_BORROW bytes the caller took. Not held past the call.
 * @param dbm Dbm
 */
void protocore_edge_proxy_bind_sd(uint8_t *work, struct protocore_dbm *dbm);
/**
 * @brief Add a sibling peer to query on a full local miss before hitting the .
 * @param work PROTOCORE_EDGE_PROXY_BORROW bytes the caller took. Not held past the call.
 * @param host Host
 * @param port Port
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_edge_proxy_add_peer(uint8_t *work, const char *host, uint16_t port);
/**
 * @brief Serve sibling queries: register the PROTO_MESH handler so this node .
 * @param work PROTOCORE_EDGE_PROXY_BORROW bytes the caller took. Not held past the call.
 */
void protocore_edge_proxy_mesh_serve(uint8_t *work);
/**
 * @brief Clear the L1 store, the L2 store (if bound), all route maps, and .
 * @param work PROTOCORE_EDGE_PROXY_BORROW bytes the caller took. Not held past the call.
 */
void protocore_edge_proxy_reset(uint8_t *work);
/**
 * @brief Invalidate a single canonical key. true if an entry was purged.
 * @param work PROTOCORE_EDGE_PROXY_BORROW bytes the caller took. Not held past the call.
 * @param canonical_key Canonical key
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_edge_proxy_purge(uint8_t *work, const char *canonical_key);
/**
 * @brief Invalidate every entry whose request path begins with prefix. the .
 * @param work PROTOCORE_EDGE_PROXY_BORROW bytes the caller took. Not held past the call.
 * @param path_prefix Path prefix
 * @return The uint32_t.
 */
uint32_t protocore_edge_proxy_purge_prefix(uint8_t *work, const char *path_prefix);
/**
 * @brief Snapshot the cache counters.
 * @param work PROTOCORE_EDGE_PROXY_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 */
void protocore_edge_proxy_stats(uint8_t *work, struct EdgeCacheStats *out);

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

/** @brief Module namespace. */
PROTOCORE_NS EdgeProxyNs EdgeProxy PROTOCORE_UNUSED = {.enable = protocore_edge_proxy_enable,
                                                       .map = protocore_edge_proxy_map,
                                                       .bind_sd = protocore_edge_proxy_bind_sd,
                                                       .add_peer = protocore_edge_proxy_add_peer,
                                                       .mesh_serve = protocore_edge_proxy_mesh_serve,
                                                       .reset = protocore_edge_proxy_reset,
                                                       .purge = protocore_edge_proxy_purge,
                                                       .purge_prefix = protocore_edge_proxy_purge_prefix,
                                                       .stats = protocore_edge_proxy_stats};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_EDGE_CACHE

#endif // PROTOCORE_EDGE_CACHE_PROXY_H
