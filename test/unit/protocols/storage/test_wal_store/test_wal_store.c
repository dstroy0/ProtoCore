// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "services/storage/wal/wal_store.h"
#include <stdint.h>
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
    int syncs;
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
static proto_bool ram_sync(void *ctx)
{
    ((RamDisk *)ctx)->syncs++;
    return PROTO_TRUE;
}

static WalDev make_dev(RamDisk *d)
{
    WalDev dev;
    dev.read = ram_read;
    dev.write = ram_write;
    dev.sync = ram_sync;
    dev.ctx = d;
    dev.size = d->size;
    return dev;
}

static uint8_t g_disk[4096];

static const uint32_t REC = (uint32_t)WAL_RECORD_HEADER;

typedef struct
{
    uint8_t *buf;
    uint64_t size;
    uint64_t read_fail_ge;
    size_t read_fail_len;
    uint64_t write_fail_ge;
    uint64_t write_fail_lt;
    int sync_calls;
    int sync_fail_on;
} FaultDisk;
static size_t fault_read(void *ctx, uint64_t off, uint8_t *buf, size_t len)
{
    FaultDisk *d = (FaultDisk *)ctx;
    if (off >= d->read_fail_ge)
    {
        return 0;
    }
    if (d->read_fail_len && len >= d->read_fail_len)
    {
        return 0;
    }
    if (off + len > d->size)
    {
        return 0;
    }
    memcpy(buf, d->buf + off, len);
    return len;
}
static size_t fault_write(void *ctx, uint64_t off, const uint8_t *buf, size_t len)
{
    FaultDisk *d = (FaultDisk *)ctx;
    if (off >= d->write_fail_ge)
    {
        return 0;
    }
    if (off < d->write_fail_lt)
    {
        return 0;
    }
    if (off + len > d->size)
    {
        return 0;
    }
    memcpy(d->buf + off, buf, len);
    return len;
}
static proto_bool fault_sync(void *ctx)
{
    FaultDisk *d = (FaultDisk *)ctx;
    d->sync_calls++;
    return !(d->sync_fail_on && d->sync_calls == d->sync_fail_on);
}
static FaultDisk make_fault(uint8_t *buf, uint64_t size)
{
    FaultDisk d;
    d.buf = buf;
    d.size = size;
    d.read_fail_ge = UINT64_MAX;
    d.read_fail_len = 0;
    d.write_fail_ge = UINT64_MAX;
    d.write_fail_lt = 0;
    d.sync_calls = 0;
    d.sync_fail_on = 0;
    return d;
}
static WalDev make_fault_dev(FaultDisk *d)
{
    WalDev dev;
    dev.read = fault_read;
    dev.write = fault_write;
    dev.sync = fault_sync;
    dev.ctx = d;
    dev.size = d->size;
    return dev;
}

static int g_scan_count;
static uint64_t g_scan_seq[4];
static uint32_t g_scan_len[4];
static void scan_cb(uint64_t seq, uint64_t data_off, const uint8_t *payload, uint32_t len, void *ctx)
{
    (void)data_off;
    (void)payload;
    (void)ctx;
    if (g_scan_count < 4)
    {
        g_scan_seq[g_scan_count] = seq;
        g_scan_len[g_scan_count] = len;
    }
    g_scan_count++;
}

void test_format_then_mount_empty(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    TEST_ASSERT_EQUAL_UINT64(0, protocore_wal_store_used(&s));
    TEST_ASSERT_EQUAL_UINT64(0, protocore_wal_store_committed(&s));

    WalStore m;
    TEST_ASSERT_TRUE(protocore_wal_store_mount(&m, &dev));
    TEST_ASSERT_EQUAL_UINT64(0, protocore_wal_store_used(&m));
    TEST_ASSERT_EQUAL_UINT64(0, protocore_wal_store_committed(&m));
}

void test_mount_unformatted_fails(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    memset(g_disk, 0xAB, sizeof(g_disk));
    WalDev dev = make_dev(&d);
    WalStore m;
    TEST_ASSERT_FALSE(protocore_wal_store_mount(&m, &dev));
}

void test_append_without_checkpoint_recovers_via_tail(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"alpha", 5));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"bravo", 5));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"c", 1));
    uint64_t expect = (REC + 5) + (REC + 5) + (REC + 1);
    TEST_ASSERT_EQUAL_UINT64(expect, protocore_wal_store_used(&s));

    WalStore m;
    TEST_ASSERT_TRUE(protocore_wal_store_mount(&m, &dev));
    TEST_ASSERT_EQUAL_UINT64(expect, protocore_wal_store_used(&m));
    TEST_ASSERT_EQUAL_UINT64(0, protocore_wal_store_committed(&m));
}

void test_checkpoint_commits_then_tail(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"one", 3));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"two", 3));
    TEST_ASSERT_TRUE(protocore_wal_store_checkpoint(&s));
    uint64_t committed = 2 * (REC + 3);
    TEST_ASSERT_EQUAL_UINT64(committed, protocore_wal_store_committed(&s));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"three", 5));

    WalStore m;
    TEST_ASSERT_TRUE(protocore_wal_store_mount(&m, &dev));
    TEST_ASSERT_EQUAL_UINT64(committed, protocore_wal_store_committed(&m));
    TEST_ASSERT_EQUAL_UINT64(committed + REC + 5, protocore_wal_store_used(&m));
}

void test_torn_tail_recovers_to_last_good(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"one", 3));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"two", 3));
    TEST_ASSERT_TRUE(protocore_wal_store_checkpoint(&s));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"three", 5));
    uint64_t good = 2 * (REC + 3);

    g_disk[WAL_DATA_OFFSET + good + WAL_RECORD_HEADER + 1] ^= 0xFF;

    WalStore m;
    TEST_ASSERT_TRUE(protocore_wal_store_mount(&m, &dev));
    TEST_ASSERT_EQUAL_UINT64(good, protocore_wal_store_used(&m));
}

void test_ab_superblock_fallback(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"one", 3));
    TEST_ASSERT_TRUE(protocore_wal_store_checkpoint(&s));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"two", 3));
    TEST_ASSERT_TRUE(protocore_wal_store_checkpoint(&s));
    uint64_t committed = 2 * (REC + 3);
    TEST_ASSERT_EQUAL_INT(0, s.ab);

    memset(g_disk + 0 * WAL_SUPER_SIZE, 0xFF, WAL_SUPER_SIZE);

    WalStore m;
    TEST_ASSERT_TRUE(protocore_wal_store_mount(&m, &dev));
    TEST_ASSERT_EQUAL_UINT64(REC + 3, protocore_wal_store_committed(&m));
    TEST_ASSERT_EQUAL_UINT64(committed, protocore_wal_store_used(&m));
}

void test_append_full_fails_closed(void)
{
    static uint8_t tiny[WAL_DATA_OFFSET + 100];
    RamDisk d = {tiny, sizeof(tiny), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    TEST_ASSERT_EQUAL_UINT64(100, protocore_wal_store_capacity(&s));

    int ok = 0;
    for (int i = 0; i < 10; i++)
    {
        if (protocore_wal_store_append(&s, NULL, 0))
        {
            ok++;
        }
    }
    TEST_ASSERT_EQUAL_INT(5, ok);
    TEST_ASSERT_TRUE(protocore_wal_store_used(&s) <= protocore_wal_store_capacity(&s));

    WalStore m;
    TEST_ASSERT_TRUE(protocore_wal_store_mount(&m, &dev));
    TEST_ASSERT_EQUAL_UINT64(5 * REC, protocore_wal_store_used(&m));
}

void test_format_and_mount_too_small(void)
{
    RamDisk d = {g_disk, WAL_DATA_OFFSET, 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_FALSE(protocore_wal_store_format(&s, &dev));
    TEST_ASSERT_FALSE(protocore_wal_store_format(&s, NULL));
    TEST_ASSERT_FALSE(protocore_wal_store_mount(&s, &dev));
    TEST_ASSERT_FALSE(protocore_wal_store_mount(&s, NULL));
}

void test_format_write_b_unwired_fails(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    dev.write = NULL;
    WalStore s;
    TEST_ASSERT_FALSE(protocore_wal_store_format(&s, &dev));
}

void test_format_write_super_a_fails(void)
{
    FaultDisk fd = make_fault(g_disk, sizeof(g_disk));
    fd.write_fail_lt = WAL_SUPER_SIZE;
    WalDev dev = make_fault_dev(&fd);
    WalStore s;
    TEST_ASSERT_FALSE(protocore_wal_store_format(&s, &dev));
}

void test_null_sync_still_commits(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    dev.sync = NULL;
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"one", 3));
    TEST_ASSERT_TRUE(protocore_wal_store_checkpoint(&s));
    TEST_ASSERT_EQUAL_UINT64(REC + 3, protocore_wal_store_committed(&s));
    WalStore m;
    TEST_ASSERT_TRUE(protocore_wal_store_mount(&m, &dev));
    TEST_ASSERT_EQUAL_UINT64(REC + 3, protocore_wal_store_committed(&m));
}

void test_mount_read_unwired_fails(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    WalDev bad = dev;
    bad.read = NULL;
    WalStore m;
    TEST_ASSERT_FALSE(protocore_wal_store_mount(&m, &bad));
}

void test_mount_super_crc_mismatch(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    g_disk[4] ^= 0xFF;
    WalStore m;
    TEST_ASSERT_FALSE(protocore_wal_store_mount(&m, &dev));
}

void test_mount_head_past_capacity_rejected(void)
{
    RamDisk big = {g_disk, sizeof(g_disk), 0};
    WalDev bigdev = make_dev(&big);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &bigdev));
    static uint8_t blob[3000];
    memset(blob, 0x5A, sizeof(blob));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, blob, sizeof(blob)));
    TEST_ASSERT_TRUE(protocore_wal_store_checkpoint(&s));

    RamDisk small = {g_disk, 2000, 0};
    WalDev smalldev = make_dev(&small);
    WalStore m;
    TEST_ASSERT_TRUE(protocore_wal_store_mount(&m, &smalldev));
    TEST_ASSERT_EQUAL_UINT64(0, protocore_wal_store_committed(&m));
}

void test_replay_truncated_len_stops(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"x", 1));
    memset(g_disk + WAL_DATA_OFFSET + 12, 0xFF, 4);
    WalStore m;
    TEST_ASSERT_TRUE(protocore_wal_store_mount(&m, &dev));
    TEST_ASSERT_EQUAL_UINT64(0, protocore_wal_store_used(&m));
}

void test_replay_header_read_fails(void)
{
    FaultDisk fd = make_fault(g_disk, sizeof(g_disk));
    WalDev dev = make_fault_dev(&fd);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    fd.read_fail_ge = WAL_DATA_OFFSET;
    WalStore m;
    TEST_ASSERT_TRUE(protocore_wal_store_mount(&m, &dev));
    TEST_ASSERT_EQUAL_UINT64(0, protocore_wal_store_used(&m));
}

void test_replay_payload_read_fails(void)
{
    FaultDisk fd = make_fault(g_disk, sizeof(g_disk));
    WalDev dev = make_fault_dev(&fd);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"hello", 5));
    fd.read_fail_ge = WAL_DATA_OFFSET + WAL_RECORD_HEADER;
    WalStore m;
    TEST_ASSERT_TRUE(protocore_wal_store_mount(&m, &dev));
    TEST_ASSERT_EQUAL_UINT64(0, protocore_wal_store_used(&m));
}

void test_append_header_write_fails(void)
{
    FaultDisk fd = make_fault(g_disk, sizeof(g_disk));
    WalDev dev = make_fault_dev(&fd);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    fd.write_fail_ge = WAL_DATA_OFFSET;
    TEST_ASSERT_FALSE(protocore_wal_store_append(&s, (const uint8_t *)"x", 1));
}

void test_append_payload_write_fails(void)
{
    FaultDisk fd = make_fault(g_disk, sizeof(g_disk));
    WalDev dev = make_fault_dev(&fd);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    fd.write_fail_ge = WAL_DATA_OFFSET + WAL_RECORD_HEADER;
    TEST_ASSERT_FALSE(protocore_wal_store_append(&s, (const uint8_t *)"hello", 5));
}

void test_checkpoint_super_write_fails(void)
{
    FaultDisk fd = make_fault(g_disk, sizeof(g_disk));
    WalDev dev = make_fault_dev(&fd);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    fd.write_fail_ge = 0;
    TEST_ASSERT_FALSE(protocore_wal_store_checkpoint(&s));
}

void test_checkpoint_second_sync_fails(void)
{
    FaultDisk fd = make_fault(g_disk, sizeof(g_disk));
    WalDev dev = make_fault_dev(&fd);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    fd.sync_calls = 0;
    fd.sync_fail_on = 2;
    TEST_ASSERT_FALSE(protocore_wal_store_checkpoint(&s));
}

void test_scan_reads_records(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"alpha", 5));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"bravo", 5));
    g_scan_count = 0;
    uint8_t scratch[128];
    TEST_ASSERT_EQUAL_UINT(2, protocore_wal_store_scan(&s, scan_cb, NULL, scratch, sizeof(scratch)));
    TEST_ASSERT_EQUAL_INT(2, g_scan_count);
    TEST_ASSERT_EQUAL_UINT64(0, g_scan_seq[0]);
    TEST_ASSERT_EQUAL_UINT64(1, g_scan_seq[1]);
    TEST_ASSERT_EQUAL_UINT32(5, g_scan_len[0]);
}

void test_scan_null_callback_counts(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"one", 3));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"two", 3));
    uint8_t scratch[128];
    TEST_ASSERT_EQUAL_UINT(2, protocore_wal_store_scan(&s, NULL, NULL, scratch, sizeof(scratch)));
}

void test_scan_scratch_too_small(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"x", 1));
    uint8_t scratch[WAL_RECORD_HEADER];
    TEST_ASSERT_EQUAL_UINT(0, protocore_wal_store_scan(&s, scan_cb, NULL, scratch, WAL_RECORD_HEADER - 1));
}

void test_scan_header_read_fails(void)
{
    FaultDisk fd = make_fault(g_disk, sizeof(g_disk));
    WalDev dev = make_fault_dev(&fd);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"x", 1));
    fd.read_fail_ge = WAL_DATA_OFFSET;
    g_scan_count = 0;
    uint8_t scratch[128];
    TEST_ASSERT_EQUAL_UINT(0, protocore_wal_store_scan(&s, scan_cb, NULL, scratch, sizeof(scratch)));
    TEST_ASSERT_EQUAL_INT(0, g_scan_count);
}

void test_scan_full_read_fails(void)
{
    FaultDisk fd = make_fault(g_disk, sizeof(g_disk));
    WalDev dev = make_fault_dev(&fd);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"hi", 2));
    fd.read_fail_len = WAL_RECORD_HEADER + 1;
    uint8_t scratch[128];
    TEST_ASSERT_EQUAL_UINT(0, protocore_wal_store_scan(&s, scan_cb, NULL, scratch, sizeof(scratch)));
}

void test_scan_bad_magic_stops(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"x", 1));
    g_disk[WAL_DATA_OFFSET] ^= 0xFF;
    uint8_t scratch[128];
    TEST_ASSERT_EQUAL_UINT(0, protocore_wal_store_scan(&s, scan_cb, NULL, scratch, sizeof(scratch)));
}

void test_scan_crc_mismatch_stops(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"hello", 5));
    g_disk[WAL_DATA_OFFSET + WAL_RECORD_HEADER + 1] ^= 0xFF;
    uint8_t scratch[128];
    TEST_ASSERT_EQUAL_UINT(0, protocore_wal_store_scan(&s, scan_cb, NULL, scratch, sizeof(scratch)));
}

void test_mount_picks_newer_generation_a(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"one", 3));
    TEST_ASSERT_TRUE(protocore_wal_store_checkpoint(&s));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"two", 3));
    TEST_ASSERT_TRUE(protocore_wal_store_checkpoint(&s));
    TEST_ASSERT_EQUAL_INT(0, s.ab);
    uint64_t committed = 2 * (REC + 3);

    WalStore m;
    TEST_ASSERT_TRUE(protocore_wal_store_mount(&m, &dev));
    TEST_ASSERT_EQUAL_INT(0, m.ab);
    TEST_ASSERT_EQUAL_UINT64(committed, protocore_wal_store_committed(&m));
}

void test_replay_tail_seq_not_bumped_when_not_newer(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));

    uint8_t buf[128];
    size_t n0 = protocore_wal_record_encode(buf, sizeof(buf), 5, (const uint8_t *)"one", 3);
    size_t n1 = protocore_wal_record_encode(buf + n0, sizeof(buf) - n0, 2, (const uint8_t *)"two", 3);
    memcpy(g_disk + WAL_DATA_OFFSET, buf, n0 + n1);

    WalStore m;
    TEST_ASSERT_TRUE(protocore_wal_store_mount(&m, &dev));
    TEST_ASSERT_EQUAL_UINT64(n0 + n1, protocore_wal_store_used(&m));
    TEST_ASSERT_EQUAL_UINT64(6, m.next_seq);
}

void test_format_sync_fails(void)
{
    FaultDisk fd = make_fault(g_disk, sizeof(g_disk));
    WalDev dev = make_fault_dev(&fd);
    fd.sync_fail_on = 1;
    WalStore s;
    TEST_ASSERT_FALSE(protocore_wal_store_format(&s, &dev));
}

void test_checkpoint_first_sync_fails(void)
{
    FaultDisk fd = make_fault(g_disk, sizeof(g_disk));
    WalDev dev = make_fault_dev(&fd);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    fd.sync_calls = 0;
    fd.sync_fail_on = 1;
    TEST_ASSERT_FALSE(protocore_wal_store_checkpoint(&s));
}

void test_scan_stops_on_length_overrun(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"hi", 2));
    memset(g_disk + WAL_DATA_OFFSET + 12, 0xFF, 4);
    uint8_t scratch[128];
    g_scan_count = 0;
    TEST_ASSERT_EQUAL_UINT(0, protocore_wal_store_scan(&s, scan_cb, NULL, scratch, sizeof(scratch)));
    TEST_ASSERT_EQUAL_INT(0, g_scan_count);
}

void test_scan_stops_when_record_exceeds_scratch(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"hello world!", 12));
    uint8_t scratch[WAL_RECORD_HEADER + 4];
    g_scan_count = 0;
    TEST_ASSERT_EQUAL_UINT(0, protocore_wal_store_scan(&s, scan_cb, NULL, scratch, sizeof(scratch)));
    TEST_ASSERT_EQUAL_INT(0, g_scan_count);
}

void test_pread_in_and_out_of_range(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(protocore_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(protocore_wal_store_append(&s, (const uint8_t *)"hello", 5));
    uint8_t buf[8];
    TEST_ASSERT_TRUE(protocore_wal_store_pread(&s, WAL_RECORD_HEADER, buf, 5));
    TEST_ASSERT_EQUAL_MEMORY("hello", buf, 5);
    TEST_ASSERT_FALSE(protocore_wal_store_pread(&s, protocore_wal_store_capacity(&s) - 2, buf, 5));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_format_then_mount_empty);
    RUN_TEST(test_mount_unformatted_fails);
    RUN_TEST(test_append_without_checkpoint_recovers_via_tail);
    RUN_TEST(test_checkpoint_commits_then_tail);
    RUN_TEST(test_torn_tail_recovers_to_last_good);
    RUN_TEST(test_ab_superblock_fallback);
    RUN_TEST(test_append_full_fails_closed);
    RUN_TEST(test_format_and_mount_too_small);
    RUN_TEST(test_format_write_b_unwired_fails);
    RUN_TEST(test_format_write_super_a_fails);
    RUN_TEST(test_null_sync_still_commits);
    RUN_TEST(test_mount_read_unwired_fails);
    RUN_TEST(test_mount_super_crc_mismatch);
    RUN_TEST(test_mount_head_past_capacity_rejected);
    RUN_TEST(test_replay_truncated_len_stops);
    RUN_TEST(test_replay_header_read_fails);
    RUN_TEST(test_replay_payload_read_fails);
    RUN_TEST(test_append_header_write_fails);
    RUN_TEST(test_append_payload_write_fails);
    RUN_TEST(test_checkpoint_super_write_fails);
    RUN_TEST(test_checkpoint_second_sync_fails);
    RUN_TEST(test_scan_reads_records);
    RUN_TEST(test_scan_null_callback_counts);
    RUN_TEST(test_scan_scratch_too_small);
    RUN_TEST(test_scan_header_read_fails);
    RUN_TEST(test_scan_full_read_fails);
    RUN_TEST(test_scan_bad_magic_stops);
    RUN_TEST(test_scan_crc_mismatch_stops);
    RUN_TEST(test_pread_in_and_out_of_range);
    RUN_TEST(test_mount_picks_newer_generation_a);
    RUN_TEST(test_replay_tail_seq_not_bumped_when_not_newer);
    RUN_TEST(test_format_sync_fails);
    RUN_TEST(test_checkpoint_first_sync_fails);
    RUN_TEST(test_scan_stops_on_length_overrun);
    RUN_TEST(test_scan_stops_when_record_exceeds_scratch);
    return UNITY_END();
}
