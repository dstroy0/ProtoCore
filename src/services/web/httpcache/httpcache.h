// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file httpcache.h
 * @brief HTTP `Cache-Control` directive builder + parser + freshness helper (RFC 9111),
 *        PC_ENABLE_HTTP_CACHE.
 *
 * The origin-side of edge caching: first-class helpers to emit correct, edge-cacheable
 * `Cache-Control` responses from app routes (so a device sitting behind a real CDN - or the
 * library's own future cache tier - is cached correctly), a tolerant parser to read the
 * directives on a request or an upstream response, and the RFC 9111 sec 4.2.1 freshness-lifetime
 * calculation. Pure text - build the value with ::cache_control_build and hand it to
 * set_cache_control(); no heap, no stdlib, host-testable.
 *
 * Directives: RFC 9111 (max-age, s-maxage, no-cache, no-store, no-transform, must-revalidate,
 * proxy-revalidate, must-understand, private, public) plus the widely-used extensions
 * `immutable` (RFC 8246) and `stale-while-revalidate` / `stale-if-error` (RFC 5861), and the
 * request directives a server may want to read (only-if-cached, max-stale, min-fresh).
 *
 * This is the standards-mechanics layer only. The caching proxy/tier itself (RAM/SD storage,
 * cache key, invalidation, mesh replication) is a separate, larger piece still being scoped.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HTTPCACHE_H
#define PROTOCORE_HTTPCACHE_H

#include "protocore_config.h"

#if PC_ENABLE_HTTP_CACHE

/**
 * @brief A `Cache-Control` directive set (a superset of request + response directives).
 *
 * Flags are presence; each delta-seconds value is -1 when the directive is absent. The optional
 * field-name lists on `no-cache` / `private` are not captured (presence only). @ref max_stale
 * uses -1 = absent and -2 = present with no value ("accept any staleness").
 */
typedef struct
{
    // response cacheability
    proto_bool cc_public;        ///< `public`
    proto_bool cc_private;       ///< `private` (field-name list not captured)
    proto_bool no_store;         ///< `no-store`
    proto_bool no_cache;         ///< `no-cache` (field-name list not captured)
    proto_bool no_transform;     ///< `no-transform`
    proto_bool must_revalidate;  ///< `must-revalidate`
    proto_bool proxy_revalidate; ///< `proxy-revalidate`
    proto_bool must_understand;  ///< `must-understand`
    proto_bool cc_immutable;     ///< `immutable` (RFC 8246)
    // request
    proto_bool only_if_cached; ///< `only-if-cached` (request)
    // delta-seconds (-1 = absent)
    int32_t max_age;                ///< `max-age=N`
    int32_t s_maxage;               ///< `s-maxage=N`
    int32_t stale_while_revalidate; ///< `stale-while-revalidate=N` (RFC 5861)
    int32_t stale_if_error;         ///< `stale-if-error=N` (RFC 5861)
    int32_t max_stale;              ///< `max-stale[=N]` (request; -1 absent, -2 no value)
    int32_t min_fresh;              ///< `min-fresh=N` (request)
} pc_cache_control;

/** @brief Reset to an empty set (all flags false, all delta-seconds -1). */
void cache_control_init(pc_cache_control *cc);

/**
 * @brief Build the canonical `Cache-Control` value (no `Cache-Control:` prefix, no CRLF).
 *
 * Emits every set directive as a comma-separated list in a stable order. Pass the result to
 * set_cache_control().
 *
 * @return bytes written (excluding NUL), or 0 on overflow or an empty directive set.
 */
size_t cache_control_build(char *buf, size_t cap, const pc_cache_control *cc);

/**
 * @brief Parse a `Cache-Control` header value into @p cc (initializes it first).
 *
 * Tolerant: directive names are case-insensitive, unknown directives are ignored, delta-seconds
 * values may be bare or quoted. Optional field-name lists are recognized but not captured.
 *
 * @return true if at least one known directive was parsed.
 */
proto_bool cache_control_parse(const char *s, size_t len, pc_cache_control *cc);

// --- first-class origin presets (fill @p cc for a common edge-cacheable response) ---

/** @brief Long-lived immutable static asset: `public, max-age=<secs>, immutable`. */
void cache_immutable_asset(pc_cache_control *cc, uint32_t max_age);

/**
 * @brief Cacheable but served-while-revalidating: `public, max-age=<secs>` plus
 *        `stale-while-revalidate=<swr>` when @p stale_while_revalidate >= 0.
 */
void cache_revalidatable(pc_cache_control *cc, uint32_t max_age, int32_t stale_while_revalidate);

/** @brief Dynamic / sensitive - never store: `no-store`. */
void cache_no_store(pc_cache_control *cc);

/** @brief Distinct shared-cache TTL: `public, max-age=<browser>, s-maxage=<shared>`. */
void cache_shared(pc_cache_control *cc, uint32_t max_age, uint32_t s_maxage);

/**
 * @brief Freshness lifetime in seconds (RFC 9111 sec 4.2.1), first-match precedence:
 *        s-maxage (only when @p shared) -> max-age -> @p expires_minus_date (if >= 0) -> heuristic.
 *
 * @param shared            true for a shared cache (honors s-maxage).
 * @param expires_minus_date `Expires` minus `Date` in seconds, or < 0 when that pair is absent.
 * @return the freshness lifetime in seconds, or -1 when none is explicit (caller applies a heuristic).
 */
long cache_freshness_lifetime(const pc_cache_control *cc, proto_bool shared, long expires_minus_date);

#endif // PC_ENABLE_HTTP_CACHE

#endif // PROTOCORE_HTTPCACHE_H
