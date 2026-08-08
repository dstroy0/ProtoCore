// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// mmgr/bytes.h: append into a pc_span, take out of a pc_cspan, and the offset-passing reads a
// parser walks a raw payload with. The oracle is a shift loop written here.

#include "mmgr/bytes.h"

#include <stdint.h>
#include <string.h>

#include <unity.h>

#define CAP 32u

static uint8_t store[CAP];

void setUp(void)
{
    memset(store, 0, sizeof(store));
}

void tearDown(void)
{
}

// ---- append ---------------------------------------------------------------

// A byte lands, and pos counts it.
void test_put_writes_and_counts()
{
    pc_span w = pc_span_from(store, CAP);
    pc_bw_put(&w, 0xA5u);
    pc_bw_put(&w, 0x5Au);
    TEST_ASSERT_EQUAL_size_t(2, pc_span_len(w));
    TEST_ASSERT_FALSE(w.overflow);
    TEST_ASSERT_EQUAL_HEX8(0xA5u, store[0]);
    TEST_ASSERT_EQUAL_HEX8(0x5Au, store[1]);
}

// Past the end nothing is written, the flag latches, and pos keeps counting so the value it reports
// is the capacity the region should have had.
void test_put_past_cap_counts_the_size_needed()
{
    pc_span w = pc_span_from(store, 4);
    for (unsigned i = 0; i < 10u; i++)
    {
        pc_bw_put(&w, (uint8_t)(0x10u + i));
    }
    TEST_ASSERT_TRUE(w.overflow);
    TEST_ASSERT_EQUAL_size_t(10, pc_span_len(w)); // the size a big-enough region needed
    TEST_ASSERT_EQUAL_HEX8(0x13u, store[3]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, store[4]); // nothing past the capacity
    TEST_ASSERT_EQUAL_size_t(0, pc_span_room(w));
    TEST_ASSERT_FALSE(pc_span_ok(w));
}

// A width goes out most significant byte first.
void test_put_be_is_network_order()
{
    static const uint64_t v = 0x0123456789ABCDEFull;
    for (int32_t n = 1; n <= 8; n++)
    {
        memset(store, 0, sizeof(store));
        pc_span w = pc_span_from(store, CAP);
        pc_bw_put_be(&w, v, n);
        TEST_ASSERT_EQUAL_size_t((size_t)n, pc_span_len(w));
        for (int32_t i = 0; i < n; i++)
        {
            uint8_t want = (uint8_t)(v >> (8 * (n - 1 - i)));
            TEST_ASSERT_EQUAL_HEX8(want, store[i]);
        }
        TEST_ASSERT_EQUAL_HEX8(0, store[n]);
    }
}

// A width that does not fit still counts every byte it would have needed.
void test_put_be_past_cap_counts_the_whole_width()
{
    pc_span w = pc_span_from(store, 3);
    pc_bw_put_be(&w, 0x0123456789ABCDEFull, 8);
    TEST_ASSERT_TRUE(w.overflow);
    TEST_ASSERT_EQUAL_size_t(8, pc_span_len(w));
    TEST_ASSERT_EQUAL_HEX8(0x01u, store[0]);
    TEST_ASSERT_EQUAL_HEX8(0x23u, store[1]);
    TEST_ASSERT_EQUAL_HEX8(0x45u, store[2]);
    TEST_ASSERT_EQUAL_HEX8(0x00u, store[3]);
}

// ---- take -----------------------------------------------------------------

// A width comes back in network order and the cursor advances by it.
void test_take_be_reads_and_advances()
{
    static const uint8_t wire[] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08};
    pc_cspan r = pc_cspan_from(wire, sizeof(wire));
    uint64_t v = 0;

    TEST_ASSERT_TRUE(pc_br_take_be(&r, 2, &v));
    TEST_ASSERT_EQUAL_HEX64(0x0102u, v);
    TEST_ASSERT_EQUAL_size_t(2, r.pos);

    TEST_ASSERT_TRUE(pc_br_take_be(&r, 4, &v));
    TEST_ASSERT_EQUAL_HEX64(0x03040506u, v);
    TEST_ASSERT_EQUAL_size_t(6, r.pos);

    TEST_ASSERT_TRUE(pc_br_take_be(&r, 2, &v));
    TEST_ASSERT_EQUAL_HEX64(0x0708u, v);
    TEST_ASSERT_EQUAL_size_t(8, r.pos);
    TEST_ASSERT_FALSE(r.err);
}

// Consuming exactly the whole region is not an error; one byte more is.
void test_take_be_at_and_past_the_end()
{
    static const uint8_t wire[] = {0xAA, 0xBB, 0xCC, 0xDD};
    pc_cspan r = pc_cspan_from(wire, sizeof(wire));
    uint64_t v = 0;

    TEST_ASSERT_TRUE(pc_br_take_be(&r, 4, &v));
    TEST_ASSERT_EQUAL_HEX64(0xAABBCCDDu, v);
    TEST_ASSERT_FALSE(r.err);

    TEST_ASSERT_FALSE(pc_br_take_be(&r, 1, &v));
    TEST_ASSERT_TRUE(r.err);
    TEST_ASSERT_EQUAL_size_t(4, r.pos); // a refused read does not advance
}

// A refused read leaves the cursor and the output alone, and the error stays set.
void test_take_be_refusal_is_sticky_and_leaves_the_cursor()
{
    static const uint8_t wire[] = {0x11, 0x22};
    pc_cspan r = pc_cspan_from(wire, sizeof(wire));
    uint64_t v = 0xDEADBEEFu;

    TEST_ASSERT_FALSE(pc_br_take_be(&r, 8, &v)); // 8 asked of a 2-byte region
    TEST_ASSERT_TRUE(r.err);
    TEST_ASSERT_EQUAL_size_t(0, r.pos);
    TEST_ASSERT_EQUAL_HEX64(0xDEADBEEFu, v); // output untouched

    TEST_ASSERT_TRUE(pc_br_take_be(&r, 2, &v)); // a read that fits still works
    TEST_ASSERT_EQUAL_HEX64(0x1122u, v);
    TEST_ASSERT_TRUE(r.err); // the flag stays set
}

// A zero-width take yields zero and moves nothing.
void test_take_be_zero_width()
{
    static const uint8_t wire[] = {0x11, 0x22};
    pc_cspan r = pc_cspan_from(wire, sizeof(wire));
    uint64_t v = 0xFFu;
    TEST_ASSERT_TRUE(pc_br_take_be(&r, 0, &v));
    TEST_ASSERT_EQUAL_HEX64(0, v);
    TEST_ASSERT_EQUAL_size_t(0, r.pos);
    TEST_ASSERT_FALSE(r.err);
}

// ---- offset-passing reads -------------------------------------------------

// A u32 comes back in network order and the offset advances by four.
void test_rd_u32_reads_and_advances()
{
    static const uint8_t p[] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x00, 0x00, 0x07};
    size_t off = 0;
    uint32_t v = 0;
    TEST_ASSERT_TRUE(pc_rd_u32(p, sizeof(p), &off, &v));
    TEST_ASSERT_EQUAL_HEX32(0xDEADBEEFu, v);
    TEST_ASSERT_EQUAL_size_t(4, off);
    TEST_ASSERT_TRUE(pc_rd_u32(p, sizeof(p), &off, &v));
    TEST_ASSERT_EQUAL_HEX32(7u, v);
    TEST_ASSERT_EQUAL_size_t(8, off);
}

// Four bytes are needed; three are refused, and the offset does not move.
void test_rd_u32_short_read_is_refused()
{
    static const uint8_t p[] = {0x01, 0x02, 0x03};
    size_t off = 0;
    uint32_t v = 0xA5A5A5A5u;
    TEST_ASSERT_FALSE(pc_rd_u32(p, sizeof(p), &off, &v));
    TEST_ASSERT_EQUAL_size_t(0, off);
    TEST_ASSERT_EQUAL_HEX32(0xA5A5A5A5u, v);

    // An offset already past the end is refused rather than subtracted into a wrap.
    off = sizeof(p) + 4u;
    TEST_ASSERT_FALSE(pc_rd_u32(p, sizeof(p), &off, &v));
}

// A length-prefixed blob points into the payload and the offset lands past it.
void test_rd_str_points_into_the_payload()
{
    static const uint8_t p[] = {0x00, 0x00, 0x00, 0x03, 'a', 'b', 'c', 0x00, 0x00, 0x00, 0x00};
    size_t off = 0;
    const uint8_t *s = NULL;
    uint32_t slen = 0;

    TEST_ASSERT_TRUE(pc_rd_str(p, sizeof(p), &off, &s, &slen));
    TEST_ASSERT_EQUAL_UINT32(3, slen);
    TEST_ASSERT_EQUAL_PTR(p + 4, s); // a view, not a copy
    TEST_ASSERT_EQUAL_size_t(7, off);

    TEST_ASSERT_TRUE(pc_rd_str(p, sizeof(p), &off, &s, &slen)); // the empty string that follows
    TEST_ASSERT_EQUAL_UINT32(0, slen);
    TEST_ASSERT_EQUAL_size_t(11, off);
}

// A blob whose length runs past the end is refused with the offset back where it started, so the
// caller can name the field that failed.
void test_rd_str_overlong_is_refused_and_rewinds()
{
    static const uint8_t p[] = {0x00, 0x00, 0x00, 0x09, 'a', 'b', 'c'};
    size_t off = 0;
    const uint8_t *s = NULL;
    uint32_t slen = 0;
    TEST_ASSERT_FALSE(pc_rd_str(p, sizeof(p), &off, &s, &slen));
    TEST_ASSERT_EQUAL_size_t(0, off);
    TEST_ASSERT_NULL(s);
}

// The peer picks the length prefix. A full-range one must be refused on the space that remains, not
// admitted by a sum that wrapped.
void test_rd_str_full_range_length_is_refused()
{
    static const uint8_t p[] = {0xFF, 0xFF, 0xFF, 0xFF, 'a', 'b', 'c', 'd'};
    size_t off = 0;
    const uint8_t *s = NULL;
    uint32_t slen = 0;
    TEST_ASSERT_FALSE(pc_rd_str(p, sizeof(p), &off, &s, &slen));
    TEST_ASSERT_EQUAL_size_t(0, off);
    TEST_ASSERT_NULL(s);

    // The same at the largest length that still fits the prefix but not the payload.
    static const uint8_t q[] = {0x7F, 0xFF, 0xFF, 0xFF, 'a'};
    off = 0;
    TEST_ASSERT_FALSE(pc_rd_str(q, sizeof(q), &off, &s, &slen));
    TEST_ASSERT_EQUAL_size_t(0, off);
}

// A blob that ends exactly at the end of the payload is accepted.
void test_rd_str_exact_fit_is_accepted()
{
    static const uint8_t p[] = {0x00, 0x00, 0x00, 0x04, 'a', 'b', 'c', 'd'};
    size_t off = 0;
    const uint8_t *s = NULL;
    uint32_t slen = 0;
    TEST_ASSERT_TRUE(pc_rd_str(p, sizeof(p), &off, &s, &slen));
    TEST_ASSERT_EQUAL_UINT32(4, slen);
    TEST_ASSERT_EQUAL_PTR(p + 4, s);
    TEST_ASSERT_EQUAL_size_t(8, off);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_put_writes_and_counts);
    RUN_TEST(test_put_past_cap_counts_the_size_needed);
    RUN_TEST(test_put_be_is_network_order);
    RUN_TEST(test_put_be_past_cap_counts_the_whole_width);
    RUN_TEST(test_take_be_reads_and_advances);
    RUN_TEST(test_take_be_at_and_past_the_end);
    RUN_TEST(test_take_be_refusal_is_sticky_and_leaves_the_cursor);
    RUN_TEST(test_take_be_zero_width);
    RUN_TEST(test_rd_u32_reads_and_advances);
    RUN_TEST(test_rd_u32_short_read_is_refused);
    RUN_TEST(test_rd_str_points_into_the_payload);
    RUN_TEST(test_rd_str_overlong_is_refused_and_rewinds);
    RUN_TEST(test_rd_str_full_range_length_is_refused);
    RUN_TEST(test_rd_str_exact_fit_is_accepted);
    return UNITY_END();
}
