// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host microbenchmarks for the embedded data-store stack (WAL / dbm / docstore / SQLite reader / RESP).
// A deterministic ns/op CPU baseline for docs/FEATURE_PERFORMANCE.md: it measures the *compute* cost of
// each hot op over a RAM-backed device, so combined with the measured SD I/O envelope (section 1) it shows
// where the real-world cost lives (spoiler: the stores are I/O-bound, the CPU cost is tiny). Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPROTOCORE_ENABLE_WAL=1 -DPROTOCORE_ENABLE_DBM=1 -DPROTOCORE_ENABLE_DOCSTORE=1 -DPROTOCORE_ENABLE_SQLITE=1
//   -DPROTOCORE_ENABLE_REDIS=1 test/performance_benching/services/wal/host.c src/services/storage/wal/wal.c
//   src/services/storage/wal/wal_store.c src/services/storage/dbm/dbm.c src/services/storage/docstore/docstore.c
//   src/network_drivers/presentation/codec/json/json.c src/services/storage/sqlite/sqlite_format.c
//   src/services/iot/redis_resp/redis_resp.c src/mmgr/protomem.c src/mmgr/protostr.c -o /tmp/bench_ds && /tmp/bench_ds

#include "services/iot/redis_resp/redis_resp.h"
#include "services/storage/dbm/dbm.h"
#include "services/storage/docstore/docstore.h"
#include "services/storage/sqlite/sqlite_format.h"
#include "services/storage/wal/wal_store.h"

#include "host_bench.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// The 40-row, 2-level b-tree fixture the sqlite table scan reads, shared with the unit suite.
#include "unit/storage/test_sqlite/db_multipage.h"

// A RAM-backed WalDev over a caller buffer (no I/O; measures pure CPU cost).
struct RamDisk
{
    uint8_t *buf;
    uint64_t size;
};

static size_t ram_read(void *ctx, uint64_t off, uint8_t *b, size_t n)
{
    struct RamDisk *d = (struct RamDisk *)ctx;
    if (off + n > d->size)
    {
        return 0;
    }
    memcpy(b, d->buf + off, n);
    return n;
}

static size_t ram_write(void *ctx, uint64_t off, const uint8_t *b, size_t n)
{
    struct RamDisk *d = (struct RamDisk *)ctx;
    if (off + n > d->size)
    {
        return 0;
    }
    memcpy(d->buf + off, b, n);
    return n;
}

static proto_bool ram_sync(void *ctx)
{
    (void)ctx;
    return PROTO_TRUE;
}

static WalDev dev_over(struct RamDisk *d)
{
    WalDev v = {ram_read, ram_write, ram_sync, d, d->size};
    return v;
}

static proto_bool mem_read(void *ctx, uint32_t pgno, uint8_t *page, uint32_t page_size)
{
    struct RamDisk *m = (struct RamDisk *)ctx;
    uint64_t off = (uint64_t)(pgno - 1) * page_size;
    if (pgno < 1 || off + page_size > m->size)
    {
        return PROTO_FALSE;
    }
    memcpy(page, m->buf + off, page_size);
    return PROTO_TRUE;
}

/** @brief Encode @p argc bulk strings, sized by @p lens, as one command into @p out; the octets written. */
static size_t resp_encode(char *out, size_t cap, const char *const *argv, const size_t *lens, size_t argc)
{
    Resp.out.buf = out;
    Resp.out.cap = cap;
    Resp.command.argv = argv;
    Resp.command.argv_len = lens;
    Resp.command.argc = argc;
    Resp.encode_command(Resp.internal);
    return Resp.n;
}

/** @brief Decode the one value at the head of @p buf into @p r; the octets it consumed, 0 on a refusal. */
static size_t resp_take(RespReply *r, const uint8_t *buf, size_t len)
{
    Resp.wire.buf = buf;
    Resp.wire.len = len;
    Resp.parse_reply(Resp.internal);
    *r = Resp.reply;
    return Resp.ok ? Resp.n : 0;
}

int main(void)
{
    hbench_header();

    // ---- WAL ----
    {
        const size_t N = 1024;
        uint8_t src[1024];
        for (size_t i = 0; i < N; i++)
        {
            src[i] = (uint8_t)(i * 31 + 7);
        }
        volatile uint32_t sink = 0;
        double ns = 0.0;
        HBENCH_NS(500000, sink += protocore_wal_crc32(src, N), ns);
        hbench_row("wal", "crc32 (1 KiB)", ns, (double)N);

        uint8_t rec[256];
        double ns2 = 0.0;
        HBENCH_NS(2000000, sink += (uint32_t)protocore_wal_record_encode(rec, sizeof(rec), 1, src, 128), ns2);
        hbench_row("wal", "record_encode (128 B)", ns2, 128.0);
        (void)sink;
    }
    {
        // append into a disk large enough for the whole run (measure the append CPU path).
        const uint64_t ITERS = 200000;
        const uint32_t PLEN = 64;
        uint64_t cap = ITERS * (WAL_RECORD_HEADER + PLEN) + 4096;
        uint8_t *disk = (uint8_t *)malloc(cap);
        struct RamDisk d = {disk, cap};
        WalDev dev = dev_over(&d);
        WalStore s;
        protocore_wal_store_format(&s, &dev);
        uint8_t pay[64];
        memset(pay, 0xAB, PLEN);
        double ns = 0.0;
        HBENCH_NS(ITERS, protocore_wal_store_append(&s, pay, PLEN), ns);
        hbench_row("wal", "store_append (64 B)", ns, (double)PLEN);
        // checkpoint cost in isolation
        double nc = 0.0;
        HBENCH_NS(200000, protocore_wal_store_checkpoint(&s), nc);
        hbench_row("wal", "store_checkpoint", nc, 0);
        free(disk);
    }

    // ---- dbm (steady state over a 100-key working set) ----
    {
        uint8_t *disk = (uint8_t *)malloc(16u * 1024 * 1024);
        struct RamDisk d = {disk, 16u * 1024 * 1024};
        WalDev dev = dev_over(&d);
        static WalStore wal;
        static protocore_dbm db;
        protocore_wal_store_format(&wal, &dev);
        protocore_dbm_open(&db, &wal);
        char keys[100][8];
        uint8_t val[64];
        memset(val, 0x5A, sizeof(val));
        for (int k = 0; k < 100; k++)
        {
            snprintf(keys[k], sizeof(keys[k]), "k%05d", k);
        }
        for (int k = 0; k < 100; k++)
        {
            protocore_dbm_put(&db, keys[k], (uint16_t)strlen(keys[k]), val, sizeof(val));
        }
        int idx = 0;
        double nput = 0.0;
        HBENCH_NS(
            500000,
            {
                protocore_dbm_put(&db, keys[idx], (uint16_t)strlen(keys[idx]), val, sizeof(val));
                idx = (idx + 1) % 100;
            },
            nput);
        hbench_row("dbm", "put (16B key/64B val)", nput, 0);
        uint8_t out[64];
        volatile long sink = 0;
        idx = 0;
        double nget = 0.0;
        HBENCH_NS(
            2000000,
            {
                sink += protocore_dbm_get(&db, keys[idx], (uint16_t)strlen(keys[idx]), out, sizeof(out));
                idx = (idx + 1) % 100;
            },
            nget);
        hbench_row("dbm", "get", nget, 0);
        (void)sink;
        free(disk);
    }

    // ---- docstore field query (scan 100 JSON docs) ----
    {
        uint8_t *disk = (uint8_t *)malloc(16u * 1024 * 1024);
        struct RamDisk d = {disk, 16u * 1024 * 1024};
        WalDev dev = dev_over(&d);
        static WalStore wal;
        static protocore_dbm db;
        static protocore_doc_store ds;
        protocore_wal_store_format(&wal, &dev);
        protocore_dbm_open(&db, &wal);
        protocore_docstore_open(&ds, &db);
        for (int k = 0; k < 100; k++)
        {
            char id[8];
            char doc[80];
            snprintf(id, sizeof(id), "u%05d", k);
            snprintf(doc, sizeof(doc), "{\"city\":\"%s\",\"age\":%d,\"n\":%d}", (k % 2) ? "paris" : "lyon", 20 + k % 40,
                     k);
            protocore_docstore_put(&ds, id, (uint16_t)strlen(id), (const uint8_t *)doc, (uint32_t)strlen(doc));
        }
        volatile uint32_t sink = 0;
        double nf = 0.0;
        HBENCH_NS(20000, sink += protocore_docstore_find_str(&ds, "city", "paris", NULL, NULL), nf);
        hbench_row("docstore", "find_str (scan 100)", nf, 0);
        hbench_row("docstore", "  -> per doc scanned", nf / 100.0, 0);
        (void)sink;
        free(disk);
    }

    // ---- SQLite reader ----
    {
        const uint8_t vi[] = {0x83, 0x5e};
        volatile uint64_t s = 0;
        double nv = 0.0;
        HBENCH_NS(
            5000000,
            {
                uint64_t v;
                s += protocore_sqlite_varint_decode(vi, 2, &v);
            },
            nv);
        hbench_row("sqlite", "varint_decode", nv, 0);

        // A full table scan of the 40-row, 2-level b-tree fixture: ns per row.
        struct RamDisk m = {(uint8_t *)DB_MULTIPAGE, sizeof(DB_MULTIPAGE)};
        double ns = 0.0;
        HBENCH_NS(
            20000,
            {
                static uint8_t leaf[512];
                static uint8_t work[512];
                SqliteTableCursor c;
                protocore_sqlite_table_cursor_begin(&c, mem_read, &m, DB_MP_PAGE_SIZE, 0, 2, leaf, work);
                uint64_t rid;
                SqliteRecordCursor rec;
                uint64_t st;
                const uint8_t *v;
                uint32_t vl;
                while (protocore_sqlite_table_cursor_next(&c, &rid, &rec))
                {
                    while (protocore_sqlite_record_next(&rec, &st, &v, &vl))
                    {
                        s += st;
                    }
                }
            },
            ns);
        hbench_row("sqlite", "table scan (40 rows)", ns, 0);
        hbench_row("sqlite", "  -> per row (+cols)", ns / (double)DB_MP_ROWS, 0);
        (void)s;
    }

    // ---- Redis RESP ----
    {
        char out[128];
        const char *const args[] = {"SET", "session:42", "hello-world-value"};
        const size_t lens[] = {3, 10, 17};
        volatile size_t sink = 0;
        double ne = 0.0;
        HBENCH_NS(2000000, sink += resp_encode(out, sizeof(out), args, lens, 3), ne);
        hbench_row("resp", "encode_command (3 args)", ne, (double)(3 + 10 + 17));

        const uint8_t bulk[] = "$17\r\nhello-world-value\r\n";
        double np = 0.0;
        HBENCH_NS(
            5000000,
            {
                RespReply r;
                if (resp_take(&r, bulk, sizeof(bulk) - 1))
                {
                    sink += r.str_len;
                }
            },
            np);
        hbench_row("resp", "parse bulk reply", np, 0);
        (void)sink;
    }

    return 0;
}
