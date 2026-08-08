// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// mmgr/endian.h: a fixed width moved between an integer and the bytes at a pointer.
// The oracle is a shift loop written here, so no serializer is checked against another.

#include "mmgr/endian.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <unity.h>

#define GUARD 8u
#define BODY 16u
#define POISON 0xAAu

static uint8_t buf[GUARD + BODY + GUARD];

void setUp(void)
{
    memset(buf, POISON, sizeof(buf));
}

void tearDown(void)
{
}

static uint8_t *at(size_t off)
{
    return buf + GUARD + off;
}

// Lay v down big-endian: the most significant byte first.
static void oracle_be(uint8_t *p, uint64_t v, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
    {
        p[i] = (uint8_t)(v >> (8u * (n - 1u - i)));
    }
}

// Lay v down little-endian: the least significant byte first.
static void oracle_le(uint8_t *p, uint64_t v, unsigned n)
{
    for (unsigned i = 0; i < n; i++)
    {
        p[i] = (uint8_t)(v >> (8u * i));
    }
}

// Every byte outside [off, off + n) still reads POISON.
static void assert_only_width_written(size_t off, size_t n)
{
    for (size_t i = 0; i < sizeof(buf); i++)
    {
        if (i >= GUARD + off && i < GUARD + off + n)
        {
            continue;
        }
        if (buf[i] != POISON)
        {
            char msg[64];
            (void)snprintf(msg, sizeof(msg), "wrote outside the width at %u", (unsigned)i);
            TEST_FAIL_MESSAGE(msg);
        }
    }
}

static const uint64_t VALS[] = {0u,
                                1u,
                                0xFFu,
                                0x0100u,
                                0x1234u,
                                0xFFFFu,
                                0x00010203u,
                                0x89ABCDEFu,
                                0xFFFFFFFFu,
                                0x0123456789ABCDEFull,
                                0xFEDCBA9876543210ull,
                                0xFFFFFFFFFFFFFFFFull};

#define NVALS (sizeof(VALS) / sizeof(VALS[0]))

// ---- byte order -----------------------------------------------------------

// Each big-endian writer lays down the order the oracle does, and touches only its width.
void test_be_writers_match_the_oracle()
{
    uint8_t want[8];
    for (unsigned v = 0; v < NVALS; v++)
    {
        memset(buf, POISON, sizeof(buf));
        TEST_ASSERT_EQUAL_size_t(2, pc_wr16be(at(0), (uint16_t)VALS[v]));
        oracle_be(want, VALS[v], 2);
        TEST_ASSERT_EQUAL_MEMORY(want, at(0), 2);
        assert_only_width_written(0, 2);

        memset(buf, POISON, sizeof(buf));
        TEST_ASSERT_EQUAL_size_t(4, pc_wr32be(at(0), (uint32_t)VALS[v]));
        oracle_be(want, VALS[v], 4);
        TEST_ASSERT_EQUAL_MEMORY(want, at(0), 4);
        assert_only_width_written(0, 4);

        memset(buf, POISON, sizeof(buf));
        TEST_ASSERT_EQUAL_size_t(8, pc_wr64be(at(0), VALS[v]));
        oracle_be(want, VALS[v], 8);
        TEST_ASSERT_EQUAL_MEMORY(want, at(0), 8);
        assert_only_width_written(0, 8);
    }
}

// Each little-endian writer lays down the order the oracle does, and touches only its width.
void test_le_writers_match_the_oracle()
{
    uint8_t want[8];
    for (unsigned v = 0; v < NVALS; v++)
    {
        memset(buf, POISON, sizeof(buf));
        TEST_ASSERT_EQUAL_size_t(2, pc_wr16le(at(0), (uint16_t)VALS[v]));
        oracle_le(want, VALS[v], 2);
        TEST_ASSERT_EQUAL_MEMORY(want, at(0), 2);
        assert_only_width_written(0, 2);

        memset(buf, POISON, sizeof(buf));
        TEST_ASSERT_EQUAL_size_t(4, pc_wr32le(at(0), (uint32_t)VALS[v]));
        oracle_le(want, VALS[v], 4);
        TEST_ASSERT_EQUAL_MEMORY(want, at(0), 4);
        assert_only_width_written(0, 4);

        memset(buf, POISON, sizeof(buf));
        TEST_ASSERT_EQUAL_size_t(8, pc_wr64le(at(0), VALS[v]));
        oracle_le(want, VALS[v], 8);
        TEST_ASSERT_EQUAL_MEMORY(want, at(0), 8);
        assert_only_width_written(0, 8);
    }
}

// Each reader reconstructs what the oracle laid down.
void test_readers_match_the_oracle()
{
    for (unsigned v = 0; v < NVALS; v++)
    {
        oracle_be(at(0), VALS[v], 2);
        TEST_ASSERT_EQUAL_HEX16((uint16_t)VALS[v], pc_rd16be(at(0)));
        oracle_be(at(0), VALS[v], 4);
        TEST_ASSERT_EQUAL_HEX32((uint32_t)VALS[v], pc_rd32be(at(0)));
        oracle_be(at(0), VALS[v], 8);
        TEST_ASSERT_EQUAL_HEX64(VALS[v], pc_rd64be(at(0)));

        oracle_le(at(0), VALS[v], 2);
        TEST_ASSERT_EQUAL_HEX16((uint16_t)VALS[v], pc_rd16le(at(0)));
        oracle_le(at(0), VALS[v], 4);
        TEST_ASSERT_EQUAL_HEX32((uint32_t)VALS[v], pc_rd32le(at(0)));
        oracle_le(at(0), VALS[v], 8);
        TEST_ASSERT_EQUAL_HEX64(VALS[v], pc_rd64le(at(0)));
    }
}

// A big-endian encoding is the byte reverse of the little-endian one at the same width.
void test_be_is_the_reverse_of_le()
{
    for (unsigned v = 0; v < NVALS; v++)
    {
        uint8_t b[8];
        uint8_t l[8];
        pc_wr64be(b, VALS[v]);
        pc_wr64le(l, VALS[v]);
        for (unsigned i = 0; i < 8; i++)
        {
            TEST_ASSERT_EQUAL_HEX8(b[i], l[7u - i]);
        }
    }
}

// ---- alignment ------------------------------------------------------------

// Byte at a time, so every width round-trips at every offset within a word.
void test_round_trip_at_every_offset()
{
    for (size_t off = 0; off < 8u; off++)
    {
        for (unsigned v = 0; v < NVALS; v++)
        {
            pc_wr16be(at(off), (uint16_t)VALS[v]);
            TEST_ASSERT_EQUAL_HEX16((uint16_t)VALS[v], pc_rd16be(at(off)));
            pc_wr32be(at(off), (uint32_t)VALS[v]);
            TEST_ASSERT_EQUAL_HEX32((uint32_t)VALS[v], pc_rd32be(at(off)));
            pc_wr64be(at(off), VALS[v]);
            TEST_ASSERT_EQUAL_HEX64(VALS[v], pc_rd64be(at(off)));

            pc_wr16le(at(off), (uint16_t)VALS[v]);
            TEST_ASSERT_EQUAL_HEX16((uint16_t)VALS[v], pc_rd16le(at(off)));
            pc_wr32le(at(off), (uint32_t)VALS[v]);
            TEST_ASSERT_EQUAL_HEX32((uint32_t)VALS[v], pc_rd32le(at(off)));
            pc_wr64le(at(off), VALS[v]);
            TEST_ASSERT_EQUAL_HEX64(VALS[v], pc_rd64le(at(off)));
        }
    }
}

// A narrow write leaves the bytes above it alone, so packed fields do not clobber each other.
void test_adjacent_fields_do_not_overlap()
{
    pc_wr16be(at(0), 0x1122u);
    pc_wr32be(at(2), 0x33445566u);
    pc_wr16be(at(6), 0x7788u);
    TEST_ASSERT_EQUAL_HEX16(0x1122u, pc_rd16be(at(0)));
    TEST_ASSERT_EQUAL_HEX32(0x33445566u, pc_rd32be(at(2)));
    TEST_ASSERT_EQUAL_HEX16(0x7788u, pc_rd16be(at(6)));
    assert_only_width_written(0, 8);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_be_writers_match_the_oracle);
    RUN_TEST(test_le_writers_match_the_oracle);
    RUN_TEST(test_readers_match_the_oracle);
    RUN_TEST(test_be_is_the_reverse_of_le);
    RUN_TEST(test_round_trip_at_every_offset);
    RUN_TEST(test_adjacent_fields_do_not_overlap);
    return UNITY_END();
}
