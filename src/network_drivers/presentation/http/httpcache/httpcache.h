// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file httpcache.h
 * @brief HTTP `Cache-Control` directive builder + parser + freshness helper (RFC 9111),
 *        PROTOCORE_ENABLE_HTTP_CACHE.
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_HTTP_CACHE

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/**
 * @brief A `Cache-Control` directive set (a superset of request + response directives).
 *
 * Flags are presence; each delta-seconds value is -1 when the directive is absent. The optional
 * field-name lists on `no-cache` / `private` are not captured (presence only). @ref max_stale
 * uses -1 = absent and -2 = present with no value ("accept any staleness").
 */
typedef struct protocore_cache_control
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
} protocore_cache_control;

/** @brief What control_init takes: cc. */
typedef struct
{
    protocore_cache_control *cc;
} HttpcacheControlInitArgs;
/** @brief What control_build takes: buf, cap, cc. */
typedef struct
{
    char *buf;
    size_t cap;
    const protocore_cache_control *cc;
} HttpcacheControlBuildArgs;
/** @brief What control_parse takes: s, len, cc. */
typedef struct
{
    const char *s;
    size_t len;
    protocore_cache_control *cc;
} HttpcacheControlParseArgs;
/** @brief What immutable_asset takes: cc, max_age. */
typedef struct
{
    protocore_cache_control *cc;
    uint32_t max_age;
} HttpcacheImmutableAssetArgs;
/** @brief What revalidatable takes: cc, max_age, ... */
typedef struct
{
    protocore_cache_control *cc;
    uint32_t max_age;
    int32_t stale_while_revalidate;
} HttpcacheRevalidatableArgs;
/** @brief What no_store takes: cc. */
typedef struct
{
    protocore_cache_control *cc;
} HttpcacheNoStoreArgs;
/** @brief What shared takes: cc, max_age, s_maxage. */
typedef struct
{
    protocore_cache_control *cc;
    uint32_t max_age;
    uint32_t s_maxage;
} HttpcacheSharedArgs;
/** @brief What freshness_lifetime takes: cc, shared, ... */
typedef struct
{
    const protocore_cache_control *cc;
    proto_bool shared;       ///< true for a shared cache (honors s-maxage)
    long expires_minus_date; ///< `Expires` minus `Date` in seconds, or < 0 when that pair is absent
} HttpcacheFreshnessLifetimeArgs;
/**
 * @brief HTTP `Cache-Control` directive builder + parser + freshness helper (RFC 9111), PROTOCORE_ENABLE_HTTP_CACHE.
 *
 * A caller sets the members a call takes, invokes it through ::Httpcache with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Httpcache.control_init_args.cc = ...;
 *   Httpcache.control_init(work);
 *
 * @var HttpcacheNs::control_init_args  what control_init takes: cc
 * @var HttpcacheNs::control_build_args  what control_build takes: buf, cap, cc
 * @var HttpcacheNs::control_parse_args  what control_parse takes: s, len, cc
 * @var HttpcacheNs::immutable_asset_args  what immutable_asset takes: cc, max_age
 * @var HttpcacheNs::revalidatable_args  what revalidatable takes: cc, max_age,
 * @var HttpcacheNs::no_store_args  what no_store takes: cc
 * @var HttpcacheNs::shared_args  what shared takes: cc, max_age, s_maxage
 * @var HttpcacheNs::freshness_lifetime_args  what freshness_lifetime takes: cc, shared,
 * @var HttpcacheNs::ok  true if at least one known directive was parsed
 * @var HttpcacheNs::n  bytes written (excluding NUL), or 0 on overflow or an empty ...
 * @var HttpcacheNs::value  the freshness lifetime in seconds, or -1 when none is explicit ...
 * @var HttpcacheNs::control_init  reset to an empty set (all flags false, all delta-seconds -1)
 * @var HttpcacheNs::control_build  build the canonical `Cache-Control` value (no `Cache-Control:` ...
 * @var HttpcacheNs::control_parse  parse a `Cache-Control` header value into cc (initializes it ...
 * @var HttpcacheNs::immutable_asset  long-lived immutable static asset: `public, max-age=<secs>, ...
 * @var HttpcacheNs::revalidatable  cacheable but served-while-revalidating: `public, max-age=<secs>` ...
 * @var HttpcacheNs::no_store  dynamic / sensitive - never store: `no-store`
 * @var HttpcacheNs::shared  distinct shared-cache TTL: `public, max-age=<browser>, ...
 * @var HttpcacheNs::freshness_lifetime  freshness lifetime in seconds (RFC 9111 sec 4.2.1), first-match ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    HttpcacheControlInitArgs control_init_args;
    HttpcacheControlBuildArgs control_build_args;
    HttpcacheControlParseArgs control_parse_args;
    HttpcacheImmutableAssetArgs immutable_asset_args;
    HttpcacheRevalidatableArgs revalidatable_args;
    HttpcacheNoStoreArgs no_store_args;
    HttpcacheSharedArgs shared_args;
    HttpcacheFreshnessLifetimeArgs freshness_lifetime_args;
    proto_bool ok;
    size_t n;
    long value;
} HttpcacheVars;

/** @brief The operands and the outcome. */
extern HttpcacheVars HttpcacheV;

/** @brief The entries. */
typedef struct
{
    void (*const control_init)(uint8_t *restrict work);
    void (*const control_build)(uint8_t *restrict work);
    void (*const control_parse)(uint8_t *restrict work);
    void (*const immutable_asset)(uint8_t *restrict work);
    void (*const revalidatable)(uint8_t *restrict work);
    void (*const no_store)(uint8_t *restrict work);
    void (*const shared)(uint8_t *restrict work);
    void (*const freshness_lifetime)(uint8_t *restrict work);
} HttpcacheNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in HttpcacheV or a region of the borrow at a fixed offset.
void protocore_httpcache_control_init(uint8_t *restrict work);
void protocore_httpcache_control_build(uint8_t *restrict work);
void protocore_httpcache_control_parse(uint8_t *restrict work);
void protocore_httpcache_immutable_asset(uint8_t *restrict work);
void protocore_httpcache_revalidatable(uint8_t *restrict work);
void protocore_httpcache_no_store(uint8_t *restrict work);
void protocore_httpcache_shared(uint8_t *restrict work);
void protocore_httpcache_freshness_lifetime(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Httpcache.control_init(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const HttpcacheNs Httpcache __attribute__((unused)) = {
    .control_init = protocore_httpcache_control_init,
    .control_build = protocore_httpcache_control_build,
    .control_parse = protocore_httpcache_control_parse,
    .immutable_asset = protocore_httpcache_immutable_asset,
    .revalidatable = protocore_httpcache_revalidatable,
    .no_store = protocore_httpcache_no_store,
    .shared = protocore_httpcache_shared,
    .freshness_lifetime = protocore_httpcache_freshness_lifetime,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP_CACHE

#endif // PROTOCORE_HTTPCACHE_H
