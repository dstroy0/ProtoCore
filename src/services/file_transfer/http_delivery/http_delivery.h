// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file http_delivery.h
 * @brief HTTP delivery optimizations: stale-while-revalidate, Range/206 delta fetch, SW precache
 *        (PROTOCORE_ENABLE_HTTP_DELIVERY).
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_HTTP_DELIVERY

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief Cache-freshness verdict (the sole return of swr). */
typedef enum PROTO_ENUM_PACKED
{
    DELIVERY_FRESH = 0,            ///< age <= max-age: serve from cache, no revalidation.
    DELIVERY_STALE_REVALIDATE = 1, ///< within the stale-while-revalidate window: serve stale, refresh in bg.
    DELIVERY_EXPIRED = 2           ///< past both windows: must revalidate before serving.
} DeliveryVerdict;

/** @brief What swr takes: age_s, max_age_s, swr_s. */
typedef struct
{
    uint32_t age_s;     ///< seconds since the response was generated
    uint32_t max_age_s; ///< the `max-age` window
    uint32_t swr_s;     ///< the `stale-while-revalidate` window past max-age
} HttpDeliverySwrArgs;

/** @brief What cache_control takes: max_age_s, ... */
typedef struct
{
    uint32_t max_age_s;
    uint32_t swr_s;
    char *out;
    size_t cap;
} HttpDeliveryCacheControlArgs;

/** @brief What sw_manifest takes: paths, n, ... */
typedef struct
{
    const char *const *paths; ///< asset paths to precache (borrowed)
    size_t n;                 ///< number of paths
    const char *version;      ///< cache version tag (busts the SW cache on change)
    char *out;
    size_t cap;
} HttpDeliverySwManifestArgs;

/** @brief What serve_sw takes: paths, n, version. */
typedef struct
{
    const char *const *paths; ///< asset paths to precache (borrowed; must outlive the server)
    size_t n;                 ///< number of paths (<= PROTOCORE_DELIVERY_PRECACHE_MAX)
    const char *version;      ///< cache version tag, e.g. a firmware version string
} HttpDeliveryServeSwArgs;

/**
 * @brief HTTP delivery optimizations: stale-while-revalidate, Range/206 delta fetch, SW precache
 * (PROTOCORE_ENABLE_HTTP_DELIVERY).
 *
 * A caller sets the members a call takes, invokes it through ::HttpDelivery with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   HttpDelivery.swr_args.age_s = ...;
 *   HttpDelivery.swr_args.max_age_s = ...;
 *   HttpDelivery.swr_args.swr_s = ...;
 *   HttpDelivery.swr(work);
 *   // HttpDelivery.value is what the call reports
 *
 * @var HttpDeliveryNs::swr_args  what swr takes: age_s, max_age_s, swr_s
 * @var HttpDeliveryNs::cache_control_args  what cache_control takes: max_age_s,
 * @var HttpDeliveryNs::sw_manifest_args  what sw_manifest takes: paths, n,
 * @var HttpDeliveryNs::serve_sw_args  what serve_sw takes: paths, n, version
 * @var HttpDeliveryNs::ok  true if both routes were registered
 * @var HttpDeliveryNs::value  DELIVERY_FRESH / DELIVERY_STALE_REVALIDATE / DELIVERY_EXPIRED
 * @var HttpDeliveryNs::n  length written (excl NUL), or 0 on overflow / bad args
 * @var HttpDeliveryNs::swr  RFC 5861 freshness decision
 * @var HttpDeliveryNs::cache_control  build a `Cache-Control` value: `public, max-age=N[, ...
 * @var HttpDeliveryNs::sw_manifest  emit the service-worker precache manifest: ...
 * @var HttpDeliveryNs::serve_sw  serve the service worker and its precache manifest. Registers two
 * ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    HttpDeliverySwrArgs swr_args;
    HttpDeliveryCacheControlArgs cache_control_args;
    HttpDeliverySwManifestArgs sw_manifest_args;
    HttpDeliveryServeSwArgs serve_sw_args;
    proto_bool ok;
    DeliveryVerdict value;
    size_t n;
} HttpDeliveryVars;

/** @brief The operands and the outcome. */
extern HttpDeliveryVars HttpDeliveryV;

/** @brief The entries. */
typedef struct
{
    void (*const swr)(uint8_t *restrict work);
    void (*const cache_control)(uint8_t *restrict work);
    void (*const sw_manifest)(uint8_t *restrict work);
    void (*const serve_sw)(uint8_t *restrict work);
} HttpDeliveryNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in HttpDeliveryV or a region of the borrow at a fixed offset.
void protocore_http_delivery_swr(uint8_t *restrict work);
void protocore_http_delivery_cache_control(uint8_t *restrict work);
void protocore_http_delivery_sw_manifest(uint8_t *restrict work);
void protocore_http_delivery_serve_sw(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `HttpDelivery.swr(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const HttpDeliveryNs HttpDelivery __attribute__((unused)) = {
    .swr = protocore_http_delivery_swr,
    .cache_control = protocore_http_delivery_cache_control,
    .sw_manifest = protocore_http_delivery_sw_manifest,
    .serve_sw = protocore_http_delivery_serve_sw,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP_DELIVERY

#endif // PROTOCORE_HTTP_DELIVERY_H
