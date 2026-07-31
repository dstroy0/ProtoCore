// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the dbm log-structured key-value store (services/storage/dbm): put
// (overwrite an existing key), get, contains, delete, and the index rebuild that pc_dbm_open performs
// by replaying the WAL - all pure CPU-side work (hashing, open-addressed probing, WAL record framing)
// with no flash/SD hardware involved. The WAL's block-device seam (WalDev) is backed by a plain static
// RAM buffer here, exactly the way test/test_dbm/test_dbm.cpp drives it on the host (a RamDisk with
// read/write/sync function pointers) - this rig has no SD card attached, so the durable-storage half of
// the stack (fs::FS/SPI) is out of scope everywhere; only the deterministic codec above that seam is
// benched. put/delete each append one WAL record, so the RAM disk is reformatted at the top of every
// pass to keep each run's log growth bounded and repeatable.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/dbm -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a capture
// opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/storage/dbm/dbm.h"
#include "services/storage/wal/wal_store.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

// RAM-backed WalDev (same shape as test/test_dbm/test_dbm.cpp's RamDisk) - satisfies the WAL's
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
        return 0;
    memcpy(buf, d->buf + off, len);
    return len;
}
static size_t ram_write(void *ctx, uint64_t off, const uint8_t *buf, size_t len)
{
    RamDisk *d = (RamDisk *)ctx;
    if (off + len > d->size)
        return 0;
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

static uint8_t g_disk[131072]; // 128 KiB: comfortably covers one pass's put/fill/delete traffic below
static RamDisk g_ramdisk = {g_disk, sizeof(g_disk)};
static WalDev g_dev;
static WalStore g_wal;
static pc_dbm g_db;

static bool put_s(const char *k, const char *v)
{
    return pc_dbm_put(&g_db, k, (uint16_t)strlen(k), (const uint8_t *)v, (uint32_t)strlen(v));
}

// Steps a shared counter through "s00000".."s00255" (PC_DBM_SLOTS distinct keys, the same fill pattern
// test_dbm.cpp uses to saturate the index) so each of the 256 delete calls below removes a real, still-
// live key instead of repeatedly missing on an already-gone one.
static int g_del_idx = 0;
static bool del_next(void)
{
    char k[16];
    snprintf(k, sizeof(k), "s%05d", g_del_idx);
    g_del_idx++;
    return pc_dbm_del(&g_db, k, (uint16_t)strlen(k));
}

static void dbm_bench_task(void *)
{
    for (;;)
    {
        // Fresh log + index each pass, so a repeating run never accumulates dead space or exhausts
        // g_disk across cycles.
        g_dev = dev_over(&g_ramdisk);
        pc_wal_store_format(&g_wal, &g_dev);
        pc_dbm_open(&g_db, &g_wal);
        put_s("alpha", "one"); // seed the key the put/get/contains benches below overwrite/read
        g_del_idx = 0;

        Serial.printf("DB ==== dbm device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());

        volatile bool sinkb = false;
        volatile long sinkl = 0;
        static uint8_t getbuf[PC_DBM_VAL_MAX];

        // Overwrite an existing key: each call still appends one WAL record (write amplification is
        // real), so N is sized (with g_disk) to stay well inside one pass's capacity.
        DBENCH_OP("pc_dbm_put (overwrite)", 1000,
                  sinkb = pc_dbm_put(&g_db, "alpha", 5, (const uint8_t *)"ONE-UPDATED", 11));

        // Read path: index lookup + a pread of the value back out of the WAL data region. Does not
        // append, so it is free to run at a much larger N.
        DBENCH_OP("pc_dbm_get (existing key)", 50000, sinkl = pc_dbm_get(&g_db, "alpha", 5, getbuf, sizeof(getbuf)));

        // Existence check: same probe as get, without the value read.
        DBENCH_OP("pc_dbm_contains", 50000, sinkb = pc_dbm_contains(&g_db, "alpha", 5));

        // Fill the index to its PC_DBM_SLOTS capacity (same "s%05d" pattern test_dbm.cpp uses) so the
        // delete bench below has 256 real, distinct live keys to remove.
        for (int i = 0; i < PC_DBM_SLOTS; i++)
        {
            char k[16];
            snprintf(k, sizeof(k), "s%05d", i);
            put_s(k, "x");
        }
        // Delete path: tombstones one WAL record per call and drops the key from the index. 255 loop
        // iterations plus the macro's one warm-up call walks all 256 filled keys exactly once each.
        DBENCH_OP("pc_dbm_del (256 distinct keys)", 255, sinkb = del_next());

        // Boot-time cost: rebuild the whole in-RAM index by replaying every record currently in the
        // WAL (puts, the overwrite churn, and the tombstones above). Pure read of the log; repeating it
        // does not grow the log further.
        DBENCH_OP("pc_dbm_open (index rebuild)", 200, sinkb = pc_dbm_open(&g_db, &g_wal));

        (void)sinkb;
        (void)sinkl;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: dbm device microbench");
    xTaskCreatePinnedToCore(dbm_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
