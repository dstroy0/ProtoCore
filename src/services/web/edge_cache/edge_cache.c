// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file edge_cache.c
 * @brief CDN edge-cache tier - pure engine. See edge_cache.h.
 */

#include "services/web/edge_cache/edge_cache.h"

#if PC_ENABLE_EDGE_CACHE

#include "crypto/hash/sha256.h"
#include <string.h>

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

proto_bool edge_header_value(const char *hdrs, size_t len, const char *name, char *out, size_t out_cap)
{
    if (!hdrs || !name || !out || out_cap == 0)
    {
        return PROTO_FALSE;
    }
    out[0] = '\0';
    size_t namelen = strnlen(name, out_cap); // header names are short literals, always < the value buffer
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
            return PROTO_TRUE;
        }
        if (overflow)
        {
            return PROTO_FALSE;
        }
        p = (le < end) ? le + 1 : end;
    }
    return PROTO_FALSE;
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

int64_t edge_parse_http_date(const char *s, size_t len)
{
    if (!s)
    {
        return -1;
    }
    char buf[64];
    while (len && (*s == ' ' || *s == '\t')) // trim leading OWS
    {
        s++;
        len--;
    }
    if (len == 0 || len >= sizeof(buf))
    {
        return -1;
    }
    memcpy(buf, s, len);
    buf[len] = '\0';

    int mday = 0;
    int mon = 0;
    int year = 0;
    int hh = 0;
    int mm = 0;
    int ss = 0;
    const char *comma = strchr(buf, ',');
    proto_bool ok = comma ? parse_date_after_comma(comma + 1, &mday, &mon, &year, &hh, &mm, &ss)
                          : parse_date_asctime(buf, &mday, &mon, &year, &hh, &mm, &ss);
    if (!ok)
    {
        return -1;
    }

    if (mon < 1 || mon > 12)
    {
        return -1;
    }
    if (mday < 1 || mday > 31 || hh > 23 || mm > 59 || ss > 60)
    {
        return -1;
    }
    int64_t days = days_from_civil(year, mon, mday);
    return days * 86400 + (int64_t)hh * 3600 + (int64_t)mm * 60 + ss;
}

long edge_freshness_lifetime(const pc_cache_control *cc, proto_bool shared, int64_t date_epoch, int64_t expires_epoch)
{
    long expires_minus_date = -1;
    if (date_epoch >= 0 && expires_epoch >= 0)
    {
        expires_minus_date = (long)(expires_epoch - date_epoch);
    }
    return cache_freshness_lifetime(cc, shared, expires_minus_date);
}

long edge_heuristic_lifetime(int64_t date_epoch, int64_t last_modified_epoch)
{
    if (date_epoch < 0 || last_modified_epoch < 0 || last_modified_epoch >= date_epoch)
    {
        return -1;
    }
    return (long)((date_epoch - last_modified_epoch) / 10); // RFC 9111 sec 4.2.2 (10%)
}

long edge_initial_age(int32_t age_hdr, int64_t date_epoch, int64_t response_time_epoch)
{
    long apparent = 0;
    if (date_epoch >= 0 && response_time_epoch >= 0 && response_time_epoch > date_epoch)
    {
        apparent = (long)(response_time_epoch - date_epoch);
    }
    long corrected = (age_hdr > 0) ? (long)age_hdr : 0;
    return (apparent > corrected) ? apparent : corrected;
}

long edge_current_age(long initial_age, uint32_t insert_ms, uint32_t now_ms)
{
    uint32_t resident_ms = now_ms - insert_ms; // unsigned: wrap-safe
    return initial_age + (long)(resident_ms / 1000u);
}

proto_bool edge_is_fresh_at(long lifetime, long current_age)
{
    return lifetime >= 0 && current_age < lifetime;
}

size_t edge_key_canon(const char *method, const char *host, const char *path, const char *query,
                      proto_bool include_query, char *out, size_t out_cap)
{
    if (!method || !host || !path || !out || out_cap == 0)
    {
        return 0;
    }
    size_t pos = 0;
    if (!k_append(out, &pos, out_cap, method, PROTO_FALSE))
    {
        return 0;
    }
    if (!k_append(out, &pos, out_cap, "\n", PROTO_FALSE) || !k_append(out, &pos, out_cap, host, PROTO_TRUE))
    {
        return 0;
    }
    if (!k_append(out, &pos, out_cap, "\n", PROTO_FALSE) || !k_append(out, &pos, out_cap, path, PROTO_FALSE))
    {
        return 0;
    }
    if (include_query && query && query[0] &&
        (!k_append(out, &pos, out_cap, "\n", PROTO_FALSE) || !k_append(out, &pos, out_cap, query, PROTO_FALSE)))
    {
        return 0;
    }
    out[pos] = '\0';
    return pos;
}

void edge_key_digest(const char *canon, size_t len, uint8_t digest[32])
{
    pc_sha256((const uint8_t *)canon, len, digest);
}

// Parse one Vary field-name token at *pp (advancing past it) and, when non-empty, emit its
// "name\x1e value \x1f" record to out so distinct names cannot alias and a present-but-empty value is
// distinguished from an absent one. Returns false on "Vary: *" (uncacheable) or on overflow.
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
    return k_append(out, pos, out_cap, "\x1f", PROTO_FALSE);
}

proto_bool edge_vary_serialize(const char *vary_header, EdgeHdrLookup lookup, void *ctx, char *out, size_t out_cap)
{
    if (!out || out_cap == 0)
    {
        return PROTO_FALSE;
    }
    out[0] = '\0';
    if (!vary_header)
    {
        return PROTO_TRUE; // no Vary -> empty key
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
            return PROTO_FALSE;
        }
    }
    out[pos] = '\0';
    return PROTO_TRUE;
}

// --- L1 store ------------------------------------------------------------------------------------

static void lru_unlink(EdgeCacheStore *s, uint16_t i)
{
    EdgeEntry *e = &s->entries[i];
    if (e->lru.prev != PC_EDGE_LRU_NONE)
    {
        s->entries[e->lru.prev].lru.next = e->lru.next;
    }
    else
    {
        s->lru_head = e->lru.next;
    }
    if (e->lru.next != PC_EDGE_LRU_NONE)
    {
        s->entries[e->lru.next].lru.prev = e->lru.prev;
    }
    else
    {
        s->lru_tail = e->lru.prev;
    }
    e->lru.next = PC_EDGE_LRU_NONE;
    e->lru.prev = PC_EDGE_LRU_NONE;
}

static void lru_push_front(EdgeCacheStore *s, uint16_t i)
{
    EdgeEntry *e = &s->entries[i];
    e->lru.prev = PC_EDGE_LRU_NONE;
    e->lru.next = s->lru_head;
    if (s->lru_head != PC_EDGE_LRU_NONE)
    {
        s->entries[s->lru_head].lru.prev = i;
    }
    s->lru_head = i;
    if (s->lru_tail == PC_EDGE_LRU_NONE)
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

void edge_store_init(EdgeCacheStore *s)
{
    memset(s, 0, sizeof(*s));
    s->lru_head = PC_EDGE_LRU_NONE;
    s->lru_tail = PC_EDGE_LRU_NONE;
    for (uint16_t i = 0; i < PC_EDGE_CACHE_SLOTS; i++)
    {
        s->entries[i].lru.prev = PC_EDGE_LRU_NONE;
        s->entries[i].lru.next = PC_EDGE_LRU_NONE;
    }
}

EdgeEntry *edge_store_alloc(EdgeCacheStore *s, const char *canon, const char *vary_key)
{
    size_t klen = strnlen(canon, sizeof(s->entries[0].key));
    if (klen >= sizeof(s->entries[0].key))
    {
        return NULL; // key too long -> non-cacheable
    }
    uint16_t slot = PC_EDGE_LRU_NONE;
    for (uint16_t i = 0; i < PC_EDGE_CACHE_SLOTS; i++)
    {
        if (!s->entries[i].used)
        {
            slot = i;
            break;
        }
    }
    if (slot == PC_EDGE_LRU_NONE)
    {
        if (s->lru_tail == PC_EDGE_LRU_NONE)
        {
            return NULL; // PC_EDGE_CACHE_SLOTS == 0
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
    memset(e, 0, sizeof(*e));
    e->lru.prev = PC_EDGE_LRU_NONE;
    e->lru.next = PC_EDGE_LRU_NONE;
    e->used = PROTO_TRUE;
    memcpy(e->key, canon, klen);
    e->key[klen] = '\0';
    edge_key_digest(canon, klen, e->digest);
    size_t vl = vary_key ? strnlen(vary_key, sizeof(e->vary_vals)) : 0;
    if (vl >= sizeof(e->vary_vals))
    {
        vl = sizeof(e->vary_vals) - 1;
    }
    if (vary_key)
    {
        memcpy(e->vary_vals, vary_key, vl);
    }
    e->vary_vals[vl] = '\0';
    e->date_epoch = -1;
    e->expires_epoch = -1;
    lru_push_front(s, slot);
    s->stats.stores++;
    return e;
}

EdgeEntry *edge_store_lookup(EdgeCacheStore *s, const char *canon, const char *vary_key, uint32_t now_ms)
{
    const char *vk = vary_key ? vary_key : "";
    for (uint16_t i = 0; i < PC_EDGE_CACHE_SLOTS; i++)
    {
        EdgeEntry *e = &s->entries[i];
        if (e->used && strcmp(e->key, canon) == 0 && strcmp(e->vary_vals, vk) == 0)
        {
            lru_unlink(s, i);
            lru_push_front(s, i);
            e->last_used_ms = now_ms;
            return e;
        }
    }
    return NULL;
}

EdgeEntry *edge_store_find(EdgeCacheStore *s, const char *canon, EdgeHdrLookup lookup, void *ctx, uint32_t now_ms)
{
    for (uint16_t i = 0; i < PC_EDGE_CACHE_SLOTS; i++)
    {
        EdgeEntry *e = &s->entries[i];
        if (!e->used || strcmp(e->key, canon) != 0)
        {
            continue;
        }
        char cur[PC_EDGE_VARY_MAX];
        // re-serialize the current request against this variant's Vary names (empty names -> "")
        if (!edge_vary_serialize(e->vary_names, lookup, ctx, cur, sizeof(cur)))
        {
            continue;
        }
        if (strcmp(cur, e->vary_vals) == 0)
        {
            lru_unlink(s, i);
            lru_push_front(s, i);
            e->last_used_ms = now_ms;
            return e;
        }
    }
    return NULL;
}

void edge_entry_set_freshness(EdgeEntry *e, const pc_cache_control *cc, proto_bool shared, int64_t date_epoch,
                              int64_t expires_epoch, int64_t last_modified_epoch, int32_t age_hdr,
                              int64_t response_time_epoch, uint32_t now_ms)
{
    long lifetime = edge_freshness_lifetime(cc, shared, date_epoch, expires_epoch);
    if (lifetime < 0)
    {
        lifetime = edge_heuristic_lifetime(date_epoch, last_modified_epoch);
    }
    if (lifetime < 0)
    {
        lifetime = PC_EDGE_DEFAULT_TTL_S;
    }
    e->lifetime_s = lifetime;
    e->initial_age = edge_initial_age(age_hdr, date_epoch, response_time_epoch);
    e->insert_ms = now_ms;
    e->date_epoch = date_epoch;
    e->expires_epoch = expires_epoch;
    e->age_hdr = (age_hdr > 0) ? age_hdr : 0;
}

proto_bool edge_entry_has_validator(const EdgeEntry *e)
{
    return e->etag[0] != '\0' || e->last_modified[0] != '\0';
}

proto_bool edge_entry_fresh(const EdgeEntry *e, uint32_t now_ms)
{
    return edge_is_fresh_at(e->lifetime_s, edge_current_age(e->initial_age, e->insert_ms, now_ms));
}

uint32_t edge_store_sweep(EdgeCacheStore *s, uint32_t now_ms)
{
    uint32_t n = 0;
    for (uint16_t i = 0; i < PC_EDGE_CACHE_SLOTS; i++)
    {
        const EdgeEntry *e = &s->entries[i];
        if (e->used && !edge_entry_has_validator(e) && !edge_entry_fresh(e, now_ms))
        {
            store_free(s, i);
            s->stats.evictions++;
            n++;
        }
    }
    return n;
}

uint32_t edge_store_purge(EdgeCacheStore *s, const char *canon)
{
    uint32_t n = 0;
    for (uint16_t i = 0; i < PC_EDGE_CACHE_SLOTS; i++)
    {
        if (s->entries[i].used && strcmp(s->entries[i].key, canon) == 0)
        {
            store_free(s, i);
            s->stats.purges++;
            n++;
        }
    }
    return n;
}

uint32_t edge_store_purge_prefix(EdgeCacheStore *s, const char *prefix)
{
    size_t plen = strnlen(prefix, sizeof(s->entries[0].key));
    uint32_t n = 0;
    for (uint16_t i = 0; i < PC_EDGE_CACHE_SLOTS; i++)
    {
        if (!s->entries[i].used)
        {
            continue;
        }
        const char *path = key_path(s->entries[i].key);
        if (path && strncmp(path, prefix, plen) == 0)
        {
            store_free(s, i);
            s->stats.purges++;
            n++;
        }
    }
    return n;
}

void edge_store_free_entry(EdgeCacheStore *s, const EdgeEntry *e)
{
    for (uint16_t i = 0; i < PC_EDGE_CACHE_SLOTS; i++)
    {
        if (&s->entries[i] == e)
        {
            store_free(s, i);
            return;
        }
    }
}

proto_bool edge_is_storeable(int status, const char *method, const pc_cache_control *cc, const char *vary_header,
                             size_t body_len)
{
    if (!method || strcmp(method, "GET") != 0)
    {
        return PROTO_FALSE;
    }
    if (status != 200)
    {
        return PROTO_FALSE; // v1: only 200 (other cacheable-by-default statuses are a follow-up)
    }
    if (cc && (cc->no_store || cc->cc_private))
    {
        return PROTO_FALSE;
    }
    if (vary_is_star(vary_header))
    {
        return PROTO_FALSE;
    }
    if (body_len > PC_EDGE_BODY_MAX)
    {
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

// --- conditional revalidation --------------------------------------------------------------------

// Append "<name>: <value>\r\n" to out[*pos..cap). False (and no write) on overflow.
static proto_bool hdr_line(char *out, size_t *pos, size_t cap, const char *name, const char *value)
{
    return k_append(out, pos, cap, name, PROTO_FALSE) && k_append(out, pos, cap, ": ", PROTO_FALSE) &&
           k_append(out, pos, cap, value, PROTO_FALSE) && k_append(out, pos, cap, "\r\n", PROTO_FALSE);
}

size_t edge_build_conditional(const EdgeEntry *e, char *out, size_t cap)
{
    if (!out || cap == 0)
    {
        return 0;
    }
    size_t pos = 0;
    if (e->etag[0] && !hdr_line(out, &pos, cap, "If-None-Match", e->etag))
    {
        return 0;
    }
    if (e->last_modified[0] && !hdr_line(out, &pos, cap, "If-Modified-Since", e->last_modified))
    {
        return 0;
    }
    out[pos] = '\0';
    return pos;
}

void edge_apply_304(EdgeEntry *e, const char *new_hdrs, size_t hdr_len, int64_t response_time_epoch, uint32_t now_ms)
{
    char v[128];
    pc_cache_control cc;
    if (edge_header_value(new_hdrs, hdr_len, "Cache-Control", v, sizeof(v)))
    {
        cache_control_parse(v, strnlen(v, sizeof(v)), &cc);
    }
    else
    {
        cache_control_init(&cc);
    }

    int64_t date = -1;
    if (edge_header_value(new_hdrs, hdr_len, "Date", v, sizeof(v)))
    {
        date = edge_parse_http_date(v, strnlen(v, sizeof(v)));
    }
    int64_t expires = -1;
    if (edge_header_value(new_hdrs, hdr_len, "Expires", v, sizeof(v)))
    {
        expires = edge_parse_http_date(v, strnlen(v, sizeof(v)));
    }

    int32_t age = 0;
    if (edge_header_value(new_hdrs, hdr_len, "Age", v, sizeof(v)))
    {
        long a = 0;
        proto_bool any = PROTO_FALSE;
        for (const char *p = v; *p >= '0' && *p <= '9'; p++)
        {
            a = a * 10 + (*p - '0');
            any = PROTO_TRUE;
        }
        if (any)
        {
            age = (int32_t)a;
        }
    }

    // Adopt any validators the 304 carried (RFC 9111 4.3.4: the newer representation metadata wins).
    if (edge_header_value(new_hdrs, hdr_len, "ETag", v, sizeof(v)))
    {
        size_t vlen = strnlen(v, sizeof(v));
        if (vlen < sizeof(e->etag))
        {
            memcpy(e->etag, v, vlen + 1);
        }
    }
    int64_t last_mod = -1;
    if (edge_header_value(new_hdrs, hdr_len, "Last-Modified", v, sizeof(v)))
    {
        size_t vlen = strnlen(v, sizeof(v));
        last_mod = edge_parse_http_date(v, vlen);
        if (vlen < sizeof(e->last_modified))
        {
            memcpy(e->last_modified, v, vlen + 1);
        }
    }
    else if (e->last_modified[0])
    {
        last_mod = edge_parse_http_date(e->last_modified, strnlen(e->last_modified, sizeof(e->last_modified)));
    }

    edge_entry_set_freshness(e, &cc, PROTO_TRUE, date, expires, last_mod, age, response_time_epoch, now_ms);
}

#endif // PC_ENABLE_EDGE_CACHE
