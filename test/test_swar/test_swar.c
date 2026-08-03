// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the lane math (shared_primitives/swar.h).
//
// These answer a question about four bytes with two arithmetic operations and no branches, which is
// only worth doing if the answer is right at every boundary. pc_swar_scan_nul is diffed against
// strnlen over every length and every NUL position within a window, including the offsets where the
// word loop hands over to the byte tail - that handover is where a hand-rolled scan gets it wrong.

#include "shared_primitives/swar.h"
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

void test_has_zero_finds_any_lane()
{
    TEST_ASSERT_TRUE(pc_swar_has_zero(0x00FFFFFFu) != 0);
    TEST_ASSERT_TRUE(pc_swar_has_zero(0xFF00FFFFu) != 0);
    TEST_ASSERT_TRUE(pc_swar_has_zero(0xFFFF00FFu) != 0);
    TEST_ASSERT_TRUE(pc_swar_has_zero(0xFFFFFF00u) != 0);
    TEST_ASSERT_TRUE(pc_swar_has_zero(0x00000000u) != 0);
    TEST_ASSERT_EQUAL_UINT32(0, pc_swar_has_zero(0xFFFFFFFFu));
    TEST_ASSERT_EQUAL_UINT32(0, pc_swar_has_zero(0x01010101u));
    // A lane whose high bit is already set is not a zero lane - the `& ~w` term is what rules it out.
    TEST_ASSERT_EQUAL_UINT32(0, pc_swar_has_zero(0x80808080u));
    TEST_ASSERT_EQUAL_UINT32(0, pc_swar_has_zero(0x80010280u));
}

// The scan must agree with strnlen for every (length, NUL position) pair across the word boundary.
void test_scan_nul_matches_strnlen()
{
    char buf[40];
    for (size_t nul_at = 0; nul_at < 24; nul_at++)
    {
        memset(buf, 'x', sizeof(buf));
        buf[nul_at] = '\0';
        for (size_t cap = 0; cap < 32; cap++)
        {
            TEST_ASSERT_EQUAL_UINT32(strnlen(buf, cap), pc_swar_scan_nul(buf, cap));
        }
    }
}

// No NUL inside the window: the answer is the window, never a read past it.
void test_scan_nul_absent_returns_cap()
{
    char buf[16];
    memset(buf, 'a', sizeof(buf));
    for (size_t cap = 0; cap <= sizeof(buf); cap++)
    {
        TEST_ASSERT_EQUAL_UINT32(cap, pc_swar_scan_nul(buf, cap));
    }
}

// High-bit bytes must not read as terminators - the case a naive signed-char scan gets wrong.
void test_scan_nul_ignores_high_bytes()
{
    const char buf[] = {(char)0x80, (char)0xFF, (char)0x7F, (char)0x80, (char)0xFE, '\0'};
    TEST_ASSERT_EQUAL_UINT32(5, pc_swar_scan_nul(buf, sizeof(buf)));
}

// The scan is called on unaligned addresses constantly (a string mid-buffer); every offset must agree.
void test_scan_nul_unaligned()
{
    char buf[32];
    for (size_t off = 0; off < 8; off++)
    {
        memset(buf, 'q', sizeof(buf));
        buf[off + 9] = '\0';
        TEST_ASSERT_EQUAL_UINT32(9, pc_swar_scan_nul(buf + off, sizeof(buf) - off));
    }
}

// The mask states the lane; this pins that reading against a word built from known bytes, at every
// position, so a byte-order or shift error cannot pass.
void test_zero_lane_from_mask()
{
    for (size_t at = 0; at < PC_SWAR_BYTES; at++)
    {
        char bytes[PC_SWAR_BYTES];
        memset(bytes, 'z', sizeof(bytes));
        bytes[at] = '\0';
        pc_swar_word m = pc_swar_has_zero(pc_swar_load(bytes));
        TEST_ASSERT_TRUE(m != 0);
        TEST_ASSERT_EQUAL_UINT32(at, pc_swar_zero_lane(m));
    }
    // With several zero lanes the answer is the first in address order, not any of the others.
    char two[PC_SWAR_BYTES];
    memset(two, 'z', sizeof(two));
    two[1] = '\0';
    two[PC_SWAR_BYTES - 1] = '\0';
    TEST_ASSERT_EQUAL_UINT32(1, pc_swar_zero_lane(pc_swar_has_zero(pc_swar_load(two))));
}

void test_lane_compares()
{
    const uint32_t w = 0x41305A61u; // lanes, high to low: 'A' 0x41, '0' 0x30, 'Z' 0x5A, 'a' 0x61

    // Every lane is >= '0'.
    TEST_ASSERT_EQUAL_UINT32(0x80808080u, pc_swar_ge(w, '0'));
    // Only the 'a' lane is >= 'a'.
    TEST_ASSERT_EQUAL_UINT32(0x00000080u, pc_swar_ge(w, 'a'));
    // 'A' and '0' are <= 'A'; 'Z' and 'a' are not.
    TEST_ASSERT_EQUAL_UINT32(0x80800000u, pc_swar_le(w, 'A'));
    // The A-Z window selects the 'A' and 'Z' lanes and nothing else.
    TEST_ASSERT_EQUAL_UINT32(0x80008000u, pc_swar_ge(w, 'A') & pc_swar_le(w, 'Z'));

    TEST_ASSERT_EQUAL_UINT32(0xFFFFFFFFu, pc_swar_spread(0x80808080u));
    TEST_ASSERT_EQUAL_UINT32(0x000000FFu, pc_swar_spread(0x00000080u));
    TEST_ASSERT_EQUAL_UINT32(0x00000000u, pc_swar_spread(0x00000000u));

    // sub7 is the lane-local subtraction the decoder folds its alphabet offsets with.
    TEST_ASSERT_EQUAL_UINT32(0x00000000u, pc_swar_sub7(0x30303030u, '0'));
    TEST_ASSERT_EQUAL_UINT32(0x01010101u, pc_swar_sub7(0x31313131u, '0'));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_has_zero_finds_any_lane);
    RUN_TEST(test_zero_lane_from_mask);
    RUN_TEST(test_scan_nul_matches_strnlen);
    RUN_TEST(test_scan_nul_absent_returns_cap);
    RUN_TEST(test_scan_nul_ignores_high_bytes);
    RUN_TEST(test_scan_nul_unaligned);
    RUN_TEST(test_lane_compares);
    return UNITY_END();
}
