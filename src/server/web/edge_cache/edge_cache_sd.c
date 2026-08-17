// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file edge_cache_sd.c
 * @brief CDN edge-cache tier - L2 SD persistence. See edge_cache_sd.h.
 */

#include "network_drivers/presentation/http/httpcache/httpcache.h"
#include "protocore_config.h" // the entry point: the enable gate below, and the widths
#include "shared/http_date/http_date.h"

static uint8_t edge_cache_work[16]; // the borrow an entry takes; EdgeCache never reads it

#if PROTOCORE_ENABLE_EDGE_CACHE

#include "mmgr/protomem.h"
#include "mmgr/protostr.h"
#include "server/web/edge_cache/edge_cache.h"
#include "server/web/edge_cache/edge_cache_sd.h"
#include "services/storage/dbm/dbm.h"

PROTOCORE_BEGIN_DECLS

#define PROTOCORE_EDGE_SD_VERSION 1

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}
static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

// Append a u16 length prefix + the NUL-terminated string @p s. False (no write) on overflow.
static proto_bool put_str(uint8_t *out, size_t cap, size_t *pos, const char *s)
{
    size_t sl = str.len(s, cap);
    // Split from the capacity check below so excluding it does not also drop that check - which IS
    // reachable and tested - out of the branch measurement. sl is str.len-capped to `cap`, the
    // caller's record buffer, which is orders of magnitude below the 16-bit prefix limit in every
    // build this library is sized for. The check guards the u16 cast if cap ever grows.
    if (sl > 0xFFFFu)
    {
        return PROTO_FALSE;
    }
    if (*pos + 2 + sl > cap)
    {
        return PROTO_FALSE;
    }
    put_u16(out + *pos, (uint16_t)sl);
    *pos += 2;
    mem.cpy(out + *pos, s, sl);
    *pos += sl;
    return PROTO_TRUE;
}

// Read a u16-prefixed string into @p out (NUL-terminated). False if short or it would not fit (no truncation).
static proto_bool get_str(const uint8_t *buf, size_t len, size_t *pos, char *out, size_t out_cap)
{
    if (*pos + 2 > len)
    {
        return PROTO_FALSE;
    }
    uint16_t sl = get_u16(buf + *pos);
    *pos += 2;
    if (*pos + sl > len || sl >= out_cap)
    {
        return PROTO_FALSE;
    }
    mem.cpy(out, buf + *pos, sl);
    out[sl] = '\0';
    *pos += sl;
    return PROTO_TRUE;
}

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void edge_cache_sd_deserialize(uint8_t *restrict work);
static void edge_cache_sd_serialize(uint8_t *restrict work);

static void edge_cache_sd_serialize(uint8_t *restrict work)
{
    (void)work;
    const EdgeEntry *e = EdgeCacheSd.serialize_args.e;
    uint8_t *out = EdgeCacheSd.serialize_args.out;
    size_t cap = EdgeCacheSd.serialize_args.cap;

    if (!e || !out || cap < 3)
    {
        EdgeCacheSd.n = 0;
        return;
    }
    size_t pos = 0;
    out[pos++] = PROTOCORE_EDGE_SD_VERSION;
    put_u16(out + pos, (uint16_t)e->status);
    pos += 2;
    if (!put_str(out, cap, &pos, e->key) || !put_str(out, cap, &pos, e->content_type) ||
        !put_str(out, cap, &pos, e->etag) || !put_str(out, cap, &pos, e->last_modified) ||
        !put_str(out, cap, &pos, e->content_encoding) || !put_str(out, cap, &pos, e->vary_names) ||
        !put_str(out, cap, &pos, e->vary_vals))
    {
        EdgeCacheSd.n = 0;
        return;
    }
    if (pos + 2 + e->body_len > cap)
    {
        EdgeCacheSd.n = 0;
        return;
    }
    put_u16(out + pos, e->body_len);
    pos += 2;
    mem.cpy(out + pos, e->body, e->body_len);
    pos += e->body_len;
    EdgeCacheSd.n = pos;
}

static void edge_cache_sd_deserialize(uint8_t *restrict work)
{
    (void)work;
    uint8_t *entry_buf = EdgeCacheSd.deserialize_args.entry_buf;
    const uint8_t *buf = EdgeCacheSd.deserialize_args.buf;
    size_t len = EdgeCacheSd.deserialize_args.len;
    EdgeEntry *e = EdgeCacheSd.deserialize_args.e;

    if (!buf || !e || len < 3 || buf[0] != PROTOCORE_EDGE_SD_VERSION)
    {
        EdgeCacheSd.ok = PROTO_FALSE;
        return;
    }
    size_t pos = 1;
    e->status = get_u16(buf + pos);
    pos += 2;
    if (!get_str(buf, len, &pos, e->key, sizeof(e->key)) ||
        !get_str(buf, len, &pos, e->content_type, sizeof(e->content_type)) ||
        !get_str(buf, len, &pos, e->etag, sizeof(e->etag)) ||
        !get_str(buf, len, &pos, e->last_modified, sizeof(e->last_modified)) ||
        !get_str(buf, len, &pos, e->content_encoding, sizeof(e->content_encoding)) ||
        !get_str(buf, len, &pos, e->vary_names, sizeof(e->vary_names)) ||
        !get_str(buf, len, &pos, e->vary_vals, sizeof(e->vary_vals)))
    {
        EdgeCacheSd.ok = PROTO_FALSE;
        return;
    }
    if (pos + 2 > len)
    {
        EdgeCacheSd.ok = PROTO_FALSE;
        return;
    }
    uint16_t bl = get_u16(buf + pos);
    pos += 2;
    if (bl > PROTOCORE_EDGE_BODY_MAX || pos + bl > len)
    {
        EdgeCacheSd.ok = PROTO_FALSE;
        return;
    }
    mem.cpy(e->body, buf + pos, bl);
    e->body_len = bl;
    // The digest is hashed in the caller's entry buffer, not in the borrow: the borrow is nominal
    // here and a SHA-256 workspace written into it would run off the end of it.
    EdgeCache.key_digest_args.digest_work = entry_buf;
    EdgeCache.key_digest_args.canon = e->key;
    EdgeCache.key_digest_args.len = str.len(e->key, sizeof(e->key));
    EdgeCache.key_digest_args.digest = e->digest;
    EdgeCache.key_digest(edge_cache_work);
    EdgeCacheSd.ok = PROTO_TRUE;
}

#if PROTOCORE_ENABLE_DBM

// The L2 key is the entry's SHA-256 digest, which must fit a dbm key exactly.
static_assert(PROTOCORE_DBM_KEY_MAX >= 32, "edge cache L2 uses a 32-byte SHA-256 digest as the dbm key");

// The path portion of a canonical key "METHOD\nhost\npath[\nquery]" (after the 2nd '\n'), or nullptr.
static const char *canon_path(const char *canon)
{
    int nl = 0;
    for (const char *p = canon; *p; p++)
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

// True if @p buf is a valid edge serialization; if so copies its canonical key into @p canon_out.
static proto_bool peek_canon(const uint8_t *buf, size_t len, char *canon_out, size_t cap)
{
    if (len < 3 || buf[0] != PROTOCORE_EDGE_SD_VERSION)
    {
        return PROTO_FALSE;
    }
    size_t pos = 3; // version(1) + status(2)
    return get_str(buf, len, &pos, canon_out, cap);
}

// Batch of L2 keys collected during an iteration for deletion afterward (dbm forbids delete-in-iterate).
#define PROTOCORE_EDGE_SD_PURGE_BATCH 8
typedef struct
{
    struct protocore_dbm *db;
    const char *prefix; // nullptr = match every edge entry
    size_t plen;
    uint8_t *scratch;
    size_t scratch_cap;
    uint8_t batch[PROTOCORE_EDGE_SD_PURGE_BATCH][32];
    int count;
    proto_bool full; // hit the batch cap: another pass is needed
} CollectCtx;

static proto_bool collect_cb(const char *key, uint16_t key_len, void *vctx)
{
    CollectCtx *c = (CollectCtx *)vctx;
    if (key_len != 32)
    {
        return PROTO_TRUE; // not an edge digest key (shared dbm) - leave it be
    }
    long n = protocore_dbm_get(c->db, key, key_len, c->scratch, c->scratch_cap);
    if (n <= 0)
    {
        return PROTO_TRUE;
    }
    char canon[PROTOCORE_EDGE_KEY_MAX];
    if (!peek_canon(c->scratch, (size_t)n, canon, sizeof(canon)))
    {
        return PROTO_TRUE; // not an edge value - do not touch it
    }
    if (c->prefix)
    {
        const char *path = canon_path(canon);
        if (!path || !str.starts(path, c->prefix, c->plen, PROTO_FALSE))
        {
            return PROTO_TRUE; // path does not match the purge prefix
        }
    }
    if (c->count >= PROTOCORE_EDGE_SD_PURGE_BATCH)
    {
        c->full = PROTO_TRUE;
        return PROTO_FALSE; // stop this pass; the caller deletes the batch then re-iterates
    }
    mem.cpy(c->batch[c->count++], key, 32);
    return PROTO_TRUE;
}

static uint32_t purge_matching(struct protocore_dbm *db, const char *prefix, uint8_t *scratch, size_t scratch_cap)
{
    uint32_t total = 0;
    for (;;)
    {
        CollectCtx c;
        c.db = db;
        c.prefix = prefix;
        c.plen = prefix ? str.len(prefix, PROTOCORE_EDGE_KEY_MAX) : 0;
        c.scratch = scratch;
        c.scratch_cap = scratch_cap;
        c.count = 0;
        c.full = PROTO_FALSE;
        protocore_dbm_iterate(db, collect_cb, &c);
        for (int i = 0; i < c.count; i++)
        {
            if (protocore_dbm_del(db, (const char *)c.batch[i], 32))
            {
                total++;
            }
        }
        if (!c.full)
        {
            break; // visited everything that matched
        }
    }
    return total;
}

static void edge_cache_sd_put(uint8_t *restrict work)
{
    (void)work;
    struct protocore_dbm *db = EdgeCacheSd.put_args.db;
    const EdgeEntry *e = EdgeCacheSd.put_args.e;
    uint8_t *scratch = EdgeCacheSd.put_args.scratch;
    size_t scratch_cap = EdgeCacheSd.put_args.scratch_cap;

    if (!db || !e || !scratch)
    {
        EdgeCacheSd.ok = PROTO_FALSE;
        return;
    }
    EdgeCache.entry_has_validator_args.e = e;
    EdgeCache.entry_has_validator(edge_cache_work);
    if (!EdgeCache.ok)
    {
        EdgeCacheSd.ok = PROTO_FALSE;
        return; // only spill what a cheap 304 can refresh after a reboot
    }
    EdgeCacheSd.serialize_args.e = e;
    EdgeCacheSd.serialize_args.out = scratch;
    EdgeCacheSd.serialize_args.cap = scratch_cap;
    edge_cache_sd_serialize(work);
    size_t n = EdgeCacheSd.n;
    if (n == 0 || n > PROTOCORE_DBM_VAL_MAX)
    {
        EdgeCacheSd.ok = PROTO_FALSE;
        return; // too large for the L2 value bound -> stays L1-only
    }
    EdgeCacheSd.ok = protocore_dbm_put(db, (const char *)e->digest, 32, scratch, (uint32_t)n);
}

static void edge_cache_sd_get(uint8_t *restrict work)
{
    (void)work;
    uint8_t *entry_buf = EdgeCacheSd.get_args.entry_buf;
    struct protocore_dbm *db = EdgeCacheSd.get_args.db;
    const uint8_t *digest = EdgeCacheSd.get_args.digest;
    EdgeEntry *e = EdgeCacheSd.get_args.e;
    uint8_t *scratch = EdgeCacheSd.get_args.scratch;
    size_t scratch_cap = EdgeCacheSd.get_args.scratch_cap;

    if (!db || !digest || !e || !scratch)
    {
        EdgeCacheSd.ok = PROTO_FALSE;
        return;
    }
    long n = protocore_dbm_get(db, (const char *)digest, 32, scratch, scratch_cap);
    if (n < 0)
    {
        EdgeCacheSd.ok = PROTO_FALSE;
        return;
    }
    EdgeCacheSd.deserialize_args.entry_buf = entry_buf;
    EdgeCacheSd.deserialize_args.buf = scratch;
    EdgeCacheSd.deserialize_args.len = (size_t)n;
    EdgeCacheSd.deserialize_args.e = e;
    edge_cache_sd_deserialize(work);
}

static void edge_cache_sd_del(uint8_t *restrict work)
{
    (void)work;
    struct protocore_dbm *db = EdgeCacheSd.del_args.db;
    const uint8_t *digest = EdgeCacheSd.del_args.digest;

    EdgeCacheSd.ok = db && digest && protocore_dbm_del(db, (const char *)digest, 32);
}

static void edge_cache_sd_purge_prefix(uint8_t *restrict work)
{
    (void)work;
    struct protocore_dbm *db = EdgeCacheSd.purge_prefix_args.db;
    const char *path_prefix = EdgeCacheSd.purge_prefix_args.path_prefix;
    uint8_t *scratch = EdgeCacheSd.purge_prefix_args.scratch;
    size_t scratch_cap = EdgeCacheSd.purge_prefix_args.scratch_cap;

    if (!db || !path_prefix || !scratch)
    {
        EdgeCacheSd.count = 0;
        return;
    }
    EdgeCacheSd.count = purge_matching(db, path_prefix, scratch, scratch_cap);
}

static void edge_cache_sd_purge_all(uint8_t *restrict work)
{
    (void)work;
    struct protocore_dbm *db = EdgeCacheSd.purge_all_args.db;

    if (!db)
    {
        EdgeCacheSd.count = 0;
        return;
    }
    // purge_all still verifies each value is an edge serialization before deleting, so a shared dbm is safe;
    // that needs a scratch buffer to read each value into.
    uint8_t scratch[PROTOCORE_EDGE_SD_VALUE_MAX];
    EdgeCacheSd.count = purge_matching(db, NULL, scratch, sizeof(scratch));
}

#endif // PROTOCORE_ENABLE_DBM

// Designated, so a member's position in the struct does not decide what it binds to. The five
// store operations exist only where the flag compiled the database backend in.
EdgeCacheSdNs EdgeCacheSd = {.serialize = edge_cache_sd_serialize,
                             .deserialize = edge_cache_sd_deserialize,
#if PROTOCORE_ENABLE_DBM
                             .put = edge_cache_sd_put,
                             .get = edge_cache_sd_get,
                             .del = edge_cache_sd_del,
                             .purge_prefix = edge_cache_sd_purge_prefix,
                             .purge_all = edge_cache_sd_purge_all
#endif
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_EDGE_CACHE
