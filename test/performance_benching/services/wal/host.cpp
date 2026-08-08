// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host microbenchmarks for the embedded data-store stack (WAL / dbm / docstore / SQLite reader / RESP).
// A deterministic ns/op CPU baseline for docs/FEATURE_PERFORMANCE.md: it measures the *compute* cost of
// each hot op over a RAM-backed device, so combined with the measured SD I/O envelope (section 1) it shows
// where the real-world cost lives (spoiler: the stores are I/O-bound, the CPU cost is tiny). Build + run:
//   g++ -O2 -std=c++17 -Isrc -DPC_ENABLE_WAL=1 -DPC_ENABLE_DBM=1 -DPC_ENABLE_DOCSTORE=1 \
//       -DPC_ENABLE_SQLITE=1 -DPC_ENABLE_REDIS=1 performance_benching/services/wal/host.cpp \
//       src/services/storage/wal/wal.cpp src/services/storage/wal/wal_store.cpp src/services/storage/dbm/dbm.cpp \
//       src/services/storage/docstore/docstore.cpp src/network_drivers/presentation/codec/json/json.cpp \
//       src/services/storage/sqlite/sqlite_format.cpp src/services/iot/redis_resp/redis_resp.cpp -o /tmp/bench_ds &&
//       /tmp/bench_ds

#include "services/iot/redis_resp/redis_resp.h"
#include "services/storage/dbm/dbm.h"
#include "services/storage/docstore/docstore.h"
#include "services/storage/sqlite/sqlite_format.h"
#include "services/storage/wal/wal_store.h"
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "../test/test_sqlite/db_multipage.h"

using clk = std::chrono::steady_clock;

template <typename F> static double bench_ns(uint64_t iters, F fn)
{
    auto t0 = clk::now();
    for (uint64_t i = 0; i < iters; i++)
    {
        fn();
    }
    auto t1 = clk::now();
    return std::chrono::duration<double, std::nano>(t1 - t0).count() / (double)iters;
}

static void row(const char *feature, const char *op, double ns_per_op, double bytes_per_op)
{
    double mbps = bytes_per_op > 0 ? (bytes_per_op / (ns_per_op * 1e-9)) / 1e6 : 0.0;
    if (bytes_per_op > 0)
    {
        printf("| %-10s | %-26s | %10.1f | %8.1f |\n", feature, op, ns_per_op, mbps);
    }
    else
    {
        printf("| %-10s | %-26s | %10.1f | %8s |\n", feature, op, ns_per_op, "-");
    }
}

// A RAM-backed WalDev over a caller buffer (no I/O; measures pure CPU cost).
struct RamDisk
{
    uint8_t *buf;
    uint64_t size;
};
static size_t ram_read(void *ctx, uint64_t off, uint8_t *b, size_t n)
{
    RamDisk *d = (RamDisk *)ctx;
    if (off + n > d->size)
    {
        return 0;
    }
    memcpy(b, d->buf + off, n);
    return n;
}
static size_t ram_write(void *ctx, uint64_t off, const uint8_t *b, size_t n)
{
    RamDisk *d = (RamDisk *)ctx;
    if (off + n > d->size)
    {
        return 0;
    }
    memcpy(d->buf + off, b, n);
    return n;
}
static bool ram_sync(void *)
{
    return true;
}
static WalDev dev_over(RamDisk *d)
{
    WalDev v{ram_read, ram_write, ram_sync, d, d->size};
    return v;
}

static bool mem_read(void *ctx, uint32_t pgno, uint8_t *page, uint32_t page_size)
{
    RamDisk *m = (RamDisk *)ctx;
    uint64_t off = (uint64_t)(pgno - 1) * page_size;
    if (pgno < 1 || off + page_size > m->size)
    {
        return false;
    }
    memcpy(page, m->buf + off, page_size);
    return true;
}

int main()
{
    printf("| Feature    | Operation                  |     ns/op |    MB/s |\n");
    printf("|------------|----------------------------|-----------|---------|\n");

    // ---- WAL ----
    {
        const size_t N = 1024;
        uint8_t src[N];
        for (size_t i = 0; i < N; i++)
        {
            src[i] = (uint8_t)(i * 31 + 7);
        }
        volatile uint32_t sink = 0;
        double ns = bench_ns(500000, [&] { sink += pc_wal_crc32(src, N); });
        row("wal", "crc32 (1 KiB)", ns, (double)N);

        uint8_t rec[256];
        double ns2 = bench_ns(2000000, [&] { sink += (uint32_t)pc_wal_record_encode(rec, sizeof(rec), 1, src, 128); });
        row("wal", "record_encode (128 B)", ns2, 128.0);
        (void)sink;
    }
    {
        // append into a disk large enough for the whole run (measure the append CPU path).
        const uint64_t ITERS = 200000;
        const uint32_t PLEN = 64;
        uint64_t cap = ITERS * (WAL_RECORD_HEADER + PLEN) + 4096;
        uint8_t *disk = (uint8_t *)malloc(cap);
        RamDisk d{disk, cap};
        WalDev dev = dev_over(&d);
        WalStore s;
        pc_wal_store_format(&s, &dev);
        uint8_t pay[PLEN];
        memset(pay, 0xAB, PLEN);
        uint64_t i = 0;
        double ns = bench_ns(ITERS, [&] {
            pc_wal_store_append(&s, pay, PLEN);
            i++;
        });
        row("wal", "store_append (64 B)", ns, (double)PLEN);
        // checkpoint cost in isolation
        double nc = bench_ns(200000, [&] { pc_wal_store_checkpoint(&s); });
        row("wal", "store_checkpoint", nc, 0);
        free(disk);
    }

    // ---- dbm (steady state over a 100-key working set) ----
    {
        uint8_t *disk = (uint8_t *)malloc(16u * 1024 * 1024);
        RamDisk d{disk, 16u * 1024 * 1024};
        WalDev dev = dev_over(&d);
        static WalStore wal;
        static pc_dbm db;
        pc_wal_store_format(&wal, &dev);
        pc_dbm_open(&db, &wal);
        char keys[100][8];
        uint8_t val[64];
        memset(val, 0x5A, sizeof(val));
        for (int k = 0; k < 100; k++)
        {
            snprintf(keys[k], sizeof(keys[k]), "k%05d", k);
        }
        for (int k = 0; k < 100; k++)
        {
            pc_dbm_put(&db, keys[k], (uint16_t)strlen(keys[k]), val, sizeof(val));
        }
        int idx = 0;
        double nput = bench_ns(500000, [&] {
            pc_dbm_put(&db, keys[idx], (uint16_t)strlen(keys[idx]), val, sizeof(val));
            idx = (idx + 1) % 100;
        });
        row("dbm", "put (16B key/64B val)", nput, 0);
        uint8_t out[64];
        volatile long sink = 0;
        idx = 0;
        double nget = bench_ns(2000000, [&] {
            sink += pc_dbm_get(&db, keys[idx], (uint16_t)strlen(keys[idx]), out, sizeof(out));
            idx = (idx + 1) % 100;
        });
        row("dbm", "get", nget, 0);
        (void)sink;
        free(disk);
    }

    // ---- docstore field query (scan 100 JSON docs) ----
    {
        uint8_t *disk = (uint8_t *)malloc(16u * 1024 * 1024);
        RamDisk d{disk, 16u * 1024 * 1024};
        WalDev dev = dev_over(&d);
        static WalStore wal;
        static pc_dbm db;
        static pc_doc_store ds;
        pc_wal_store_format(&wal, &dev);
        pc_dbm_open(&db, &wal);
        pc_docstore_open(&ds, &db);
        for (int k = 0; k < 100; k++)
        {
            char id[8], doc[80];
            snprintf(id, sizeof(id), "u%05d", k);
            snprintf(doc, sizeof(doc), "{\"city\":\"%s\",\"age\":%d,\"n\":%d}", (k % 2) ? "paris" : "lyon", 20 + k % 40,
                     k);
            pc_docstore_put(&ds, id, (uint16_t)strlen(id), (const uint8_t *)doc, (uint32_t)strlen(doc));
        }
        volatile uint32_t sink = 0;
        double nf = bench_ns(20000, [&] { sink += pc_docstore_find_str(&ds, "city", "paris", nullptr, nullptr); });
        row("docstore", "find_str (scan 100)", nf, 0);
        printf("| %-10s | %-26s | %10.1f | %8s |\n", "docstore", "  -> per doc scanned", nf / 100.0, "-");
        (void)sink;
        free(disk);
    }

    // ---- SQLite reader ----
    {
        const uint8_t vi[] = {0x83, 0x5e};
        volatile uint64_t s = 0;
        double nv = bench_ns(5000000, [&] {
            uint64_t v;
            s += pc_sqlite_varint_decode(vi, 2, &v);
        });
        row("sqlite", "varint_decode", nv, 0);

        // A full table scan of the 40-row, 2-level b-tree fixture: ns per row.
        RamDisk m{(uint8_t *)DB_MULTIPAGE, sizeof(DB_MULTIPAGE)};
        double ns = bench_ns(20000, [&] {
            static uint8_t leaf[512], work[512];
            SqliteTableCursor c;
            pc_sqlite_table_cursor_begin(&c, mem_read, &m, DB_MP_PAGE_SIZE, 0, 2, leaf, work);
            uint64_t rid;
            SqliteRecordCursor row;
            uint64_t st;
            const uint8_t *v;
            uint32_t vl;
            while (pc_sqlite_table_cursor_next(&c, &rid, &row))
            {
                while (pc_sqlite_record_next(&row, &st, &v, &vl))
                {
                    s += st;
                }
            }
        });
        row("sqlite", "table scan (40 rows)", ns, 0);
        printf("| %-10s | %-26s | %10.1f | %8s |\n", "sqlite", "  -> per row (+cols)", ns / (double)DB_MP_ROWS, "-");
        (void)s;
    }

    // ---- Redis RESP ----
    {
        char out[128];
        const char *argv[] = {"SET", "session:42", "hello-world-value"};
        const size_t lens[] = {3, 10, 17};
        volatile size_t sink = 0;
        double ne = bench_ns(2000000, [&] { sink += pc_resp_encode_command(out, sizeof(out), argv, lens, 3); });
        row("resp", "encode_command (3 args)", ne, (double)(3 + 10 + 17));

        const uint8_t bulk[] = "$17\r\nhello-world-value\r\n";
        double np = bench_ns(5000000, [&] {
            RespReply r;
            size_t c;
            if (pc_resp_parse(bulk, sizeof(bulk) - 1, &r, &c))
            {
                sink += r.str_len;
            }
        });
        row("resp", "parse bulk reply", np, 0);
        (void)sink;
    }

    return 0;
}
