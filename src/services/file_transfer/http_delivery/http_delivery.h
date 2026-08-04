// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file http_delivery.h
 * @brief HTTP delivery optimizations: stale-while-revalidate, Range/206 delta fetch, SW precache
 *        (PC_ENABLE_HTTP_DELIVERY).
 *
 * Three pure cores that make HTTP serving cheaper on a constrained device, each mapping to a real web
 * standard:
 *
 *  - **Stale-while-revalidate** (RFC 5861): given a cached response's age and its `max-age` +
 *    `stale-while-revalidate` windows, decide FRESH / serve-stale-and-revalidate / EXPIRED, and build the
 *    matching `Cache-Control` header so a browser keeps the UI responsive while the device refreshes in
 *    the background.
 *  - **Delta / offset log fetch** (RFC 7233 byte ranges): parse a `Range: bytes=...` request against a
 *    resource of known length (all three forms - `X-Y`, `X-`, `-N`), and build the `Content-Range` header
 *    for the `206 Partial Content` reply, so a client streams only the new tail of a growing log.
 *  - **Service-worker precache manifest**: emit the versioned `{"version":..,"precache":[..]}` JSON a
 *    generated service worker consumes to cache-inject the app shell for offline / instant loads.
 *
 * Pure, zero heap, no stdlib (hand-rolled decimal parse/format), host-testable.
 */

#ifndef PROTOCORE_HTTP_DELIVERY_H
#define PROTOCORE_HTTP_DELIVERY_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_HTTP_DELIVERY

/** @brief Freshness verdict for a cached response. */
/** @brief Cache-freshness verdict (the sole return of pc_delivery_swr). */
typedef enum PROTO_ENUM_PACKED
{
    DELIVERY_FRESH = 0,            ///< age <= max-age: serve from cache, no revalidation.
    DELIVERY_STALE_REVALIDATE = 1, ///< within the stale-while-revalidate window: serve stale, refresh in bg.
    DELIVERY_EXPIRED = 2           ///< past both windows: must revalidate before serving.
} DeliveryVerdict;

/**
 * @brief RFC 5861 freshness decision.
 * @param age_s     seconds since the response was generated.
 * @param max_age_s the `max-age` window.
 * @param swr_s     the `stale-while-revalidate` window past max-age.
 * @return DELIVERY_FRESH / DELIVERY_STALE_REVALIDATE / DELIVERY_EXPIRED.
 */
DeliveryVerdict pc_delivery_swr(uint32_t age_s, uint32_t max_age_s, uint32_t swr_s);

/**
 * @brief Build a `Cache-Control` value: `public, max-age=N[, stale-while-revalidate=M]`.
 *        The swr directive is omitted when @p swr_s is 0.
 * @return length written (excl NUL), or 0 on overflow / bad args.
 */
size_t pc_delivery_cache_control(uint32_t max_age_s, uint32_t swr_s, char *out, size_t cap);

// Byte-range serving is NOT here. `network_drivers/application/http_range.h` (`http_parse_byte_range`, PC_ENABLE_RANGE)
// owns the RFC 7233 range math and is already wired into static file serving and the edge cache -
// it emits `Accept-Ranges`, the 206 `Content-Range`, and a 416 with `bytes */<size>`, which this
// header's earlier duplicate could not signal. Two parsers for one concern is how a request ends up
// answered by the wrong one, so the duplicate was removed rather than given a second call site.

/**
 * @brief Emit the service-worker precache manifest: `{"version":"..","precache":["/a","/b",...]}`.
 * @param paths   asset paths to precache (borrowed).
 * @param n       number of paths.
 * @param version cache version tag (busts the SW cache on change).
 * @return length written (excl NUL), or 0 on overflow / bad args. Strings are JSON-escaped.
 */
size_t pc_delivery_sw_manifest(const char *const *paths, size_t n, const char *version, char *out, size_t cap);

#if PROTOCORE_HOT

/**
 * @brief Serve the service worker and its precache manifest.
 *
 * Registers two GET routes on @p srv, which together are the client half of the delivery story:
 *   - `/sw.js`         the worker (a flash-resident asset; registers with `navigator.serviceWorker`)
 *   - `/precache.json` the versioned manifest built by pc_delivery_sw_manifest()
 *
 * The worker precaches @p paths and then serves them stale-while-revalidate, so the browser gets the
 * shell instantly and the device is only asked for a refresh in the background. Bump @p version
 * whenever the shell changes: the worker names its cache after it, so a new version invalidates the
 * old shell exactly once.
 *
 * @param paths   asset paths to precache (borrowed; must outlive the server).
 * @param n       number of paths (<= PC_DELIVERY_PRECACHE_MAX).
 * @param version cache version tag, e.g. a firmware version string.
 * @return true if both routes were registered.
 */
proto_bool pc_delivery_serve_sw(const char *const *paths, size_t n, const char *version);
#endif // PROTOCORE_HOT

#endif // PC_ENABLE_HTTP_DELIVERY

PROTO_END_DECLS

#endif // PROTOCORE_HTTP_DELIVERY_H
