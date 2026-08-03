// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/wal: record framing + CRC32 + crash-recovery replay (the atomicity core).

#include "services/storage/wal/wal.h"
#include <stdint.h>
#include <string.h>
#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

// Replay callback: collect seq + payload of each recovered record.
typedef struct
{
    int n;
    uint64_t seq[16];
    uint32_t len[16];
    uint8_t first[16]; // first payload byte (spot check)
} Collected;
static void collect(uint64_t seq, const uint8_t *payload, uint32_t len, void *ctx)
{
    Collected *c = (Collected *)ctx;
    if (c->n < 16)
    {
        c->seq[c->n] = seq;
        c->len[c->n] = len;
        c->first[c->n] = len ? payload[0] : 0;
        c->n++;
    }
}

void test_crc32_known_vector(void)
{
    // The canonical CRC-32/ISO-HDLC check value for "123456789".
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, pc_wal_crc32((const uint8_t *)"123456789", 9));
    TEST_ASSERT_EQUAL_HEX32(0x00000000u, pc_wal_crc32((const uint8_t *)"", 0));
}

void test_encode_replay_roundtrip(void)
{
    uint8_t log[512];
    size_t off = 0;
    const char *p0 = "alpha";
    const char *p1 = "bravo-payload";
    const char *p2 = "c";
    off += pc_wal_record_encode(log + off, sizeof(log) - off, 10, (const uint8_t *)p0, 5);
    off += pc_wal_record_encode(log + off, sizeof(log) - off, 11, (const uint8_t *)p1, 13);
    off += pc_wal_record_encode(log + off, sizeof(log) - off, 12, (const uint8_t *)p2, 1);
    TEST_ASSERT_EQUAL_size_t((size_t)(WAL_RECORD_HEADER * 3 + 5 + 13 + 1), off);

    Collected c = {0};
    size_t durable = pc_wal_replay(log, off, collect, &c);
    TEST_ASSERT_EQUAL_size_t(off, durable); // all records good -> full length
    TEST_ASSERT_EQUAL_INT(3, c.n);
    TEST_ASSERT_EQUAL_UINT64(10, c.seq[0]);
    TEST_ASSERT_EQUAL_UINT64(12, c.seq[2]);
    TEST_ASSERT_EQUAL_UINT32(13, c.len[1]);
    TEST_ASSERT_EQUAL_UINT8('a', c.first[0]);
    TEST_ASSERT_EQUAL_UINT8('b', c.first[1]);
}

void test_replay_recovers_to_last_good_on_corrupt_tail(void)
{
    uint8_t log[512];
    size_t r0 = pc_wal_record_encode(log, sizeof(log), 1, (const uint8_t *)"one", 3);
    size_t r1 = pc_wal_record_encode(log + r0, sizeof(log) - r0, 2, (const uint8_t *)"two", 3);
    size_t r2 = pc_wal_record_encode(log + r0 + r1, sizeof(log) - r0 - r1, 3, (const uint8_t *)"three", 5);
    size_t total = r0 + r1 + r2;

    // Corrupt a payload byte of the third record -> its CRC now fails.
    log[r0 + r1 + WAL_RECORD_HEADER + 1] ^= 0xFF;

    Collected c = {0};
    size_t durable = pc_wal_replay(log, total, collect, &c);
    TEST_ASSERT_EQUAL_size_t(r0 + r1, durable); // recovered up to the end of record 2
    TEST_ASSERT_EQUAL_INT(2, c.n);
    TEST_ASSERT_EQUAL_UINT64(2, c.seq[1]);
}

void test_replay_stops_on_truncated_tail(void)
{
    uint8_t log[512];
    size_t r0 = pc_wal_record_encode(log, sizeof(log), 1, (const uint8_t *)"one", 3);
    size_t r1 = pc_wal_record_encode(log + r0, sizeof(log) - r0, 2, (const uint8_t *)"twotwotwo", 9);
    size_t total = r0 + r1;

    // Simulate a power loss mid-write of record 2: only part of it made it to media.
    Collected c = {0};
    size_t durable = pc_wal_replay(log, total - 4, collect, &c); // last 4 bytes never landed
    TEST_ASSERT_EQUAL_size_t(r0, durable);                       // only record 1 is durable
    TEST_ASSERT_EQUAL_INT(1, c.n);
    TEST_ASSERT_EQUAL_UINT64(1, c.seq[0]);
}

void test_encode_capacity_and_empty_payload(void)
{
    uint8_t small[WAL_RECORD_HEADER + 3];
    // Exactly fits a 3-byte payload.
    TEST_ASSERT_EQUAL_size_t((size_t)WAL_RECORD_HEADER + 3,
                             pc_wal_record_encode(small, sizeof(small), 1, (const uint8_t *)"abc", 3));
    // One byte short -> fails closed.
    TEST_ASSERT_EQUAL_size_t(0, pc_wal_record_encode(small, sizeof(small), 1, (const uint8_t *)"abcd", 4));

    // A zero-length record is valid (a marker / checkpoint sentinel) and replays.
    uint8_t log[64];
    size_t n = pc_wal_record_encode(log, sizeof(log), 99, NULL, 0);
    TEST_ASSERT_EQUAL_size_t((size_t)WAL_RECORD_HEADER, n);
    Collected c = {0};
    TEST_ASSERT_EQUAL_size_t(n, pc_wal_replay(log, n, collect, &c));
    TEST_ASSERT_EQUAL_INT(1, c.n);
    TEST_ASSERT_EQUAL_UINT64(99, c.seq[0]);
    TEST_ASSERT_EQUAL_UINT32(0, c.len[0]);
}

void test_replay_empty_and_garbage(void)
{
    Collected c = {0};
    TEST_ASSERT_EQUAL_size_t(0, pc_wal_replay(NULL, 0, collect, &c));
    uint8_t junk[40];
    memset(junk, 0xAB, sizeof(junk)); // no valid magic at the start
    TEST_ASSERT_EQUAL_size_t(0, pc_wal_replay(junk, sizeof(junk), collect, &c));
    TEST_ASSERT_EQUAL_INT(0, c.n);
}

// A null `out` fails closed regardless of capacity (the "out == NULL" arm of the encode guard).
void test_encode_null_out_fails(void)
{
    TEST_ASSERT_EQUAL_size_t(0, pc_wal_record_encode(NULL, 64, 1, (const uint8_t *)"x", 1));
}

// A null callback still walks every valid record and reports the durable length (the "if (cb)" false arm).
void test_replay_null_callback(void)
{
    uint8_t log[128];
    size_t r0 = pc_wal_record_encode(log, sizeof(log), 1, (const uint8_t *)"one", 3);
    size_t r1 = pc_wal_record_encode(log + r0, sizeof(log) - r0, 2, (const uint8_t *)"two", 3);
    size_t total = r0 + r1;
    TEST_ASSERT_EQUAL_size_t(total, pc_wal_replay(log, total, NULL, NULL));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_crc32_known_vector);
    RUN_TEST(test_encode_replay_roundtrip);
    RUN_TEST(test_replay_recovers_to_last_good_on_corrupt_tail);
    RUN_TEST(test_replay_stops_on_truncated_tail);
    RUN_TEST(test_encode_capacity_and_empty_payload);
    RUN_TEST(test_replay_empty_and_garbage);
    RUN_TEST(test_encode_null_out_fails);
    RUN_TEST(test_replay_null_callback);
    return UNITY_END();
}
