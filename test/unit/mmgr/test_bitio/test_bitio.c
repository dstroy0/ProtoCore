// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// mmgr/bitio.h: the LSB-first bit writer. The oracle is a bit reader written here.

#include "mmgr/bitio.h"

#include <stdint.h>
#include <string.h>

#include <unity.h>

#define OUT_CAP 64u

static uint8_t s_out[OUT_CAP];
static pc_bit_writer w;

static void writer_reset(size_t cap)
{
    memset(s_out, 0, sizeof(s_out));
    w.out = s_out;
    w.cap = cap;
    w.cnt = 0;
    w.acc = 0;
    w.nbits = 0;
    w.overflow = PROTO_FALSE;
}

void setUp(void)
{
    writer_reset(OUT_CAP);
}

void tearDown(void)
{
}

// Read n bits LSB-first starting at bit position *pos, the way a decoder walks the stream.
static uint32_t oracle_get(const uint8_t *buf, size_t *pos, int n)
{
    uint32_t v = 0;
    for (int i = 0; i < n; i++)
    {
        size_t bit = *pos + (size_t)i;
        uint32_t b = (uint32_t)((buf[bit >> 3] >> (bit & 7u)) & 1u);
        v |= b << i;
    }
    *pos += (size_t)n;
    return v;
}

// ---- packing --------------------------------------------------------------

// Bits come back in the order they went in, at the widths they went in at.
void test_put_round_trips_lsb_first()
{
    static const uint32_t vals[] = {1u, 0u, 5u, 0x2Au, 0x1FFu, 3u, 0x7Fu, 0x555u};
    static const int widths[] = {1, 1, 3, 6, 9, 2, 7, 11};

    for (unsigned i = 0; i < sizeof(widths) / sizeof(widths[0]); i++)
    {
        pc_bitw_put(&w, vals[i], widths[i]);
    }
    pc_bitw_align(&w);
    TEST_ASSERT_FALSE(w.overflow);

    size_t pos = 0;
    for (unsigned i = 0; i < sizeof(widths) / sizeof(widths[0]); i++)
    {
        TEST_ASSERT_EQUAL_HEX32(vals[i], oracle_get(s_out, &pos, widths[i]));
    }
}

// Eight bits is exactly one output byte, and it spills without an align.
void test_eight_bits_spills_one_byte()
{
    pc_bitw_put(&w, 0xA5u, 8);
    TEST_ASSERT_EQUAL_size_t(1, w.cnt);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, s_out[0]);
    TEST_ASSERT_EQUAL_INT(0, w.nbits);
}

// A single put wider than a byte spills every whole byte it completes.
void test_wide_put_spills_every_whole_byte()
{
    pc_bitw_put(&w, 0x00ABCDEFu, 24);
    TEST_ASSERT_EQUAL_size_t(3, w.cnt);
    TEST_ASSERT_EQUAL_HEX8(0xEFu, s_out[0]);
    TEST_ASSERT_EQUAL_HEX8(0xCDu, s_out[1]);
    TEST_ASSERT_EQUAL_HEX8(0xABu, s_out[2]);
    TEST_ASSERT_EQUAL_INT(0, w.nbits);
}

// A partial byte is padded with zero in the high bits, not with stale accumulator bits.
void test_align_pads_high_bits_with_zero()
{
    pc_bitw_put(&w, 0xFFu, 8); // dirty the accumulator's path first
    pc_bitw_put(&w, 0x5u, 3);
    pc_bitw_align(&w);
    TEST_ASSERT_EQUAL_size_t(2, w.cnt);
    TEST_ASSERT_EQUAL_HEX8(0x05u, s_out[1]); // 3 bits of value, 5 zero bits above
    TEST_ASSERT_EQUAL_INT(0, w.nbits);
}

// Align on a byte boundary emits nothing.
void test_align_on_boundary_emits_nothing()
{
    pc_bitw_put(&w, 0xA5u, 8);
    pc_bitw_align(&w);
    TEST_ASSERT_EQUAL_size_t(1, w.cnt);
    pc_bitw_align(&w);
    TEST_ASSERT_EQUAL_size_t(1, w.cnt);
    TEST_ASSERT_FALSE(w.overflow);
}

// ---- the capacity edge ----------------------------------------------------

// Filling the buffer exactly is not an overflow.
void test_exact_fill_is_not_overflow()
{
    writer_reset(4);
    for (int i = 0; i < 4; i++)
    {
        pc_bitw_put(&w, (uint32_t)(0x10u + i), 8);
    }
    TEST_ASSERT_FALSE(w.overflow);
    TEST_ASSERT_EQUAL_size_t(4, w.cnt);
}

// One byte past the end latches overflow and writes nothing past cap.
void test_one_byte_past_cap_latches_overflow()
{
    writer_reset(4);
    for (int i = 0; i < 5; i++)
    {
        pc_bitw_put(&w, 0xFFu, 8);
    }
    TEST_ASSERT_TRUE(w.overflow);
    TEST_ASSERT_EQUAL_size_t(4, w.cnt);
    TEST_ASSERT_EQUAL_HEX8(0, s_out[4]); // the byte past cap is untouched
}

// An align that needs a byte the buffer does not have latches overflow instead of writing.
void test_align_at_cap_latches_overflow()
{
    writer_reset(1);
    pc_bitw_put(&w, 0xFFu, 8); // fills the one byte
    TEST_ASSERT_FALSE(w.overflow);
    pc_bitw_put(&w, 0x1u, 3); // buffered, nothing to spill yet
    pc_bitw_align(&w);
    TEST_ASSERT_TRUE(w.overflow);
    TEST_ASSERT_EQUAL_size_t(1, w.cnt);
}

// Overflow stays latched: it is never cleared by a later put that happens to fit.
void test_overflow_stays_latched()
{
    writer_reset(2);
    for (int i = 0; i < 4; i++)
    {
        pc_bitw_put(&w, 0xFFu, 8);
    }
    TEST_ASSERT_TRUE(w.overflow);
    pc_bitw_put(&w, 0x1u, 1);
    pc_bitw_align(&w);
    TEST_ASSERT_TRUE(w.overflow);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_put_round_trips_lsb_first);
    RUN_TEST(test_eight_bits_spills_one_byte);
    RUN_TEST(test_wide_put_spills_every_whole_byte);
    RUN_TEST(test_align_pads_high_bits_with_zero);
    RUN_TEST(test_align_on_boundary_emits_nothing);
    RUN_TEST(test_exact_fill_is_not_overflow);
    RUN_TEST(test_one_byte_past_cap_latches_overflow);
    RUN_TEST(test_align_at_cap_latches_overflow);
    RUN_TEST(test_overflow_stays_latched);
    return UNITY_END();
}
