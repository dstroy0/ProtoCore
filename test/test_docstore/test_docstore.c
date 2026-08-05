// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/docstore: a JSON document store on the WAL (via dbm), with top-level field
// queries. Exercised over a RAM-backed WalDev; "reboot" = remount the store + reopen the dbm + rebind.

#include "services/storage/dbm/dbm.h"
#include "services/storage/docstore/docstore.h"
#include "services/storage/wal/wal_store.h"
#include <stdint.h>
#include <stdio.h>

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// RAM-backed WalDev.
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

static uint8_t g_disk[64 * 1024];
static RamDisk g_d;
static WalDev g_dev;
static WalStore g_wal;
static pc_dbm g_db;
static pc_doc_store g_ds;

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
static void fresh(void)
{
    g_d.buf = g_disk;
    g_d.size = sizeof(g_disk);
    g_dev = dev_over(&g_d);
    TEST_ASSERT_TRUE(pc_wal_store_format(&g_wal, &g_dev));
    TEST_ASSERT_TRUE(pc_dbm_open(&g_db, &g_wal));
    pc_docstore_open(&g_ds, &g_db);
}
static proto_bool reboot(void)
{
    g_dev = dev_over(&g_d);
    if (!pc_wal_store_mount(&g_wal, &g_dev))
    {
        return PROTO_FALSE;
    }
    if (!pc_dbm_open(&g_db, &g_wal))
    {
        return PROTO_FALSE;
    }
    pc_docstore_open(&g_ds, &g_db);
    return PROTO_TRUE;
}

static proto_bool put_doc(const char *id, const char *json)
{
    return pc_docstore_put(&g_ds, id, (uint16_t)strlen(id), (const uint8_t *)json, (uint32_t)strlen(json));
}
static proto_bool get_eq(const char *id, const char *expect)
{
    uint8_t buf[PC_DBM_VAL_MAX + 1];
    long n = pc_docstore_get(&g_ds, id, (uint16_t)strlen(id), buf, sizeof(buf));
    if (n < 0)
    {
        return PROTO_FALSE;
    }
    return (size_t)n == strlen(expect) && memcmp(buf, expect, n) == 0;
}

// Match callback: collect matched ids into a small set.
typedef struct
{
    int n;
    char ids[8][16];
} Collected;
static proto_bool collect(const char *id, uint16_t id_len, const uint8_t *json, uint32_t json_len, void *ctx)
{
    (void)json;
    (void)json_len;
    Collected *c = (Collected *)ctx;
    if (c->n < 8 && id_len < 16)
    {
        memcpy(c->ids[c->n], id, id_len);
        c->ids[c->n][id_len] = 0;
        c->n++;
    }
    return PROTO_TRUE;
}
static proto_bool has_id(const Collected *c, const char *id)
{
    for (int i = 0; i < c->n; i++)
    {
        if (strcmp(c->ids[i], id) == 0)
        {
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

void test_put_get_del(void)
{
    fresh();
    TEST_ASSERT_TRUE(put_doc("u1", "{\"name\":\"alice\",\"age\":30,\"admin\":true}"));
    TEST_ASSERT_TRUE(put_doc("u2", "{\"name\":\"bob\",\"age\":25,\"admin\":false}"));
    TEST_ASSERT_TRUE(get_eq("u1", "{\"name\":\"alice\",\"age\":30,\"admin\":true}"));
    TEST_ASSERT_EQUAL_UINT32(2, pc_docstore_count(&g_ds));

    TEST_ASSERT_TRUE(pc_docstore_del(&g_ds, "u2", 2));
    TEST_ASSERT_FALSE(pc_docstore_contains(&g_ds, "u2", 2));
    TEST_ASSERT_EQUAL_UINT32(1, pc_docstore_count(&g_ds));

    // Replace u1's document.
    TEST_ASSERT_TRUE(put_doc("u1", "{\"name\":\"alice2\",\"age\":31}"));
    TEST_ASSERT_TRUE(get_eq("u1", "{\"name\":\"alice2\",\"age\":31}"));
}

void test_find_by_field(void)
{
    fresh();
    put_doc("u1", "{\"name\":\"alice\",\"age\":30,\"city\":\"paris\"}");
    put_doc("u2", "{\"name\":\"bob\",\"age\":30,\"city\":\"paris\"}");
    put_doc("u3", "{\"name\":\"carol\",\"age\":25,\"city\":\"lyon\"}");

    // String field.
    Collected c = {0};
    uint32_t m = pc_docstore_find_str(&g_ds, "city", "paris", collect, &c);
    TEST_ASSERT_EQUAL_UINT32(2, m);
    TEST_ASSERT_EQUAL_INT(2, c.n);
    TEST_ASSERT_TRUE(has_id(&c, "u1"));
    TEST_ASSERT_TRUE(has_id(&c, "u2"));
    TEST_ASSERT_FALSE(has_id(&c, "u3"));

    // Integer field.
    Collected c2 = {0};
    m = pc_docstore_find_int(&g_ds, "age", 30, collect, &c2);
    TEST_ASSERT_EQUAL_UINT32(2, m);
    TEST_ASSERT_TRUE(has_id(&c2, "u1"));
    TEST_ASSERT_TRUE(has_id(&c2, "u2"));

    // No matches.
    Collected c3 = {0};
    m = pc_docstore_find_str(&g_ds, "city", "berlin", collect, &c3);
    TEST_ASSERT_EQUAL_UINT32(0, m);
    TEST_ASSERT_EQUAL_INT(0, c3.n);
}

void test_find_bool(void)
{
    fresh();
    put_doc("a", "{\"on\":true,\"n\":1}");
    put_doc("b", "{\"on\":false,\"n\":2}");
    put_doc("c", "{\"on\":true,\"n\":3}");
    Collected c = {0};
    uint32_t m = pc_docstore_find_bool(&g_ds, "on", PROTO_TRUE, collect, &c);
    TEST_ASSERT_EQUAL_UINT32(2, m);
    TEST_ASSERT_TRUE(has_id(&c, "a"));
    TEST_ASSERT_TRUE(has_id(&c, "c"));
}

void test_persist_and_query_across_reboot(void)
{
    fresh();
    put_doc("u1", "{\"name\":\"alice\",\"role\":\"admin\"}");
    put_doc("u2", "{\"name\":\"bob\",\"role\":\"user\"}");
    put_doc("u3", "{\"name\":\"carol\",\"role\":\"admin\"}");
    TEST_ASSERT_TRUE(pc_docstore_sync(&g_ds));

    TEST_ASSERT_TRUE(reboot());
    TEST_ASSERT_EQUAL_UINT32(3, pc_docstore_count(&g_ds));
    TEST_ASSERT_TRUE(get_eq("u2", "{\"name\":\"bob\",\"role\":\"user\"}"));

    // The field index (JSON scan) works after a remount too.
    Collected c = {0};
    uint32_t m = pc_docstore_find_str(&g_ds, "role", "admin", collect, &c);
    TEST_ASSERT_EQUAL_UINT32(2, m);
    TEST_ASSERT_TRUE(has_id(&c, "u1"));
    TEST_ASSERT_TRUE(has_id(&c, "u3"));
}

// Counts one match then stops, so the early-out path is what the caller observes.
typedef struct
{
    int seen;
} Once;
static proto_bool stop_after_one(const char *name, uint16_t nlen, const uint8_t *doc, uint32_t len, void *ctx)
{
    (void)name;
    (void)nlen;
    (void)doc;
    (void)len;
    ((Once *)ctx)->seen++;
    return PROTO_FALSE;
}

// find matches only the queried field, and a stop request halts the scan.
void test_find_early_stop(void)
{
    fresh();
    for (int i = 0; i < 5; i++)
    {
        char id[8], doc[48];
        snprintf(id, sizeof(id), "d%d", i);
        snprintf(doc, sizeof(doc), "{\"grp\":\"x\",\"i\":%d}", i);
        put_doc(id, doc);
    }
    // A callback that stops after the first match sees exactly one.
    Once once = {0};
    uint32_t m = pc_docstore_find_str(&g_ds, "grp", "x", stop_after_one, &once);
    TEST_ASSERT_EQUAL_UINT32(1, m);
    TEST_ASSERT_EQUAL_INT(1, once.seen);
}

// A document missing the queried field never matches (json_get_* returns false), for every field kind.
void test_find_field_absent(void)
{
    fresh();
    put_doc("a", "{\"name\":\"x\",\"age\":5,\"on\":true}");
    put_doc("b", "{\"other\":\"y\"}"); // lacks name / age / on

    Collected cs = {0};
    TEST_ASSERT_EQUAL_UINT32(1, pc_docstore_find_str(&g_ds, "name", "x", collect, &cs)); // "b" has no name
    TEST_ASSERT_TRUE(has_id(&cs, "a"));
    TEST_ASSERT_FALSE(has_id(&cs, "b"));

    Collected ci = {0};
    TEST_ASSERT_EQUAL_UINT32(1, pc_docstore_find_int(&g_ds, "age", 5, collect, &ci)); // "b" has no age
    TEST_ASSERT_TRUE(has_id(&ci, "a"));

    Collected cb = {0};
    TEST_ASSERT_EQUAL_UINT32(1, pc_docstore_find_bool(&g_ds, "on", PROTO_TRUE, collect, &cb)); // "b" has no on
    TEST_ASSERT_TRUE(has_id(&cb, "a"));
}

// find with a null callback just counts the matches (the per-match callback branch is skipped).
void test_find_count_only_null_cb(void)
{
    fresh();
    put_doc("u1", "{\"grp\":\"x\"}");
    put_doc("u2", "{\"grp\":\"x\"}");
    put_doc("u3", "{\"grp\":\"y\"}");
    TEST_ASSERT_EQUAL_UINT32(2, pc_docstore_find_str(&g_ds, "grp", "x", NULL, NULL));
    TEST_ASSERT_EQUAL_UINT32(1, pc_docstore_find_str(&g_ds, "grp", "y", NULL, NULL));
    TEST_ASSERT_EQUAL_UINT32(0, pc_docstore_find_str(&g_ds, "grp", "z", NULL, NULL));
}

// A document whose value becomes unreadable mid-scan (e.g. a truncated/corrupted backing store) is
// skipped rather than counted as a match, and the scan still runs to completion over the rest.
void test_find_skips_unreadable_document(void)
{
    fresh();
    put_doc("a", "{\"grp\":\"x\"}");
    put_doc("b", "{\"grp\":\"x\"}");

    // Truncate the backing device out from under the store: any value pread now reads short and fails,
    // without touching the in-RAM slot index that pc_dbm_iterate walks.
    g_d.size = 4;

    Collected c = {0};
    uint32_t m = pc_docstore_find_str(&g_ds, "grp", "x", collect, &c);
    TEST_ASSERT_EQUAL_UINT32(0, m);
    TEST_ASSERT_EQUAL_INT(0, c.n);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_put_get_del);
    RUN_TEST(test_find_by_field);
    RUN_TEST(test_find_bool);
    RUN_TEST(test_persist_and_query_across_reboot);
    RUN_TEST(test_find_early_stop);
    RUN_TEST(test_find_field_absent);
    RUN_TEST(test_find_count_only_null_cb);
    RUN_TEST(test_find_skips_unreadable_document);
    return UNITY_END();
}
