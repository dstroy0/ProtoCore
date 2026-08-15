// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
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

typedef struct
{
    uint8_t *buf;
    uint64_t size;
} RamDisk;

static proto_bool g_fail_read = PROTO_FALSE;
static proto_bool g_fail_sync = PROTO_FALSE;

static size_t ram_read(void *ctx, uint64_t off, uint8_t *buf, size_t len)
{
    if (g_fail_read)
    {
        return 0;
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
    return !g_fail_sync;
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

static WalStore g_wal;
static protocore_dbm g_db;
static void fresh_sized(uint64_t bytes)
{
    g_d.buf = g_disk;
    g_d.size = bytes;
    g_dev = dev_over(&g_d);
    TEST_ASSERT_TRUE(protocore_wal_store_format(&g_wal, &g_dev));
    TEST_ASSERT_TRUE(protocore_dbm_open(&g_db, &g_wal));
}
static void fresh(void)
{
    fresh_sized(sizeof(g_disk));
}

static proto_bool reboot(void)
{
    g_dev = dev_over(&g_d);
    if (!protocore_wal_store_mount(&g_wal, &g_dev))
    {
        return PROTO_FALSE;
    }
    return protocore_dbm_open(&g_db, &g_wal);
}

static proto_bool put_s(const char *k, const char *v)
{
    return protocore_dbm_put(&g_db, k, (uint16_t)strlen(k), (const uint8_t *)v, (uint32_t)strlen(v));
}

static proto_bool get_eq(const char *k, const char *expect)
{
    uint8_t buf[PROTOCORE_DBM_VAL_MAX];
    long n = protocore_dbm_get(&g_db, k, (uint16_t)strlen(k), buf, sizeof(buf));
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
    TEST_ASSERT_EQUAL_UINT32(2, protocore_dbm_count(&g_db));

    TEST_ASSERT_TRUE(put_s("alpha", "ONE-UPDATED"));
    TEST_ASSERT_TRUE(get_eq("alpha", "ONE-UPDATED"));
    TEST_ASSERT_EQUAL_UINT32(2, protocore_dbm_count(&g_db));

    uint8_t b[8];
    TEST_ASSERT_EQUAL_INT(-1, protocore_dbm_get(&g_db, "missing", 7, b, sizeof(b)));
}

void test_delete_and_contains(void)
{
    fresh();
    put_s("k1", "v1");
    put_s("k2", "v2");
    TEST_ASSERT_TRUE(protocore_dbm_contains(&g_db, "k1", 2));
    TEST_ASSERT_TRUE(protocore_dbm_del(&g_db, "k1", 2));
    TEST_ASSERT_FALSE(protocore_dbm_contains(&g_db, "k1", 2));
    TEST_ASSERT_FALSE(protocore_dbm_del(&g_db, "k1", 2));
    TEST_ASSERT_EQUAL_UINT32(1, protocore_dbm_count(&g_db));

    TEST_ASSERT_TRUE(put_s("k1", "again"));
    TEST_ASSERT_TRUE(get_eq("k1", "again"));
    TEST_ASSERT_EQUAL_UINT32(2, protocore_dbm_count(&g_db));
}

void test_persist_across_reboot_with_checkpoint(void)
{
    fresh();
    put_s("name", "pc");
    put_s("role", "server");
    put_s("name", "pc2");
    TEST_ASSERT_TRUE(protocore_dbm_sync(&g_db));

    TEST_ASSERT_TRUE(reboot());
    TEST_ASSERT_TRUE(get_eq("name", "pc2"));
    TEST_ASSERT_TRUE(get_eq("role", "server"));
    TEST_ASSERT_EQUAL_UINT32(2, protocore_dbm_count(&g_db));
}

void test_persist_across_reboot_without_checkpoint(void)
{
    fresh();
    put_s("a", "1");
    put_s("b", "2");
    put_s("c", "3");

    TEST_ASSERT_TRUE(reboot());
    TEST_ASSERT_TRUE(get_eq("a", "1"));
    TEST_ASSERT_TRUE(get_eq("b", "2"));
    TEST_ASSERT_TRUE(get_eq("c", "3"));
    TEST_ASSERT_EQUAL_UINT32(3, protocore_dbm_count(&g_db));
}

void test_delete_persists_across_reboot(void)
{
    fresh();
    put_s("keep", "y");
    put_s("drop", "n");
    TEST_ASSERT_TRUE(protocore_dbm_del(&g_db, "drop", 4));
    TEST_ASSERT_TRUE(protocore_dbm_sync(&g_db));

    TEST_ASSERT_TRUE(reboot());
    TEST_ASSERT_TRUE(protocore_dbm_contains(&g_db, "keep", 4));
    TEST_ASSERT_FALSE(protocore_dbm_contains(&g_db, "drop", 4));
    TEST_ASSERT_EQUAL_UINT32(1, protocore_dbm_count(&g_db));
}

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
    TEST_ASSERT_EQUAL_UINT32((uint32_t)N, protocore_dbm_count(&g_db));
    TEST_ASSERT_TRUE(protocore_dbm_sync(&g_db));
    TEST_ASSERT_TRUE(reboot());
    for (int i = 0; i < N; i++)
    {
        snprintf(k, sizeof(k), "key%04d", i);
        snprintf(v, sizeof(v), "val%d", i * 7);
        TEST_ASSERT_TRUE(get_eq(k, v));
    }
    TEST_ASSERT_EQUAL_UINT32((uint32_t)N, protocore_dbm_count(&g_db));
}

void test_index_full_fails_closed(void)
{
    fresh();
    char k[16];

    for (int i = 0; i < PROTOCORE_DBM_SLOTS; i++)
    {
        snprintf(k, sizeof(k), "s%05d", i);
        TEST_ASSERT_TRUE(put_s(k, "x"));
    }
    TEST_ASSERT_EQUAL_UINT32((uint32_t)PROTOCORE_DBM_SLOTS, protocore_dbm_count(&g_db));

    TEST_ASSERT_FALSE(put_s("overflow-key", "x"));

    TEST_ASSERT_TRUE(put_s("s00000", "updated"));
    TEST_ASSERT_TRUE(get_eq("s00000", "updated"));
}

void test_bounds_and_empty_value(void)
{
    fresh();
    char bigk[PROTOCORE_DBM_KEY_MAX + 2];
    memset(bigk, 'k', sizeof(bigk));
    TEST_ASSERT_FALSE(protocore_dbm_put(&g_db, bigk, PROTOCORE_DBM_KEY_MAX + 1, (const uint8_t *)"v", 1));

    uint8_t bigv[PROTOCORE_DBM_VAL_MAX + 1];
    memset(bigv, 0xAB, sizeof(bigv));
    TEST_ASSERT_FALSE(protocore_dbm_put(&g_db, "k", 1, bigv, PROTOCORE_DBM_VAL_MAX + 1));

    TEST_ASSERT_TRUE(protocore_dbm_put(&g_db, "empty", 5, NULL, 0));
    uint8_t b[4];
    TEST_ASSERT_EQUAL_INT(0, protocore_dbm_get(&g_db, "empty", 5, b, sizeof(b)));
    TEST_ASSERT_TRUE(protocore_dbm_contains(&g_db, "empty", 5));
}

void test_max_value_roundtrip(void)
{
    fresh();
    uint8_t val[PROTOCORE_DBM_VAL_MAX];
    for (int i = 0; i < PROTOCORE_DBM_VAL_MAX; i++)
    {
        val[i] = (uint8_t)(i * 13 + 7);
    }
    TEST_ASSERT_TRUE(protocore_dbm_put(&g_db, "big", 3, val, PROTOCORE_DBM_VAL_MAX));
    TEST_ASSERT_TRUE(protocore_dbm_sync(&g_db));
    TEST_ASSERT_TRUE(reboot());
    uint8_t out[PROTOCORE_DBM_VAL_MAX];
    TEST_ASSERT_EQUAL_INT(PROTOCORE_DBM_VAL_MAX, protocore_dbm_get(&g_db, "big", 3, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(val, out, PROTOCORE_DBM_VAL_MAX);

    uint8_t small[4];
    TEST_ASSERT_EQUAL_INT(-1, protocore_dbm_get(&g_db, "big", 3, small, sizeof(small)));
}

static uint8_t g_disk2[64 * 1024];
static RamDisk g_d2;
static WalDev g_dev2;
static WalStore g_wal2;
static WalStore *fresh_dest(uint64_t size)
{
    g_d2.buf = g_disk2;
    g_d2.size = size;
    g_dev2 = dev_over(&g_d2);
    TEST_ASSERT_TRUE(protocore_wal_store_format(&g_wal2, &g_dev2));
    return &g_wal2;
}

void test_compact_reclaims_space(void)
{
    fresh();
    put_s("k1", "v1");
    put_s("k2", "v2");
    put_s("k3", "v3");
    put_s("k4", "v4");

    for (int i = 0; i < 30; i++)
    {
        char v[32];
        snprintf(v, sizeof(v), "k1-value-revision-%d", i);
        TEST_ASSERT_TRUE(put_s("k1", v));
    }
    TEST_ASSERT_TRUE(protocore_dbm_del(&g_db, "k3", 2));
    TEST_ASSERT_EQUAL_UINT32(3, protocore_dbm_count(&g_db));

    uint64_t used_before = protocore_wal_store_used(&g_wal);
    uint64_t live = protocore_dbm_live_bytes(&g_db);
    TEST_ASSERT_TRUE(live < used_before);

    WalStore *dst = fresh_dest(sizeof(g_disk2));
    TEST_ASSERT_TRUE(protocore_dbm_compact(&g_db, dst));

    TEST_ASSERT_EQUAL_UINT32(3, protocore_dbm_count(&g_db));
    TEST_ASSERT_TRUE(get_eq("k1", "k1-value-revision-29"));
    TEST_ASSERT_TRUE(get_eq("k2", "v2"));
    TEST_ASSERT_TRUE(get_eq("k4", "v4"));
    TEST_ASSERT_FALSE(protocore_dbm_contains(&g_db, "k3", 2));

    uint64_t used_after = protocore_wal_store_used(&g_wal2);
    TEST_ASSERT_TRUE(used_after < used_before);
    TEST_ASSERT_TRUE(used_after >= live);

    TEST_ASSERT_TRUE(put_s("k5", "v5"));
    TEST_ASSERT_TRUE(get_eq("k5", "v5"));
}

void test_compact_dest_too_small_fails_closed(void)
{
    fresh();
    char big[200];
    memset(big, 'Z', sizeof(big));
    for (int i = 0; i < 4; i++)
    {
        char k[8];
        snprintf(k, sizeof(k), "key%d", i);
        TEST_ASSERT_TRUE(protocore_dbm_put(&g_db, k, (uint16_t)strlen(k), (const uint8_t *)big, sizeof(big)));
    }
    TEST_ASSERT_EQUAL_UINT32(4, protocore_dbm_count(&g_db));

    WalStore *dst = fresh_dest(512);
    TEST_ASSERT_FALSE(protocore_dbm_compact(&g_db, dst));

    TEST_ASSERT_EQUAL_UINT32(4, protocore_dbm_count(&g_db));
    uint8_t out[256];
    TEST_ASSERT_EQUAL_INT(200, protocore_dbm_get(&g_db, "key0", 4, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(big, out, 200);
}

void test_compact_source_read_failure(void)
{

    fresh();
    put_s("a", "one");
    put_s("b", "two");
    put_s("a", "one-updated");
    WalStore *dst = fresh_dest(sizeof(g_disk2));

    g_fail_read = PROTO_TRUE;
    TEST_ASSERT_FALSE(protocore_dbm_compact(&g_db, dst));
    g_fail_read = PROTO_FALSE;

    TEST_ASSERT_EQUAL_UINT32(2, protocore_dbm_count(&g_db));
    TEST_ASSERT_TRUE(get_eq("a", "one-updated"));
    TEST_ASSERT_TRUE(get_eq("b", "two"));
}

void test_compact_checkpoint_failure(void)
{

    fresh();
    put_s("x", "10");
    put_s("y", "20");
    WalStore *dst = fresh_dest(sizeof(g_disk2));

    g_fail_sync = PROTO_TRUE;
    TEST_ASSERT_FALSE(protocore_dbm_compact(&g_db, dst));
    g_fail_sync = PROTO_FALSE;

    TEST_ASSERT_EQUAL_UINT32(2, protocore_dbm_count(&g_db));
    TEST_ASSERT_TRUE(get_eq("x", "10"));
    TEST_ASSERT_TRUE(get_eq("y", "20"));

    WalStore *dst2 = fresh_dest(sizeof(g_disk2));
    TEST_ASSERT_TRUE(protocore_dbm_compact(&g_db, dst2));
    TEST_ASSERT_TRUE(get_eq("x", "10"));
    TEST_ASSERT_TRUE(get_eq("y", "20"));
}

static const size_t DBM_RECORD_HDR = 1 + 2 + 4;
static proto_bool raw_append(uint8_t op, uint16_t key_len_field, uint32_t val_len_field, const void *tail,
                             size_t tail_len)
{
    uint8_t rec[DBM_RECORD_HDR + PROTOCORE_DBM_KEY_MAX + PROTOCORE_DBM_VAL_MAX];
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
    return protocore_wal_store_append(&g_wal, rec, (uint32_t)(DBM_RECORD_HDR + tail_len));
}

void test_replay_skips_malformed_records(void)
{

    fresh();
    TEST_ASSERT_TRUE(put_s("good", "yes"));

    uint8_t stub[3] = {0, 0, 0};
    TEST_ASSERT_TRUE(protocore_wal_store_append(&g_wal, stub, sizeof(stub)));
    TEST_ASSERT_TRUE(raw_append(0, 0, 0, NULL, 0));
    TEST_ASSERT_TRUE(raw_append(0, PROTOCORE_DBM_KEY_MAX + 1, 0, NULL, 0));
    TEST_ASSERT_TRUE(raw_append(0, 4, 100, "abcd", 4));
    TEST_ASSERT_TRUE(raw_append(7, 4, 0, "abcd", 4));
    TEST_ASSERT_TRUE(raw_append(1, 5, 0, "ghost", 5));
    TEST_ASSERT_TRUE(protocore_dbm_sync(&g_db));

    TEST_ASSERT_TRUE(reboot());
    TEST_ASSERT_TRUE(get_eq("good", "yes"));
    TEST_ASSERT_EQUAL_UINT32(1, protocore_dbm_count(&g_db));
    TEST_ASSERT_FALSE(protocore_dbm_contains(&g_db, "abcd", 4));
    TEST_ASSERT_FALSE(protocore_dbm_contains(&g_db, "ghost", 5));
}

void test_reopen_rejects_a_log_with_more_keys_than_slots(void)
{

    fresh();
    char k[16];
    for (int i = 0; i < PROTOCORE_DBM_SLOTS; i++)
    {
        snprintf(k, sizeof(k), "s%05d", i);
        TEST_ASSERT_TRUE(put_s(k, "x"));
    }
    TEST_ASSERT_TRUE(raw_append(0, 5, 0, "extra", 5));
    TEST_ASSERT_TRUE(protocore_dbm_sync(&g_db));

    g_dev = dev_over(&g_d);
    TEST_ASSERT_TRUE(protocore_wal_store_mount(&g_wal, &g_dev));
    TEST_ASSERT_FALSE(protocore_dbm_open(&g_db, &g_wal));
}

void test_probe_walks_a_saturated_table_for_an_absent_key(void)
{

    fresh();
    char k[16];
    for (int i = 0; i < PROTOCORE_DBM_SLOTS; i++)
    {
        snprintf(k, sizeof(k), "s%05d", i);
        TEST_ASSERT_TRUE(put_s(k, "x"));
    }
    TEST_ASSERT_FALSE(protocore_dbm_contains(&g_db, "absent", 6));
    uint8_t b[8];
    TEST_ASSERT_EQUAL_INT(-1, protocore_dbm_get(&g_db, "absent", 6, b, sizeof(b)));
    TEST_ASSERT_TRUE(get_eq("s00000", "x"));
}

void test_insert_reuses_a_tombstone_in_a_saturated_table(void)
{

    fresh();
    char k[16];
    for (int i = 0; i < PROTOCORE_DBM_SLOTS; i++)
    {
        snprintf(k, sizeof(k), "s%05d", i);
        TEST_ASSERT_TRUE(put_s(k, "x"));
    }
    for (int i = 0; i < PROTOCORE_DBM_SLOTS; i++)
    {
        snprintf(k, sizeof(k), "s%05d", i);
        TEST_ASSERT_TRUE(protocore_dbm_del(&g_db, k, (uint16_t)strlen(k)));
    }
    TEST_ASSERT_EQUAL_UINT32(0, protocore_dbm_count(&g_db));

    TEST_ASSERT_TRUE(put_s("recycled", "v"));
    TEST_ASSERT_TRUE(get_eq("recycled", "v"));
    TEST_ASSERT_EQUAL_UINT32(1, protocore_dbm_count(&g_db));
}

void test_hash_collision_slots_are_walked_past(void)
{

    fresh();
    const char *key = "collide-me";
    const uint16_t klen = (uint16_t)strlen(key);
    TEST_ASSERT_TRUE(put_s(key, "v1"));

    int j = -1;
    for (uint32_t i = 0; i < PROTOCORE_DBM_SLOTS; i++)
    {
        if (g_db.slots[i].state == 1 && g_db.slots[i].key_len == klen && memcmp(g_db.slots[i].key, key, klen) == 0)
        {
            j = (int)i;
        }
    }
    TEST_ASSERT_TRUE(j >= 0);
    const uint64_t h = g_db.slots[j].hash;
    const int j1 = (j + 1) % PROTOCORE_DBM_SLOTS;
    const int j2 = (j + 2) % PROTOCORE_DBM_SLOTS;
    TEST_ASSERT_EQUAL_UINT8(0, g_db.slots[j1].state);
    TEST_ASSERT_EQUAL_UINT8(0, g_db.slots[j2].state);

    g_db.slots[j].key_len = (uint16_t)(klen + 1);
    memcpy(g_db.slots[j].key, "collide-me!", klen + 1);
    g_db.slots[j1] = g_db.slots[j];
    g_db.slots[j1].key_len = klen;
    memcpy(g_db.slots[j1].key, "collide-mE", klen);
    g_db.slots[j1].hash = h;
    g_db.count = 2;

    TEST_ASSERT_FALSE(protocore_dbm_contains(&g_db, key, klen));

    TEST_ASSERT_TRUE(put_s(key, "v2"));
    TEST_ASSERT_EQUAL_UINT8(1, g_db.slots[j2].state);
    TEST_ASSERT_EQUAL_UINT16(klen + 1, g_db.slots[j].key_len);
    TEST_ASSERT_EQUAL_INT(0, memcmp(g_db.slots[j1].key, "collide-mE", klen));
    TEST_ASSERT_TRUE(get_eq(key, "v2"));
    TEST_ASSERT_EQUAL_UINT32(3, protocore_dbm_count(&g_db));
}

void test_put_rejects_an_empty_key(void)
{

    fresh();
    TEST_ASSERT_FALSE(protocore_dbm_put(&g_db, "", 0, (const uint8_t *)"v", 1));
    TEST_ASSERT_EQUAL_UINT32(0, protocore_dbm_count(&g_db));
}

void test_put_fails_closed_when_the_log_is_full(void)
{

    fresh_sized(1024);
    uint8_t val[200];
    memset(val, 'V', sizeof(val));
    char k[8];
    int stored = 0;
    for (int i = 0; i < 32; i++)
    {
        snprintf(k, sizeof(k), "k%02d", i);
        if (!protocore_dbm_put(&g_db, k, (uint16_t)strlen(k), val, sizeof(val)))
        {
            break;
        }
        stored++;
    }
    TEST_ASSERT_TRUE(stored > 0);
    TEST_ASSERT_TRUE(stored < 32);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)stored, protocore_dbm_count(&g_db));

    snprintf(k, sizeof(k), "k%02d", stored);
    TEST_ASSERT_FALSE(protocore_dbm_contains(&g_db, k, (uint16_t)strlen(k)));
    uint8_t out[256];
    TEST_ASSERT_EQUAL_INT((long)sizeof(val), protocore_dbm_get(&g_db, "k00", 3, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(val, out, sizeof(val));
}

void test_get_fails_when_the_value_cannot_be_read_back(void)
{

    fresh();
    TEST_ASSERT_TRUE(put_s("v", "payload"));
    uint8_t out[16];
    g_fail_read = PROTO_TRUE;
    TEST_ASSERT_EQUAL_INT(-1, protocore_dbm_get(&g_db, "v", 1, out, sizeof(out)));
    g_fail_read = PROTO_FALSE;
    TEST_ASSERT_TRUE(get_eq("v", "payload"));
}

typedef struct
{
    int seen;
    int stop_after;
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

    fresh();
    put_s("a", "1");
    put_s("b", "2");
    put_s("c", "3");
    TEST_ASSERT_TRUE(protocore_dbm_del(&g_db, "b", 1));

    TEST_ASSERT_EQUAL_UINT32(2, protocore_dbm_iterate(&g_db, NULL, NULL));

    IterCtx all = {0, 0};
    TEST_ASSERT_EQUAL_UINT32(2, protocore_dbm_iterate(&g_db, iter_cb, &all));
    TEST_ASSERT_EQUAL_INT(2, all.seen);

    IterCtx one = {0, 1};
    TEST_ASSERT_EQUAL_UINT32(1, protocore_dbm_iterate(&g_db, iter_cb, &one));
    TEST_ASSERT_EQUAL_INT(1, one.seen);
}

void test_compact_carries_empty_values(void)
{

    fresh();
    TEST_ASSERT_TRUE(put_s("a", "1"));
    TEST_ASSERT_TRUE(protocore_dbm_put(&g_db, "empty", 5, NULL, 0));

    WalStore *dst = fresh_dest(sizeof(g_disk2));
    TEST_ASSERT_TRUE(protocore_dbm_compact(&g_db, dst));
    TEST_ASSERT_EQUAL_UINT32(2, protocore_dbm_count(&g_db));
    TEST_ASSERT_TRUE(get_eq("a", "1"));
    uint8_t b[4];
    TEST_ASSERT_EQUAL_INT(0, protocore_dbm_get(&g_db, "empty", 5, b, sizeof(b)));
    TEST_ASSERT_TRUE(protocore_dbm_contains(&g_db, "empty", 5));
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
