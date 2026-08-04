// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file edge_cache_sd.c
 * @brief CDN edge-cache tier - L2 SD persistence. See edge_cache_sd.h.
 */

#include "services/web/edge_cache/edge_cache_sd.h"

#if PC_ENABLE_EDGE_CACHE

#include <string.h>

#define PC_EDGE_SD_VERSION 1

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
    size_t sl = strnlen(s, cap);
    // Split from the capacity check below so excluding it does not also drop that check - which IS
    // reachable and tested - out of the branch measurement. sl is strnlen-capped to `cap`, the
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
    memcpy(out + *pos, s, sl);
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
    memcpy(out, buf + *pos, sl);
    out[sl] = '\0';
    *pos += sl;
    return PROTO_TRUE;
}

size_t edge_sd_serialize(const EdgeEntry *e, uint8_t *out, size_t cap)
{
    if (!e || !out || cap < 3)
    {
        return 0;
    }
    size_t pos = 0;
    out[pos++] = PC_EDGE_SD_VERSION;
    put_u16(out + pos, (uint16_t)e->status);
    pos += 2;
    if (!put_str(out, cap, &pos, e->key) || !put_str(out, cap, &pos, e->content_type) ||
        !put_str(out, cap, &pos, e->etag) || !put_str(out, cap, &pos, e->last_modified) ||
        !put_str(out, cap, &pos, e->content_encoding) || !put_str(out, cap, &pos, e->vary_names) ||
        !put_str(out, cap, &pos, e->vary_vals))
    {
        return 0;
    }
    if (pos + 2 + e->body_len > cap)
    {
        return 0;
    }
    put_u16(out + pos, e->body_len);
    pos += 2;
    memcpy(out + pos, e->body, e->body_len);
    pos += e->body_len;
    return pos;
}

proto_bool edge_sd_deserialize(const uint8_t *buf, size_t len, EdgeEntry *e)
{
    if (!buf || !e || len < 3 || buf[0] != PC_EDGE_SD_VERSION)
    {
        return PROTO_FALSE;
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
        return PROTO_FALSE;
    }
    if (pos + 2 > len)
    {
        return PROTO_FALSE;
    }
    uint16_t bl = get_u16(buf + pos);
    pos += 2;
    if (bl > PC_EDGE_BODY_MAX || pos + bl > len)
    {
        return PROTO_FALSE;
    }
    memcpy(e->body, buf + pos, bl);
    e->body_len = bl;
    edge_key_digest(e->key, strnlen(e->key, sizeof(e->key)), e->digest); // re-derive the digest from the key
    return PROTO_TRUE;
}

#if PC_ENABLE_DBM

// The L2 key is the entry's SHA-256 digest, which must fit a dbm key exactly.
static_assert(PC_DBM_KEY_MAX >= 32, "edge cache L2 uses a 32-byte SHA-256 digest as the dbm key");

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
    if (len < 3 || buf[0] != PC_EDGE_SD_VERSION)
    {
        return PROTO_FALSE;
    }
    size_t pos = 3; // version(1) + status(2)
    return get_str(buf, len, &pos, canon_out, cap);
}

// Batch of L2 keys collected during an iteration for deletion afterward (dbm forbids delete-in-iterate).
#define PC_EDGE_SD_PURGE_BATCH 8
typedef struct
{
    struct pc_dbm *db;
    const char *prefix; // nullptr = match every edge entry
    size_t plen;
    uint8_t *scratch;
    size_t scratch_cap;
    uint8_t batch[PC_EDGE_SD_PURGE_BATCH][32];
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
    long n = pc_dbm_get(c->db, key, key_len, c->scratch, c->scratch_cap);
    if (n <= 0)
    {
        return PROTO_TRUE;
    }
    char canon[PC_EDGE_KEY_MAX];
    if (!peek_canon(c->scratch, (size_t)n, canon, sizeof(canon)))
    {
        return PROTO_TRUE; // not an edge value - do not touch it
    }
    if (c->prefix)
    {
        const char *path = canon_path(canon);
        if (!path || strncmp(path, c->prefix, c->plen) != 0)
        {
            return PROTO_TRUE; // path does not match the purge prefix
        }
    }
    if (c->count >= PC_EDGE_SD_PURGE_BATCH)
    {
        c->full = PROTO_TRUE;
        return PROTO_FALSE; // stop this pass; the caller deletes the batch then re-iterates
    }
    memcpy(c->batch[c->count++], key, 32);
    return PROTO_TRUE;
}

static uint32_t purge_matching(struct pc_dbm *db, const char *prefix, uint8_t *scratch, size_t scratch_cap)
{
    uint32_t total = 0;
    for (;;)
    {
        CollectCtx c;
        c.db = db;
        c.prefix = prefix;
        c.plen = prefix ? strnlen(prefix, PC_EDGE_KEY_MAX) : 0;
        c.scratch = scratch;
        c.scratch_cap = scratch_cap;
        c.count = 0;
        c.full = PROTO_FALSE;
        pc_dbm_iterate(db, collect_cb, &c);
        for (int i = 0; i < c.count; i++)
        {
            if (pc_dbm_del(db, (const char *)c.batch[i], 32))
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

proto_bool edge_sd_put(struct pc_dbm *db, const EdgeEntry *e, uint8_t *scratch, size_t scratch_cap)
{
    if (!db || !e || !scratch)
    {
        return PROTO_FALSE;
    }
    if (!edge_entry_has_validator(e))
    {
        return PROTO_FALSE; // only spill what a cheap 304 can refresh after a reboot
    }
    size_t n = edge_sd_serialize(e, scratch, scratch_cap);
    if (n == 0 || n > PC_DBM_VAL_MAX)
    {
        return PROTO_FALSE; // too large for the L2 value bound -> stays L1-only
    }
    return pc_dbm_put(db, (const char *)e->digest, 32, scratch, (uint32_t)n);
}

proto_bool edge_sd_get(struct pc_dbm *db, const uint8_t digest[32], EdgeEntry *e, uint8_t *scratch, size_t scratch_cap)
{
    if (!db || !digest || !e || !scratch)
    {
        return PROTO_FALSE;
    }
    long n = pc_dbm_get(db, (const char *)digest, 32, scratch, scratch_cap);
    if (n < 0)
    {
        return PROTO_FALSE;
    }
    return edge_sd_deserialize(scratch, (size_t)n, e);
}

proto_bool edge_sd_del(struct pc_dbm *db, const uint8_t digest[32])
{
    return db && digest && pc_dbm_del(db, (const char *)digest, 32);
}

uint32_t edge_sd_purge_prefix(struct pc_dbm *db, const char *path_prefix, uint8_t *scratch, size_t scratch_cap)
{
    if (!db || !path_prefix || !scratch)
    {
        return 0;
    }
    return purge_matching(db, path_prefix, scratch, scratch_cap);
}

uint32_t edge_sd_purge_all(struct pc_dbm *db)
{
    if (!db)
    {
        return 0;
    }
    // purge_all still verifies each value is an edge serialization before deleting, so a shared dbm is safe;
    // that needs a scratch buffer to read each value into.
    uint8_t scratch[PC_EDGE_SD_VALUE_MAX];
    return purge_matching(db, NULL, scratch, sizeof(scratch));
}

#endif // PC_ENABLE_DBM

#endif // PC_ENABLE_EDGE_CACHE
