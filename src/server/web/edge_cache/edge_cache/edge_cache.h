// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file edge_cache.h
 * @brief CDN edge-cache tier - pure engine (PROTOCORE_ENABLE_EDGE_CACHE).
 *
 * The caching reverse-proxy edge that network_drivers/presentation/http/httpcache is the origin-side groundwork for.
 * This header is the pure, host-testable core: the response header-field access and HTTP-date math that httpcache
 * lacks, RFC 9111 freshness (lifetime + age), and the deterministic cache key + SHA-256 digest + `Vary` secondary key.
 * No sockets, no PC, no heap - the socket glue (edge_cache_proxy) and the L2 SD tier (edge_cache_sd) layer on top.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_EDGE_CACHE_H
#define PROTOCORE_EDGE_CACHE_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_EDGE_CACHE

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

#define PROTOCORE_EDGE_LRU_NONE 0xFFFFu

/** @brief Request-header lookup used to build the Vary secondary key; return nullptr when absent. */
typedef const char *(*EdgeHdrLookup)(void *ctx, const char *name);

typedef struct EdgeEntry
{
    proto_bool used;
    char key[PROTOCORE_EDGE_KEY_MAX];               ///< canonical key (collision-safe exact compare)
    uint8_t digest[32];                             ///< sha256.hash(key) - the L2 dbm key
    char vary_names[PROTOCORE_EDGE_VARY_MAX];       ///< the response Vary header value (field-name list), "" if none
    char vary_vals[PROTOCORE_EDGE_VARY_MAX];        ///< serialized request Vary values at store time (secondary key)
    int status;                                     ///< stored response status (200)
    char content_type[PROTOCORE_EDGE_CTYPE_MAX];    ///< Content-Type to replay
    char etag[PROTOCORE_EDGE_ETAG_MAX];             ///< validator (quotes included), "" if none
    char last_modified[PROTOCORE_EDGE_LASTMOD_MAX]; ///< Last-Modified (RFC 1123), "" if none
    char content_encoding[PROTOCORE_EDGE_CENC_MAX]; ///< Content-Encoding to replay (e.g. gzip), "" if none
    int64_t date_epoch;                             ///< origin Date (-1 absent)
    int64_t expires_epoch;                          ///< origin Expires (-1 absent)
    int32_t age_hdr;                                ///< origin Age at store (>=0)
    long lifetime_s;                                ///< resolved freshness lifetime (always >=0)
    long initial_age;                               ///< corrected initial age at store
    uint32_t insert_ms;                             ///< monotonic store time (TTL/age base)
    uint32_t last_used_ms;                          ///< recency
    struct                                          ///< intrusive LRU node (PROTOCORE_EDGE_LRU_NONE = end)
    {
        uint16_t prev;
        uint16_t next;
    } lru;
    uint16_t body_len;
    uint8_t body[PROTOCORE_EDGE_BODY_MAX];
} EdgeEntry;

/** @brief Cache observability counters. */
typedef struct EdgeCacheStats
{
    uint32_t hits;
    uint32_t misses;
    uint32_t revalidations_304;
    uint32_t replaces_200;
    uint32_t stores;
    uint32_t evictions;
    uint32_t purges;
    uint32_t l2_spills;
    uint32_t l2_promotes;
    uint32_t mesh_hits;   ///< sibling pulls served (PROTOCORE_ENABLE_EDGE_MESH)
    uint32_t mesh_misses; ///< peer queries that missed
    uint64_t bytes_stored;
} EdgeCacheStats;

/**
 * @brief Write-back hook fired with the LRU victim just before ::edge_store_alloc recycles its slot.
 *
 * Lets the L2 SD tier (edge_cache_sd) spill an evicted entry to persistent storage without the pure
 * engine ever depending on dbm: the glue installs the callback, the engine only calls it. @p victim is
 * still fully populated (not yet unlinked). Transient passthrough entries (empty key) are not offered.
 */
typedef void (*EdgeEvictFn)(void *ctx, const EdgeEntry *victim);

/** @brief The L1 store: a fixed pool of entries with an intrusive MRU..LRU list. */
typedef struct
{
    EdgeEntry entries[PROTOCORE_EDGE_CACHE_SLOTS];
    uint16_t lru_head; ///< MRU end (PROTOCORE_EDGE_LRU_NONE when empty)
    uint16_t lru_tail; ///< LRU end (PROTOCORE_EDGE_LRU_NONE when empty)
    EdgeCacheStats stats;
    EdgeEvictFn on_evict; ///< nullptr = no L2 write-back; else called with each evicted victim
    void *evict_ctx;      ///< opaque context passed to on_evict
    // The bytes the key digest works out of. Live with the store, so hashing a key costs no borrow.
    uint8_t digest_work[PROTOCORE_SHA256_BORROW];
} EdgeCacheStore;

/** @brief protocore_cache_control, as the caller already knows it. */
struct protocore_cache_control;

/** @brief What header_value takes: hdrs, len, name, out, out_cap. */
typedef struct
{
    const char *hdrs;
    size_t len;
    const char *name;
    char *out;
    size_t out_cap;
} EdgeCacheHeaderValueArgs;

/** @brief What parse_http_date takes: s, len. */
typedef struct
{
    const char *s;
    size_t len;
} EdgeCacheParseHttpDateArgs;

/** @brief What freshness_lifetime takes: cc, shared, date_epoch, ... */
typedef struct
{
    const struct protocore_cache_control *cc;
    proto_bool shared;
    int64_t date_epoch;
    int64_t expires_epoch;
} EdgeCacheFreshnessLifetimeArgs;

/** @brief What heuristic_lifetime takes: date_epoch, ... */
typedef struct
{
    int64_t date_epoch;
    int64_t last_modified_epoch;
} EdgeCacheHeuristicLifetimeArgs;

/** @brief What initial_age takes: age_hdr, date_epoch, ... */
typedef struct
{
    int32_t age_hdr;
    int64_t date_epoch;
    int64_t response_time_epoch;
} EdgeCacheInitialAgeArgs;

/** @brief What current_age takes: initial_age, insert_ms, now_ms. */
typedef struct
{
    long initial_age;
    uint32_t insert_ms;
    uint32_t now_ms;
} EdgeCacheCurrentAgeArgs;

/** @brief What is_fresh_at takes: lifetime, current_age. */
typedef struct
{
    long lifetime;
    long current_age;
} EdgeCacheIsFreshAtArgs;

/** @brief What key_canon takes: method, host, path, query, ... */
typedef struct
{
    const char *method;
    const char *host;
    const char *path;
    const char *query;
    proto_bool include_query;
    char *out;
    size_t out_cap;
} EdgeCacheKeyCanonArgs;

/** @brief What key_digest takes: digest_work, canon, len, digest. */
typedef struct
{
    uint8_t *digest_work;
    const char *canon;
    size_t len;
    uint8_t *digest; ///< 32 bytes.
} EdgeCacheKeyDigestArgs;

/** @brief What vary_serialize takes: vary_header, lookup, ctx, out, ... */
typedef struct
{
    const char *vary_header;
    EdgeHdrLookup lookup;
    void *ctx;
    char *out;
    size_t out_cap;
} EdgeCacheVarySerializeArgs;

/** @brief What store_init takes: s. */
typedef struct
{
    EdgeCacheStore *s;
} EdgeCacheStoreInitArgs;

/** @brief What store_alloc takes: s, canon, vary_key. */
typedef struct
{
    EdgeCacheStore *s;
    const char *canon;
    const char *vary_key;
} EdgeCacheStoreAllocArgs;

/** @brief What store_lookup takes: s, canon, vary_key, now_ms. */
typedef struct
{
    EdgeCacheStore *s;
    const char *canon;
    const char *vary_key;
    uint32_t now_ms;
} EdgeCacheStoreLookupArgs;

/** @brief What store_find takes: s, canon, lookup, ctx, now_ms. */
typedef struct
{
    EdgeCacheStore *s;
    const char *canon;
    EdgeHdrLookup lookup;
    void *ctx;
    uint32_t now_ms;
} EdgeCacheStoreFindArgs;

/** @brief What entry_set_freshness takes: e, cc, shared, date_epoch, ... */
typedef struct
{
    EdgeEntry *e;
    const struct protocore_cache_control *cc;
    proto_bool shared;
    int64_t date_epoch;
    int64_t expires_epoch;
    int64_t last_modified_epoch;
    int32_t age_hdr;
    int64_t response_time_epoch;
    uint32_t now_ms;
} EdgeCacheEntrySetFreshnessArgs;

/** @brief What entry_has_validator takes: e. */
typedef struct
{
    const EdgeEntry *e;
} EdgeCacheEntryHasValidatorArgs;

/** @brief What entry_fresh takes: e, now_ms. */
typedef struct
{
    const EdgeEntry *e;
    uint32_t now_ms;
} EdgeCacheEntryFreshArgs;

/** @brief What store_sweep takes: s, now_ms. */
typedef struct
{
    EdgeCacheStore *s;
    uint32_t now_ms;
} EdgeCacheStoreSweepArgs;

/** @brief What store_purge takes: s, canon. */
typedef struct
{
    EdgeCacheStore *s;
    const char *canon;
} EdgeCacheStorePurgeArgs;

/** @brief What store_purge_prefix takes: s, prefix. */
typedef struct
{
    EdgeCacheStore *s;
    const char *prefix;
} EdgeCacheStorePurgePrefixArgs;

/** @brief What store_free_entry takes: s, e. */
typedef struct
{
    EdgeCacheStore *s;
    const EdgeEntry *e;
} EdgeCacheStoreFreeEntryArgs;

/** @brief What is_storeable takes: status, method, cc, vary_header, ... */
typedef struct
{
    int status;
    const char *method;
    const struct protocore_cache_control *cc;
    const char *vary_header;
    size_t body_len;
} EdgeCacheIsStoreableArgs;

/** @brief What build_conditional takes: e, out, cap. */
typedef struct
{
    const EdgeEntry *e;
    char *out;
    size_t cap;
} EdgeCacheBuildConditionalArgs;

/** @brief What apply_304 takes: e, new_hdrs, hdr_len, ... */
typedef struct
{
    EdgeEntry *e;
    const char *new_hdrs;
    size_t hdr_len;
    int64_t response_time_epoch;
    uint32_t now_ms;
} EdgeCacheApply304Args;

/**
 * @brief CDN edge-cache tier - pure engine (PROTOCORE_ENABLE_EDGE_CACHE). The caching reverse-proxy edge that ...
 *
 * A caller sets the members a call takes, invokes it through ::EdgeCache with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   EdgeCache.header_value_args.hdrs = ...;
 *   EdgeCache.header_value_args.len = ...;
 *   EdgeCache.header_value_args.name = ...;
 *   EdgeCache.header_value_args.out = ...;
 *   EdgeCache.header_value_args.out_cap = ...;
 *   EdgeCache.header_value(work);
 *   // EdgeCache.ok is what the call reports
 *
 * @var EdgeCacheNs::header_value_args  what header_value takes: hdrs, len, name, out, out_cap
 * @var EdgeCacheNs::parse_http_date_args  what parse_http_date takes: s, len
 * @var EdgeCacheNs::freshness_lifetime_args  what freshness_lifetime takes: cc, shared, date_epoch,
 * @var EdgeCacheNs::heuristic_lifetime_args  what heuristic_lifetime takes: date_epoch,
 * @var EdgeCacheNs::initial_age_args  what initial_age takes: age_hdr, date_epoch,
 * @var EdgeCacheNs::current_age_args  what current_age takes: initial_age, insert_ms, now_ms
 * @var EdgeCacheNs::is_fresh_at_args  what is_fresh_at takes: lifetime, current_age
 * @var EdgeCacheNs::key_canon_args  what key_canon takes: method, host, path, query,
 * @var EdgeCacheNs::key_digest_args  what key_digest takes: digest_work, canon, len, digest
 * @var EdgeCacheNs::vary_serialize_args  what vary_serialize takes: vary_header, lookup, ctx, out,
 * @var EdgeCacheNs::store_init_args  what store_init takes: s
 * @var EdgeCacheNs::store_alloc_args  what store_alloc takes: s, canon, vary_key
 * @var EdgeCacheNs::store_lookup_args  what store_lookup takes: s, canon, vary_key, now_ms
 * @var EdgeCacheNs::store_find_args  what store_find takes: s, canon, lookup, ctx, now_ms
 * @var EdgeCacheNs::entry_set_freshness_args  what entry_set_freshness takes: e, cc, shared, date_epoch,
 * @var EdgeCacheNs::entry_has_validator_args  what entry_has_validator takes: e
 * @var EdgeCacheNs::entry_fresh_args  what entry_fresh takes: e, now_ms
 * @var EdgeCacheNs::store_sweep_args  what store_sweep takes: s, now_ms
 * @var EdgeCacheNs::store_purge_args  what store_purge takes: s, canon
 * @var EdgeCacheNs::store_purge_prefix_args  what store_purge_prefix takes: s, prefix
 * @var EdgeCacheNs::store_free_entry_args  what store_free_entry takes: s, e
 * @var EdgeCacheNs::is_storeable_args  what is_storeable takes: status, method, cc, vary_header,
 * @var EdgeCacheNs::build_conditional_args  what build_conditional takes: e, out, cap
 * @var EdgeCacheNs::apply_304_args  what apply_304 takes: e, new_hdrs, hdr_len,
 * @var EdgeCacheNs::ok  a call's true/false outcome
 * @var EdgeCacheNs::epoch  what a call reports
 * @var EdgeCacheNs::secs  what a call reports
 * @var EdgeCacheNs::n  the key length (excluding NUL), or 0 if it would overflow out_cap ...
 * @var EdgeCacheNs::entry  the entry, or nullptr on a miss. Freshness is the caller's decision ...
 * @var EdgeCacheNs::count  the number evicted. Revalidatable stale entries are kept (they can ...
 * @var EdgeCacheNs::header_value  copy the value of header name from a raw HTTP response head (status ...
 * @var EdgeCacheNs::parse_http_date  parse an HTTP-date to epoch seconds (UTC), or return -1 if it does ...
 * @var EdgeCacheNs::freshness_lifetime  freshness lifetime in seconds, or -1 when none is explicit (caller ...
 * @var EdgeCacheNs::heuristic_lifetime  heuristic freshness (RFC 9111 sec 4.2.2): 10% of (Date - ...
 * @var EdgeCacheNs::initial_age  corrected initial age at store time (RFC 9111 sec 4.2.3)
 * @var EdgeCacheNs::current_age  current age = initial_age + resident time, taken from the monotonic ...
 * @var EdgeCacheNs::is_fresh_at  fresh iff a lifetime is known (>= 0) and the current age has not ...
 * @var EdgeCacheNs::key_canon  build the canonical cache key `METHOD "\n" host "\n" path [ "\n" ...
 * @var EdgeCacheNs::key_digest  SHA-256 of the canonical key -> digest[32] (doubles as the L2 dbm ...
 * @var EdgeCacheNs::vary_serialize  serialize a request's values for each field-name in a response ...
 * @var EdgeCacheNs::store_init  reset a store to empty
 * @var EdgeCacheNs::store_alloc  reserve a slot for canon / vary_key, evicting the LRU entry if the ...
 * @var EdgeCacheNs::store_lookup  find the entry for canon whose stored Vary values equal vary_key; ...
 * @var EdgeCacheNs::store_find  vary-aware lookup: find the entry for canon whose stored `Vary` ...
 * @var EdgeCacheNs::entry_set_freshness  resolve and store an entry's freshness (lifetime with heuristic / ...
 * @var EdgeCacheNs::entry_has_validator  true if the entry carries a validator (ETag or Last-Modified) ...
 * @var EdgeCacheNs::entry_fresh  true if the entry is still fresh at now_ms
 * @var EdgeCacheNs::store_sweep  drop entries that are both stale AND unrevalidatable (no validator) ...
 * @var EdgeCacheNs::store_purge  purge every variant stored under the exact canonical key canon. ...
 * @var EdgeCacheNs::store_purge_prefix  purge every entry whose request path begins with prefix. count ...
 * @var EdgeCacheNs::store_free_entry  unlink e and free its slot (no stat bump). Used to release a ...
 * @var EdgeCacheNs::is_storeable  may a response be stored? GET + 200 + not no-store/private + not ...
 * @var EdgeCacheNs::build_conditional  build the conditional-request header lines for revalidating e ...
 * @var EdgeCacheNs::apply_304  apply an origin `304 Not Modified` to a stored entry: recompute its ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    EdgeCacheHeaderValueArgs header_value_args;
    EdgeCacheParseHttpDateArgs parse_http_date_args;
    EdgeCacheFreshnessLifetimeArgs freshness_lifetime_args;
    EdgeCacheHeuristicLifetimeArgs heuristic_lifetime_args;
    EdgeCacheInitialAgeArgs initial_age_args;
    EdgeCacheCurrentAgeArgs current_age_args;
    EdgeCacheIsFreshAtArgs is_fresh_at_args;
    EdgeCacheKeyCanonArgs key_canon_args;
    EdgeCacheKeyDigestArgs key_digest_args;
    EdgeCacheVarySerializeArgs vary_serialize_args;
    EdgeCacheStoreInitArgs store_init_args;
    EdgeCacheStoreAllocArgs store_alloc_args;
    EdgeCacheStoreLookupArgs store_lookup_args;
    EdgeCacheStoreFindArgs store_find_args;
    EdgeCacheEntrySetFreshnessArgs entry_set_freshness_args;
    EdgeCacheEntryHasValidatorArgs entry_has_validator_args;
    EdgeCacheEntryFreshArgs entry_fresh_args;
    EdgeCacheStoreSweepArgs store_sweep_args;
    EdgeCacheStorePurgeArgs store_purge_args;
    EdgeCacheStorePurgePrefixArgs store_purge_prefix_args;
    EdgeCacheStoreFreeEntryArgs store_free_entry_args;
    EdgeCacheIsStoreableArgs is_storeable_args;
    EdgeCacheBuildConditionalArgs build_conditional_args;
    EdgeCacheApply304Args apply_304_args;

    proto_bool ok;
    int64_t epoch;
    long secs;
    size_t n;
    EdgeEntry *entry;
    uint32_t count;

    void (*const header_value)(uint8_t *restrict work);
    void (*const parse_http_date)(uint8_t *restrict work);
    void (*const freshness_lifetime)(uint8_t *restrict work);
    void (*const heuristic_lifetime)(uint8_t *restrict work);
    void (*const initial_age)(uint8_t *restrict work);
    void (*const current_age)(uint8_t *restrict work);
    void (*const is_fresh_at)(uint8_t *restrict work);
    void (*const key_canon)(uint8_t *restrict work);
    void (*const key_digest)(uint8_t *restrict work);
    void (*const vary_serialize)(uint8_t *restrict work);
    void (*const store_init)(uint8_t *restrict work);
    void (*const store_alloc)(uint8_t *restrict work);
    void (*const store_lookup)(uint8_t *restrict work);
    void (*const store_find)(uint8_t *restrict work);
    void (*const entry_set_freshness)(uint8_t *restrict work);
    void (*const entry_has_validator)(uint8_t *restrict work);
    void (*const entry_fresh)(uint8_t *restrict work);
    void (*const store_sweep)(uint8_t *restrict work);
    void (*const store_purge)(uint8_t *restrict work);
    void (*const store_purge_prefix)(uint8_t *restrict work);
    void (*const store_free_entry)(uint8_t *restrict work);
    void (*const is_storeable)(uint8_t *restrict work);
    void (*const build_conditional)(uint8_t *restrict work);
    void (*const apply_304)(uint8_t *restrict work);
} EdgeCacheNs;

/** @brief The one symbol this module exports. */
extern EdgeCacheNs EdgeCache;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_EDGE_CACHE

#endif // PROTOCORE_EDGE_CACHE_H
