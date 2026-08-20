// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "network_drivers/presentation/http/httpcache/httpcache.h"
#include "server/web/edge_cache/edge_cache/edge_cache.h"
#include "server/web/edge_cache/edge_cache_sd/edge_cache_sd.h"
#include "services/storage/dbm/dbm.h"
#include "services/storage/wal/wal_store/wal_store.h"
#include "shared/http_date/http_date.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <unity.h>

static uint8_t edge_cache_work[16]; // the borrow an entry takes; EdgeCache never reads it

static uint8_t edge_cache_sd_work[16]; // the borrow an entry takes; EdgeCacheSd never reads it

static uint8_t tw[4096];

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
static size_t ram_read(void *ctx, uint64_t off, uint8_t *buf, size_t len)
{
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
    return PROTO_TRUE;
}

static uint8_t g_disk[128 * 1024];
static RamDisk g_d;
static WalDev g_dev;
static WalStore g_wal;
static protocore_dbm g_db;
static uint8_t g_scratch[PROTOCORE_EDGE_SD_VALUE_MAX];

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

static void mkcanon(char *out, size_t cap, const char *path)
{
    snprintf(out, cap, "GET\nexample.com\n%s", path);
}

static void fill_entry(EdgeEntry *e, const char *canon, const char *etag, const uint8_t *body, uint16_t body_len)
{
    memset(e, 0, sizeof(*e));
    strncpy(e->key, canon, sizeof(e->key) - 1);
    EdgeCacheV.key_digest_args.digest_work = tw;
    EdgeCacheV.key_digest_args.canon = e->key;
    EdgeCacheV.key_digest_args.len = strlen(e->key);
    EdgeCacheV.key_digest_args.digest = e->digest;
    EdgeCache.key_digest(edge_cache_work);
    e->status = 200;
    strncpy(e->content_type, "text/plain", sizeof(e->content_type) - 1);
    strncpy(e->etag, etag, sizeof(e->etag) - 1);
    if (body && body_len)
    {
        memcpy(e->body, body, body_len);
    }
    e->body_len = body_len;
}

static proto_bool digest_eq(const uint8_t a[32], const uint8_t b[32])
{
    return memcmp(a, b, 32) == 0;
}

void test_serialize_roundtrip_all_fields(void)
{
    EdgeEntry in;
    memset(&in, 0, sizeof(in));
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/img.png?w=64");
    strncpy(in.key, canon, sizeof(in.key) - 1);
    EdgeCacheV.key_digest_args.digest_work = tw;
    EdgeCacheV.key_digest_args.canon = in.key;
    EdgeCacheV.key_digest_args.len = strlen(in.key);
    EdgeCacheV.key_digest_args.digest = in.digest;
    EdgeCache.key_digest(edge_cache_work);
    in.status = 200;
    strncpy(in.content_type, "image/png", sizeof(in.content_type) - 1);
    strncpy(in.etag, "\"v1-abc\"", sizeof(in.etag) - 1);
    strncpy(in.last_modified, "Wed, 01 Jan 2025 00:00:00 GMT", sizeof(in.last_modified) - 1);
    strncpy(in.content_encoding, "gzip", sizeof(in.content_encoding) - 1);
    strncpy(in.vary_names, "Accept-Encoding", sizeof(in.vary_names) - 1);
    strncpy(in.vary_vals, "Accept-Encoding\x1egzip\x1f", sizeof(in.vary_vals) - 1);
    uint8_t body[300];
    for (int i = 0; i < 300; i++)
    {
        body[i] = (uint8_t)(i * 7 + 3);
    }
    memcpy(in.body, body, sizeof(body));
    in.body_len = sizeof(body);

    EdgeCacheSdV.serialize_args.e = &in;
    EdgeCacheSdV.serialize_args.out = g_scratch;
    EdgeCacheSdV.serialize_args.cap = sizeof(g_scratch);
    EdgeCacheSd.serialize(edge_cache_sd_work);
    size_t n = EdgeCacheSdV.n;
    TEST_ASSERT_TRUE(n > 0);

    EdgeEntry out;
    memset(&out, 0xEE, sizeof(out));
    EdgeCacheSdV.deserialize_args.entry_buf = tw;
    EdgeCacheSdV.deserialize_args.buf = g_scratch;
    EdgeCacheSdV.deserialize_args.len = n;
    EdgeCacheSdV.deserialize_args.e = &out;
    EdgeCacheSd.deserialize(edge_cache_sd_work);
    TEST_ASSERT_TRUE(EdgeCacheSdV.ok);

    TEST_ASSERT_EQUAL_STRING(in.key, out.key);
    TEST_ASSERT_EQUAL_INT(in.status, out.status);
    TEST_ASSERT_EQUAL_STRING(in.content_type, out.content_type);
    TEST_ASSERT_EQUAL_STRING(in.etag, out.etag);
    TEST_ASSERT_EQUAL_STRING(in.last_modified, out.last_modified);
    TEST_ASSERT_EQUAL_STRING(in.content_encoding, out.content_encoding);
    TEST_ASSERT_EQUAL_STRING(in.vary_names, out.vary_names);
    TEST_ASSERT_EQUAL_STRING(in.vary_vals, out.vary_vals);
    TEST_ASSERT_EQUAL_UINT16(in.body_len, out.body_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(in.body, out.body, in.body_len);
    TEST_ASSERT_TRUE(digest_eq(in.digest, out.digest));
}

void test_serialize_max_body(void)
{
    EdgeEntry in;
    uint8_t body[PROTOCORE_EDGE_BODY_MAX];
    for (int i = 0; i < PROTOCORE_EDGE_BODY_MAX; i++)
    {
        body[i] = (uint8_t)(i * 131 + 17);
    }
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/big.bin");
    fill_entry(&in, canon, "\"big\"", body, PROTOCORE_EDGE_BODY_MAX);

    EdgeCacheSdV.serialize_args.e = &in;
    EdgeCacheSdV.serialize_args.out = g_scratch;
    EdgeCacheSdV.serialize_args.cap = sizeof(g_scratch);
    EdgeCacheSd.serialize(edge_cache_sd_work);
    size_t n = EdgeCacheSdV.n;
    TEST_ASSERT_TRUE(n > 0);
    EdgeEntry out;
    EdgeCacheSdV.deserialize_args.entry_buf = tw;
    EdgeCacheSdV.deserialize_args.buf = g_scratch;
    EdgeCacheSdV.deserialize_args.len = n;
    EdgeCacheSdV.deserialize_args.e = &out;
    EdgeCacheSd.deserialize(edge_cache_sd_work);
    TEST_ASSERT_TRUE(EdgeCacheSdV.ok);
    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_EDGE_BODY_MAX, out.body_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(body, out.body, PROTOCORE_EDGE_BODY_MAX);
}

void test_serialize_too_small_scratch_fails(void)
{
    EdgeEntry in;
    uint8_t body[300];
    memset(body, 'x', sizeof(body));
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/x");
    fill_entry(&in, canon, "\"e\"", body, sizeof(body));
    uint8_t tiny[16];
    EdgeCacheSdV.serialize_args.e = &in;
    EdgeCacheSdV.serialize_args.out = tiny;
    EdgeCacheSdV.serialize_args.cap = sizeof(tiny);
    EdgeCacheSd.serialize(edge_cache_sd_work);
    TEST_ASSERT_EQUAL_UINT(0, EdgeCacheSdV.n);
}

void test_deserialize_corrupt_fails_closed(void)
{
    EdgeEntry in;
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/y");
    fill_entry(&in, canon, "\"e\"", (const uint8_t *)"hello", 5);
    EdgeCacheSdV.serialize_args.e = &in;
    EdgeCacheSdV.serialize_args.out = g_scratch;
    EdgeCacheSdV.serialize_args.cap = sizeof(g_scratch);
    EdgeCacheSd.serialize(edge_cache_sd_work);
    size_t n = EdgeCacheSdV.n;
    TEST_ASSERT_TRUE(n > 0);

    EdgeEntry out;
    uint8_t bad = g_scratch[0];
    g_scratch[0] = 0x42;
    EdgeCacheSdV.deserialize_args.entry_buf = tw;
    EdgeCacheSdV.deserialize_args.buf = g_scratch;
    EdgeCacheSdV.deserialize_args.len = n;
    EdgeCacheSdV.deserialize_args.e = &out;
    EdgeCacheSd.deserialize(edge_cache_sd_work);
    TEST_ASSERT_FALSE(EdgeCacheSdV.ok);
    g_scratch[0] = bad;
    EdgeCacheSdV.deserialize_args.entry_buf = tw;
    EdgeCacheSdV.deserialize_args.buf = g_scratch;
    EdgeCacheSdV.deserialize_args.len = 2;
    EdgeCacheSdV.deserialize_args.e = &out;
    EdgeCacheSd.deserialize(edge_cache_sd_work);
    TEST_ASSERT_FALSE(EdgeCacheSdV.ok);
    EdgeCacheSdV.deserialize_args.entry_buf = tw;
    EdgeCacheSdV.deserialize_args.buf = g_scratch;
    EdgeCacheSdV.deserialize_args.len = n - 3;
    EdgeCacheSdV.deserialize_args.e = &out;
    EdgeCacheSd.deserialize(edge_cache_sd_work);
    TEST_ASSERT_FALSE(EdgeCacheSdV.ok);
}

void test_put_get_roundtrip(void)
{
    fresh();
    EdgeEntry in;
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/a.txt");
    fill_entry(&in, canon, "\"a1\"", (const uint8_t *)"payload-A", 9);
    EdgeCacheSdV.put_args.db = &g_db;
    EdgeCacheSdV.put_args.e = &in;
    EdgeCacheSdV.put_args.scratch = g_scratch;
    EdgeCacheSdV.put_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.put(edge_cache_sd_work);
    TEST_ASSERT_TRUE(EdgeCacheSdV.ok);

    EdgeEntry out;
    memset(&out, 0, sizeof(out));
    EdgeCacheSdV.get_args.entry_buf = tw;
    EdgeCacheSdV.get_args.db = &g_db;
    EdgeCacheSdV.get_args.digest = in.digest;
    EdgeCacheSdV.get_args.e = &out;
    EdgeCacheSdV.get_args.scratch = g_scratch;
    EdgeCacheSdV.get_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.get(edge_cache_sd_work);
    TEST_ASSERT_TRUE(EdgeCacheSdV.ok);
    TEST_ASSERT_EQUAL_STRING(canon, out.key);
    TEST_ASSERT_EQUAL_STRING("\"a1\"", out.etag);
    TEST_ASSERT_EQUAL_UINT16(9, out.body_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("payload-A", out.body, 9);

    EdgeEntry in2;
    char c2[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(c2, sizeof(c2), "/cdn/never");
    fill_entry(&in2, c2, "\"n\"", (const uint8_t *)"x", 1);
    EdgeCacheSdV.get_args.entry_buf = tw;
    EdgeCacheSdV.get_args.db = &g_db;
    EdgeCacheSdV.get_args.digest = in2.digest;
    EdgeCacheSdV.get_args.e = &out;
    EdgeCacheSdV.get_args.scratch = g_scratch;
    EdgeCacheSdV.get_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.get(edge_cache_sd_work);
    TEST_ASSERT_FALSE(EdgeCacheSdV.ok);
}

void test_no_validator_not_spilled(void)
{
    fresh();
    EdgeEntry in;
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/novalidator");
    fill_entry(&in, canon, "", (const uint8_t *)"body", 4);
    in.last_modified[0] = '\0';
    EdgeCacheSdV.put_args.db = &g_db;
    EdgeCacheSdV.put_args.e = &in;
    EdgeCacheSdV.put_args.scratch = g_scratch;
    EdgeCacheSdV.put_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.put(edge_cache_sd_work);
    TEST_ASSERT_FALSE(EdgeCacheSdV.ok);

    EdgeEntry out;
    EdgeCacheSdV.get_args.entry_buf = tw;
    EdgeCacheSdV.get_args.db = &g_db;
    EdgeCacheSdV.get_args.digest = in.digest;
    EdgeCacheSdV.get_args.e = &out;
    EdgeCacheSdV.get_args.scratch = g_scratch;
    EdgeCacheSdV.get_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.get(edge_cache_sd_work);
    TEST_ASSERT_FALSE(EdgeCacheSdV.ok);
}

void test_oversize_body_stays_l1_only(void)
{
    fresh();

    EdgeEntry in;
    uint8_t body[PROTOCORE_EDGE_BODY_MAX];
    memset(body, 'Z', sizeof(body));
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/toobig");
    fill_entry(&in, canon, "\"big\"", body, PROTOCORE_EDGE_BODY_MAX);
    EdgeCacheSdV.serialize_args.e = &in;
    EdgeCacheSdV.serialize_args.out = g_scratch;
    EdgeCacheSdV.serialize_args.cap = sizeof(g_scratch);
    EdgeCacheSd.serialize(edge_cache_sd_work);
    size_t serialized = EdgeCacheSdV.n;
    TEST_ASSERT_TRUE(serialized > PROTOCORE_DBM_VAL_MAX);
    EdgeCacheSdV.put_args.db = &g_db;
    EdgeCacheSdV.put_args.e = &in;
    EdgeCacheSdV.put_args.scratch = g_scratch;
    EdgeCacheSdV.put_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.put(edge_cache_sd_work);
    TEST_ASSERT_FALSE(EdgeCacheSdV.ok);
}

static uint32_t g_spills = 0;
static void spill_cb(void *ctx, const EdgeEntry *v)
{
    (void)ctx;
    EdgeCacheSdV.put_args.db = (struct protocore_dbm *)ctx;
    EdgeCacheSdV.put_args.e = v;
    EdgeCacheSdV.put_args.scratch = g_scratch;
    EdgeCacheSdV.put_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.put(edge_cache_sd_work);
    if (EdgeCacheSdV.ok)
    {
        g_spills++;
    }
}

static EdgeEntry *store_mk(EdgeCacheStore *s, const char *path, const char *etag, const char *body)
{
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), path);
    EdgeCacheV.store_alloc_args.s = s;
    EdgeCacheV.store_alloc_args.canon = canon;
    EdgeCacheV.store_alloc_args.vary_key = "";
    EdgeCache.store_alloc(edge_cache_work);
    EdgeEntry *e = EdgeCacheV.entry;
    TEST_ASSERT_NOT_NULL(e);
    e->status = 200;
    strncpy(e->content_type, "text/plain", sizeof(e->content_type) - 1);
    strncpy(e->etag, etag, sizeof(e->etag) - 1);
    size_t bl = strlen(body);
    memcpy(e->body, body, bl);
    e->body_len = (uint16_t)bl;
    return e;
}

void test_spill_on_evict_and_promote(void)
{
    fresh();
    g_spills = 0;
    EdgeCacheStore store;
    EdgeCacheV.store_init_args.s = &store;
    EdgeCache.store_init(edge_cache_work);
    store.on_evict = spill_cb;
    store.evict_ctx = &g_db;

    char first_canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(first_canon, sizeof(first_canon), "/cdn/e0");
    uint8_t first_digest[32];
    EdgeCacheV.key_digest_args.digest_work = tw;
    EdgeCacheV.key_digest_args.canon = first_canon;
    EdgeCacheV.key_digest_args.len = strlen(first_canon);
    EdgeCacheV.key_digest_args.digest = first_digest;
    EdgeCache.key_digest(edge_cache_work);

    for (int i = 0; i < PROTOCORE_EDGE_CACHE_SLOTS; i++)
    {
        char path[24];
        snprintf(path, sizeof(path), "/cdn/e%d", i);
        char et[16];
        snprintf(et, sizeof(et), "\"e%d\"", i);
        store_mk(&store, path, et, "body");
    }
    TEST_ASSERT_EQUAL_UINT32(0, g_spills);
    store_mk(&store, "/cdn/eN", "\"eN\"", "body");
    TEST_ASSERT_EQUAL_UINT32(1, g_spills);

    EdgeEntry out;
    EdgeCacheSdV.get_args.entry_buf = tw;
    EdgeCacheSdV.get_args.db = &g_db;
    EdgeCacheSdV.get_args.digest = first_digest;
    EdgeCacheSdV.get_args.e = &out;
    EdgeCacheSdV.get_args.scratch = g_scratch;
    EdgeCacheSdV.get_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.get(edge_cache_sd_work);
    TEST_ASSERT_TRUE(EdgeCacheSdV.ok);
    TEST_ASSERT_EQUAL_STRING(first_canon, out.key);
    TEST_ASSERT_EQUAL_STRING("\"e0\"", out.etag);

    char last_canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(last_canon, sizeof(last_canon), "/cdn/eN");
    uint8_t last_digest[32];
    EdgeCacheV.key_digest_args.digest_work = tw;
    EdgeCacheV.key_digest_args.canon = last_canon;
    EdgeCacheV.key_digest_args.len = strlen(last_canon);
    EdgeCacheV.key_digest_args.digest = last_digest;
    EdgeCache.key_digest(edge_cache_work);
    EdgeCacheSdV.get_args.entry_buf = tw;
    EdgeCacheSdV.get_args.db = &g_db;
    EdgeCacheSdV.get_args.digest = last_digest;
    EdgeCacheSdV.get_args.e = &out;
    EdgeCacheSdV.get_args.scratch = g_scratch;
    EdgeCacheSdV.get_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.get(edge_cache_sd_work);
    TEST_ASSERT_FALSE(EdgeCacheSdV.ok);
}

void test_transient_entry_not_spilled(void)
{
    fresh();
    g_spills = 0;
    EdgeCacheStore store;
    EdgeCacheV.store_init_args.s = &store;
    EdgeCache.store_init(edge_cache_work);
    store.on_evict = spill_cb;
    store.evict_ctx = &g_db;

    for (int i = 0; i <= PROTOCORE_EDGE_CACHE_SLOTS; i++)
    {
        EdgeCacheV.store_alloc_args.s = &store;
        EdgeCacheV.store_alloc_args.canon = "";
        EdgeCacheV.store_alloc_args.vary_key = "";
        EdgeCache.store_alloc(edge_cache_work);
        EdgeEntry *e = EdgeCacheV.entry;
        TEST_ASSERT_NOT_NULL(e);
        e->body_len = 4;
        memcpy(e->body, "data", 4);
    }
    TEST_ASSERT_EQUAL_UINT32(0, g_spills);
}

void test_survives_reboot(void)
{
    fresh();
    EdgeEntry in;
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/persist");
    fill_entry(&in, canon, "\"p9\"", (const uint8_t *)"survive-me", 10);
    strncpy(in.last_modified, "Wed, 01 Jan 2025 00:00:00 GMT", sizeof(in.last_modified) - 1);
    EdgeCacheSdV.put_args.db = &g_db;
    EdgeCacheSdV.put_args.e = &in;
    EdgeCacheSdV.put_args.scratch = g_scratch;
    EdgeCacheSdV.put_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.put(edge_cache_sd_work);
    TEST_ASSERT_TRUE(EdgeCacheSdV.ok);
    TEST_ASSERT_TRUE(protocore_dbm_sync(&g_db));

    TEST_ASSERT_TRUE(reboot());
    EdgeEntry out;
    EdgeCacheSdV.get_args.entry_buf = tw;
    EdgeCacheSdV.get_args.db = &g_db;
    EdgeCacheSdV.get_args.digest = in.digest;
    EdgeCacheSdV.get_args.e = &out;
    EdgeCacheSdV.get_args.scratch = g_scratch;
    EdgeCacheSdV.get_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.get(edge_cache_sd_work);
    TEST_ASSERT_TRUE(EdgeCacheSdV.ok);
    TEST_ASSERT_EQUAL_STRING(canon, out.key);
    TEST_ASSERT_EQUAL_STRING("\"p9\"", out.etag);
    TEST_ASSERT_EQUAL_STRING("Wed, 01 Jan 2025 00:00:00 GMT", out.last_modified);
    TEST_ASSERT_EQUAL_UINT16(10, out.body_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("survive-me", out.body, 10);
}

void test_del(void)
{
    fresh();
    EdgeEntry in;
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/del");
    fill_entry(&in, canon, "\"d\"", (const uint8_t *)"gone", 4);
    EdgeCacheSdV.put_args.db = &g_db;
    EdgeCacheSdV.put_args.e = &in;
    EdgeCacheSdV.put_args.scratch = g_scratch;
    EdgeCacheSdV.put_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.put(edge_cache_sd_work);
    TEST_ASSERT_TRUE(EdgeCacheSdV.ok);
    EdgeCacheSdV.del_args.db = &g_db;
    EdgeCacheSdV.del_args.digest = in.digest;
    EdgeCacheSd.del(edge_cache_sd_work);
    TEST_ASSERT_TRUE(EdgeCacheSdV.ok);
    EdgeEntry out;
    EdgeCacheSdV.get_args.entry_buf = tw;
    EdgeCacheSdV.get_args.db = &g_db;
    EdgeCacheSdV.get_args.digest = in.digest;
    EdgeCacheSdV.get_args.e = &out;
    EdgeCacheSdV.get_args.scratch = g_scratch;
    EdgeCacheSdV.get_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.get(edge_cache_sd_work);
    TEST_ASSERT_FALSE(EdgeCacheSdV.ok);
    EdgeCacheSdV.del_args.db = &g_db;
    EdgeCacheSdV.del_args.digest = in.digest;
    EdgeCacheSd.del(edge_cache_sd_work);
    TEST_ASSERT_FALSE(EdgeCacheSdV.ok);
}

static void put_path(const char *path)
{
    EdgeEntry in;
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), path);
    fill_entry(&in, canon, "\"v\"", (const uint8_t *)"x", 1);
    EdgeCacheSdV.put_args.db = &g_db;
    EdgeCacheSdV.put_args.e = &in;
    EdgeCacheSdV.put_args.scratch = g_scratch;
    EdgeCacheSdV.put_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.put(edge_cache_sd_work);
    TEST_ASSERT_TRUE(EdgeCacheSdV.ok);
}
static proto_bool has_path(const char *path)
{
    EdgeEntry in, out;
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), path);
    fill_entry(&in, canon, "\"v\"", (const uint8_t *)"x", 1);
    EdgeCacheSdV.get_args.entry_buf = tw;
    EdgeCacheSdV.get_args.db = &g_db;
    EdgeCacheSdV.get_args.digest = in.digest;
    EdgeCacheSdV.get_args.e = &out;
    EdgeCacheSdV.get_args.scratch = g_scratch;
    EdgeCacheSdV.get_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.get(edge_cache_sd_work);
    return EdgeCacheSdV.ok;
}

void test_purge_prefix(void)
{
    fresh();
    put_path("/cdn/a");
    put_path("/cdn/b");
    put_path("/other/c");
    EdgeCacheSdV.purge_prefix_args.db = &g_db;
    EdgeCacheSdV.purge_prefix_args.path_prefix = "/cdn/";
    EdgeCacheSdV.purge_prefix_args.scratch = g_scratch;
    EdgeCacheSdV.purge_prefix_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.purge_prefix(edge_cache_sd_work);
    TEST_ASSERT_EQUAL_UINT32(2, EdgeCacheSdV.count);
    TEST_ASSERT_FALSE(has_path("/cdn/a"));
    TEST_ASSERT_FALSE(has_path("/cdn/b"));
    TEST_ASSERT_TRUE(has_path("/other/c"));
}

void test_purge_prefix_multipass(void)
{
    fresh();

    const int N = 20;
    char path[24];
    for (int i = 0; i < N; i++)
    {
        snprintf(path, sizeof(path), "/cdn/p%d", i);
        put_path(path);
    }
    put_path("/keep/one");
    EdgeCacheSdV.purge_prefix_args.db = &g_db;
    EdgeCacheSdV.purge_prefix_args.path_prefix = "/cdn/";
    EdgeCacheSdV.purge_prefix_args.scratch = g_scratch;
    EdgeCacheSdV.purge_prefix_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.purge_prefix(edge_cache_sd_work);
    TEST_ASSERT_EQUAL_UINT32((uint32_t)N, EdgeCacheSdV.count);
    for (int i = 0; i < N; i++)
    {
        snprintf(path, sizeof(path), "/cdn/p%d", i);
        TEST_ASSERT_FALSE(has_path(path));
    }
    TEST_ASSERT_TRUE(has_path("/keep/one"));
}

void test_purge_all(void)
{
    fresh();
    put_path("/cdn/a");
    put_path("/cdn/b");
    put_path("/x/y");
    EdgeCacheSdV.purge_all_args.db = &g_db;
    EdgeCacheSd.purge_all(edge_cache_sd_work);
    TEST_ASSERT_EQUAL_UINT32(3, EdgeCacheSdV.count);
    TEST_ASSERT_FALSE(has_path("/cdn/a"));
    TEST_ASSERT_FALSE(has_path("/x/y"));
}

void test_shared_dbm_foreign_value_untouched(void)
{
    fresh();

    uint8_t foreign_key[32];
    memset(foreign_key, 0xA5, sizeof(foreign_key));
    uint8_t foreign_val[16];
    memset(foreign_val, 0xFF, sizeof(foreign_val));
    TEST_ASSERT_TRUE(protocore_dbm_put(&g_db, (const char *)foreign_key, 32, foreign_val, sizeof(foreign_val)));
    put_path("/cdn/mine");

    EdgeCacheSdV.purge_all_args.db = &g_db;
    EdgeCacheSd.purge_all(edge_cache_sd_work);
    TEST_ASSERT_EQUAL_UINT32(1, EdgeCacheSdV.count);
    TEST_ASSERT_FALSE(has_path("/cdn/mine"));
    uint8_t out[16];
    TEST_ASSERT_EQUAL_INT(16, protocore_dbm_get(&g_db, (const char *)foreign_key, 32, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(foreign_val, out, 16);
}

void test_serialize_null_guards_and_every_overflow_point(void)
{
    EdgeEntry in;
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/caps");
    fill_entry(&in, canon, "\"c\"", (const uint8_t *)"hello", 5);
    strncpy(in.last_modified, "Wed, 01 Jan 2025 00:00:00 GMT", sizeof(in.last_modified) - 1);
    strncpy(in.content_encoding, "gzip", sizeof(in.content_encoding) - 1);
    strncpy(in.vary_names, "Accept-Encoding", sizeof(in.vary_names) - 1);
    strncpy(in.vary_vals,
            "accept-encoding\x1e"
            "gzip\x1f",
            sizeof(in.vary_vals) - 1);

    EdgeCacheSdV.serialize_args.e = NULL;
    EdgeCacheSdV.serialize_args.out = g_scratch;
    EdgeCacheSdV.serialize_args.cap = sizeof(g_scratch);
    EdgeCacheSd.serialize(edge_cache_sd_work);
    TEST_ASSERT_EQUAL_UINT(0, EdgeCacheSdV.n);
    EdgeCacheSdV.serialize_args.e = &in;
    EdgeCacheSdV.serialize_args.out = NULL;
    EdgeCacheSdV.serialize_args.cap = sizeof(g_scratch);
    EdgeCacheSd.serialize(edge_cache_sd_work);
    TEST_ASSERT_EQUAL_UINT(0, EdgeCacheSdV.n);
    EdgeCacheSdV.serialize_args.e = &in;
    EdgeCacheSdV.serialize_args.out = g_scratch;
    EdgeCacheSdV.serialize_args.cap = 2;
    EdgeCacheSd.serialize(edge_cache_sd_work);
    TEST_ASSERT_EQUAL_UINT(0, EdgeCacheSdV.n);

    EdgeCacheSdV.serialize_args.e = &in;
    EdgeCacheSdV.serialize_args.out = g_scratch;
    EdgeCacheSdV.serialize_args.cap = sizeof(g_scratch);
    EdgeCacheSd.serialize(edge_cache_sd_work);
    size_t n = EdgeCacheSdV.n;
    TEST_ASSERT_TRUE(n > 3);

    for (size_t cap = 3; cap < n; cap++)
    {
        EdgeCacheSdV.serialize_args.e = &in;
        EdgeCacheSdV.serialize_args.out = g_scratch;
        EdgeCacheSdV.serialize_args.cap = cap;
        EdgeCacheSd.serialize(edge_cache_sd_work);
        TEST_ASSERT_EQUAL_UINT(0, EdgeCacheSdV.n);
    }
    EdgeCacheSdV.serialize_args.e = &in;
    EdgeCacheSdV.serialize_args.out = g_scratch;
    EdgeCacheSdV.serialize_args.cap = n;
    EdgeCacheSd.serialize(edge_cache_sd_work);
    TEST_ASSERT_EQUAL_UINT(n, EdgeCacheSdV.n);
}

void test_deserialize_null_guards_and_every_truncation(void)
{
    EdgeEntry in;
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/trunc");
    fill_entry(&in, canon, "\"t\"", (const uint8_t *)"body-bytes", 10);
    strncpy(in.last_modified, "Wed, 01 Jan 2025 00:00:00 GMT", sizeof(in.last_modified) - 1);
    strncpy(in.content_encoding, "br", sizeof(in.content_encoding) - 1);
    strncpy(in.vary_names, "Accept-Encoding", sizeof(in.vary_names) - 1);
    strncpy(in.vary_vals,
            "accept-encoding\x1e"
            "br\x1f",
            sizeof(in.vary_vals) - 1);
    EdgeCacheSdV.serialize_args.e = &in;
    EdgeCacheSdV.serialize_args.out = g_scratch;
    EdgeCacheSdV.serialize_args.cap = sizeof(g_scratch);
    EdgeCacheSd.serialize(edge_cache_sd_work);
    size_t n = EdgeCacheSdV.n;
    TEST_ASSERT_TRUE(n > 0);

    EdgeEntry out;
    memset(&out, 0, sizeof(out));
    EdgeCacheSdV.deserialize_args.entry_buf = tw;
    EdgeCacheSdV.deserialize_args.buf = NULL;
    EdgeCacheSdV.deserialize_args.len = n;
    EdgeCacheSdV.deserialize_args.e = &out;
    EdgeCacheSd.deserialize(edge_cache_sd_work);
    TEST_ASSERT_FALSE(EdgeCacheSdV.ok);
    EdgeCacheSdV.deserialize_args.entry_buf = tw;
    EdgeCacheSdV.deserialize_args.buf = g_scratch;
    EdgeCacheSdV.deserialize_args.len = n;
    EdgeCacheSdV.deserialize_args.e = NULL;
    EdgeCacheSd.deserialize(edge_cache_sd_work);
    TEST_ASSERT_FALSE(EdgeCacheSdV.ok);

    for (size_t l = 0; l < n; l++)
    {
        EdgeCacheSdV.deserialize_args.entry_buf = tw;
        EdgeCacheSdV.deserialize_args.buf = g_scratch;
        EdgeCacheSdV.deserialize_args.len = l;
        EdgeCacheSdV.deserialize_args.e = &out;
        EdgeCacheSd.deserialize(edge_cache_sd_work);
        TEST_ASSERT_FALSE(EdgeCacheSdV.ok);
    }
    EdgeCacheSdV.deserialize_args.entry_buf = tw;
    EdgeCacheSdV.deserialize_args.buf = g_scratch;
    EdgeCacheSdV.deserialize_args.len = n;
    EdgeCacheSdV.deserialize_args.e = &out;
    EdgeCacheSd.deserialize(edge_cache_sd_work);
    TEST_ASSERT_TRUE(EdgeCacheSdV.ok);
    TEST_ASSERT_EQUAL_STRING(canon, out.key);
}

void test_deserialize_rejects_field_longer_than_its_slot(void)
{

    uint8_t buf[512];
    memset(buf, 'k', sizeof(buf));
    buf[0] = 1;
    buf[1] = 200;
    buf[2] = 0;
    buf[3] = (uint8_t)(PROTOCORE_EDGE_KEY_MAX & 0xFF);
    buf[4] = (uint8_t)(PROTOCORE_EDGE_KEY_MAX >> 8);
    EdgeEntry out;
    memset(&out, 0, sizeof(out));
    EdgeCacheSdV.deserialize_args.entry_buf = tw;
    EdgeCacheSdV.deserialize_args.buf = buf;
    EdgeCacheSdV.deserialize_args.len = sizeof(buf);
    EdgeCacheSdV.deserialize_args.e = &out;
    EdgeCacheSd.deserialize(edge_cache_sd_work);
    TEST_ASSERT_FALSE(EdgeCacheSdV.ok);
}

void test_deserialize_rejects_oversize_body_length(void)
{
    EdgeEntry in;
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/blen");
    fill_entry(&in, canon, "\"b\"", (const uint8_t *)"z", 1);
    EdgeCacheSdV.serialize_args.e = &in;
    EdgeCacheSdV.serialize_args.out = g_scratch;
    EdgeCacheSdV.serialize_args.cap = sizeof(g_scratch);
    EdgeCacheSd.serialize(edge_cache_sd_work);
    size_t n = EdgeCacheSdV.n;
    TEST_ASSERT_TRUE(n > 3);

    g_scratch[n - 3] = 0xFF;
    g_scratch[n - 2] = 0xFF;
    EdgeEntry out;
    memset(&out, 0, sizeof(out));
    EdgeCacheSdV.deserialize_args.entry_buf = tw;
    EdgeCacheSdV.deserialize_args.buf = g_scratch;
    EdgeCacheSdV.deserialize_args.len = n;
    EdgeCacheSdV.deserialize_args.e = &out;
    EdgeCacheSd.deserialize(edge_cache_sd_work);
    TEST_ASSERT_FALSE(EdgeCacheSdV.ok);
}

void test_dbm_api_null_guards(void)
{
    fresh();
    EdgeEntry in;
    EdgeEntry out;
    memset(&out, 0, sizeof(out));
    char canon[PROTOCORE_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/guards");
    fill_entry(&in, canon, "\"g\"", (const uint8_t *)"v", 1);

    EdgeCacheSdV.put_args.db = NULL;
    EdgeCacheSdV.put_args.e = &in;
    EdgeCacheSdV.put_args.scratch = g_scratch;
    EdgeCacheSdV.put_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.put(edge_cache_sd_work);
    TEST_ASSERT_FALSE(EdgeCacheSdV.ok);
    EdgeCacheSdV.put_args.db = &g_db;
    EdgeCacheSdV.put_args.e = NULL;
    EdgeCacheSdV.put_args.scratch = g_scratch;
    EdgeCacheSdV.put_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.put(edge_cache_sd_work);
    TEST_ASSERT_FALSE(EdgeCacheSdV.ok);
    EdgeCacheSdV.put_args.db = &g_db;
    EdgeCacheSdV.put_args.e = &in;
    EdgeCacheSdV.put_args.scratch = NULL;
    EdgeCacheSdV.put_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.put(edge_cache_sd_work);
    TEST_ASSERT_FALSE(EdgeCacheSdV.ok);
    EdgeCacheSdV.put_args.db = &g_db;
    EdgeCacheSdV.put_args.e = &in;
    EdgeCacheSdV.put_args.scratch = g_scratch;
    EdgeCacheSdV.put_args.scratch_cap = 8;
    EdgeCacheSd.put(edge_cache_sd_work);
    TEST_ASSERT_FALSE(EdgeCacheSdV.ok);

    EdgeCacheSdV.get_args.entry_buf = tw;
    EdgeCacheSdV.get_args.db = NULL;
    EdgeCacheSdV.get_args.digest = in.digest;
    EdgeCacheSdV.get_args.e = &out;
    EdgeCacheSdV.get_args.scratch = g_scratch;
    EdgeCacheSdV.get_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.get(edge_cache_sd_work);
    TEST_ASSERT_FALSE(EdgeCacheSdV.ok);
    EdgeCacheSdV.get_args.entry_buf = tw;
    EdgeCacheSdV.get_args.db = &g_db;
    EdgeCacheSdV.get_args.digest = NULL;
    EdgeCacheSdV.get_args.e = &out;
    EdgeCacheSdV.get_args.scratch = g_scratch;
    EdgeCacheSdV.get_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.get(edge_cache_sd_work);
    TEST_ASSERT_FALSE(EdgeCacheSdV.ok);
    EdgeCacheSdV.get_args.entry_buf = tw;
    EdgeCacheSdV.get_args.db = &g_db;
    EdgeCacheSdV.get_args.digest = in.digest;
    EdgeCacheSdV.get_args.e = NULL;
    EdgeCacheSdV.get_args.scratch = g_scratch;
    EdgeCacheSdV.get_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.get(edge_cache_sd_work);
    TEST_ASSERT_FALSE(EdgeCacheSdV.ok);
    EdgeCacheSdV.get_args.entry_buf = tw;
    EdgeCacheSdV.get_args.db = &g_db;
    EdgeCacheSdV.get_args.digest = in.digest;
    EdgeCacheSdV.get_args.e = &out;
    EdgeCacheSdV.get_args.scratch = NULL;
    EdgeCacheSdV.get_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.get(edge_cache_sd_work);
    TEST_ASSERT_FALSE(EdgeCacheSdV.ok);

    EdgeCacheSdV.del_args.db = NULL;
    EdgeCacheSdV.del_args.digest = in.digest;
    EdgeCacheSd.del(edge_cache_sd_work);
    TEST_ASSERT_FALSE(EdgeCacheSdV.ok);
    EdgeCacheSdV.del_args.db = &g_db;
    EdgeCacheSdV.del_args.digest = NULL;
    EdgeCacheSd.del(edge_cache_sd_work);
    TEST_ASSERT_FALSE(EdgeCacheSdV.ok);

    EdgeCacheSdV.purge_prefix_args.db = NULL;
    EdgeCacheSdV.purge_prefix_args.path_prefix = "/cdn/";
    EdgeCacheSdV.purge_prefix_args.scratch = g_scratch;
    EdgeCacheSdV.purge_prefix_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.purge_prefix(edge_cache_sd_work);
    TEST_ASSERT_EQUAL_UINT32(0, EdgeCacheSdV.count);
    EdgeCacheSdV.purge_prefix_args.db = &g_db;
    EdgeCacheSdV.purge_prefix_args.path_prefix = NULL;
    EdgeCacheSdV.purge_prefix_args.scratch = g_scratch;
    EdgeCacheSdV.purge_prefix_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.purge_prefix(edge_cache_sd_work);
    TEST_ASSERT_EQUAL_UINT32(0, EdgeCacheSdV.count);
    EdgeCacheSdV.purge_prefix_args.db = &g_db;
    EdgeCacheSdV.purge_prefix_args.path_prefix = "/cdn/";
    EdgeCacheSdV.purge_prefix_args.scratch = NULL;
    EdgeCacheSdV.purge_prefix_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.purge_prefix(edge_cache_sd_work);
    TEST_ASSERT_EQUAL_UINT32(0, EdgeCacheSdV.count);
    EdgeCacheSdV.purge_all_args.db = NULL;
    EdgeCacheSd.purge_all(edge_cache_sd_work);
    TEST_ASSERT_EQUAL_UINT32(0, EdgeCacheSdV.count);
}

void test_purge_skips_foreign_and_unreadable_records(void)
{
    fresh();

    TEST_ASSERT_TRUE(protocore_dbm_put(&g_db, "short-key", 9, (const uint8_t *)"x", 1));

    uint8_t empty_key[32];
    memset(empty_key, 0x11, sizeof(empty_key));
    TEST_ASSERT_TRUE(protocore_dbm_put(&g_db, (const char *)empty_key, 32, NULL, 0));

    uint8_t stub_key[32];
    memset(stub_key, 0x22, sizeof(stub_key));
    uint8_t stub[2] = {1, 0};
    TEST_ASSERT_TRUE(protocore_dbm_put(&g_db, (const char *)stub_key, 32, stub, sizeof(stub)));

    put_path("/cdn/real");
    EdgeCacheSdV.purge_all_args.db = &g_db;
    EdgeCacheSd.purge_all(edge_cache_sd_work);
    TEST_ASSERT_EQUAL_UINT32(1, EdgeCacheSdV.count);

    uint8_t v[8];
    TEST_ASSERT_EQUAL_INT(1, protocore_dbm_get(&g_db, "short-key", 9, v, sizeof(v)));
    TEST_ASSERT_EQUAL_INT(0, protocore_dbm_get(&g_db, (const char *)empty_key, 32, v, sizeof(v)));
    TEST_ASSERT_EQUAL_INT(2, protocore_dbm_get(&g_db, (const char *)stub_key, 32, v, sizeof(v)));
}

void test_purge_prefix_skips_key_without_a_path(void)
{
    fresh();

    EdgeEntry odd;
    memset(&odd, 0, sizeof(odd));
    strncpy(odd.key, "malformed-key", sizeof(odd.key) - 1);
    EdgeCacheV.key_digest_args.digest_work = tw;
    EdgeCacheV.key_digest_args.canon = odd.key;
    EdgeCacheV.key_digest_args.len = strlen(odd.key);
    EdgeCacheV.key_digest_args.digest = odd.digest;
    EdgeCache.key_digest(edge_cache_work);
    odd.status = 200;
    strncpy(odd.etag, "\"o\"", sizeof(odd.etag) - 1);
    memcpy(odd.body, "x", 1);
    odd.body_len = 1;
    EdgeCacheSdV.put_args.db = &g_db;
    EdgeCacheSdV.put_args.e = &odd;
    EdgeCacheSdV.put_args.scratch = g_scratch;
    EdgeCacheSdV.put_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.put(edge_cache_sd_work);
    TEST_ASSERT_TRUE(EdgeCacheSdV.ok);
    put_path("/cdn/keepme");

    EdgeCacheSdV.purge_prefix_args.db = &g_db;
    EdgeCacheSdV.purge_prefix_args.path_prefix = "malformed";
    EdgeCacheSdV.purge_prefix_args.scratch = g_scratch;
    EdgeCacheSdV.purge_prefix_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.purge_prefix(edge_cache_sd_work);
    TEST_ASSERT_EQUAL_UINT32(0, EdgeCacheSdV.count);
    EdgeEntry out;
    memset(&out, 0, sizeof(out));
    EdgeCacheSdV.get_args.entry_buf = tw;
    EdgeCacheSdV.get_args.db = &g_db;
    EdgeCacheSdV.get_args.digest = odd.digest;
    EdgeCacheSdV.get_args.e = &out;
    EdgeCacheSdV.get_args.scratch = g_scratch;
    EdgeCacheSdV.get_args.scratch_cap = sizeof(g_scratch);
    EdgeCacheSd.get(edge_cache_sd_work);
    TEST_ASSERT_TRUE(EdgeCacheSdV.ok);
    TEST_ASSERT_TRUE(has_path("/cdn/keepme"));

    EdgeCacheSdV.purge_all_args.db = &g_db;
    EdgeCacheSd.purge_all(edge_cache_sd_work);
    TEST_ASSERT_EQUAL_UINT32(2, EdgeCacheSdV.count);
}

void test_purge_counts_only_the_deletes_that_were_logged(void)
{

    fresh_sized(1024);
    put_path("/cdn/f0");
    put_path("/cdn/f1");
    const uint32_t before = protocore_dbm_count(&g_db);
    TEST_ASSERT_EQUAL_UINT32(2, before);

    const uint64_t DBM_PAYLOAD_HDR = 1 + 2 + 4;
    const uint64_t TOMB = WAL_RECORD_HEADER + DBM_PAYLOAD_HDR + 32;
    const char pad_key_fmt[] = "pad%d";
    const uint64_t PAD_REC_HDR = WAL_RECORD_HEADER + DBM_PAYLOAD_HDR + 4;
    const uint64_t leave = 2 * TOMB - 1;

    uint8_t pad[PROTOCORE_DBM_VAL_MAX];
    memset(pad, 'P', sizeof(pad));
    uint64_t room = protocore_wal_store_capacity(&g_wal) - protocore_wal_store_used(&g_wal);
    TEST_ASSERT_TRUE(room > leave + PAD_REC_HDR);
    for (int i = 0; room > leave; i++)
    {
        TEST_ASSERT_TRUE(room - leave >= PAD_REC_HDR);
        uint64_t want = room - leave - PAD_REC_HDR;
        uint32_t vlen = want > (uint64_t)PROTOCORE_DBM_VAL_MAX ? (uint32_t)PROTOCORE_DBM_VAL_MAX : (uint32_t)want;
        char key[8];
        snprintf(key, sizeof(key), pad_key_fmt, i);
        TEST_ASSERT_EQUAL_UINT(4, strlen(key));
        TEST_ASSERT_TRUE(protocore_dbm_put(&g_db, key, 4, pad, vlen));
        room = protocore_wal_store_capacity(&g_wal) - protocore_wal_store_used(&g_wal);
    }
    TEST_ASSERT_EQUAL_UINT64(leave, room);
    const uint32_t live = protocore_dbm_count(&g_db);

    EdgeCacheSdV.purge_all_args.db = &g_db;
    EdgeCacheSd.purge_all(edge_cache_sd_work);
    TEST_ASSERT_EQUAL_UINT32(1, EdgeCacheSdV.count);
    TEST_ASSERT_EQUAL_UINT32(live - 1, protocore_dbm_count(&g_db));
    TEST_ASSERT_TRUE(has_path("/cdn/f0") != has_path("/cdn/f1"));
}
