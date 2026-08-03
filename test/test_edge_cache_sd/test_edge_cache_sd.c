// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/web/edge_cache/edge_cache_sd: the CDN edge cache's L2 SD-persistence tier over a
// RAM-backed WalDev + dbm. Covers the entry <-> dbm-value serialization roundtrip (incl. a max body, Vary
// variants, and binary bodies), the spill/promote/del/purge API, the L1 store's on_evict write-back hook,
// storeability gates (no-validator and oversize entries stay L1-only), reboot survival (remount + reopen),
// and that a shared dbm's foreign values are never touched.
//
// "Reboot" is modeled by remounting a fresh WalStore + reopening the dbm over the same disk buffer.

#include "services/storage/dbm/dbm.h"
#include "services/storage/wal/wal_store.h"
#include "services/web/edge_cache/edge_cache.h"
#include "services/web/edge_cache/edge_cache_sd.h"
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

// --- RAM-backed WalDev (same shape as the dbm / pc_wal_store tests) ---------------------------------
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
static pc_dbm g_db;
static uint8_t g_scratch[PC_EDGE_SD_VALUE_MAX];

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
    TEST_ASSERT_TRUE(pc_wal_store_format(&g_wal, &g_dev));
    TEST_ASSERT_TRUE(pc_dbm_open(&g_db, &g_wal));
}
static void fresh(void)
{
    fresh_sized(sizeof(g_disk));
}
static proto_bool reboot(void)
{
    g_dev = dev_over(&g_d);
    if (!pc_wal_store_mount(&g_wal, &g_dev))
    {
        return PROTO_FALSE;
    }
    return pc_dbm_open(&g_db, &g_wal);
}

// --- entry construction helpers ------------------------------------------------------------------
static void mkcanon(char *out, size_t cap, const char *path)
{
    snprintf(out, cap, "GET\nexample.com\n%s", path);
}

// Fill a bare entry with a validator + body. Not linked to any store (fields only).
static void fill_entry(EdgeEntry *e, const char *canon, const char *etag, const uint8_t *body, uint16_t body_len)
{
    memset(e, 0, sizeof(*e));
    strncpy(e->key, canon, sizeof(e->key) - 1);
    edge_key_digest(e->key, strlen(e->key), e->digest);
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

// --- serialization ------------------------------------------------------------------------------
void test_serialize_roundtrip_all_fields(void)
{
    EdgeEntry in;
    memset(&in, 0, sizeof(in));
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/img.png?w=64");
    strncpy(in.key, canon, sizeof(in.key) - 1);
    edge_key_digest(in.key, strlen(in.key), in.digest);
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
        body[i] = (uint8_t)(i * 7 + 3); // includes NUL bytes and high bytes
    }
    memcpy(in.body, body, sizeof(body));
    in.body_len = sizeof(body);

    size_t n = edge_sd_serialize(&in, g_scratch, sizeof(g_scratch));
    TEST_ASSERT_TRUE(n > 0);

    EdgeEntry out;
    memset(&out, 0xEE, sizeof(out)); // poison, ensure deserialize writes what it should
    TEST_ASSERT_TRUE(edge_sd_deserialize(g_scratch, n, &out));

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
    TEST_ASSERT_TRUE(digest_eq(in.digest, out.digest)); // re-derived from the restored key
}

void test_serialize_max_body(void)
{
    EdgeEntry in;
    uint8_t body[PC_EDGE_BODY_MAX];
    for (int i = 0; i < PC_EDGE_BODY_MAX; i++)
    {
        body[i] = (uint8_t)(i * 131 + 17);
    }
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/big.bin");
    fill_entry(&in, canon, "\"big\"", body, PC_EDGE_BODY_MAX);

    size_t n = edge_sd_serialize(&in, g_scratch, sizeof(g_scratch));
    TEST_ASSERT_TRUE(n > 0);
    EdgeEntry out;
    TEST_ASSERT_TRUE(edge_sd_deserialize(g_scratch, n, &out));
    TEST_ASSERT_EQUAL_UINT16(PC_EDGE_BODY_MAX, out.body_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(body, out.body, PC_EDGE_BODY_MAX);
}

void test_serialize_too_small_scratch_fails(void)
{
    EdgeEntry in;
    uint8_t body[300];
    memset(body, 'x', sizeof(body));
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/x");
    fill_entry(&in, canon, "\"e\"", body, sizeof(body));
    uint8_t tiny[16];
    TEST_ASSERT_EQUAL_UINT(0, edge_sd_serialize(&in, tiny, sizeof(tiny))); // won't fit -> 0
}

void test_deserialize_corrupt_fails_closed(void)
{
    EdgeEntry in;
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/y");
    fill_entry(&in, canon, "\"e\"", (const uint8_t *)"hello", 5);
    size_t n = edge_sd_serialize(&in, g_scratch, sizeof(g_scratch));
    TEST_ASSERT_TRUE(n > 0);

    EdgeEntry out;
    uint8_t bad = g_scratch[0];
    g_scratch[0] = 0x42; // wrong version
    TEST_ASSERT_FALSE(edge_sd_deserialize(g_scratch, n, &out));
    g_scratch[0] = bad;
    TEST_ASSERT_FALSE(edge_sd_deserialize(g_scratch, 2, &out));     // truncated header
    TEST_ASSERT_FALSE(edge_sd_deserialize(g_scratch, n - 3, &out)); // truncated body
}

// --- put / get over dbm --------------------------------------------------------------------------
void test_put_get_roundtrip(void)
{
    fresh();
    EdgeEntry in;
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/a.txt");
    fill_entry(&in, canon, "\"a1\"", (const uint8_t *)"payload-A", 9);
    TEST_ASSERT_TRUE(edge_sd_put(&g_db, &in, g_scratch, sizeof(g_scratch)));

    EdgeEntry out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_TRUE(edge_sd_get(&g_db, in.digest, &out, g_scratch, sizeof(g_scratch)));
    TEST_ASSERT_EQUAL_STRING(canon, out.key);
    TEST_ASSERT_EQUAL_STRING("\"a1\"", out.etag);
    TEST_ASSERT_EQUAL_UINT16(9, out.body_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("payload-A", out.body, 9);

    // A digest that was never stored misses.
    EdgeEntry in2;
    char c2[PC_EDGE_KEY_MAX];
    mkcanon(c2, sizeof(c2), "/cdn/never");
    fill_entry(&in2, c2, "\"n\"", (const uint8_t *)"x", 1);
    TEST_ASSERT_FALSE(edge_sd_get(&g_db, in2.digest, &out, g_scratch, sizeof(g_scratch)));
}

void test_no_validator_not_spilled(void)
{
    fresh();
    EdgeEntry in;
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/novalidator");
    fill_entry(&in, canon, "", (const uint8_t *)"body", 4); // no etag / last-modified
    in.last_modified[0] = '\0';
    TEST_ASSERT_FALSE(edge_sd_put(&g_db, &in, g_scratch, sizeof(g_scratch))); // nothing to revalidate -> skip

    EdgeEntry out;
    TEST_ASSERT_FALSE(edge_sd_get(&g_db, in.digest, &out, g_scratch, sizeof(g_scratch)));
}

void test_oversize_body_stays_l1_only(void)
{
    fresh();
    // A body whose serialized size exceeds PC_DBM_VAL_MAX must not be spilled (stays L1-only).
    EdgeEntry in;
    uint8_t body[PC_EDGE_BODY_MAX];
    memset(body, 'Z', sizeof(body));
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/toobig");
    fill_entry(&in, canon, "\"big\"", body, PC_EDGE_BODY_MAX);
    size_t serialized = edge_sd_serialize(&in, g_scratch, sizeof(g_scratch));
    TEST_ASSERT_TRUE(serialized > PC_DBM_VAL_MAX); // the env sizes DBM_VAL_MAX below a full entry
    TEST_ASSERT_FALSE(edge_sd_put(&g_db, &in, g_scratch, sizeof(g_scratch)));
}

// --- L1 on_evict write-back hook -----------------------------------------------------------------
static uint32_t g_spills = 0;
static void spill_cb(void *ctx, const EdgeEntry *v)
{
    (void)ctx;
    if (edge_sd_put((struct pc_dbm *)ctx, v, g_scratch, sizeof(g_scratch)))
    {
        g_spills++;
    }
}

static EdgeEntry *store_mk(EdgeCacheStore *s, const char *path, const char *etag, const char *body)
{
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), path);
    EdgeEntry *e = edge_store_alloc(s, canon, "");
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
    edge_store_init(&store);
    store.on_evict = spill_cb;
    store.evict_ctx = &g_db;

    // Fill every L1 slot, then one more: the LRU victim (the first inserted) is evicted -> spilled to L2.
    char first_canon[PC_EDGE_KEY_MAX];
    mkcanon(first_canon, sizeof(first_canon), "/cdn/e0");
    uint8_t first_digest[32];
    edge_key_digest(first_canon, strlen(first_canon), first_digest);

    for (int i = 0; i < PC_EDGE_CACHE_SLOTS; i++)
    {
        char path[24];
        snprintf(path, sizeof(path), "/cdn/e%d", i);
        char et[16];
        snprintf(et, sizeof(et), "\"e%d\"", i);
        store_mk(&store, path, et, "body");
    }
    TEST_ASSERT_EQUAL_UINT32(0, g_spills); // nothing evicted yet
    store_mk(&store, "/cdn/eN", "\"eN\"", "body");
    TEST_ASSERT_EQUAL_UINT32(1, g_spills); // the LRU victim spilled

    // The evicted entry is now promotable from L2; a still-resident one is not there.
    EdgeEntry out;
    TEST_ASSERT_TRUE(edge_sd_get(&g_db, first_digest, &out, g_scratch, sizeof(g_scratch)));
    TEST_ASSERT_EQUAL_STRING(first_canon, out.key);
    TEST_ASSERT_EQUAL_STRING("\"e0\"", out.etag);

    char last_canon[PC_EDGE_KEY_MAX];
    mkcanon(last_canon, sizeof(last_canon), "/cdn/eN");
    uint8_t last_digest[32];
    edge_key_digest(last_canon, strlen(last_canon), last_digest);
    TEST_ASSERT_FALSE(edge_sd_get(&g_db, last_digest, &out, g_scratch, sizeof(g_scratch)));
}

void test_transient_entry_not_spilled(void)
{
    fresh();
    g_spills = 0;
    EdgeCacheStore store;
    edge_store_init(&store);
    store.on_evict = spill_cb;
    store.evict_ctx = &g_db;

    // Fill the store with transient (empty-key) entries; evicting one must NOT fire the write-back hook.
    for (int i = 0; i <= PC_EDGE_CACHE_SLOTS; i++)
    {
        EdgeEntry *e = edge_store_alloc(&store, "", "");
        TEST_ASSERT_NOT_NULL(e);
        e->body_len = 4;
        memcpy(e->body, "data", 4);
    }
    TEST_ASSERT_EQUAL_UINT32(0, g_spills); // empty-key victims are never offered to on_evict
}

// --- reboot survival -----------------------------------------------------------------------------
void test_survives_reboot(void)
{
    fresh();
    EdgeEntry in;
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/persist");
    fill_entry(&in, canon, "\"p9\"", (const uint8_t *)"survive-me", 10);
    strncpy(in.last_modified, "Wed, 01 Jan 2025 00:00:00 GMT", sizeof(in.last_modified) - 1);
    TEST_ASSERT_TRUE(edge_sd_put(&g_db, &in, g_scratch, sizeof(g_scratch)));
    TEST_ASSERT_TRUE(pc_dbm_sync(&g_db));

    TEST_ASSERT_TRUE(reboot());
    EdgeEntry out;
    TEST_ASSERT_TRUE(edge_sd_get(&g_db, in.digest, &out, g_scratch, sizeof(g_scratch)));
    TEST_ASSERT_EQUAL_STRING(canon, out.key);
    TEST_ASSERT_EQUAL_STRING("\"p9\"", out.etag);
    TEST_ASSERT_EQUAL_STRING("Wed, 01 Jan 2025 00:00:00 GMT", out.last_modified);
    TEST_ASSERT_EQUAL_UINT16(10, out.body_len);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("survive-me", out.body, 10);
}

// --- del / purge ---------------------------------------------------------------------------------
void test_del(void)
{
    fresh();
    EdgeEntry in;
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/del");
    fill_entry(&in, canon, "\"d\"", (const uint8_t *)"gone", 4);
    TEST_ASSERT_TRUE(edge_sd_put(&g_db, &in, g_scratch, sizeof(g_scratch)));
    TEST_ASSERT_TRUE(edge_sd_del(&g_db, in.digest));
    EdgeEntry out;
    TEST_ASSERT_FALSE(edge_sd_get(&g_db, in.digest, &out, g_scratch, sizeof(g_scratch)));
    TEST_ASSERT_FALSE(edge_sd_del(&g_db, in.digest)); // already gone
}

static void put_path(const char *path)
{
    EdgeEntry in;
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), path);
    fill_entry(&in, canon, "\"v\"", (const uint8_t *)"x", 1);
    TEST_ASSERT_TRUE(edge_sd_put(&g_db, &in, g_scratch, sizeof(g_scratch)));
}
static proto_bool has_path(const char *path)
{
    EdgeEntry in, out;
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), path);
    fill_entry(&in, canon, "\"v\"", (const uint8_t *)"x", 1);
    return edge_sd_get(&g_db, in.digest, &out, g_scratch, sizeof(g_scratch));
}

void test_purge_prefix(void)
{
    fresh();
    put_path("/cdn/a");
    put_path("/cdn/b");
    put_path("/other/c");
    TEST_ASSERT_EQUAL_UINT32(2, edge_sd_purge_prefix(&g_db, "/cdn/", g_scratch, sizeof(g_scratch)));
    TEST_ASSERT_FALSE(has_path("/cdn/a"));
    TEST_ASSERT_FALSE(has_path("/cdn/b"));
    TEST_ASSERT_TRUE(has_path("/other/c")); // outside the prefix, untouched
}

void test_purge_prefix_multipass(void)
{
    fresh();
    // More than the internal batch size, to exercise the collect-delete-reiterate loop.
    const int N = 20;
    char path[24];
    for (int i = 0; i < N; i++)
    {
        snprintf(path, sizeof(path), "/cdn/p%d", i);
        put_path(path);
    }
    put_path("/keep/one");
    TEST_ASSERT_EQUAL_UINT32((uint32_t)N, edge_sd_purge_prefix(&g_db, "/cdn/", g_scratch, sizeof(g_scratch)));
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
    TEST_ASSERT_EQUAL_UINT32(3, edge_sd_purge_all(&g_db));
    TEST_ASSERT_FALSE(has_path("/cdn/a"));
    TEST_ASSERT_FALSE(has_path("/x/y"));
}

void test_shared_dbm_foreign_value_untouched(void)
{
    fresh();
    // A foreign (non-edge) value under a 32-byte key: purge_all must skip it (peek_canon guards on version).
    uint8_t foreign_key[32];
    memset(foreign_key, 0xA5, sizeof(foreign_key));
    uint8_t foreign_val[16];
    memset(foreign_val, 0xFF, sizeof(foreign_val)); // first byte 0xFF != edge version 1
    TEST_ASSERT_TRUE(pc_dbm_put(&g_db, (const char *)foreign_key, 32, foreign_val, sizeof(foreign_val)));
    put_path("/cdn/mine");

    TEST_ASSERT_EQUAL_UINT32(1, edge_sd_purge_all(&g_db)); // only the edge value
    TEST_ASSERT_FALSE(has_path("/cdn/mine"));
    uint8_t out[16];
    TEST_ASSERT_EQUAL_INT(16, pc_dbm_get(&g_db, (const char *)foreign_key, 32, out, sizeof(out)));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(foreign_val, out, 16); // foreign value intact
}

// --- serialization guards + every overflow / truncation point ------------------------------------
void test_serialize_null_guards_and_every_overflow_point(void)
{
    EdgeEntry in;
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/caps");
    fill_entry(&in, canon, "\"c\"", (const uint8_t *)"hello", 5);
    strncpy(in.last_modified, "Wed, 01 Jan 2025 00:00:00 GMT", sizeof(in.last_modified) - 1);
    strncpy(in.content_encoding, "gzip", sizeof(in.content_encoding) - 1);
    strncpy(in.vary_names, "Accept-Encoding", sizeof(in.vary_names) - 1);
    strncpy(in.vary_vals,
            "accept-encoding\x1e"
            "gzip\x1f",
            sizeof(in.vary_vals) - 1);

    TEST_ASSERT_EQUAL_UINT(0, edge_sd_serialize(NULL, g_scratch, sizeof(g_scratch)));
    TEST_ASSERT_EQUAL_UINT(0, edge_sd_serialize(&in, NULL, sizeof(g_scratch)));
    TEST_ASSERT_EQUAL_UINT(0, edge_sd_serialize(&in, g_scratch, 2)); // no room for the fixed header

    size_t n = edge_sd_serialize(&in, g_scratch, sizeof(g_scratch));
    TEST_ASSERT_TRUE(n > 3);
    // Every cap short of the exact size fails at whichever field runs out - never a partial record.
    for (size_t cap = 3; cap < n; cap++)
    {
        TEST_ASSERT_EQUAL_UINT(0, edge_sd_serialize(&in, g_scratch, cap));
    }
    TEST_ASSERT_EQUAL_UINT(n, edge_sd_serialize(&in, g_scratch, n));
}

void test_deserialize_null_guards_and_every_truncation(void)
{
    EdgeEntry in;
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/trunc");
    fill_entry(&in, canon, "\"t\"", (const uint8_t *)"body-bytes", 10);
    strncpy(in.last_modified, "Wed, 01 Jan 2025 00:00:00 GMT", sizeof(in.last_modified) - 1);
    strncpy(in.content_encoding, "br", sizeof(in.content_encoding) - 1);
    strncpy(in.vary_names, "Accept-Encoding", sizeof(in.vary_names) - 1);
    strncpy(in.vary_vals,
            "accept-encoding\x1e"
            "br\x1f",
            sizeof(in.vary_vals) - 1);
    size_t n = edge_sd_serialize(&in, g_scratch, sizeof(g_scratch));
    TEST_ASSERT_TRUE(n > 0);

    EdgeEntry out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_FALSE(edge_sd_deserialize(NULL, n, &out));
    TEST_ASSERT_FALSE(edge_sd_deserialize(g_scratch, n, NULL));
    // Every truncation short of the whole record fails closed at whichever length prefix runs out.
    for (size_t l = 0; l < n; l++)
    {
        TEST_ASSERT_FALSE(edge_sd_deserialize(g_scratch, l, &out));
    }
    TEST_ASSERT_TRUE(edge_sd_deserialize(g_scratch, n, &out));
    TEST_ASSERT_EQUAL_STRING(canon, out.key);
}

void test_deserialize_rejects_field_longer_than_its_slot(void)
{
    // A record claiming a key exactly as long as the entry's key field leaves no room for the NUL:
    // refuse it rather than truncate the cache key (which would alias two resources).
    uint8_t buf[512];
    memset(buf, 'k', sizeof(buf));
    buf[0] = 1; // the entry-serialization version
    buf[1] = 200;
    buf[2] = 0; // status
    buf[3] = (uint8_t)(PC_EDGE_KEY_MAX & 0xFF);
    buf[4] = (uint8_t)(PC_EDGE_KEY_MAX >> 8);
    EdgeEntry out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_FALSE(edge_sd_deserialize(buf, sizeof(buf), &out));
}

void test_deserialize_rejects_oversize_body_length(void)
{
    EdgeEntry in;
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/blen");
    fill_entry(&in, canon, "\"b\"", (const uint8_t *)"z", 1);
    size_t n = edge_sd_serialize(&in, g_scratch, sizeof(g_scratch));
    TEST_ASSERT_TRUE(n > 3);
    // The body length prefix is the last two bytes ahead of the 1-byte body.
    g_scratch[n - 3] = 0xFF;
    g_scratch[n - 2] = 0xFF; // 65535 claimed, past PC_EDGE_BODY_MAX
    EdgeEntry out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_FALSE(edge_sd_deserialize(g_scratch, n, &out));
}

// --- dbm-backed API guards -----------------------------------------------------------------------
void test_dbm_api_null_guards(void)
{
    fresh();
    EdgeEntry in;
    EdgeEntry out;
    memset(&out, 0, sizeof(out));
    char canon[PC_EDGE_KEY_MAX];
    mkcanon(canon, sizeof(canon), "/cdn/guards");
    fill_entry(&in, canon, "\"g\"", (const uint8_t *)"v", 1);

    TEST_ASSERT_FALSE(edge_sd_put(NULL, &in, g_scratch, sizeof(g_scratch)));
    TEST_ASSERT_FALSE(edge_sd_put(&g_db, NULL, g_scratch, sizeof(g_scratch)));
    TEST_ASSERT_FALSE(edge_sd_put(&g_db, &in, NULL, sizeof(g_scratch)));
    TEST_ASSERT_FALSE(edge_sd_put(&g_db, &in, g_scratch, 8)); // does not serialize into the scratch

    TEST_ASSERT_FALSE(edge_sd_get(NULL, in.digest, &out, g_scratch, sizeof(g_scratch)));
    TEST_ASSERT_FALSE(edge_sd_get(&g_db, NULL, &out, g_scratch, sizeof(g_scratch)));
    TEST_ASSERT_FALSE(edge_sd_get(&g_db, in.digest, NULL, g_scratch, sizeof(g_scratch)));
    TEST_ASSERT_FALSE(edge_sd_get(&g_db, in.digest, &out, NULL, sizeof(g_scratch)));

    TEST_ASSERT_FALSE(edge_sd_del(NULL, in.digest));
    TEST_ASSERT_FALSE(edge_sd_del(&g_db, NULL));

    TEST_ASSERT_EQUAL_UINT32(0, edge_sd_purge_prefix(NULL, "/cdn/", g_scratch, sizeof(g_scratch)));
    TEST_ASSERT_EQUAL_UINT32(0, edge_sd_purge_prefix(&g_db, NULL, g_scratch, sizeof(g_scratch)));
    TEST_ASSERT_EQUAL_UINT32(0, edge_sd_purge_prefix(&g_db, "/cdn/", NULL, sizeof(g_scratch)));
    TEST_ASSERT_EQUAL_UINT32(0, edge_sd_purge_all(NULL));
}

// --- purge: keys and values a shared dbm may hold alongside the cache ----------------------------
void test_purge_skips_foreign_and_unreadable_records(void)
{
    fresh();
    // A key that is not a 32-byte digest at all.
    TEST_ASSERT_TRUE(pc_dbm_put(&g_db, "short-key", 9, (const uint8_t *)"x", 1));
    // A 32-byte key holding a zero-length value (nothing to inspect).
    uint8_t empty_key[32];
    memset(empty_key, 0x11, sizeof(empty_key));
    TEST_ASSERT_TRUE(pc_dbm_put(&g_db, (const char *)empty_key, 32, NULL, 0));
    // A 32-byte key whose value is too short to carry even the edge header.
    uint8_t stub_key[32];
    memset(stub_key, 0x22, sizeof(stub_key));
    uint8_t stub[2] = {1, 0};
    TEST_ASSERT_TRUE(pc_dbm_put(&g_db, (const char *)stub_key, 32, stub, sizeof(stub)));

    put_path("/cdn/real");
    TEST_ASSERT_EQUAL_UINT32(1, edge_sd_purge_all(&g_db)); // only the real edge entry is dropped

    uint8_t v[8];
    TEST_ASSERT_EQUAL_INT(1, pc_dbm_get(&g_db, "short-key", 9, v, sizeof(v)));
    TEST_ASSERT_EQUAL_INT(0, pc_dbm_get(&g_db, (const char *)empty_key, 32, v, sizeof(v)));
    TEST_ASSERT_EQUAL_INT(2, pc_dbm_get(&g_db, (const char *)stub_key, 32, v, sizeof(v)));
}

void test_purge_prefix_skips_key_without_a_path(void)
{
    fresh();
    // An L2 value whose cache key is not "METHOD\nhost\npath" has no path portion, so a prefix purge
    // must skip it rather than match against the raw key.
    EdgeEntry odd;
    memset(&odd, 0, sizeof(odd));
    strncpy(odd.key, "malformed-key", sizeof(odd.key) - 1);
    edge_key_digest(odd.key, strlen(odd.key), odd.digest);
    odd.status = 200;
    strncpy(odd.etag, "\"o\"", sizeof(odd.etag) - 1);
    memcpy(odd.body, "x", 1);
    odd.body_len = 1;
    TEST_ASSERT_TRUE(edge_sd_put(&g_db, &odd, g_scratch, sizeof(g_scratch)));
    put_path("/cdn/keepme");

    TEST_ASSERT_EQUAL_UINT32(0, edge_sd_purge_prefix(&g_db, "malformed", g_scratch, sizeof(g_scratch)));
    EdgeEntry out;
    memset(&out, 0, sizeof(out));
    TEST_ASSERT_TRUE(edge_sd_get(&g_db, odd.digest, &out, g_scratch, sizeof(g_scratch)));
    TEST_ASSERT_TRUE(has_path("/cdn/keepme"));
    // It is still an edge value, so a purge-all does reach it.
    TEST_ASSERT_EQUAL_UINT32(2, edge_sd_purge_all(&g_db));
}

void test_purge_counts_only_the_deletes_that_were_logged(void)
{
    // A dbm delete is an append of a tombstone record, so it fails once the log has no room left. The purge
    // loop must count only the deletes that actually landed; a key whose tombstone could not be written
    // stays live (and is still there for a later purge once the log has been compacted).
    fresh_sized(1024); // a data region small enough to run out during the purge
    put_path("/cdn/f0");
    put_path("/cdn/f1");
    const uint32_t before = pc_dbm_count(&g_db);
    TEST_ASSERT_EQUAL_UINT32(2, before);

    // Record framing: the WAL header plus the dbm payload header (op u8 + key_len u16 + val_len u32).
    const uint64_t DBM_PAYLOAD_HDR = 1 + 2 + 4;
    const uint64_t TOMB = WAL_RECORD_HEADER + DBM_PAYLOAD_HDR + 32; // one delete of a 32-byte digest key
    const char pad_key_fmt[] = "pad%d";
    const uint64_t PAD_REC_HDR = WAL_RECORD_HEADER + DBM_PAYLOAD_HDR + 4; // "padN" is a 4-byte key
    const uint64_t leave = 2 * TOMB - 1; // room for exactly one tombstone, one byte short of two

    uint8_t pad[PC_DBM_VAL_MAX];
    memset(pad, 'P', sizeof(pad));
    uint64_t room = pc_wal_store_capacity(&g_wal) - pc_wal_store_used(&g_wal);
    TEST_ASSERT_TRUE(room > leave + PAD_REC_HDR);
    for (int i = 0; room > leave; i++)
    {
        TEST_ASSERT_TRUE(room - leave >= PAD_REC_HDR); // the gap is always fillable by a whole record
        uint64_t want = room - leave - PAD_REC_HDR;
        uint32_t vlen = want > (uint64_t)PC_DBM_VAL_MAX ? (uint32_t)PC_DBM_VAL_MAX : (uint32_t)want;
        char key[8];
        snprintf(key, sizeof(key), pad_key_fmt, i); // a 4-byte key: not a digest, so the purge skips it
        TEST_ASSERT_EQUAL_UINT(4, strlen(key));
        TEST_ASSERT_TRUE(pc_dbm_put(&g_db, key, 4, pad, vlen));
        room = pc_wal_store_capacity(&g_wal) - pc_wal_store_used(&g_wal);
    }
    TEST_ASSERT_EQUAL_UINT64(leave, room);
    const uint32_t live = pc_dbm_count(&g_db); // the two entries plus the pad keys

    // Both entries match, but only the first tombstone fits: the purge reports one deletion, not two.
    TEST_ASSERT_EQUAL_UINT32(1, edge_sd_purge_all(&g_db));
    TEST_ASSERT_EQUAL_UINT32(live - 1, pc_dbm_count(&g_db));      // exactly one key actually left the index
    TEST_ASSERT_TRUE(has_path("/cdn/f0") != has_path("/cdn/f1")); // and it is one of the two, not both
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_serialize_roundtrip_all_fields);
    RUN_TEST(test_serialize_max_body);
    RUN_TEST(test_serialize_too_small_scratch_fails);
    RUN_TEST(test_deserialize_corrupt_fails_closed);
    RUN_TEST(test_put_get_roundtrip);
    RUN_TEST(test_no_validator_not_spilled);
    RUN_TEST(test_oversize_body_stays_l1_only);
    RUN_TEST(test_spill_on_evict_and_promote);
    RUN_TEST(test_transient_entry_not_spilled);
    RUN_TEST(test_survives_reboot);
    RUN_TEST(test_del);
    RUN_TEST(test_purge_prefix);
    RUN_TEST(test_purge_prefix_multipass);
    RUN_TEST(test_purge_all);
    RUN_TEST(test_shared_dbm_foreign_value_untouched);
    RUN_TEST(test_serialize_null_guards_and_every_overflow_point);
    RUN_TEST(test_deserialize_null_guards_and_every_truncation);
    RUN_TEST(test_deserialize_rejects_field_longer_than_its_slot);
    RUN_TEST(test_deserialize_rejects_oversize_body_length);
    RUN_TEST(test_dbm_api_null_guards);
    RUN_TEST(test_purge_skips_foreign_and_unreadable_records);
    RUN_TEST(test_purge_prefix_skips_key_without_a_path);
    RUN_TEST(test_purge_counts_only_the_deletes_that_were_logged);
    return UNITY_END();
}
