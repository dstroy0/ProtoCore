// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file edge_cache_proxy.h
 * @brief CDN edge-cache tier - server glue (PC_ENABLE_EDGE_CACHE).
 *
 * Wires the pure engine (edge_cache) + async fetch (edge_fetch) into a PC: registers the
 * cache as a middleware and installs the async-fetch poll hook, maps request path prefixes to upstream
 * origins (fetched over pc_client), and serves hits with the constant-memory send-pump. A miss or a
 * stale-entry revalidation suspends the client request and drives the origin fetch from the slot's poll,
 * so the worker never stalls; every failure path fails open. Purge + stats round it out.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_EDGE_CACHE_PROXY_H
#define PROTOCORE_EDGE_CACHE_PROXY_H

#include "protocore_config.h"

#if PC_ENABLE_EDGE_CACHE

#include "services/web/edge_cache/edge_cache.h" // EdgeCacheStats

/**
 * @brief Enable the edge cache on @p server: register the cache middleware + the async-fetch poll hook,
 *        bind the origin transport to pc_client, and clear the L1 store. Call once.
 */
void pc_edge_cache_enable(void);

/**
 * @brief Map a request path prefix to an upstream origin (e.g. "/cdn/" -> "http://origin.local").
 *
 * A cacheable GET/HEAD under @p path_prefix is fetched from the origin host + the full request path. An
 * `https://` origin is fetched over TLS when PC_ENABLE_EDGE_ORIGIN_TLS is set, otherwise rejected.
 * @return false if the map table is full, an argument overflows, or the origin URL is https (TLS off) /
 * malformed.
 */
proto_bool pc_edge_cache_map(const char *path_prefix, const char *origin_base_url);

#if PC_ENABLE_EDGE_ORIGIN_TLS
/**
 * @brief Set the CA used to verify a TLS (`https://`) origin (PEM incl. NUL, or DER). Without a CA the
 *        origin fetch is encrypt-only (no authentication - MITM-able); a CA switches to full chain +
 *        hostname verification. NOTE: the client-TLS trust store is shared, so this CA also applies to
 *        MQTTS / wss / the HTTP client. Pass nullptr to clear. Call before the first https fetch.
 */
void pc_edge_cache_set_origin_ca(const uint8_t *ca_pem, size_t len);

/** @brief Pin the origin cert by the SHA-256 of its DER (32 bytes); nullptr clears. Also shared client-wide. */
void pc_edge_cache_set_origin_pin(const uint8_t sha256[32]);
#endif

#if PC_ENABLE_DBM
struct pc_dbm;
/**
 * @brief Bind an L2 persistent tier: an opened dbm handle (on a mounted WAL store, SD-backed on device)
 *        the cache spills evicted L1 entries to and promotes them back from - so the cached set survives
 *        a reboot. Pass nullptr to detach. Call after ::pc_edge_cache_enable.
 *
 * Only entries carrying a validator are spilled (a promoted entry is force-revalidated, since the
 * monotonic clock resets across a reboot). The dbm should be dedicated to the cache.
 */
void pc_edge_cache_bind_sd(pc_dbm *dbm);
#endif

#if PC_ENABLE_EDGE_MESH
/**
 * @brief Add a sibling peer to query on a full local miss before hitting the origin (mesh sibling cache).
 *
 * On a cold miss the cache asks each configured peer (in order, first hit wins) over a plaintext
 * ConnProto::PROTO_MESH link and, if a peer holds a fresh copy, pulls and serves it (age propagated) without
 * hitting the origin. @p host is a sibling's address, @p port the port it serves PROTO_MESH on. @return false
 * if the peer table is full or @p host is empty / too long. Pull-only: no push, no invalidation.
 */
proto_bool pc_edge_cache_add_peer(const char *host, uint16_t port);

/**
 * @brief Serve sibling queries: register the PROTO_MESH handler so this node answers a peer's content-addressed
 *        request from its LOCAL cache (one hop - never re-queries this node's own origin or peers). Open the
 *        port first with `listen(port, ConnProto::PROTO_MESH)`. Call once.
 */
void pc_edge_cache_mesh_serve(void);
#endif

/** @brief Clear the L1 store, the L2 store (if bound), all route maps, and (mesh) the peer list. */
void pc_edge_cache_reset(void);

/** @brief Invalidate a single canonical key. @return true if an entry was purged. */
proto_bool pc_edge_cache_purge(const char *canonical_key);

/** @brief Invalidate every entry whose request path begins with @p prefix. @return the count purged. */
uint32_t pc_edge_cache_purge_prefix(const char *path_prefix);

/** @brief Snapshot the cache counters. */
void pc_edge_cache_stats(EdgeCacheStats *out);

#endif // PC_ENABLE_EDGE_CACHE

#endif // PROTOCORE_EDGE_CACHE_PROXY_H
