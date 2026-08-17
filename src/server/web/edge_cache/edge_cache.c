// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file edge_cache.c
 * @brief CDN edge-cache tier - pure engine. See edge_cache.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_EDGE_CACHE

#include "mmgr/protomem.h"
#include "mmgr/protostr.h"
#include "network_drivers/presentation/http/httpcache/httpcache.h"
#include "server/web/edge_cache/edge_cache.h"
#include "shared/http_date/http_date.h"

#include "crypto/hash/sha256.h"

PROTOCORE_BEGIN_DECLS

static char lc(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
}

// Read a run of decimal digits at *pp into @p out; advance *pp. False if no digit is present.
static proto_bool rd_uint(const char **pp, int *out)
{
    const char *p = *pp;
    if (*p < '0' || *p > '9')
    {
        return PROTO_FALSE;
    }
    int v = 0;
    while (*p >= '0' && *p <= '9')
    {
        v = v * 10 + (*p - '0');
        p++;
    }
    *pp = p;
    *out = v;
    return PROTO_TRUE;
}

// Read a 3-letter month abbreviation at *pp -> 1..12; advance past it. False if unrecognized.
static proto_bool rd_month(const char **pp, int *out)
{
    static const char MONTHS[] = "janfebmaraprmayjunjulaugsepoctnovdec";
    const char *p = *pp;
    char a = lc(p[0]);
    char b = lc(p[1]);
    char c = lc(p[2]);
    for (int m = 0; m < 12; m++)
    {
        if (a == MONTHS[m * 3] && b == MONTHS[m * 3 + 1] && c == MONTHS[m * 3 + 2])
        {
            *pp = p + 3;
            *out = m + 1;
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

// Read "hh:mm:ss" at *pp; advance past it.
static proto_bool rd_time(const char **pp, int *hh, int *mm, int *ss)
{
    const char *p = *pp;
    if (!rd_uint(&p, hh) || *p != ':')
    {
        return PROTO_FALSE;
    }
    p++;
    if (!rd_uint(&p, mm) || *p != ':')
    {
        return PROTO_FALSE;
    }
    p++;
    if (!rd_uint(&p, ss))
    {
        return PROTO_FALSE;
    }
    *pp = p;
    return PROTO_TRUE;
}

// Days since 1970-01-01 for a proleptic-Gregorian y-m-d (Howard Hinnant's civil algorithm).
static int64_t days_from_civil(int y, int m, int d)
{
    if (m <= 2)
    {
        y -= 1;
    }
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400;                                  // [0, 399]
    int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1; // [0, 365]
    int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;          // [0, 146096]
    return era * 146097 + doe - 719468;
}

// Append @p s to out[*pos..cap), optionally lowercased. False (and no write) on overflow.
static proto_bool k_append(char *out, size_t *pos, size_t cap, const char *s, proto_bool lower)
{
    size_t p = *pos;
    for (const char *q = s; *q; q++)
    {
        if (p + 1 >= cap) // leave room for the terminating NUL
        {
            return PROTO_FALSE;
        }
        out[p++] = lower ? lc(*q) : *q;
    }
    *pos = p;
    return PROTO_TRUE;
}

// If the header line [p, lend) is "<name>: <value>" (case-insensitive name), copy the OWS-trimmed value
// into out[0..out_cap) and return true. Non-match returns false with no write; a value that will not fit
// sets *overflow (the caller must then fail the whole lookup - a validator is never truncated).
static proto_bool header_line_value(const char *p, const char *lend, const char *name, size_t namelen, char *out,
                                    size_t out_cap, proto_bool *overflow)
{
    const char *colon = p;
    while (colon < lend && *colon != ':')
    {
        colon++;
    }
    if (colon >= lend || (size_t)(colon - p) != namelen)
    {
        return PROTO_FALSE;
    }
    for (size_t i = 0; i < namelen; i++)
    {
        if (lc(p[i]) != lc(name[i]))
        {
            return PROTO_FALSE;
        }
    }
    const char *v = colon + 1;
    while (v < lend && (*v == ' ' || *v == '\t'))
    {
        v++;
    }
    const char *ve = lend;
    while (ve > v && (ve[-1] == ' ' || ve[-1] == '\t'))
    {
        ve--;
    }
    size_t vl = (size_t)(ve - v);
    if (vl >= out_cap)
    {
        *overflow = PROTO_TRUE;
        return PROTO_FALSE; // never truncate a validator
    }
    for (size_t i = 0; i < vl; i++)
    {
        out[i] = v[i];
    }
    out[vl] = '\0';
    return PROTO_TRUE;
}

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void edge_cache_current_age(uint8_t *restrict work);
static void edge_cache_entry_fresh(uint8_t *restrict work);
static void edge_cache_entry_has_validator(uint8_t *restrict work);
static void edge_cache_entry_set_freshness(uint8_t *restrict work);
static void edge_cache_freshness_lifetime(uint8_t *restrict work);
static void edge_cache_header_value(uint8_t *restrict work);
static void edge_cache_heuristic_lifetime(uint8_t *restrict work);
static void edge_cache_initial_age(uint8_t *restrict work);
static void edge_cache_is_fresh_at(uint8_t *restrict work);
static void edge_cache_key_digest(uint8_t *restrict work);
static void edge_cache_parse_http_date(uint8_t *restrict work);
static void edge_cache_vary_serialize(uint8_t *restrict work);

static void edge_cache_header_value(uint8_t *restrict work)
{
    (void)work;
    const char *hdrs = EdgeCache.header_value_args.hdrs;
    size_t len = EdgeCache.header_value_args.len;
    const char *name = EdgeCache.header_value_args.name;
    char *out = EdgeCache.header_value_args.out;
    size_t out_cap = EdgeCache.header_value_args.out_cap;

    if (!hdrs || !name || !out || out_cap == 0)
    {
        EdgeCache.ok = PROTO_FALSE;
        return;
    }
    out[0] = '\0';
    size_t namelen = str.len(name, out_cap); // header names are short literals, always < the value buffer
    const char *p = hdrs;
    const char *end = hdrs + len;
    // Skip the status line.
    while (p < end && *p != '\n')
    {
        p++;
    }
    if (p < end)
    {
        p++;
    }
    while (p < end)
    {
        if (*p == '\r' || *p == '\n') // blank line: end of the header block
        {
            break;
        }
        const char *le = p;
        while (le < end && *le != '\n')
        {
            le++;
        }
        const char *lend = le; // exclusive; drop a trailing CR
        // `lend > p` has no false arm to reach: a line starting with CR or LF is the blank line that
        // ends the header block and already broke out above, so p is always on a content byte and
        // the line is at least one byte long. The guard stays to keep lend[-1] in bounds.
        if (lend > p && lend[-1] == '\r')
        {
            lend--;
        }
        proto_bool overflow = PROTO_FALSE;
        if (header_line_value(p, lend, name, namelen, out, out_cap, &overflow))
        {
            EdgeCache.ok = PROTO_TRUE;
            return;
        }
        if (overflow)
        {
            EdgeCache.ok = PROTO_FALSE;
            return;
        }
        p = (le < end) ? le + 1 : end;
    }
    EdgeCache.ok = PROTO_FALSE;
}

// IMF-fixdate "Sun, 06 Nov 1994 08:49:37 GMT" or RFC 850 "Sunday, 06-Nov-94 08:49:37 GMT" (from past ',').
static proto_bool parse_date_after_comma(const char *p, int *mday, int *mon, int *year, int *hh, int *mm, int *ss)
{
    while (*p == ' ')
    {
        p++;
    }
    if (!rd_uint(&p, mday))
    {
        return PROTO_FALSE;
    }
    proto_bool rfc850 = (*p == '-');
    if (*p == '-' || *p == ' ')
    {
        p++;
    }
    else
    {
        return PROTO_FALSE;
    }
    if (!rd_month(&p, mon))
    {
        return PROTO_FALSE;
    }
    if (*p == '-')
    {
        p++;
    }
    else
    {
        while (*p == ' ')
        {
            p++;
        }
    }
    if (!rd_uint(&p, year))
    {
        return PROTO_FALSE;
    }
    if (rfc850 && *year < 100) // 2-digit year window (RFC 6265-style)
    {
        *year += (*year < 70) ? 2000 : 1900;
    }
    while (*p == ' ')
    {
        p++;
    }
    return rd_time(&p, hh, mm, ss);
}

// asctime "Sun Nov  6 08:49:37 1994".
static proto_bool parse_date_asctime(const char *p, int *mday, int *mon, int *year, int *hh, int *mm, int *ss)
{
    while (*p && *p != ' ')
    {
        p++;
    }
    while (*p == ' ')
    {
        p++;
    }
    if (!rd_month(&p, mon))
    {
        return PROTO_FALSE;
    }
    while (*p == ' ')
    {
        p++;
    }
    if (!rd_uint(&p, mday))
    {
        return PROTO_FALSE;
    }
    while (*p == ' ')
    {
        p++;
    }
    if (!rd_time(&p, hh, mm, ss))
    {
        return PROTO_FALSE;
    }
    while (*p == ' ')
    {
        p++;
    }
    return rd_uint(&p, year);
}

static void edge_cache_parse_http_date(uint8_t *restrict work)
{
    (void)work;
    const char *s = EdgeCache.parse_http_date_args.s;
    size_t len = EdgeCache.parse_http_date_args.len;

    if (!s)
    {
        EdgeCache.epoch = -1;
        return;
    }
    char buf[64];
    while (len && (*s == ' ' || *s == '\t')) // trim leading OWS
    {
        s++;
        len--;
    }
    if (len == 0 || len >= sizeof(buf))
    {
        EdgeCache.epoch = -1;
        return;
    }
    mem.cpy(buf, s, len);
    buf[len] = '\0';

    int mday = 0;
    int mon = 0;
    int year = 0;
    int hh = 0;
    int mm = 0;
    int ss = 0;
    const char *comma = str.find(buf, len + 1u, ",", sizeof(","), PROTO_FALSE);
    proto_bool ok = comma ? parse_date_after_comma(comma + 1, &mday, &mon, &year, &hh, &mm, &ss)
                          : parse_date_asctime(buf, &mday, &mon, &year, &hh, &mm, &ss);
    if (!ok)
    {
        EdgeCache.epoch = -1;
        return;
    }

    if (mon < 1 || mon > 12)
    {
        EdgeCache.epoch = -1;
        return;
    }
    if (mday < 1 || mday > 31 || hh > 23 || mm > 59 || ss > 60)
    {
        EdgeCache.epoch = -1;
        return;
    }
    int64_t days = days_from_civil(year, mon, mday);
    EdgeCache.epoch = days * 86400 + (int64_t)hh * 3600 + (int64_t)mm * 60 + ss;
}

static void edge_cache_freshness_lifetime(uint8_t *restrict work)
{
    (void)work;
    const protocore_cache_control *cc = EdgeCache.freshness_lifetime_args.cc;
    proto_bool shared = EdgeCache.freshness_lifetime_args.shared;
    int64_t date_epoch = EdgeCache.freshness_lifetime_args.date_epoch;
    int64_t expires_epoch = EdgeCache.freshness_lifetime_args.expires_epoch;

    long lifetime = cache_freshness_lifetime(cc, shared, -1); // s-maxage / max-age, or -1
    if (lifetime >= 0)
    {
        EdgeCache.secs = lifetime;
        return;
    }
    if (date_epoch >= 0 && expires_epoch >= 0)
    {
        // RFC 9111 sec 4.2.1: Expires minus Date. An Expires in the past is a negative lifetime -
        // an explicit expiration that has already passed, not an absent one.
        EdgeCache.secs = (long)(expires_epoch - date_epoch);
        return;
    }
    EdgeCache.secs = -1;
}

static void edge_cache_heuristic_lifetime(uint8_t *restrict work)
{
    (void)work;
    int64_t date_epoch = EdgeCache.heuristic_lifetime_args.date_epoch;
    int64_t last_modified_epoch = EdgeCache.heuristic_lifetime_args.last_modified_epoch;

    if (date_epoch < 0 || last_modified_epoch < 0 || last_modified_epoch >= date_epoch)
    {
        EdgeCache.secs = -1;
        return;
    }
    EdgeCache.secs = (long)((date_epoch - last_modified_epoch) / 10);
    return; // RFC 9111 sec 4.2.2 (10%)
}

static void edge_cache_initial_age(uint8_t *restrict work)
{
    (void)work;
    int32_t age_hdr = EdgeCache.initial_age_args.age_hdr;
    int64_t date_epoch = EdgeCache.initial_age_args.date_epoch;
    int64_t response_time_epoch = EdgeCache.initial_age_args.response_time_epoch;

    long apparent = 0;
    if (date_epoch >= 0 && response_time_epoch >= 0 && response_time_epoch > date_epoch)
    {
        apparent = (long)(response_time_epoch - date_epoch);
    }
    long corrected = (age_hdr > 0) ? (long)age_hdr : 0;
    EdgeCache.secs = (apparent > corrected) ? apparent : corrected;
}

static void edge_cache_current_age(uint8_t *restrict work)
{
    (void)work;
    long initial_age = EdgeCache.current_age_args.initial_age;
    uint32_t insert_ms = EdgeCache.current_age_args.insert_ms;
    uint32_t now_ms = EdgeCache.current_age_args.now_ms;

    uint32_t resident_ms = now_ms - insert_ms; // unsigned: wrap-safe
    EdgeCache.secs = initial_age + (long)(resident_ms / 1000u);
}

static void edge_cache_is_fresh_at(uint8_t *restrict work)
{
    (void)work;
    long lifetime = EdgeCache.is_fresh_at_args.lifetime;
    long current_age = EdgeCache.is_fresh_at_args.current_age;

    EdgeCache.ok = lifetime >= 0 && current_age < lifetime;
}

static void edge_cache_key_canon(uint8_t *restrict work)
{
    (void)work;
    const char *method = EdgeCache.key_canon_args.method;
    const char *host = EdgeCache.key_canon_args.host;
    const char *path = EdgeCache.key_canon_args.path;
    const char *query = EdgeCache.key_canon_args.query;
    proto_bool include_query = EdgeCache.key_canon_args.include_query;
    char *out = EdgeCache.key_canon_args.out;
    size_t out_cap = EdgeCache.key_canon_args.out_cap;

    if (!method || !host || !path || !out || out_cap == 0)
    {
        EdgeCache.n = 0;
        return;
    }
    size_t pos = 0;
    if (!k_append(out, &pos, out_cap, method, PROTO_FALSE))
    {
        EdgeCache.n = 0;
        return;
    }
    if (!k_append(out, &pos, out_cap, "\n", PROTO_FALSE) || !k_append(out, &pos, out_cap, host, PROTO_TRUE))
    {
        EdgeCache.n = 0;
        return;
    }
    if (!k_append(out, &pos, out_cap, "\n", PROTO_FALSE) || !k_append(out, &pos, out_cap, path, PROTO_FALSE))
    {
        EdgeCache.n = 0;
        return;
    }
    if (include_query && query && query[0] &&
        (!k_append(out, &pos, out_cap, "\n", PROTO_FALSE) || !k_append(out, &pos, out_cap, query, PROTO_FALSE)))
    {
        EdgeCache.n = 0;
        return;
    }
    out[pos] = '\0';
    EdgeCache.n = pos;
}

static void edge_cache_key_digest(uint8_t *restrict work)
{
    (void)work;
    uint8_t *digest_work = EdgeCache.key_digest_args.digest_work;
    const char *canon = EdgeCache.key_digest_args.canon;
    size_t len = EdgeCache.key_digest_args.len;
    uint8_t *digest = EdgeCache.key_digest_args.digest;

    Sha256.hash_args.data = (const uint8_t *)canon;
    Sha256.hash_args.len = len;
    Sha256.hash_args.out = digest;
    Sha256.hash(digest_work); // the caller's SHA-256 borrow, PROTOCORE_SHA256_BORROW bytes
}

// Parse one Vary field-name token at *pp (advancing past it) and, when non-empty, emit its
// "name\x1e value" record to out, closed by \x1f when the request carried the field and by \x1d when
// it did not, so distinct names cannot alias and a present-but-empty value does not serialize like an
// absent one. Returns false on "Vary: *" (uncacheable) or on overflow.
static proto_bool vary_emit_one(const char **pp, EdgeHdrLookup lookup, void *ctx, char *out, size_t *pos,
                                size_t out_cap)
{
    const char *p = *pp;
    char name[48];
    size_t nl = 0;
    while (*p && *p != ',' && *p != ' ' && *p != '\t')
    {
        if (*p == '*') // Vary: * -> uncacheable
        {
            return PROTO_FALSE;
        }
        if (nl + 1 < sizeof(name))
        {
            name[nl++] = lc(*p);
        }
        p++;
    }
    name[nl] = '\0';
    *pp = p;
    if (nl == 0)
    {
        return PROTO_TRUE; // nothing to emit; caller advances
    }
    const char *val = lookup ? lookup(ctx, name) : NULL;
    if (!k_append(out, pos, out_cap, name, PROTO_FALSE) || !k_append(out, pos, out_cap, "\x1e", PROTO_FALSE))
    {
        return PROTO_FALSE;
    }
    if (val && !k_append(out, pos, out_cap, val, PROTO_FALSE))
    {
        return PROTO_FALSE;
    }
    return k_append(out, pos, out_cap, val ? "\x1f" : "\x1d", PROTO_FALSE);
}

static void edge_cache_vary_serialize(uint8_t *restrict work)
{
    (void)work;
    const char *vary_header = EdgeCache.vary_serialize_args.vary_header;
    EdgeHdrLookup lookup = EdgeCache.vary_serialize_args.lookup;
    void *ctx = EdgeCache.vary_serialize_args.ctx;
    char *out = EdgeCache.vary_serialize_args.out;
    size_t out_cap = EdgeCache.vary_serialize_args.out_cap;

    if (!out || out_cap == 0)
    {
        EdgeCache.ok = PROTO_FALSE;
        return;
    }
    out[0] = '\0';
    if (!vary_header)
    {
        EdgeCache.ok = PROTO_TRUE;
        return; // no Vary -> empty key
    }
    size_t pos = 0;
    const char *p = vary_header;
    while (*p)
    {
        while (*p == ' ' || *p == '\t' || *p == ',')
        {
            p++;
        }
        if (!*p)
        {
            break;
        }
        if (!vary_emit_one(&p, lookup, ctx, out, &pos, out_cap))
        {
            EdgeCache.ok = PROTO_FALSE;
            return;
        }
    }
    out[pos] = '\0';
    EdgeCache.ok = PROTO_TRUE;
}

// --- L1 store ------------------------------------------------------------------------------------

static void lru_unlink(EdgeCacheStore *s, uint16_t i)
{
    EdgeEntry *e = &s->entries[i];
    if (e->lru.prev != PROTOCORE_EDGE_LRU_NONE)
    {
        s->entries[e->lru.prev].lru.next = e->lru.next;
    }
    else
    {
        s->lru_head = e->lru.next;
    }
    if (e->lru.next != PROTOCORE_EDGE_LRU_NONE)
    {
        s->entries[e->lru.next].lru.prev = e->lru.prev;
    }
    else
    {
        s->lru_tail = e->lru.prev;
    }
    e->lru.next = PROTOCORE_EDGE_LRU_NONE;
    e->lru.prev = PROTOCORE_EDGE_LRU_NONE;
}

static void lru_push_front(EdgeCacheStore *s, uint16_t i)
{
    EdgeEntry *e = &s->entries[i];
    e->lru.prev = PROTOCORE_EDGE_LRU_NONE;
    e->lru.next = s->lru_head;
    if (s->lru_head != PROTOCORE_EDGE_LRU_NONE)
    {
        s->entries[s->lru_head].lru.prev = i;
    }
    s->lru_head = i;
    if (s->lru_tail == PROTOCORE_EDGE_LRU_NONE)
    {
        s->lru_tail = i;
    }
}

static void store_free(EdgeCacheStore *s, uint16_t i)
{
    lru_unlink(s, i);
    s->entries[i].used = PROTO_FALSE;
}

// The path portion of a canonical key "METHOD\nhost\npath[\nquery]" (after the 2nd '\n'), or nullptr.
static const char *key_path(const char *key)
{
    int nl = 0;
    for (const char *p = key; *p; p++)
    {
        if (*p != '\n')
        {
            continue;
        }
        if (++nl == 2)
        {
            return p + 1;
        }
    }
    return NULL;
}

static proto_bool vary_is_star(const char *vary_header)
{
    if (!vary_header)
    {
        return PROTO_FALSE;
    }
    for (const char *p = vary_header; *p; p++)
    {
        if (*p == '*')
        {
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

static void edge_cache_store_init(uint8_t *restrict work)
{
    (void)work;
    EdgeCacheStore *s = EdgeCache.store_init_args.s;

    mem.set(s, 0, sizeof(*s));
    s->lru_head = PROTOCORE_EDGE_LRU_NONE;
    s->lru_tail = PROTOCORE_EDGE_LRU_NONE;
    for (uint16_t i = 0; i < PROTOCORE_EDGE_CACHE_SLOTS; i++)
    {
        s->entries[i].lru.prev = PROTOCORE_EDGE_LRU_NONE;
        s->entries[i].lru.next = PROTOCORE_EDGE_LRU_NONE;
    }
}

static void edge_cache_store_alloc(uint8_t *restrict work)
{
    (void)work;
    EdgeCacheStore *s = EdgeCache.store_alloc_args.s;
    const char *canon = EdgeCache.store_alloc_args.canon;
    const char *vary_key = EdgeCache.store_alloc_args.vary_key;

    size_t klen = str.len(canon, sizeof(s->entries[0].key));
    if (klen >= sizeof(s->entries[0].key))
    {
        EdgeCache.entry = NULL;
        return; // key too long -> non-cacheable
    }
    uint16_t slot = PROTOCORE_EDGE_LRU_NONE;
    for (uint16_t i = 0; i < PROTOCORE_EDGE_CACHE_SLOTS; i++)
    {
        if (!s->entries[i].used)
        {
            slot = i;
            break;
        }
    }
    if (slot == PROTOCORE_EDGE_LRU_NONE)
    {
        if (s->lru_tail == PROTOCORE_EDGE_LRU_NONE)
        {
            EdgeCache.entry = NULL;
            return; // PROTOCORE_EDGE_CACHE_SLOTS == 0
        }
        slot = s->lru_tail;
        // Offer the still-populated victim to the L2 write-back hook (skip transient passthrough slots).
        if (s->on_evict && s->entries[slot].key[0] != '\0')
        {
            s->on_evict(s->evict_ctx, &s->entries[slot]);
        }
        store_free(s, slot);
        s->stats.evictions++;
    }
    EdgeEntry *e = &s->entries[slot];
    mem.set(e, 0, sizeof(*e));
    e->lru.prev = PROTOCORE_EDGE_LRU_NONE;
    e->lru.next = PROTOCORE_EDGE_LRU_NONE;
    e->used = PROTO_TRUE;
    mem.cpy(e->key, canon, klen);
    e->key[klen] = '\0';
    EdgeCache.key_digest_args.digest_work = s->digest_work;
    EdgeCache.key_digest_args.canon = canon;
    EdgeCache.key_digest_args.len = klen;
    EdgeCache.key_digest_args.digest = e->digest;
    edge_cache_key_digest(work);
    size_t vl = vary_key ? str.len(vary_key, sizeof(e->vary_vals)) : 0;
    if (vl >= sizeof(e->vary_vals))
    {
        vl = sizeof(e->vary_vals) - 1;
    }
    if (vary_key)
    {
        mem.cpy(e->vary_vals, vary_key, vl);
    }
    e->vary_vals[vl] = '\0';
    e->date_epoch = -1;
    e->expires_epoch = -1;
    lru_push_front(s, slot);
    s->stats.stores++;
    EdgeCache.entry = e;
}

static void edge_cache_store_lookup(uint8_t *restrict work)
{
    (void)work;
    EdgeCacheStore *s = EdgeCache.store_lookup_args.s;
    const char *canon = EdgeCache.store_lookup_args.canon;
    const char *vary_key = EdgeCache.store_lookup_args.vary_key;
    uint32_t now_ms = EdgeCache.store_lookup_args.now_ms;

    const char *vk = vary_key ? vary_key : "";
    for (uint16_t i = 0; i < PROTOCORE_EDGE_CACHE_SLOTS; i++)
    {
        EdgeEntry *e = &s->entries[i];
        if (e->used && str.eq(canon, e->key, sizeof(e->key), PROTO_FALSE) &&
            str.eq(vk, e->vary_vals, sizeof(e->vary_vals), PROTO_FALSE))
        {
            lru_unlink(s, i);
            lru_push_front(s, i);
            e->last_used_ms = now_ms;
            EdgeCache.entry = e;
            return;
        }
    }
    EdgeCache.entry = NULL;
}

static void edge_cache_store_find(uint8_t *restrict work)
{
    (void)work;
    EdgeCacheStore *s = EdgeCache.store_find_args.s;
    const char *canon = EdgeCache.store_find_args.canon;
    EdgeHdrLookup lookup = EdgeCache.store_find_args.lookup;
    void *ctx = EdgeCache.store_find_args.ctx;
    uint32_t now_ms = EdgeCache.store_find_args.now_ms;

    for (uint16_t i = 0; i < PROTOCORE_EDGE_CACHE_SLOTS; i++)
    {
        EdgeEntry *e = &s->entries[i];
        if (!e->used || !str.eq(canon, e->key, sizeof(e->key), PROTO_FALSE))
        {
            continue;
        }
        char cur[PROTOCORE_EDGE_VARY_MAX];
        // re-serialize the current request against this variant's Vary names (empty names -> "")
        EdgeCache.vary_serialize_args.vary_header = e->vary_names;
        EdgeCache.vary_serialize_args.lookup = lookup;
        EdgeCache.vary_serialize_args.ctx = ctx;
        EdgeCache.vary_serialize_args.out = cur;
        EdgeCache.vary_serialize_args.out_cap = sizeof(cur);
        edge_cache_vary_serialize(work);
        if (!EdgeCache.ok)
        {
            continue;
        }
        if (str.eq(cur, e->vary_vals, sizeof(cur), PROTO_FALSE))
        {
            lru_unlink(s, i);
            lru_push_front(s, i);
            e->last_used_ms = now_ms;
            EdgeCache.entry = e;
            return;
        }
    }
    EdgeCache.entry = NULL;
}

static void edge_cache_entry_set_freshness(uint8_t *restrict work)
{
    (void)work;
    EdgeEntry *e = EdgeCache.entry_set_freshness_args.e;
    const protocore_cache_control *cc = EdgeCache.entry_set_freshness_args.cc;
    proto_bool shared = EdgeCache.entry_set_freshness_args.shared;
    int64_t date_epoch = EdgeCache.entry_set_freshness_args.date_epoch;
    int64_t expires_epoch = EdgeCache.entry_set_freshness_args.expires_epoch;
    int64_t last_modified_epoch = EdgeCache.entry_set_freshness_args.last_modified_epoch;
    int32_t age_hdr = EdgeCache.entry_set_freshness_args.age_hdr;
    int64_t response_time_epoch = EdgeCache.entry_set_freshness_args.response_time_epoch;
    uint32_t now_ms = EdgeCache.entry_set_freshness_args.now_ms;

    EdgeCache.freshness_lifetime_args.cc = cc;
    EdgeCache.freshness_lifetime_args.shared = shared;
    EdgeCache.freshness_lifetime_args.date_epoch = date_epoch;
    EdgeCache.freshness_lifetime_args.expires_epoch = expires_epoch;
    edge_cache_freshness_lifetime(work);
    long lifetime = EdgeCache.secs;
    // RFC 9111 sec 4.2.2: heuristics apply only when no explicit expiration time is present. Date
    // plus Expires is an explicit one however far in the past it lies, so it clamps to 0 (stale on
    // arrival) instead of falling through to a heuristic or the default.
    if (lifetime < 0 && !(date_epoch >= 0 && expires_epoch >= 0))
    {
        EdgeCache.heuristic_lifetime_args.date_epoch = date_epoch;
        EdgeCache.heuristic_lifetime_args.last_modified_epoch = last_modified_epoch;
        edge_cache_heuristic_lifetime(work);
        lifetime = EdgeCache.secs;
        if (lifetime < 0)
        {
            lifetime = PROTOCORE_EDGE_DEFAULT_TTL_S;
        }
    }
    if (lifetime < 0)
    {
        lifetime = 0;
    }
    e->lifetime_s = lifetime;
    EdgeCache.initial_age_args.age_hdr = age_hdr;
    EdgeCache.initial_age_args.date_epoch = date_epoch;
    EdgeCache.initial_age_args.response_time_epoch = response_time_epoch;
    edge_cache_initial_age(work);
    e->initial_age = EdgeCache.secs;
    e->insert_ms = now_ms;
    e->date_epoch = date_epoch;
    e->expires_epoch = expires_epoch;
    e->age_hdr = (age_hdr > 0) ? age_hdr : 0;
}

static void edge_cache_entry_has_validator(uint8_t *restrict work)
{
    (void)work;
    const EdgeEntry *e = EdgeCache.entry_has_validator_args.e;

    EdgeCache.ok = e->etag[0] != '\0' || e->last_modified[0] != '\0';
}

static void edge_cache_entry_fresh(uint8_t *restrict work)
{
    (void)work;
    const EdgeEntry *e = EdgeCache.entry_fresh_args.e;
    uint32_t now_ms = EdgeCache.entry_fresh_args.now_ms;

    // The age is captured before the freshness test runs: both report through the one namespace,
    // so nesting them would have the test read its own outcome.
    EdgeCache.current_age_args.initial_age = e->initial_age;
    EdgeCache.current_age_args.insert_ms = e->insert_ms;
    EdgeCache.current_age_args.now_ms = now_ms;
    edge_cache_current_age(work);
    const long age = EdgeCache.secs;
    EdgeCache.is_fresh_at_args.lifetime = e->lifetime_s;
    EdgeCache.is_fresh_at_args.current_age = age;
    edge_cache_is_fresh_at(work);
}

static void edge_cache_store_sweep(uint8_t *restrict work)
{
    (void)work;
    EdgeCacheStore *s = EdgeCache.store_sweep_args.s;
    uint32_t now_ms = EdgeCache.store_sweep_args.now_ms;

    uint32_t n = 0;
    for (uint16_t i = 0; i < PROTOCORE_EDGE_CACHE_SLOTS; i++)
    {
        const EdgeEntry *e = &s->entries[i];
        if (!e->used)
        {
            continue;
        }
        // Each test is captured before the next runs: both report through the one namespace, and
        // the short-circuit is kept by testing the captured values rather than the calls.
        EdgeCache.entry_has_validator_args.e = e;
        edge_cache_entry_has_validator(work);
        const proto_bool has_validator = EdgeCache.ok;
        EdgeCache.entry_fresh_args.e = e;
        EdgeCache.entry_fresh_args.now_ms = now_ms;
        edge_cache_entry_fresh(work);
        const proto_bool fresh = EdgeCache.ok;
        if (!has_validator && !fresh)
        {
            store_free(s, i);
            s->stats.evictions++;
            n++;
        }
    }
    EdgeCache.count = n;
}

static void edge_cache_store_purge(uint8_t *restrict work)
{
    (void)work;
    EdgeCacheStore *s = EdgeCache.store_purge_args.s;
    const char *canon = EdgeCache.store_purge_args.canon;

    uint32_t n = 0;
    for (uint16_t i = 0; i < PROTOCORE_EDGE_CACHE_SLOTS; i++)
    {
        if (s->entries[i].used && str.eq(canon, s->entries[i].key, sizeof(s->entries[i].key), PROTO_FALSE))
        {
            store_free(s, i);
            s->stats.purges++;
            n++;
        }
    }
    EdgeCache.count = n;
}

static void edge_cache_store_purge_prefix(uint8_t *restrict work)
{
    (void)work;
    EdgeCacheStore *s = EdgeCache.store_purge_prefix_args.s;
    const char *prefix = EdgeCache.store_purge_prefix_args.prefix;

    size_t plen = str.len(prefix, sizeof(s->entries[0].key));
    uint32_t n = 0;
    for (uint16_t i = 0; i < PROTOCORE_EDGE_CACHE_SLOTS; i++)
    {
        if (!s->entries[i].used)
        {
            continue;
        }
        const char *path = key_path(s->entries[i].key);
        if (path && str.starts(path, prefix, plen, PROTO_FALSE))
        {
            store_free(s, i);
            s->stats.purges++;
            n++;
        }
    }
    EdgeCache.count = n;
}

static void edge_cache_store_free_entry(uint8_t *restrict work)
{
    (void)work;
    EdgeCacheStore *s = EdgeCache.store_free_entry_args.s;
    const EdgeEntry *e = EdgeCache.store_free_entry_args.e;

    for (uint16_t i = 0; i < PROTOCORE_EDGE_CACHE_SLOTS; i++)
    {
        if (&s->entries[i] == e)
        {
            store_free(s, i);
            return;
        }
    }
}

static void edge_cache_is_storeable(uint8_t *restrict work)
{
    (void)work;
    int status = EdgeCache.is_storeable_args.status;
    const char *method = EdgeCache.is_storeable_args.method;
    const protocore_cache_control *cc = EdgeCache.is_storeable_args.cc;
    const char *vary_header = EdgeCache.is_storeable_args.vary_header;
    size_t body_len = EdgeCache.is_storeable_args.body_len;

    if (!method || !str.eq(method, "GET", sizeof("GET"), PROTO_FALSE))
    {
        EdgeCache.ok = PROTO_FALSE;
        return;
    }
    if (status != 200)
    {
        EdgeCache.ok = PROTO_FALSE;
        return; // v1: only 200 (other cacheable-by-default statuses are a follow-up)
    }
    if (cc && (cc->no_store || cc->cc_private))
    {
        EdgeCache.ok = PROTO_FALSE;
        return;
    }
    if (vary_is_star(vary_header))
    {
        EdgeCache.ok = PROTO_FALSE;
        return;
    }
    if (body_len > PROTOCORE_EDGE_BODY_MAX)
    {
        EdgeCache.ok = PROTO_FALSE;
        return;
    }
    EdgeCache.ok = PROTO_TRUE;
}

// --- conditional revalidation --------------------------------------------------------------------

// Append "<name>: <value>\r\n" to out[*pos..cap). False (and no write) on overflow.
static proto_bool hdr_line(char *out, size_t *pos, size_t cap, const char *name, const char *value)
{
    return k_append(out, pos, cap, name, PROTO_FALSE) && k_append(out, pos, cap, ": ", PROTO_FALSE) &&
           k_append(out, pos, cap, value, PROTO_FALSE) && k_append(out, pos, cap, "\r\n", PROTO_FALSE);
}

static void edge_cache_build_conditional(uint8_t *restrict work)
{
    (void)work;
    const EdgeEntry *e = EdgeCache.build_conditional_args.e;
    char *out = EdgeCache.build_conditional_args.out;
    size_t cap = EdgeCache.build_conditional_args.cap;

    if (!out || cap == 0)
    {
        EdgeCache.n = 0;
        return;
    }
    size_t pos = 0;
    if (e->etag[0] && !hdr_line(out, &pos, cap, "If-None-Match", e->etag))
    {
        EdgeCache.n = 0;
        return;
    }
    if (e->last_modified[0] && !hdr_line(out, &pos, cap, "If-Modified-Since", e->last_modified))
    {
        EdgeCache.n = 0;
        return;
    }
    out[pos] = '\0';
    EdgeCache.n = pos;
}

static void edge_cache_apply_304(uint8_t *restrict work)
{
    (void)work;
    EdgeEntry *e = EdgeCache.apply_304_args.e;
    const char *new_hdrs = EdgeCache.apply_304_args.new_hdrs;
    size_t hdr_len = EdgeCache.apply_304_args.hdr_len;
    int64_t response_time_epoch = EdgeCache.apply_304_args.response_time_epoch;
    uint32_t now_ms = EdgeCache.apply_304_args.now_ms;

    char v[128];
    protocore_cache_control cc;
    EdgeCache.header_value_args.hdrs = new_hdrs;
    EdgeCache.header_value_args.len = hdr_len;
    EdgeCache.header_value_args.name = "Cache-Control";
    EdgeCache.header_value_args.out = v;
    EdgeCache.header_value_args.out_cap = sizeof(v);
    edge_cache_header_value(work);
    if (EdgeCache.ok)
    {
        cache_control_parse(v, str.len(v, sizeof(v)), &cc);
    }
    else
    {
        cache_control_init(&cc);
    }

    int64_t date = -1;
    EdgeCache.header_value_args.hdrs = new_hdrs;
    EdgeCache.header_value_args.len = hdr_len;
    EdgeCache.header_value_args.name = "Date";
    EdgeCache.header_value_args.out = v;
    EdgeCache.header_value_args.out_cap = sizeof(v);
    edge_cache_header_value(work);
    if (EdgeCache.ok)
    {
        EdgeCache.parse_http_date_args.s = v;
        EdgeCache.parse_http_date_args.len = str.len(v, sizeof(v));
        edge_cache_parse_http_date(work);
        date = EdgeCache.epoch;
    }
    int64_t expires = -1;
    EdgeCache.header_value_args.hdrs = new_hdrs;
    EdgeCache.header_value_args.len = hdr_len;
    EdgeCache.header_value_args.name = "Expires";
    EdgeCache.header_value_args.out = v;
    EdgeCache.header_value_args.out_cap = sizeof(v);
    edge_cache_header_value(work);
    if (EdgeCache.ok)
    {
        EdgeCache.parse_http_date_args.s = v;
        EdgeCache.parse_http_date_args.len = str.len(v, sizeof(v));
        edge_cache_parse_http_date(work);
        expires = EdgeCache.epoch;
    }

    int32_t age = 0;
    EdgeCache.header_value_args.hdrs = new_hdrs;
    EdgeCache.header_value_args.len = hdr_len;
    EdgeCache.header_value_args.name = "Age";
    EdgeCache.header_value_args.out = v;
    EdgeCache.header_value_args.out_cap = sizeof(v);
    edge_cache_header_value(work);
    if (EdgeCache.ok)
    {
        // Clamp every digit so the accumulator stays at or below INT32_MAX and the next multiply-add
        // stays inside int64_t however many digits the origin sent.
        int64_t a = 0;
        proto_bool any = PROTO_FALSE;
        for (const char *p = v; *p >= '0' && *p <= '9'; p++)
        {
            a = a * 10 + (*p - '0');
            if (a > INT32_MAX)
            {
                a = INT32_MAX;
            }
            any = PROTO_TRUE;
        }
        if (any)
        {
            age = (int32_t)a;
        }
    }

    // Adopt any validators the 304 carried (RFC 9111 4.3.4: the newer representation metadata wins).
    EdgeCache.header_value_args.hdrs = new_hdrs;
    EdgeCache.header_value_args.len = hdr_len;
    EdgeCache.header_value_args.name = "ETag";
    EdgeCache.header_value_args.out = v;
    EdgeCache.header_value_args.out_cap = sizeof(v);
    edge_cache_header_value(work);
    if (EdgeCache.ok)
    {
        size_t vlen = str.len(v, sizeof(v));
        if (vlen < sizeof(e->etag))
        {
            mem.cpy(e->etag, v, vlen + 1);
        }
    }
    int64_t last_mod = -1;
    EdgeCache.header_value_args.hdrs = new_hdrs;
    EdgeCache.header_value_args.len = hdr_len;
    EdgeCache.header_value_args.name = "Last-Modified";
    EdgeCache.header_value_args.out = v;
    EdgeCache.header_value_args.out_cap = sizeof(v);
    edge_cache_header_value(work);
    if (EdgeCache.ok)
    {
        size_t vlen = str.len(v, sizeof(v));
        EdgeCache.parse_http_date_args.s = v;
        EdgeCache.parse_http_date_args.len = vlen;
        edge_cache_parse_http_date(work);
        last_mod = EdgeCache.epoch;
        if (vlen < sizeof(e->last_modified))
        {
            mem.cpy(e->last_modified, v, vlen + 1);
        }
    }
    else if (e->last_modified[0])
    {
        EdgeCache.parse_http_date_args.s = e->last_modified;
        EdgeCache.parse_http_date_args.len = str.len(e->last_modified, sizeof(e->last_modified));
        edge_cache_parse_http_date(work);
        last_mod = EdgeCache.epoch;
    }

    EdgeCache.entry_set_freshness_args.e = e;
    EdgeCache.entry_set_freshness_args.cc = &cc;
    EdgeCache.entry_set_freshness_args.shared = PROTO_TRUE;
    EdgeCache.entry_set_freshness_args.date_epoch = date;
    EdgeCache.entry_set_freshness_args.expires_epoch = expires;
    EdgeCache.entry_set_freshness_args.last_modified_epoch = last_mod;
    EdgeCache.entry_set_freshness_args.age_hdr = age;
    EdgeCache.entry_set_freshness_args.response_time_epoch = response_time_epoch;
    EdgeCache.entry_set_freshness_args.now_ms = now_ms;
    edge_cache_entry_set_freshness(work);
}

EdgeCacheNs EdgeCache = {.header_value = edge_cache_header_value,
                         .parse_http_date = edge_cache_parse_http_date,
                         .freshness_lifetime = edge_cache_freshness_lifetime,
                         .heuristic_lifetime = edge_cache_heuristic_lifetime,
                         .initial_age = edge_cache_initial_age,
                         .current_age = edge_cache_current_age,
                         .is_fresh_at = edge_cache_is_fresh_at,
                         .key_canon = edge_cache_key_canon,
                         .key_digest = edge_cache_key_digest,
                         .vary_serialize = edge_cache_vary_serialize,
                         .store_init = edge_cache_store_init,
                         .store_alloc = edge_cache_store_alloc,
                         .store_lookup = edge_cache_store_lookup,
                         .store_find = edge_cache_store_find,
                         .entry_set_freshness = edge_cache_entry_set_freshness,
                         .entry_has_validator = edge_cache_entry_has_validator,
                         .entry_fresh = edge_cache_entry_fresh,
                         .store_sweep = edge_cache_store_sweep,
                         .store_purge = edge_cache_store_purge,
                         .store_purge_prefix = edge_cache_store_purge_prefix,
                         .store_free_entry = edge_cache_store_free_entry,
                         .is_storeable = edge_cache_is_storeable,
                         .build_conditional = edge_cache_build_conditional,
                         .apply_304 = edge_cache_apply_304};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_EDGE_CACHE
