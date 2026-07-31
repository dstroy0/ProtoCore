// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/storage/wal pc_wal_store: A/B superblock + checkpoint + mount/recover over a RAM device.
// A crash is modeled as: stop touching the store, then mount() a fresh WalStore over the same buffer.

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

// A RAM-backed WalDev: the "flash" is just a byte buffer; sync is a no-op that succeeds.
struct RamDisk
{
    uint8_t *buf;
    uint64_t size;
    int syncs;
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
static bool ram_sync(void *ctx)
{
    ((RamDisk *)ctx)->syncs++;
    return true;
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

// Shared backing buffer big enough for both superblocks + a comfortable data region.
static uint8_t g_disk[4096];

static const uint32_t REC = (uint32_t)WAL_RECORD_HEADER; // header-only record size base

// A fault-injecting device: a byte buffer with optional read/write faults keyed on offset or length,
// plus a sync that fails on a chosen call. make_fault() injects nothing; a test then arms a field to
// exercise the "device I/O failed" guards (short read/write, unreadable data region, sync failure).
struct FaultDisk
{
    uint8_t *buf;
    uint64_t size;
    uint64_t read_fail_ge;  // reads at off >= this return short
    size_t read_fail_len;   // reads with len >= this return short (0 = never)
    uint64_t write_fail_ge; // writes at off >= this return short
    uint64_t write_fail_lt; // writes at off <  this return short
    int sync_calls;
    int sync_fail_on; // 1-based sync call number that returns false (0 = never)
};
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
static bool fault_sync(void *ctx)
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

// Scan callback: records the seq / len of each record it is handed.
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
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));
    TEST_ASSERT_EQUAL_UINT64(0, pc_wal_store_used(&s));
    TEST_ASSERT_EQUAL_UINT64(0, pc_wal_store_committed(&s));

    WalStore m;
    TEST_ASSERT_TRUE(pc_wal_store_mount(&m, &dev));
    TEST_ASSERT_EQUAL_UINT64(0, pc_wal_store_used(&m));
    TEST_ASSERT_EQUAL_UINT64(0, pc_wal_store_committed(&m));
}

void test_mount_unformatted_fails(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    memset(g_disk, 0xAB, sizeof(g_disk)); // no valid superblock anywhere
    WalDev dev = make_dev(&d);
    WalStore m;
    TEST_ASSERT_FALSE(pc_wal_store_mount(&m, &dev));
}

// Records appended but never checkpointed must still be recovered by the tail replay (crash-before-commit).
void test_append_without_checkpoint_recovers_via_tail(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"alpha", 5));
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"bravo", 5));
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"c", 1));
    uint64_t expect = (REC + 5) + (REC + 5) + (REC + 1);
    TEST_ASSERT_EQUAL_UINT64(expect, pc_wal_store_used(&s));

    WalStore m; // "reboot"
    TEST_ASSERT_TRUE(pc_wal_store_mount(&m, &dev));
    TEST_ASSERT_EQUAL_UINT64(expect, pc_wal_store_used(&m)); // all three recovered
    TEST_ASSERT_EQUAL_UINT64(0, pc_wal_store_committed(&m)); // but none were checkpointed
}

// Checkpoint commits the head; a further append past it recovers on top of the committed head.
void test_checkpoint_commits_then_tail(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"one", 3));
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"two", 3));
    TEST_ASSERT_TRUE(pc_wal_store_checkpoint(&s));
    uint64_t committed = 2 * (REC + 3);
    TEST_ASSERT_EQUAL_UINT64(committed, pc_wal_store_committed(&s));
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"three", 5)); // not checkpointed

    WalStore m;
    TEST_ASSERT_TRUE(pc_wal_store_mount(&m, &dev));
    TEST_ASSERT_EQUAL_UINT64(committed, pc_wal_store_committed(&m));      // durable pointer
    TEST_ASSERT_EQUAL_UINT64(committed + REC + 5, pc_wal_store_used(&m)); // + the replayed 3rd
}

// A torn record after the checkpoint is discarded; mount recovers to the last good record.
void test_torn_tail_recovers_to_last_good(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"one", 3));
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"two", 3));
    TEST_ASSERT_TRUE(pc_wal_store_checkpoint(&s));
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"three", 5));
    uint64_t good = 2 * (REC + 3);

    // Corrupt a payload byte of the 3rd record on media -> its CRC now fails.
    g_disk[WAL_DATA_OFFSET + good + WAL_RECORD_HEADER + 1] ^= 0xFF;

    WalStore m;
    TEST_ASSERT_TRUE(pc_wal_store_mount(&m, &dev));
    TEST_ASSERT_EQUAL_UINT64(good, pc_wal_store_used(&m)); // torn 3rd dropped
}

// Two checkpoints make B the newest superblock; wiping B must fall back to A and still recover correctly.
void test_ab_superblock_fallback(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev)); // super in copy A
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"one", 3));
    TEST_ASSERT_TRUE(pc_wal_store_checkpoint(&s)); // writes copy B (gen 2)
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"two", 3));
    TEST_ASSERT_TRUE(pc_wal_store_checkpoint(&s)); // writes copy A (gen 3) - A now newest
    uint64_t committed = 2 * (REC + 3);
    TEST_ASSERT_EQUAL_INT(0, s.ab); // newest is copy A

    // Tear the *newest* superblock (copy A). Mount must fall back to copy B (gen 2, committed after 1 rec)
    // and then replay the tail to recover the 2nd record anyway.
    memset(g_disk + 0 * WAL_SUPER_SIZE, 0xFF, WAL_SUPER_SIZE);

    WalStore m;
    TEST_ASSERT_TRUE(pc_wal_store_mount(&m, &dev));
    TEST_ASSERT_EQUAL_UINT64(REC + 3, pc_wal_store_committed(&m)); // fell back to B's committed head
    TEST_ASSERT_EQUAL_UINT64(committed, pc_wal_store_used(&m));    // but recovered both via tail replay
}

// Appends fail closed when the data region is full; head never exceeds capacity.
void test_append_full_fails_closed(void)
{
    static uint8_t tiny[WAL_DATA_OFFSET + 100];
    RamDisk d = {tiny, sizeof(tiny), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));
    TEST_ASSERT_EQUAL_UINT64(100, pc_wal_store_capacity(&s));

    // Each header-only (len=0) record is WAL_RECORD_HEADER=20 bytes: 5 fit in 100, the 6th must fail.
    int ok = 0;
    for (int i = 0; i < 10; i++)
    {
        if (pc_wal_store_append(&s, nullptr, 0))
        {
            ok++;
        }
    }
    TEST_ASSERT_EQUAL_INT(5, ok);
    TEST_ASSERT_TRUE(pc_wal_store_used(&s) <= pc_wal_store_capacity(&s));

    // The full log still mounts back to exactly what fit.
    WalStore m;
    TEST_ASSERT_TRUE(pc_wal_store_mount(&m, &dev));
    TEST_ASSERT_EQUAL_UINT64(5 * REC, pc_wal_store_used(&m));
}

// ---------------------------------------------------------------------------------------------
// Guard / error-path coverage: too-small devices, unwired or failing device I/O, corrupt media.
// ---------------------------------------------------------------------------------------------

// format / mount reject a device that cannot hold both superblocks plus a data region (or a null one).
void test_format_and_mount_too_small(void)
{
    RamDisk d = {g_disk, WAL_DATA_OFFSET, 0}; // exactly the two superblocks, no data region
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_FALSE(pc_wal_store_format(&s, &dev));
    TEST_ASSERT_FALSE(pc_wal_store_format(&s, nullptr));
    TEST_ASSERT_FALSE(pc_wal_store_mount(&s, &dev));
    TEST_ASSERT_FALSE(pc_wal_store_mount(&s, nullptr));
}

// format fails when the copy-B invalidation write cannot be issued (write pointer unwired).
void test_format_write_b_unwired_fails(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    dev.write = nullptr; // dev_write's null-pointer guard trips on the first write
    WalStore s;
    TEST_ASSERT_FALSE(pc_wal_store_format(&s, &dev));
}

// format fails when copy A (the live superblock) cannot be written, though copy B was invalidated fine.
void test_format_write_super_a_fails(void)
{
    FaultDisk fd = make_fault(g_disk, sizeof(g_disk));
    fd.write_fail_lt = WAL_SUPER_SIZE; // writes below offset 64 (copy A) fail; copy B at 64 succeeds
    WalDev dev = make_fault_dev(&fd);
    WalStore s;
    TEST_ASSERT_FALSE(pc_wal_store_format(&s, &dev));
}

// A device with no sync barrier: format/append/checkpoint all take the "sync == null" arms and still commit.
void test_null_sync_still_commits(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    dev.sync = nullptr;
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"one", 3));
    TEST_ASSERT_TRUE(pc_wal_store_checkpoint(&s));
    TEST_ASSERT_EQUAL_UINT64(REC + 3, pc_wal_store_committed(&s));
    WalStore m;
    TEST_ASSERT_TRUE(pc_wal_store_mount(&m, &dev));
    TEST_ASSERT_EQUAL_UINT64(REC + 3, pc_wal_store_committed(&m));
}

// mount fails when the device read is unwired: read_super cannot read either copy.
void test_mount_read_unwired_fails(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev)); // valid superblocks on media
    WalDev bad = dev;
    bad.read = nullptr;
    WalStore m;
    TEST_ASSERT_FALSE(pc_wal_store_mount(&m, &bad));
}

// A superblock with a good magic but a corrupted CRC-covered byte is rejected by read_super's CRC check.
void test_mount_super_crc_mismatch(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));
    g_disk[4] ^= 0xFF; // flip a generation byte of copy A (magic at [0,4) stays intact) -> CRC fails
    WalStore m;
    TEST_ASSERT_FALSE(pc_wal_store_mount(&m, &dev)); // A fails CRC, B was zeroed at format -> both invalid
}

// A committed head that runs past the (shrunken) data capacity is rejected as corruption by read_super.
void test_mount_head_past_capacity_rejected(void)
{
    RamDisk big = {g_disk, sizeof(g_disk), 0};
    WalDev bigdev = make_dev(&big);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &bigdev));
    static uint8_t blob[3000];
    memset(blob, 0x5A, sizeof(blob));
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, blob, sizeof(blob)));
    TEST_ASSERT_TRUE(pc_wal_store_checkpoint(&s)); // copy B now commits a head of ~3020

    // Remount the same media through a device that reports a smaller size: copy B's committed head
    // exceeds the shrunken capacity, so read_super rejects it and mount falls back to copy A (head 0).
    RamDisk small = {g_disk, 2000, 0};
    WalDev smalldev = make_dev(&small);
    WalStore m;
    TEST_ASSERT_TRUE(pc_wal_store_mount(&m, &smalldev));
    TEST_ASSERT_EQUAL_UINT64(0, pc_wal_store_committed(&m));
}

// A tail record whose length field is corrupt to a huge value is dropped as a truncated tail (pre-CRC).
void test_replay_truncated_len_stops(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"x", 1)); // uncheckpointed tail record
    memset(g_disk + WAL_DATA_OFFSET + 12, 0xFF, 4);                     // len field -> 0xFFFFFFFF
    WalStore m;
    TEST_ASSERT_TRUE(pc_wal_store_mount(&m, &dev));
    TEST_ASSERT_EQUAL_UINT64(0, pc_wal_store_used(&m)); // record header + len overruns capacity -> stop
}

// The tail-replay header read fails: mount succeeds (super reads ok) but recovers nothing.
void test_replay_header_read_fails(void)
{
    FaultDisk fd = make_fault(g_disk, sizeof(g_disk));
    WalDev dev = make_fault_dev(&fd);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));
    fd.read_fail_ge = WAL_DATA_OFFSET; // data-region reads fail; superblock reads (< 128) still pass
    WalStore m;
    TEST_ASSERT_TRUE(pc_wal_store_mount(&m, &dev));
    TEST_ASSERT_EQUAL_UINT64(0, pc_wal_store_used(&m));
}

// The tail-replay payload read fails mid-record: the record is dropped as torn.
void test_replay_payload_read_fails(void)
{
    FaultDisk fd = make_fault(g_disk, sizeof(g_disk));
    WalDev dev = make_fault_dev(&fd);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"hello", 5));
    fd.read_fail_ge = WAL_DATA_OFFSET + WAL_RECORD_HEADER; // header read passes, payload read fails
    WalStore m;
    TEST_ASSERT_TRUE(pc_wal_store_mount(&m, &dev));
    TEST_ASSERT_EQUAL_UINT64(0, pc_wal_store_used(&m));
}

// append fails when the record header write is short.
void test_append_header_write_fails(void)
{
    FaultDisk fd = make_fault(g_disk, sizeof(g_disk));
    WalDev dev = make_fault_dev(&fd);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));
    fd.write_fail_ge = WAL_DATA_OFFSET; // the header write (first data-region write) fails
    TEST_ASSERT_FALSE(pc_wal_store_append(&s, (const uint8_t *)"x", 1));
}

// append fails when the header writes but the payload write is short.
void test_append_payload_write_fails(void)
{
    FaultDisk fd = make_fault(g_disk, sizeof(g_disk));
    WalDev dev = make_fault_dev(&fd);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));
    fd.write_fail_ge = WAL_DATA_OFFSET + WAL_RECORD_HEADER; // header write passes, payload write fails
    TEST_ASSERT_FALSE(pc_wal_store_append(&s, (const uint8_t *)"hello", 5));
}

// checkpoint fails when the new superblock write is short.
void test_checkpoint_super_write_fails(void)
{
    FaultDisk fd = make_fault(g_disk, sizeof(g_disk));
    WalDev dev = make_fault_dev(&fd);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));
    fd.write_fail_ge = 0; // every write now fails; the data sync still succeeds first
    TEST_ASSERT_FALSE(pc_wal_store_checkpoint(&s));
}

// checkpoint fails when the post-superblock sync fails (the data sync before it succeeded).
void test_checkpoint_second_sync_fails(void)
{
    FaultDisk fd = make_fault(g_disk, sizeof(g_disk));
    WalDev dev = make_fault_dev(&fd);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));
    fd.sync_calls = 0;
    fd.sync_fail_on = 2; // first sync (data barrier) passes, second (commit barrier) fails
    TEST_ASSERT_FALSE(pc_wal_store_checkpoint(&s));
}

// ---------------------------------------------------------------------------------------------
// pc_wal_store_scan: happy path, guards, and mid-scan failure/corruption stops.
// ---------------------------------------------------------------------------------------------

void test_scan_reads_records(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"alpha", 5));
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"bravo", 5));
    g_scan_count = 0;
    uint8_t scratch[128];
    TEST_ASSERT_EQUAL_UINT(2, pc_wal_store_scan(&s, scan_cb, nullptr, scratch, sizeof(scratch)));
    TEST_ASSERT_EQUAL_INT(2, g_scan_count);
    TEST_ASSERT_EQUAL_UINT64(0, g_scan_seq[0]);
    TEST_ASSERT_EQUAL_UINT64(1, g_scan_seq[1]);
    TEST_ASSERT_EQUAL_UINT32(5, g_scan_len[0]);
}

// A null callback still counts every valid record (the "if (cb)" false arm).
void test_scan_null_callback_counts(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"one", 3));
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"two", 3));
    uint8_t scratch[128];
    TEST_ASSERT_EQUAL_UINT(2, pc_wal_store_scan(&s, nullptr, nullptr, scratch, sizeof(scratch)));
}

// scan rejects a scratch buffer too small to hold even a record header.
void test_scan_scratch_too_small(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"x", 1));
    uint8_t scratch[WAL_RECORD_HEADER];
    TEST_ASSERT_EQUAL_UINT(0, pc_wal_store_scan(&s, scan_cb, nullptr, scratch, WAL_RECORD_HEADER - 1));
}

// scan stops at a record whose header read fails.
void test_scan_header_read_fails(void)
{
    FaultDisk fd = make_fault(g_disk, sizeof(g_disk));
    WalDev dev = make_fault_dev(&fd);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"x", 1));
    fd.read_fail_ge = WAL_DATA_OFFSET; // the scan's header read fails
    g_scan_count = 0;
    uint8_t scratch[128];
    TEST_ASSERT_EQUAL_UINT(0, pc_wal_store_scan(&s, scan_cb, nullptr, scratch, sizeof(scratch)));
    TEST_ASSERT_EQUAL_INT(0, g_scan_count);
}

// scan stops when the full-record read fails after the header read succeeded.
void test_scan_full_read_fails(void)
{
    FaultDisk fd = make_fault(g_disk, sizeof(g_disk));
    WalDev dev = make_fault_dev(&fd);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"hi", 2));
    fd.read_fail_len = WAL_RECORD_HEADER + 1; // the 20-byte header read passes, the 22-byte read fails
    uint8_t scratch[128];
    TEST_ASSERT_EQUAL_UINT(0, pc_wal_store_scan(&s, scan_cb, nullptr, scratch, sizeof(scratch)));
}

// scan stops at a record with a corrupt magic.
void test_scan_bad_magic_stops(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"x", 1));
    g_disk[WAL_DATA_OFFSET] ^= 0xFF; // corrupt the record magic
    uint8_t scratch[128];
    TEST_ASSERT_EQUAL_UINT(0, pc_wal_store_scan(&s, scan_cb, nullptr, scratch, sizeof(scratch)));
}

// scan stops at a record whose CRC fails (a corrupted payload byte).
void test_scan_crc_mismatch_stops(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"hello", 5));
    g_disk[WAL_DATA_OFFSET + WAL_RECORD_HEADER + 1] ^= 0xFF; // corrupt a payload byte
    uint8_t scratch[128];
    TEST_ASSERT_EQUAL_UINT(0, pc_wal_store_scan(&s, scan_cb, nullptr, scratch, sizeof(scratch)));
}

// Two checkpoints in a row (with no tearing) leave copy A the newer generation; a plain mount right there
// must pick A via the "genA >= genB" arm (both copies valid, unlike test_ab_superblock_fallback which only
// exercises the A-torn fallback path).
void test_mount_picks_newer_generation_a(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev)); // gen 1 @ A
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"one", 3));
    TEST_ASSERT_TRUE(pc_wal_store_checkpoint(&s)); // gen 2 @ B
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"two", 3));
    TEST_ASSERT_TRUE(pc_wal_store_checkpoint(&s)); // gen 3 @ A
    TEST_ASSERT_EQUAL_INT(0, s.ab);
    uint64_t committed = 2 * (REC + 3);

    WalStore m;
    TEST_ASSERT_TRUE(pc_wal_store_mount(&m, &dev));
    TEST_ASSERT_EQUAL_INT(0, m.ab); // both valid; A (gen 3) >= B (gen 2) wins outright
    TEST_ASSERT_EQUAL_UINT64(committed, pc_wal_store_committed(&m));
}

// The tail-replay next_seq bump only fires when a record's seq is strictly newer than what is already known;
// hand-craft two raw records (bypassing append's own monotonic counter) so the second is not newer than the
// first, exercising the "if (seq + 1 > s->next_seq)" false arm.
void test_replay_tail_seq_not_bumped_when_not_newer(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));

    uint8_t buf[128];
    size_t n0 = pc_wal_record_encode(buf, sizeof(buf), 5, (const uint8_t *)"one", 3);
    size_t n1 = pc_wal_record_encode(buf + n0, sizeof(buf) - n0, 2, (const uint8_t *)"two", 3);
    memcpy(g_disk + WAL_DATA_OFFSET, buf, n0 + n1);

    WalStore m;
    TEST_ASSERT_TRUE(pc_wal_store_mount(&m, &dev));
    TEST_ASSERT_EQUAL_UINT64(n0 + n1, pc_wal_store_used(&m)); // both records are individually CRC-valid
    TEST_ASSERT_EQUAL_UINT64(6, m.next_seq);                  // seq 5 -> next_seq=6; seq 2 is not newer, so it stays 6
}

// format fails when its own sync call (after copy A is written) fails.
void test_format_sync_fails(void)
{
    FaultDisk fd = make_fault(g_disk, sizeof(g_disk));
    WalDev dev = make_fault_dev(&fd);
    fd.sync_fail_on = 1; // format's single sync call fails
    WalStore s;
    TEST_ASSERT_FALSE(pc_wal_store_format(&s, &dev));
}

// checkpoint fails when the *first* sync (the pre-superblock data barrier) fails, before write_super runs.
void test_checkpoint_first_sync_fails(void)
{
    FaultDisk fd = make_fault(g_disk, sizeof(g_disk));
    WalDev dev = make_fault_dev(&fd);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));
    fd.sync_calls = 0;   // reset past format's own sync call
    fd.sync_fail_on = 1; // checkpoint's data-sync barrier fails immediately
    TEST_ASSERT_FALSE(pc_wal_store_checkpoint(&s));
}

// scan stops when a record's (corrupted) length claims more bytes than remain before the head - the
// "off + total > s->head" arm, distinct from a CRC or magic failure.
void test_scan_stops_on_length_overrun(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"hi", 2));
    memset(g_disk + WAL_DATA_OFFSET + 12, 0xFF, 4); // len field -> 0xFFFFFFFF
    uint8_t scratch[128];
    g_scan_count = 0;
    TEST_ASSERT_EQUAL_UINT(0, pc_wal_store_scan(&s, scan_cb, nullptr, scratch, sizeof(scratch)));
    TEST_ASSERT_EQUAL_INT(0, g_scan_count);
}

// scan stops on a legitimately-sized record that is simply too large for the caller's scratch buffer - the
// "total > scratch_len" arm, distinct from scratch being too small to hold even a header.
void test_scan_stops_when_record_exceeds_scratch(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"hello world!", 12)); // total = REC + 12 = 32
    uint8_t scratch[WAL_RECORD_HEADER + 4];                                         // 24: >= header, < total
    g_scan_count = 0;
    TEST_ASSERT_EQUAL_UINT(0, pc_wal_store_scan(&s, scan_cb, nullptr, scratch, sizeof(scratch)));
    TEST_ASSERT_EQUAL_INT(0, g_scan_count);
}

// pc_wal_store_pread reads a record payload straight from the log, and rejects an out-of-range read.
void test_pread_in_and_out_of_range(void)
{
    RamDisk d = {g_disk, sizeof(g_disk), 0};
    WalDev dev = make_dev(&d);
    WalStore s;
    TEST_ASSERT_TRUE(pc_wal_store_format(&s, &dev));
    TEST_ASSERT_TRUE(pc_wal_store_append(&s, (const uint8_t *)"hello", 5));
    uint8_t buf[8];
    TEST_ASSERT_TRUE(pc_wal_store_pread(&s, WAL_RECORD_HEADER, buf, 5)); // payload sits right after the header
    TEST_ASSERT_EQUAL_MEMORY("hello", buf, 5);
    TEST_ASSERT_FALSE(pc_wal_store_pread(&s, pc_wal_store_capacity(&s) - 2, buf, 5)); // runs past the data region
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
