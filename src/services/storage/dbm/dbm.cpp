// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file dbm.cpp
 * @brief Log-structured hash key-value store on the WAL (see dbm.h).
 */

#include "services/storage/dbm/dbm.h"

#if PC_ENABLE_DBM

#include <string.h>

namespace
{
// dbm record payload header: op u8 | key_len u16 | val_len u32.
const size_t DBM_HDR = 7;

void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// FNV-1a 64-bit over the key.
uint64_t key_hash(const char *key, uint16_t len)
{
    uint64_t h = 0xcbf29ce484222325ULL;
    for (uint16_t i = 0; i < len; i++)
    {
        h ^= (uint8_t)key[i];
        h *= 0x100000001b3ULL;
    }
    return h;
}

// Find a live slot for (hash,key). Linear probe, stopping at the first empty. -1 if absent.
int find_live(const pc_dbm *db, uint64_t hash, const char *key, uint16_t key_len)
{
    const size_t n = PC_DBM_SLOTS;
    size_t start = (size_t)(hash % n);
    for (size_t i = 0; i < n; i++)
    {
        size_t j = (start + i) % n;
        const pc_dbm_slot *s = &db->slots[j];
        if (s->state == 0)
        {
            return -1; // empty -> the probe chain ends, key not present
        }
        if (s->state == 1 && s->hash == hash && s->key_len == key_len && memcmp(s->key, key, key_len) == 0)
        {
            return (int)j;
        }
    }
    return -1;
}

// Find the slot to write (hash,key): an existing live match, or the first reusable slot (tombstone/empty).
// Sets *is_new when the returned slot is not already this key. -1 if the table has no room for a new key.
int reserve(const pc_dbm *db, uint64_t hash, const char *key, uint16_t key_len, bool *is_new)
{
    const size_t n = PC_DBM_SLOTS;
    size_t start = (size_t)(hash % n);
    int first_free = -1;
    for (size_t i = 0; i < n; i++)
    {
        size_t j = (start + i) % n;
        const pc_dbm_slot *s = &db->slots[j];
        if (s->state == 1)
        {
            if (s->hash == hash && s->key_len == key_len && memcmp(s->key, key, key_len) == 0)
            {
                *is_new = false;
                return (int)j;
            }
            continue;
        }
        if (s->state == 2)
        {
            if (first_free < 0)
            {
                first_free = (int)j; // reusable tombstone; keep probing for a live match
            }
            continue;
        }
        // empty: the key is not present -> insert at the earliest reusable slot (tombstone or here)
        *is_new = true;
        return first_free >= 0 ? first_free : (int)j;
    }
    // No empty slot seen (table saturated with live/tombstones); only a tombstone can hold a new key.
    *is_new = true;
    return first_free;
}

struct ReplayCtx
{
    pc_dbm *db;
    bool overflow;
};

void replay_cb(uint64_t seq, uint64_t data_off, const uint8_t *payload, uint32_t len, void *ctx)
{
    (void)seq;
    ReplayCtx *rc = (ReplayCtx *)ctx;
    pc_dbm *db = rc->db;
    if (len < DBM_HDR)
    {
        return;
    }
    uint8_t op = payload[0];
    uint16_t klen = get_u16(payload + 1);
    uint32_t vlen = get_u32(payload + 3);
    if (klen == 0 || klen > PC_DBM_KEY_MAX)
    {
        return;
    }
    if (DBM_HDR + (size_t)klen + vlen > len)
    {
        return; // truncated / malformed payload
    }
    const char *key = (const char *)(payload + DBM_HDR);
    uint64_t h = key_hash(key, klen);
    if (op == 0) // put
    {
        bool is_new = false;
        int slot = reserve(db, h, key, klen, &is_new);
        if (slot < 0)
        {
            rc->overflow = true;
            return;
        }
        pc_dbm_slot *s = &db->slots[slot];
        if (s->state != 1)
        {
            db->count++;
        }
        s->state = 1;
        s->hash = h;
        s->key_len = klen;
        memcpy(s->key, key, klen);
        s->val_off = data_off + WAL_RECORD_HEADER + DBM_HDR + klen;
        s->val_len = vlen;
    }
    else if (op == 1) // delete
    {
        int slot = find_live(db, h, key, klen);
        if (slot >= 0)
        {
            db->slots[slot].state = 2;
            db->count--;
        }
    }
}
} // namespace

bool pc_dbm_open(pc_dbm *db, WalStore *wal)
{
    memset(db, 0, sizeof(*db));
    db->wal = wal;
    ReplayCtx rc = {db, false};
    uint8_t scratch[WAL_RECORD_HEADER + DBM_HDR + PC_DBM_KEY_MAX + PC_DBM_VAL_MAX];
    pc_wal_store_scan(wal, replay_cb, &rc, scratch, sizeof(scratch));
    return !rc.overflow;
}

bool pc_dbm_put(pc_dbm *db, const char *key, uint16_t key_len, const uint8_t *val, uint32_t val_len)
{
    if (key_len == 0 || key_len > PC_DBM_KEY_MAX || val_len > PC_DBM_VAL_MAX)
    {
        return false;
    }
    uint64_t h = key_hash(key, key_len);
    bool is_new = false;
    int slot = reserve(db, h, key, key_len, &is_new);
    if (slot < 0)
    {
        return false; // index full: do not append an orphan record
    }

    uint8_t rec[DBM_HDR + PC_DBM_KEY_MAX + PC_DBM_VAL_MAX];
    rec[0] = 0;
    put_u16(rec + 1, key_len);
    put_u32(rec + 3, val_len);
    memcpy(rec + DBM_HDR, key, key_len);
    if (val_len)
    {
        memcpy(rec + DBM_HDR + key_len, val, val_len);
    }
    uint64_t old_head = pc_wal_store_used(db->wal);
    if (!pc_wal_store_append(db->wal, rec, (uint32_t)(DBM_HDR + key_len + val_len)))
    {
        return false; // WAL full: index unchanged
    }

    pc_dbm_slot *s = &db->slots[slot];
    if (s->state != 1)
    {
        db->count++;
    }
    s->state = 1;
    s->hash = h;
    s->key_len = key_len;
    memcpy(s->key, key, key_len);
    s->val_off = old_head + WAL_RECORD_HEADER + DBM_HDR + key_len;
    s->val_len = val_len;
    return true;
}

long pc_dbm_get(pc_dbm *db, const char *key, uint16_t key_len, uint8_t *buf, size_t cap)
{
    uint64_t h = key_hash(key, key_len);
    int slot = find_live(db, h, key, key_len);
    if (slot < 0)
    {
        return -1;
    }
    const pc_dbm_slot *s = &db->slots[slot];
    if (s->val_len > cap)
    {
        return -1;
    }
    if (s->val_len && !pc_wal_store_pread(db->wal, s->val_off, buf, s->val_len))
    {
        return -1;
    }
    return (long)s->val_len;
}

bool pc_dbm_del(pc_dbm *db, const char *key, uint16_t key_len)
{
    uint64_t h = key_hash(key, key_len);
    int slot = find_live(db, h, key, key_len);
    if (slot < 0)
    {
        return false;
    }
    uint8_t rec[DBM_HDR + PC_DBM_KEY_MAX];
    rec[0] = 1;
    put_u16(rec + 1, key_len);
    put_u32(rec + 3, 0);
    memcpy(rec + DBM_HDR, key, key_len);
    if (!pc_wal_store_append(db->wal, rec, (uint32_t)(DBM_HDR + key_len)))
    {
        return false; // WAL full: key stays live
    }
    db->slots[slot].state = 2;
    db->count--;
    return true;
}

bool pc_dbm_contains(const pc_dbm *db, const char *key, uint16_t key_len)
{
    return find_live(db, key_hash(key, key_len), key, key_len) >= 0;
}

uint32_t pc_dbm_count(const pc_dbm *db)
{
    return db->count;
}

bool pc_dbm_sync(pc_dbm *db)
{
    return pc_wal_store_checkpoint(db->wal);
}

uint32_t pc_dbm_iterate(const pc_dbm *db, pc_dbm_iter_cb cb, void *ctx)
{
    uint32_t visited = 0;
    for (uint32_t i = 0; i < PC_DBM_SLOTS; i++)
    {
        const pc_dbm_slot *s = &db->slots[i];
        if (s->state != 1)
        {
            continue;
        }
        visited++;
        if (cb && !cb(s->key, s->key_len, ctx))
        {
            break;
        }
    }
    return visited;
}

uint64_t pc_dbm_live_bytes(const pc_dbm *db)
{
    uint64_t bytes = 0;
    for (uint32_t i = 0; i < PC_DBM_SLOTS; i++)
    {
        const pc_dbm_slot *s = &db->slots[i];
        if (s->state == 1)
        {
            bytes += WAL_RECORD_HEADER + DBM_HDR + s->key_len + s->val_len; // one framed record per live key
        }
    }
    return bytes;
}

bool pc_dbm_compact(pc_dbm *db, WalStore *dst)
{
    // Copy each live key (latest value, no tombstones) into the fresh destination. Read the value straight
    // from the old log so this needs no per-key RAM beyond one record buffer, the same the put path uses.
    uint8_t rec[DBM_HDR + PC_DBM_KEY_MAX + PC_DBM_VAL_MAX];
    for (uint32_t i = 0; i < PC_DBM_SLOTS; i++)
    {
        const pc_dbm_slot *s = &db->slots[i];
        if (s->state != 1)
        {
            continue;
        }
        rec[0] = 0; // put
        put_u16(rec + 1, s->key_len);
        put_u32(rec + 3, s->val_len);
        memcpy(rec + DBM_HDR, s->key, s->key_len);
        // On any failure, return before rebinding so db keeps using its intact original log (no data loss).
        if (s->val_len && !pc_wal_store_pread(db->wal, s->val_off, rec + DBM_HDR + s->key_len, s->val_len))
        {
            return false;
        }
        if (!pc_wal_store_append(dst, rec, (uint32_t)(DBM_HDR + s->key_len + s->val_len)))
        {
            return false; // destination too small
        }
    }
    if (!pc_wal_store_checkpoint(dst))
    {
        return false;
    }
    return pc_dbm_open(db, dst); // rebind to the compacted log + rebuild the index with fresh offsets
}

#endif // PC_ENABLE_DBM
