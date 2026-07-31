// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the docstore JSON document store (services/storage/docstore): put
// (overwrite an existing document), get, and the two top-level field queries (find_str / find_int) that
// scan the live document set through the zero-heap JSON reader - all pure CPU-side work (dbm index probe
// + JSON parse/compare) with no flash/SD hardware involved. Docstore is a thin layer over dbm (dbm.h), so
// - exactly like performance_benching/device/dbm - the WAL's block-device seam (WalDev) is backed by a plain static RAM
// buffer here, the same way test/test_docstore/test_docstore.cpp drives it on the host (a RamDisk with
// read/write/sync function pointers): this rig has no SD card attached, so the durable-storage half of the
// stack (fs::FS/SPI) is out of scope everywhere; only the deterministic codec above that seam is benched.
// put appends one WAL record and the field-query fill seeds NUM_DOCS more, so the RAM disk is reformatted
// at the top of every pass to keep each run's log growth bounded and repeatable.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/docstore -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a capture
// opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/storage/dbm/dbm.h"
#include "services/storage/docstore/docstore.h"
#include "services/storage/wal/wal_store.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

// RAM-backed WalDev (same shape as test/test_docstore/test_docstore.cpp's RamDisk) - satisfies the WAL's
// block-device seam with a plain buffer instead of a real fs::FS/SD transaction.
struct RamDisk
{
    uint8_t *buf;
    uint64_t size;
};

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
static bool ram_sync(void *)
{
    return true; // RAM has no durability barrier to wait on
}
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

static uint8_t g_disk[131072]; // 128 KiB: comfortably covers one pass's seed + put/find traffic below
static RamDisk g_ramdisk = {g_disk, sizeof(g_disk)};
static WalDev g_dev;
static WalStore g_wal;
static pc_dbm g_db;
static pc_doc_store g_ds;

static bool put_s(const char *id, const char *json)
{
    return pc_docstore_put(&g_ds, id, (uint16_t)strlen(id), (const uint8_t *)json, (uint32_t)strlen(json));
}

// Field-query fill: NUM_DOCS documents split 2:1 paris/lyon, bodies lifted verbatim from
// test_docstore.cpp's test_find_by_field, so pc_docstore_find_str/find_int below scan a realistic-sized
// live set instead of a single document.
#define NUM_DOCS 60
static const char *doc_paris = "{\"name\":\"alice\",\"age\":30,\"city\":\"paris\"}"; // test_find_by_field
static const char *doc_lyon = "{\"name\":\"carol\",\"age\":25,\"city\":\"lyon\"}";   // test_find_by_field

static void docstore_bench_task(void *)
{
    static uint8_t getbuf[PC_DBM_VAL_MAX + 1];

    for (;;)
    {
        // Fresh log + index + docstore each pass, so a repeating run never accumulates dead space or
        // exhausts g_disk across cycles (same reformat-per-pass pattern as performance_benching/device/dbm).
        g_dev = dev_over(&g_ramdisk);
        pc_wal_store_format(&g_wal, &g_dev);
        pc_dbm_open(&g_db, &g_wal);
        pc_docstore_open(&g_ds, &g_db);

        // Seed the field-query set.
        for (int i = 0; i < NUM_DOCS; i++)
        {
            char id[8];
            snprintf(id, sizeof(id), "d%03d", i);
            put_s(id, (i % 3 == 0) ? doc_lyon : doc_paris);
        }
        // Seed the put/get target (same id + literal body as test_put_get_del).
        put_s("u1", "{\"name\":\"alice\",\"age\":30,\"admin\":true}");

        Serial.printf("DB ==== docstore device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());

        volatile bool sinkb = false;
        volatile long sinkl = 0;
        volatile uint32_t sinku = 0;

        // Replacement body for the overwrite bench below (test_put_get_del's "alice2" update) - length
        // computed once per pass, outside the timed expr.
        static const char *put_update_json = "{\"name\":\"alice2\",\"age\":31}";
        const uint32_t put_update_len = (uint32_t)strlen(put_update_json);

        // Overwrite an existing document: pc_docstore_put is pc_dbm_put underneath, so this still
        // appends one WAL record per call - N is sized to stay well inside one pass's g_disk capacity.
        DBENCH_OP("pc_docstore_put (overwrite)", 1000,
                  sinkb = pc_docstore_put(&g_ds, "u1", 2, (const uint8_t *)put_update_json, put_update_len));

        // Read path: dbm index lookup + a pread of the JSON body back out of the WAL data region. Does
        // not append, so it is free to run at a much larger N.
        DBENCH_OP("pc_docstore_get (existing id)", 50000,
                  sinkl = pc_docstore_get(&g_ds, "u1", 2, getbuf, sizeof(getbuf)));

        // Field query: scan all NUM_DOCS live documents, parsing each JSON body through the zero-heap
        // json.h reader and comparing the top-level string field. Null callback = count-only (the same
        // shape as test_find_count_only_null_cb).
        DBENCH_OP("pc_docstore_find_str (city==paris)", 2000,
                  sinku = pc_docstore_find_str(&g_ds, "city", "paris", nullptr, nullptr));

        // Same scan, top-level integer field comparison.
        DBENCH_OP("pc_docstore_find_int (age==30)", 2000,
                  sinku = pc_docstore_find_int(&g_ds, "age", 30, nullptr, nullptr));

        (void)sinkb;
        (void)sinkl;
        (void)sinku;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: docstore device microbench");
    xTaskCreatePinnedToCore(docstore_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
