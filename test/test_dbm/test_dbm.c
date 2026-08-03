// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/dbm: a log-structured hash KV over the WAL, exercised on a RAM-backed WalDev.
// "Reboot" is modeled by remounting a fresh WalStore + reopening the dbm over the same disk buffer.

#include "services/storage/dbm/dbm.h"
#include "services/storage/wal/wal_store.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// RAM-backed WalDev (same shape as the pc_wal_store tests).
typedef struct
{
    uint8_t *buf;
    uint64_t size;
} RamDisk;
// Fault-injection switches (default off): simulate a device whose read or sync fails, to exercise the
// compaction fail-closed / no-data-loss paths.
static proto_bool g_fail_read = PROTO_FALSE;
static proto_bool g_fail_sync = PROTO_FALSE;

static size_t ram_read(void *ctx, uint64_t off, uint8_t *buf, size_t len)
{
    if (g_fail_read)
    {
        return 0; // simulated read error
    }
    RamDisk *d = (RamDisk *)ctx;
    if (off + len > d->size)
    {
        return 0;
    }
    memcpy(buf, d->buf + off, len);
    return len;
}
static size_t ram_write(void *ctx, uint64_t off, const uint8_t *buf, size_t len)
{
    RamDisk *d = (RamDisk *)ctx;
    if (off + len > d->size)
    {
        return 0;
    }
    memcpy(d->buf + off, buf, len);
    return len;
}
static proto_bool ram_sync(void *)
{
    return !g_fail_sync; // simulated sync barrier failure when set
}

static uint8_t g_disk[64 * 1024];
static RamDisk g_d;
static WalDev g_dev;

static WalDev dev_over(RamDisk *d)
{
    WalDev v;
    v.read = ram_read;
    v.write = ram_write;
    v.sync = ram_sync;
    v.ctx = d;
    v.size = d->size;
    return v;
}

// Fresh, formatted store + dbm over the shared disk.
static WalStore g_wal;
static pc_dbm g_db;
static void fresh_sized(uint64_t bytes)
{
    g_d.buf = g_disk;
    g_d.size = bytes;
    g_dev = dev_over(&g_d);
    TEST_ASSERT_TRUE(pc_wal_store_format(&g_wal, &g_dev));
    TEST_ASSERT_TRUE(pc_dbm_open(&g_db, &g_wal));
}
static void fresh(void)
{
    fresh_sized(sizeof(g_disk));
}
// Remount the store + reopen the dbm over the SAME disk bytes (a "reboot").
static proto_bool reboot(void)
{
    g_dev = dev_over(&g_d);
    if (!pc_wal_store_mount(&g_wal, &g_dev))
    {
        return PROTO_FALSE;
    }
    return pc_dbm_open(&g_db, &g_wal);
}

static proto_bool put_s(const char *k, const char *v)
{
    return pc_dbm_put(&g_db, k, (uint16_t)strlen(k), (const uint8_t *)v, (uint32_t)strlen(v));
}
// Get and compare to expected string. Returns true if present and equal.
static proto_bool get_eq(const char *k, const char *expect)
{
    uint8_t buf[PC_DBM_VAL_MAX];
    long n = pc_dbm_get(&g_db, k, (uint16_t)strlen(k), buf, sizeof(buf));
    if (n < 0)
    {
        return PROTO_FALSE;
    }
    return (size_t)n == strlen(expect) && memcmp(buf, expect, n) == 0;
}

void test_put_get_overwrite(void)
{
    fresh();
    TEST_ASSERT_TRUE(put_s("alpha", "one"));
    TEST_ASSERT_TRUE(put_s("beta", "two"));
    TEST_ASSERT_TRUE(get_eq("alpha", "one"));
    TEST_ASSERT_TRUE(get_eq("beta", "two"));
    TEST_ASSERT_EQUAL_UINT32(2, pc_dbm_count(&g_db));

    TEST_ASSERT_TRUE(put_s("alpha", "ONE-UPDATED")); // overwrite
    TEST_ASSERT_TRUE(get_eq("alpha", "ONE-UPDATED"));
    TEST_ASSERT_EQUAL_UINT32(2, pc_dbm_count(&g_db)); // still 2 live keys

    uint8_t b[8];
    TEST_ASSERT_EQUAL_INT(-1, pc_dbm_get(&g_db, "missing", 7, b, sizeof(b))); // absent
}

void test_delete_and_contains(void)
{
    fresh();
    put_s("k1", "v1");
    put_s("k2", "v2");
    TEST_ASSERT_TRUE(pc_dbm_contains(&g_db, "k1", 2));
    TEST_ASSERT_TRUE(pc_dbm_del(&g_db, "k1", 2));
    TEST_ASSERT_FALSE(pc_dbm_contains(&g_db, "k1", 2));
    TEST_ASSERT_FALSE(pc_dbm_del(&g_db, "k1", 2)); // already gone
    TEST_ASSERT_EQUAL_UINT32(1, pc_dbm_count(&g_db));

    // A tombstoned key can be re-inserted (resurrect through the tombstone slot).
    TEST_ASSERT_TRUE(put_s("k1", "again"));
    TEST_ASSERT_TRUE(get_eq("k1", "again"));
    TEST_ASSERT_EQUAL_UINT32(2, pc_dbm_count(&g_db));
}

void test_persist_across_reboot_with_checkpoint(void)
{
    fresh();
    put_s("name", "pc");
    put_s("role", "server");
    put_s("name", "pc2"); // overwrite before checkpoint
    TEST_ASSERT_TRUE(pc_dbm_sync(&g_db));

    TEST_ASSERT_TRUE(reboot());
    TEST_ASSERT_TRUE(get_eq("name", "pc2")); // latest value wins
    TEST_ASSERT_TRUE(get_eq("role", "server"));
    TEST_ASSERT_EQUAL_UINT32(2, pc_dbm_count(&g_db));
}

// Writes that were never checkpointed still recover, because the WAL tail-replays and the dbm replays it.
void test_persist_across_reboot_without_checkpoint(void)
{
    fresh();
    put_s("a", "1");
    put_s("b", "2");
    put_s("c", "3");
    // no sync/checkpoint
    TEST_ASSERT_TRUE(reboot());
    TEST_ASSERT_TRUE(get_eq("a", "1"));
    TEST_ASSERT_TRUE(get_eq("b", "2"));
    TEST_ASSERT_TRUE(get_eq("c", "3"));
    TEST_ASSERT_EQUAL_UINT32(3, pc_dbm_count(&g_db));
}

void test_delete_persists_across_reboot(void)
{
    fresh();
    put_s("keep", "y");
    put_s("drop", "n");
    TEST_ASSERT_TRUE(pc_dbm_del(&g_db, "drop", 4));
    TEST_ASSERT_TRUE(pc_dbm_sync(&g_db));

    TEST_ASSERT_TRUE(reboot());
    TEST_ASSERT_TRUE(pc_dbm_contains(&g_db, "keep", 4));
    TEST_ASSERT_FALSE(pc_dbm_contains(&g_db, "drop", 4)); // tombstone replayed
    TEST_ASSERT_EQUAL_UINT32(1, pc_dbm_count(&g_db));
}

// Many keys (forces hash collisions / probing) all retrievable and survive a reboot.
void test_many_keys_and_collisions(void)
{
    fresh();
    const int N = 100;
    char k[16], v[16];
    for (int i = 0; i < N; i++)
    {
        snprintf(k, sizeof(k), "key%04d", i);
        snprintf(v, sizeof(v), "val%d", i * 7);
        TEST_ASSERT_TRUE(put_s(k, v));
    }
    TEST_ASSERT_EQUAL_UINT32((uint32_t)N, pc_dbm_count(&g_db));
    TEST_ASSERT_TRUE(pc_dbm_sync(&g_db));
    TEST_ASSERT_TRUE(reboot());
    for (int i = 0; i < N; i++)
    {
        snprintf(k, sizeof(k), "key%04d", i);
        snprintf(v, sizeof(v), "val%d", i * 7);
        TEST_ASSERT_TRUE(get_eq(k, v));
    }
    TEST_ASSERT_EQUAL_UINT32((uint32_t)N, pc_dbm_count(&g_db));
}

void test_index_full_fails_closed(void)
{
    fresh();
    char k[16];
    // Fill every slot with a distinct live key.
    for (int i = 0; i < PC_DBM_SLOTS; i++)
    {
        snprintf(k, sizeof(k), "s%05d", i);
        TEST_ASSERT_TRUE(put_s(k, "x"));
    }
    TEST_ASSERT_EQUAL_UINT32((uint32_t)PC_DBM_SLOTS, pc_dbm_count(&g_db));
    // A brand-new key has no slot -> fail closed.
    TEST_ASSERT_FALSE(put_s("overflow-key", "x"));
    // But overwriting an existing key still works (no new slot needed).
    TEST_ASSERT_TRUE(put_s("s00000", "updated"));
    TEST_ASSERT_TRUE(get_eq("s00000", "updated"));
}

void test_bounds_and_empty_value(void)
{
    fresh();
    char bigk[PC_DBM_KEY_MAX + 2];
    memset(bigk, 'k', sizeof(bigk));
    TEST_ASSERT_FALSE(pc_dbm_put(&g_db, bigk, PC_DBM_KEY_MAX + 1, (const uint8_t *)"v", 1)); // key too long

    uint8_t bigv[PC_DBM_VAL_MAX + 1];
    memset(bigv, 0xAB, sizeof(bigv));
    TEST_ASSERT_FALSE(pc_dbm_put(&g_db, "k", 1, bigv, PC_DBM_VAL_MAX + 1)); // value too long

    // Empty value is valid: get returns 0, key is present.
    TEST_ASSERT_TRUE(pc_dbm_put(&g_db, "empty", 5, NULL, 0));
    uint8_t b[4];
    TEST_ASSERT_EQUAL_INT(0, pc_dbm_get(&g_db, "empty", 5, b, sizeof(b)));
    TEST_ASSERT_TRUE(pc_dbm_contains(&g_db, "empty", 5));
}

void test_max_value_roundtrip(void)
{
    fresh();
    uint8_t val[PC_DBM_VAL_MAX];
    for (int i = 0; i < PC_DBM_VAL_MAX; i++)
    {
        val[i] = (uint8_t)(i * 13 + 7);
    }
    TEST_ASSERT_TRUE(pc_dbm_put(&g_db, "big", 3, val, PC_DBM_VAL_MAX));
    TEST_ASSERT_TRUE(pc_dbm_sync(&g_db));
    TEST_ASSERT_TRUE(reboot());
    uint8_t out[PC_DBM_VAL_MAX];
    TEST_ASSERT_EQUAL_INT(PC_DBM_VAL_MAX, pc_dbm_get(&g_db, "big", 3, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(val, out, PC_DBM_VAL_MAX);

    // A too-small buffer fails rather than truncating.
    uint8_t small[4];
    TEST_ASSERT_EQUAL_INT(-1, pc_dbm_get(&g_db, "big", 3, small, sizeof(small)));
}

// A second RAM disk + store to compact INTO (compaction is merge-to-new, never in place).
static uint8_t g_disk2[64 * 1024];
static RamDisk g_d2;
static WalDev g_dev2;
static WalStore g_wal2;
static WalStore *fresh_dest(uint64_t size)
{
    g_d2.buf = g_disk2;
    g_d2.size = size;
    g_dev2 = dev_over(&g_d2);
    TEST_ASSERT_TRUE(pc_wal_store_format(&g_wal2, &g_dev2));
    return &g_wal2;
}

void test_compact_reclaims_space(void)
{
    fresh();
    put_s("k1", "v1");
    put_s("k2", "v2");
    put_s("k3", "v3");
    put_s("k4", "v4");
    // Churn: overwrite k1 many times and delete k3 - all of that becomes dead space in the log.
    for (int i = 0; i < 30; i++)
    {
        char v[32];
        snprintf(v, sizeof(v), "k1-value-revision-%d", i);
        TEST_ASSERT_TRUE(put_s("k1", v));
    }
    TEST_ASSERT_TRUE(pc_dbm_del(&g_db, "k3", 2));
    TEST_ASSERT_EQUAL_UINT32(3, pc_dbm_count(&g_db)); // k1,k2,k4 live

    uint64_t used_before = pc_wal_store_used(&g_wal);
    uint64_t live = pc_dbm_live_bytes(&g_db);
    TEST_ASSERT_TRUE(live < used_before); // the log carries reclaimable dead space

    // Compact into the fresh destination; db is now bound to it.
    WalStore *dst = fresh_dest(sizeof(g_disk2));
    TEST_ASSERT_TRUE(pc_dbm_compact(&g_db, dst));

    // Live set preserved, tombstoned key gone, latest value intact.
    TEST_ASSERT_EQUAL_UINT32(3, pc_dbm_count(&g_db));
    TEST_ASSERT_TRUE(get_eq("k1", "k1-value-revision-29"));
    TEST_ASSERT_TRUE(get_eq("k2", "v2"));
    TEST_ASSERT_TRUE(get_eq("k4", "v4"));
    TEST_ASSERT_FALSE(pc_dbm_contains(&g_db, "k3", 2));

    // The compacted log is smaller than the churned one and holds only the live records.
    uint64_t used_after = pc_wal_store_used(&g_wal2);
    TEST_ASSERT_TRUE(used_after < used_before);
    TEST_ASSERT_TRUE(used_after >= live);

    // A further write goes to the new log and reads back.
    TEST_ASSERT_TRUE(put_s("k5", "v5"));
    TEST_ASSERT_TRUE(get_eq("k5", "v5"));
}

void test_compact_dest_too_small_fails_closed(void)
{
    fresh();
    char big[200]; // within PC_DBM_VAL_MAX (256)
    memset(big, 'Z', sizeof(big));
    for (int i = 0; i < 4; i++)
    {
        char k[8];
        snprintf(k, sizeof(k), "key%d", i);
        TEST_ASSERT_TRUE(pc_dbm_put(&g_db, k, (uint16_t)strlen(k), (const uint8_t *)big, sizeof(big)));
    }
    TEST_ASSERT_EQUAL_UINT32(4, pc_dbm_count(&g_db)); // ~800+ B of live values, plus framing

    // A 512-byte destination (384 B usable) cannot hold the live set: compact must fail closed.
    WalStore *dst = fresh_dest(512);
    TEST_ASSERT_FALSE(pc_dbm_compact(&g_db, dst));

    // db is untouched: still on the original log, every key still readable.
    TEST_ASSERT_EQUAL_UINT32(4, pc_dbm_count(&g_db));
    uint8_t out[256];
    TEST_ASSERT_EQUAL_INT(200, pc_dbm_get(&g_db, "key0", 4, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(big, out, 200);
}

void test_compact_source_read_failure(void)
{
    // If reading a value back from the source log fails mid-compaction, compact must fail closed BEFORE
    // rebinding, so db keeps using its intact original log - no data loss.
    fresh();
    put_s("a", "one");
    put_s("b", "two");
    put_s("a", "one-updated"); // some churn so there is real live data to copy
    WalStore *dst = fresh_dest(sizeof(g_disk2));

    g_fail_read = PROTO_TRUE;
    TEST_ASSERT_FALSE(pc_dbm_compact(&g_db, dst)); // a source pread fails
    g_fail_read = PROTO_FALSE;

    // db is untouched: still on the original log, every live key intact.
    TEST_ASSERT_EQUAL_UINT32(2, pc_dbm_count(&g_db));
    TEST_ASSERT_TRUE(get_eq("a", "one-updated"));
    TEST_ASSERT_TRUE(get_eq("b", "two"));
}

void test_compact_checkpoint_failure(void)
{
    // If the destination checkpoint (sync) fails after the live keys are copied, compact must fail closed and
    // leave db on the intact original log.
    fresh();
    put_s("x", "10");
    put_s("y", "20");
    WalStore *dst = fresh_dest(sizeof(g_disk2));

    g_fail_sync = PROTO_TRUE;
    TEST_ASSERT_FALSE(pc_dbm_compact(&g_db, dst)); // dst checkpoint sync fails
    g_fail_sync = PROTO_FALSE;

    TEST_ASSERT_EQUAL_UINT32(2, pc_dbm_count(&g_db));
    TEST_ASSERT_TRUE(get_eq("x", "10"));
    TEST_ASSERT_TRUE(get_eq("y", "20"));

    // And a normal compaction still works afterward (the store was never corrupted).
    WalStore *dst2 = fresh_dest(sizeof(g_disk2));
    TEST_ASSERT_TRUE(pc_dbm_compact(&g_db, dst2));
    TEST_ASSERT_TRUE(get_eq("x", "10"));
    TEST_ASSERT_TRUE(get_eq("y", "20"));
}

// --- corrupt / hand-built log records -------------------------------------------------------------
// Append a dbm record payload straight to the WAL, bypassing pc_dbm_put, so a reopen replays records
// the public API would never emit. key_len / val_len are written as given, independently of the actual
// tail, which is how a truncated or garbage record is modeled.
static const size_t DBM_RECORD_HDR = 1 + 2 + 4; // op u8 | key_len u16 | val_len u32
static proto_bool raw_append(uint8_t op, uint16_t key_len_field, uint32_t val_len_field, const void *tail,
                             size_t tail_len)
{
    uint8_t rec[DBM_RECORD_HDR + PC_DBM_KEY_MAX + PC_DBM_VAL_MAX];
    rec[0] = op;
    rec[1] = (uint8_t)key_len_field;
    rec[2] = (uint8_t)(key_len_field >> 8);
    rec[3] = (uint8_t)val_len_field;
    rec[4] = (uint8_t)(val_len_field >> 8);
    rec[5] = (uint8_t)(val_len_field >> 16);
    rec[6] = (uint8_t)(val_len_field >> 24);
    if (tail_len)
    {
        memcpy(rec + DBM_RECORD_HDR, tail, tail_len);
    }
    return pc_wal_store_append(&g_wal, rec, (uint32_t)(DBM_RECORD_HDR + tail_len));
}

void test_replay_skips_malformed_records(void)
{
    // Replay must step over anything it cannot trust and keep rebuilding the index from the rest, so one
    // bad record in the log never costs the whole key set.
    fresh();
    TEST_ASSERT_TRUE(put_s("good", "yes"));

    uint8_t stub[3] = {0, 0, 0};
    TEST_ASSERT_TRUE(pc_wal_store_append(&g_wal, stub, sizeof(stub))); // shorter than the record header
    TEST_ASSERT_TRUE(raw_append(0, 0, 0, NULL, 0));                    // zero-length key
    TEST_ASSERT_TRUE(raw_append(0, PC_DBM_KEY_MAX + 1, 0, NULL, 0));   // key longer than the bound
    TEST_ASSERT_TRUE(raw_append(0, 4, 100, "abcd", 4));                // claims 100 value bytes it does not have
    TEST_ASSERT_TRUE(raw_append(7, 4, 0, "abcd", 4));                  // opcode that is neither put nor delete
    TEST_ASSERT_TRUE(raw_append(1, 5, 0, "ghost", 5));                 // tombstone for a key never stored
    TEST_ASSERT_TRUE(pc_dbm_sync(&g_db));

    TEST_ASSERT_TRUE(reboot());
    TEST_ASSERT_TRUE(get_eq("good", "yes"));
    TEST_ASSERT_EQUAL_UINT32(1, pc_dbm_count(&g_db)); // nothing bogus was admitted to the index
    TEST_ASSERT_FALSE(pc_dbm_contains(&g_db, "abcd", 4));
    TEST_ASSERT_FALSE(pc_dbm_contains(&g_db, "ghost", 5));
}

void test_reopen_rejects_a_log_with_more_keys_than_slots(void)
{
    // The index is a fixed array: a log carrying more distinct live keys than it has slots cannot be
    // represented, and open must say so rather than silently dropping keys.
    fresh();
    char k[16];
    for (int i = 0; i < PC_DBM_SLOTS; i++)
    {
        snprintf(k, sizeof(k), "s%05d", i);
        TEST_ASSERT_TRUE(put_s(k, "x"));
    }
    TEST_ASSERT_TRUE(raw_append(0, 5, 0, "extra", 5)); // one key past the index capacity
    TEST_ASSERT_TRUE(pc_dbm_sync(&g_db));

    g_dev = dev_over(&g_d);
    TEST_ASSERT_TRUE(pc_wal_store_mount(&g_wal, &g_dev));
    TEST_ASSERT_FALSE(pc_dbm_open(&g_db, &g_wal));
}

void test_probe_walks_a_saturated_table_for_an_absent_key(void)
{
    // With no empty slot left to end the probe chain on, a lookup has to walk the whole table and
    // still report the key as absent (rather than looping or reporting a neighbour).
    fresh();
    char k[16];
    for (int i = 0; i < PC_DBM_SLOTS; i++)
    {
        snprintf(k, sizeof(k), "s%05d", i);
        TEST_ASSERT_TRUE(put_s(k, "x"));
    }
    TEST_ASSERT_FALSE(pc_dbm_contains(&g_db, "absent", 6));
    uint8_t b[8];
    TEST_ASSERT_EQUAL_INT(-1, pc_dbm_get(&g_db, "absent", 6, b, sizeof(b)));
    TEST_ASSERT_TRUE(get_eq("s00000", "x")); // the keys that are there are still found
}

void test_insert_reuses_a_tombstone_in_a_saturated_table(void)
{
    // Once every slot has been used and freed, a new key must land in the earliest reusable tombstone
    // even though the probe never meets an empty slot.
    fresh();
    char k[16];
    for (int i = 0; i < PC_DBM_SLOTS; i++)
    {
        snprintf(k, sizeof(k), "s%05d", i);
        TEST_ASSERT_TRUE(put_s(k, "x"));
    }
    for (int i = 0; i < PC_DBM_SLOTS; i++)
    {
        snprintf(k, sizeof(k), "s%05d", i);
        TEST_ASSERT_TRUE(pc_dbm_del(&g_db, k, (uint16_t)strlen(k)));
    }
    TEST_ASSERT_EQUAL_UINT32(0, pc_dbm_count(&g_db));

    TEST_ASSERT_TRUE(put_s("recycled", "v"));
    TEST_ASSERT_TRUE(get_eq("recycled", "v"));
    TEST_ASSERT_EQUAL_UINT32(1, pc_dbm_count(&g_db));
}

void test_hash_collision_slots_are_walked_past(void)
{
    // The stored 64-bit hash is only a prefilter; key_len + the key bytes are what actually identify a
    // slot. Plant two slots carrying the probe hash but a different key - one differing by length, one
    // by content - and neither lookup nor insert may mistake them for the key.
    fresh();
    const char *key = "collide-me";
    const uint16_t klen = (uint16_t)strlen(key);
    TEST_ASSERT_TRUE(put_s(key, "v1"));

    int j = -1;
    for (uint32_t i = 0; i < PC_DBM_SLOTS; i++)
    {
        if (g_db.slots[i].state == 1 && g_db.slots[i].key_len == klen && memcmp(g_db.slots[i].key, key, klen) == 0)
        {
            j = (int)i;
        }
    }
    TEST_ASSERT_TRUE(j >= 0);
    const uint64_t h = g_db.slots[j].hash;
    const int j1 = (j + 1) % PC_DBM_SLOTS;
    const int j2 = (j + 2) % PC_DBM_SLOTS;
    TEST_ASSERT_EQUAL_UINT8(0, g_db.slots[j1].state); // a fresh table: the rest of the chain is empty
    TEST_ASSERT_EQUAL_UINT8(0, g_db.slots[j2].state);

    // Slot j: same hash, longer key. Slot j+1: same hash and length, different bytes.
    g_db.slots[j].key_len = (uint16_t)(klen + 1);
    memcpy(g_db.slots[j].key, "collide-me!", klen + 1);
    g_db.slots[j1] = g_db.slots[j];
    g_db.slots[j1].key_len = klen;
    memcpy(g_db.slots[j1].key, "collide-mE", klen);
    g_db.slots[j1].hash = h;
    g_db.count = 2;

    // Lookup walks both and reports the key absent.
    TEST_ASSERT_FALSE(pc_dbm_contains(&g_db, key, klen));
    // Insert walks both too and claims the next free slot instead of overwriting either.
    TEST_ASSERT_TRUE(put_s(key, "v2"));
    TEST_ASSERT_EQUAL_UINT8(1, g_db.slots[j2].state);
    TEST_ASSERT_EQUAL_UINT16(klen + 1, g_db.slots[j].key_len); // the colliding slots are untouched
    TEST_ASSERT_EQUAL_INT(0, memcmp(g_db.slots[j1].key, "collide-mE", klen));
    TEST_ASSERT_TRUE(get_eq(key, "v2"));
    TEST_ASSERT_EQUAL_UINT32(3, pc_dbm_count(&g_db));
}

void test_put_rejects_an_empty_key(void)
{
    // A zero-length key has no identity in the log format (key_len 0 is how a corrupt record reads).
    fresh();
    TEST_ASSERT_FALSE(pc_dbm_put(&g_db, "", 0, (const uint8_t *)"v", 1));
    TEST_ASSERT_EQUAL_UINT32(0, pc_dbm_count(&g_db));
}

void test_put_fails_closed_when_the_log_is_full(void)
{
    // The record is appended before the index is touched, so a full log leaves the index exactly as it
    // was - the failed key is simply not there, and the ones already stored still read back.
    fresh_sized(1024);
    uint8_t val[200];
    memset(val, 'V', sizeof(val));
    char k[8];
    int stored = 0;
    for (int i = 0; i < 32; i++)
    {
        snprintf(k, sizeof(k), "k%02d", i);
        if (!pc_dbm_put(&g_db, k, (uint16_t)strlen(k), val, sizeof(val)))
        {
            break;
        }
        stored++;
    }
    TEST_ASSERT_TRUE(stored > 0);
    TEST_ASSERT_TRUE(stored < 32); // the log really did run out
    TEST_ASSERT_EQUAL_UINT32((uint32_t)stored, pc_dbm_count(&g_db));

    snprintf(k, sizeof(k), "k%02d", stored); // the key that did not fit is absent
    TEST_ASSERT_FALSE(pc_dbm_contains(&g_db, k, (uint16_t)strlen(k)));
    uint8_t out[256];
    TEST_ASSERT_EQUAL_INT((long)sizeof(val), pc_dbm_get(&g_db, "k00", 3, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(val, out, sizeof(val));
}

void test_get_fails_when_the_value_cannot_be_read_back(void)
{
    // The index says where the value is, but the read still has to succeed; a device error is reported
    // as a miss rather than handing back an uninitialized buffer.
    fresh();
    TEST_ASSERT_TRUE(put_s("v", "payload"));
    uint8_t out[16];
    g_fail_read = PROTO_TRUE;
    TEST_ASSERT_EQUAL_INT(-1, pc_dbm_get(&g_db, "v", 1, out, sizeof(out)));
    g_fail_read = PROTO_FALSE;
    TEST_ASSERT_TRUE(get_eq("v", "payload")); // and it reads fine once the device recovers
}

typedef struct
{
    int seen;
    int stop_after; // 0 = never stop
} IterCtx;
static proto_bool iter_cb(const char *key, uint16_t key_len, void *ctx)
{
    (void)key;
    (void)key_len;
    IterCtx *c = (IterCtx *)ctx;
    c->seen++;
    return c->stop_after == 0 || c->seen < c->stop_after;
}

void test_iterate_visits_live_keys_and_honours_an_early_stop(void)
{
    // iterate walks only live keys (tombstones are skipped), a null callback just counts them, and a
    // callback that returns false stops the walk where it stands.
    fresh();
    put_s("a", "1");
    put_s("b", "2");
    put_s("c", "3");
    TEST_ASSERT_TRUE(pc_dbm_del(&g_db, "b", 1));

    TEST_ASSERT_EQUAL_UINT32(2, pc_dbm_iterate(&g_db, NULL, NULL));

    IterCtx all = {0, 0};
    TEST_ASSERT_EQUAL_UINT32(2, pc_dbm_iterate(&g_db, iter_cb, &all));
    TEST_ASSERT_EQUAL_INT(2, all.seen);

    IterCtx one = {0, 1};
    TEST_ASSERT_EQUAL_UINT32(1, pc_dbm_iterate(&g_db, iter_cb, &one)); // stopped after the first key
    TEST_ASSERT_EQUAL_INT(1, one.seen);
}

void test_compact_carries_empty_values(void)
{
    // A key stored with a zero-length value has nothing to read back from the old log; compaction must
    // still carry the key across rather than skipping or failing on it.
    fresh();
    TEST_ASSERT_TRUE(put_s("a", "1"));
    TEST_ASSERT_TRUE(pc_dbm_put(&g_db, "empty", 5, NULL, 0));

    WalStore *dst = fresh_dest(sizeof(g_disk2));
    TEST_ASSERT_TRUE(pc_dbm_compact(&g_db, dst));
    TEST_ASSERT_EQUAL_UINT32(2, pc_dbm_count(&g_db));
    TEST_ASSERT_TRUE(get_eq("a", "1"));
    uint8_t b[4];
    TEST_ASSERT_EQUAL_INT(0, pc_dbm_get(&g_db, "empty", 5, b, sizeof(b)));
    TEST_ASSERT_TRUE(pc_dbm_contains(&g_db, "empty", 5));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_put_get_overwrite);
    RUN_TEST(test_delete_and_contains);
    RUN_TEST(test_persist_across_reboot_with_checkpoint);
    RUN_TEST(test_persist_across_reboot_without_checkpoint);
    RUN_TEST(test_delete_persists_across_reboot);
    RUN_TEST(test_many_keys_and_collisions);
    RUN_TEST(test_index_full_fails_closed);
    RUN_TEST(test_bounds_and_empty_value);
    RUN_TEST(test_max_value_roundtrip);
    RUN_TEST(test_compact_reclaims_space);
    RUN_TEST(test_compact_dest_too_small_fails_closed);
    RUN_TEST(test_compact_source_read_failure);
    RUN_TEST(test_compact_checkpoint_failure);
    RUN_TEST(test_replay_skips_malformed_records);
    RUN_TEST(test_reopen_rejects_a_log_with_more_keys_than_slots);
    RUN_TEST(test_probe_walks_a_saturated_table_for_an_absent_key);
    RUN_TEST(test_insert_reuses_a_tombstone_in_a_saturated_table);
    RUN_TEST(test_hash_collision_slots_are_walked_past);
    RUN_TEST(test_put_rejects_an_empty_key);
    RUN_TEST(test_put_fails_closed_when_the_log_is_full);
    RUN_TEST(test_get_fails_when_the_value_cannot_be_read_back);
    RUN_TEST(test_iterate_visits_live_keys_and_honours_an_early_stop);
    RUN_TEST(test_compact_carries_empty_values);
    return UNITY_END();
}
