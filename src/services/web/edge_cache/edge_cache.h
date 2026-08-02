// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file edge_cache.h
 * @brief CDN edge-cache tier - pure engine (PC_ENABLE_EDGE_CACHE).
 *
 * The caching reverse-proxy edge that services/web/httpcache is the origin-side groundwork for. This
 * header is the pure, host-testable core: the response header-field access and HTTP-date math that
 * httpcache lacks, RFC 9111 freshness (lifetime + age), and the deterministic cache key + SHA-256
 * digest + `Vary` secondary key. No sockets, no PC, no heap - the socket glue
 * (edge_cache_proxy) and the L2 SD tier (edge_cache_sd) layer on top.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_EDGE_CACHE_H
#define PROTOCORE_EDGE_CACHE_H

#include "protocore_config.h"

#if PC_ENABLE_EDGE_CACHE

#include "services/web/httpcache/httpcache.h" // pc_cache_control, cache_freshness_lifetime
#include "shared_primitives/http_date.h"      // PC_HTTP_DATE_MAX (the stored-date floor)

// --- raw response header-block field access ------------------------------------------------------

/**
 * @brief Copy the value of header @p name from a raw HTTP response head (status line + CRLF headers)
 *        into @p out, OWS-trimmed and NUL-terminated.
 *
 * Case-insensitive name match; the first occurrence wins. Fails (returns false, @p out emptied) if the
 * header is absent or its value would not fit @p out_cap (never truncates a validator).
 */
proto_bool edge_header_value(const char *hdrs, size_t len, const char *name, char *out, size_t out_cap);

// --- HTTP-date <-> epoch (RFC 9110 sec 5.6.7: IMF-fixdate, obsolete RFC 850, asctime) -------------

/** @brief Parse an HTTP-date to epoch seconds (UTC), or return -1 if it does not parse. */
int64_t edge_parse_http_date(const char *s, size_t len);

// --- freshness (RFC 9111 sec 4.2) ----------------------------------------------------------------

/**
 * @brief Freshness lifetime in seconds, or -1 when none is explicit (caller applies a heuristic).
 *
 * Wraps ::cache_freshness_lifetime and computes its `Expires - Date` from the two response epochs
 * locally (a difference of two origin-supplied times - valid with no local wall clock). @p date_epoch
 * and @p expires_epoch are -1 when the header was absent.
 */
long edge_freshness_lifetime(const pc_cache_control *cc, proto_bool shared, int64_t date_epoch, int64_t expires_epoch);

/**
 * @brief Heuristic freshness (RFC 9111 sec 4.2.2): 10% of (Date - Last-Modified), or -1 if either
 *        epoch is absent / not ordered.
 */
long edge_heuristic_lifetime(int64_t date_epoch, int64_t last_modified_epoch);

/**
 * @brief Corrected initial age at store time (RFC 9111 sec 4.2.3).
 *
 * @p response_time_epoch < 0 (no wall clock) falls back to @p age_hdr alone; @p age_hdr < 0 is 0.
 */
long edge_initial_age(int32_t age_hdr, int64_t date_epoch, int64_t response_time_epoch);

/** @brief Current age = @p initial_age + resident time, taken from the monotonic clock (wrap-safe). */
long edge_current_age(long initial_age, uint32_t insert_ms, uint32_t now_ms);

/** @brief Fresh iff a lifetime is known (>= 0) and the current age has not reached it. */
proto_bool edge_is_fresh_at(long lifetime, long current_age);

// --- cache key + digest + Vary -------------------------------------------------------------------

/**
 * @brief Build the canonical cache key `METHOD "\n" host "\n" path [ "\n" query ]` (host lowercased)
 *        into @p out.
 *
 * @return the key length (excluding NUL), or 0 if it would overflow @p out_cap (caller treats 0 as
 *         non-cacheable and fails open).
 */
size_t edge_key_canon(const char *method, const char *host, const char *path, const char *query,
                      proto_bool include_query, char *out, size_t out_cap);

/** @brief SHA-256 of the canonical key -> @p digest[32] (doubles as the L2 dbm key). */
void edge_key_digest(const char *canon, size_t len, uint8_t digest[32]);

/** @brief Request-header lookup used to build the Vary secondary key; return nullptr when absent. */
using EdgeHdrLookup = const char *(*)(void *ctx, const char *name);

/**
 * @brief Serialize a request's values for each field-name in a response `Vary` header into @p out
 *        (a stable, order-preserving key for the Vary secondary match).
 *
 * Two requests select the same stored variant iff their serialized strings are equal. An empty / absent
 * Vary writes "" and returns true. Returns false if Vary is "*" (uncacheable) or it would overflow.
 */
proto_bool edge_vary_serialize(const char *vary_header, EdgeHdrLookup lookup, void *ctx, char *out, size_t out_cap);

// --- L1 RAM store: entries, LRU, TTL, purge ------------------------------------------------------

#define PC_EDGE_LRU_NONE 0xFFFFu

/** @brief One cached object (fixed-size, zero-heap). */
/* PC_EDGE_LASTMOD_MAX is a knob, but its floor is the protocol's: a stored date that
 * cannot hold an IMF-fixdate is a date that silently vanishes. */
#if PC_EDGE_LASTMOD_MAX < PC_HTTP_DATE_MAX
#error "PC_EDGE_LASTMOD_MAX must be >= PC_HTTP_DATE_MAX (RFC 7231 IMF-fixdate + NUL)"
#endif

typedef struct
{
    proto_bool used;
    char key[PC_EDGE_KEY_MAX];               ///< canonical key (collision-safe exact compare)
    uint8_t digest[32];                      ///< pc_sha256(key) - the L2 dbm key
    char vary_names[PC_EDGE_VARY_MAX];       ///< the response Vary header value (field-name list), "" if none
    char vary_vals[PC_EDGE_VARY_MAX];        ///< serialized request Vary values at store time (secondary key)
    int status;                              ///< stored response status (200)
    char content_type[PC_EDGE_CTYPE_MAX];    ///< Content-Type to replay
    char etag[PC_EDGE_ETAG_MAX];             ///< validator (quotes included), "" if none
    char last_modified[PC_EDGE_LASTMOD_MAX]; ///< Last-Modified (RFC 1123), "" if none
    char content_encoding[PC_EDGE_CENC_MAX]; ///< Content-Encoding to replay (e.g. gzip), "" if none
    int64_t date_epoch;                      ///< origin Date (-1 absent)
    int64_t expires_epoch;                   ///< origin Expires (-1 absent)
    int32_t age_hdr;                         ///< origin Age at store (>=0)
    long lifetime_s;                         ///< resolved freshness lifetime (always >=0)
    long initial_age;                        ///< corrected initial age at store
    uint32_t insert_ms;                      ///< monotonic store time (TTL/age base)
    uint32_t last_used_ms;                   ///< recency
    struct                                   ///< intrusive LRU node (PC_EDGE_LRU_NONE = end)
    {
        uint16_t prev;
        uint16_t next;
    } lru;
    uint16_t body_len;
    uint8_t body[PC_EDGE_BODY_MAX];
} EdgeEntry;

/** @brief Cache observability counters. */
typedef struct
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
    uint32_t mesh_hits;   ///< sibling pulls served (PC_ENABLE_EDGE_MESH)
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
using EdgeEvictFn = void (*)(void *ctx, const EdgeEntry *victim);

/** @brief The L1 store: a fixed pool of entries with an intrusive MRU..LRU list. */
typedef struct
{
    EdgeEntry entries[PC_EDGE_CACHE_SLOTS];
    uint16_t lru_head; ///< MRU end (PC_EDGE_LRU_NONE when empty)
    uint16_t lru_tail; ///< LRU end (PC_EDGE_LRU_NONE when empty)
    EdgeCacheStats stats;
    EdgeEvictFn on_evict; ///< nullptr = no L2 write-back; else called with each evicted victim
    void *evict_ctx;      ///< opaque context passed to on_evict
} EdgeCacheStore;

/** @brief Reset a store to empty. */
void edge_store_init(EdgeCacheStore *s);

/**
 * @brief Reserve a slot for @p canon / @p vary_key, evicting the LRU entry if the pool is full.
 *
 * The returned entry has its key/digest/vary set, is marked used, and is linked at the MRU end; the
 * caller fills status/body/validators/freshness. Returns nullptr only if @p canon would not fit
 * `PC_EDGE_KEY_MAX` (non-cacheable). Bumps `stores` (and `evictions` if it displaced an entry).
 */
EdgeEntry *edge_store_alloc(EdgeCacheStore *s, const char *canon, const char *vary_key);

/**
 * @brief Find the entry for @p canon whose stored Vary values equal @p vary_key; touch it to MRU.
 * @return the entry, or nullptr on a miss. Freshness is the caller's decision (see ::edge_entry_fresh).
 */
EdgeEntry *edge_store_lookup(EdgeCacheStore *s, const char *canon, const char *vary_key, uint32_t now_ms);

/**
 * @brief Vary-aware lookup: find the entry for @p canon whose stored `Vary` field-name list, serialized
 *        against the current request (via @p lookup), matches the values seen at store time.
 *
 * This resolves the secondary key the caller cannot precompute (the Vary names come from the stored
 * response). @return the matching variant (touched to MRU), or nullptr.
 */
EdgeEntry *edge_store_find(EdgeCacheStore *s, const char *canon, EdgeHdrLookup lookup, void *ctx, uint32_t now_ms);

/** @brief Resolve and store an entry's freshness (lifetime with heuristic / default fallback + age). */
void edge_entry_set_freshness(EdgeEntry *e, const pc_cache_control *cc, proto_bool shared, int64_t date_epoch,
                              int64_t expires_epoch, int64_t last_modified_epoch, int32_t age_hdr,
                              int64_t response_time_epoch, uint32_t now_ms);

/** @brief True if the entry carries a validator (ETag or Last-Modified) usable for revalidation. */
proto_bool edge_entry_has_validator(const EdgeEntry *e);

/** @brief True if the entry is still fresh at @p now_ms. */
proto_bool edge_entry_fresh(const EdgeEntry *e, uint32_t now_ms);

/**
 * @brief Drop entries that are both stale AND unrevalidatable (no validator) - pure dead weight.
 * @return the number evicted. Revalidatable stale entries are kept (they can still 304-refresh).
 */
uint32_t edge_store_sweep(EdgeCacheStore *s, uint32_t now_ms);

/** @brief Purge every variant stored under the exact canonical key @p canon. @return count purged. */
uint32_t edge_store_purge(EdgeCacheStore *s, const char *canon);

/** @brief Purge every entry whose request path begins with @p prefix. @return count purged. */
uint32_t edge_store_purge_prefix(EdgeCacheStore *s, const char *prefix);

/** @brief Unlink @p e and free its slot (no stat bump). Used to release a transient passthrough entry. */
void edge_store_free_entry(EdgeCacheStore *s, const EdgeEntry *e);

// --- storeability (RFC 9111 sec 3) ---------------------------------------------------------------

/**
 * @brief May a response be stored? GET + 200 + not no-store/private + not `Vary: *` + body fits.
 *
 * @p vary_header may be nullptr. Authorization handling is the caller's (private requests bypass first).
 */
proto_bool edge_is_storeable(int status, const char *method, const pc_cache_control *cc, const char *vary_header,
                             size_t body_len);

// --- conditional revalidation (RFC 9111 sec 4.3) -------------------------------------------------

/**
 * @brief Build the conditional-request header lines for revalidating @p e (`If-None-Match` from its
 *        ETag and/or `If-Modified-Since` from its Last-Modified), into @p out.
 * @return bytes written (0 if the entry carries no validator, or on overflow).
 */
size_t edge_build_conditional(const EdgeEntry *e, char *out, size_t cap);

/**
 * @brief Apply an origin `304 Not Modified` to a stored entry: recompute its freshness from the 304
 *        response headers and adopt any validators it carried, keeping the stored body (RFC 9111 4.3.4).
 *
 * @p new_hdrs / @p hdr_len are the 304 response head; @p response_time_epoch is when it arrived (-1 with
 * no wall clock); @p now_ms is the monotonic clock.
 */
void edge_apply_304(EdgeEntry *e, const char *new_hdrs, size_t hdr_len, int64_t response_time_epoch, uint32_t now_ms);

#endif // PC_ENABLE_EDGE_CACHE

#endif // PROTOCORE_EDGE_CACHE_H
