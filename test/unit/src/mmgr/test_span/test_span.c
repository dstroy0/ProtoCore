// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the bounded byte region (mmgr/span.h), driven by the byte verbs it was written for
// (mmgr/bytes.h).
//
// No standard governs a span; every expectation here is PROPERTIES.
//
// test_an_empty_region_cannot_carry_a_live_capacity is the load-bearing case. The whole point of
// binding the pointer to its length is that the two can never disagree, so a constructor must
// normalize storage-without-capacity AND capacity-without-storage to the same empty region. A
// {NULL, n} that survived construction is a null pointer with a capacity a caller is entitled to
// write, which is exactly the failure the type exists to remove.

#include "mmgr/bytes/bytes.h"
#include "mmgr/span/span.h"
#include <string.h>

#include <unity.h>

#define SMALL_N 7u
#define BIG_N 2048u

void setUp(void)
{
}
void tearDown(void)
{
}

static uint8_t g_small[SMALL_N];
static uint8_t g_big[BIG_N];

// The run length is stated once, at the bind, and travels with the storage from there.
void test_the_capacity_is_the_constant_it_was_bound_with(void)
{
    protocore_span s = span.from(g_small, SMALL_N);
    TEST_ASSERT_EQUAL_PTR(g_small, s.buf);
    TEST_ASSERT_EQUAL_size_t(SMALL_N, s.cap);
    TEST_ASSERT_EQUAL_size_t(0u, s.pos);
    TEST_ASSERT_FALSE(s.overflow);
    TEST_ASSERT_TRUE(span.ok(s));
    TEST_ASSERT_EQUAL_size_t(SMALL_N, span.room(s));

    protocore_span b = span.from(g_big, BIG_N);
    TEST_ASSERT_EQUAL_PTR(g_big, b.buf);
    TEST_ASSERT_EQUAL_size_t(BIG_N, b.cap);

    protocore_cspan c = span.cfrom(g_big, BIG_N);
    TEST_ASSERT_EQUAL_PTR(g_big, c.buf);
    TEST_ASSERT_EQUAL_size_t(BIG_N, c.len);
    TEST_ASSERT_TRUE(span.cok(c));
}

// Storage without capacity and capacity without storage are the same empty region, so no span ever
// names bytes it may not write.
void test_an_empty_region_cannot_carry_a_live_capacity(void)
{
    protocore_span no_buf = span.from(NULL, BIG_N);
    TEST_ASSERT_NULL(no_buf.buf);
    TEST_ASSERT_EQUAL_size_t(0u, no_buf.cap);
    TEST_ASSERT_FALSE(span.ok(no_buf));
    TEST_ASSERT_FALSE(span.has_storage(no_buf));
    TEST_ASSERT_EQUAL_size_t(0u, span.room(no_buf));

    protocore_span no_cap = span.from(g_small, 0);
    TEST_ASSERT_NULL(no_cap.buf);
    TEST_ASSERT_EQUAL_size_t(0u, no_cap.cap);
    TEST_ASSERT_FALSE(span.ok(no_cap));

    protocore_cspan cno_buf = span.cfrom(NULL, BIG_N);
    TEST_ASSERT_NULL(cno_buf.buf);
    TEST_ASSERT_EQUAL_size_t(0u, cno_buf.len);
    TEST_ASSERT_FALSE(span.cok(cno_buf));

    protocore_cspan cno_len = span.cfrom(g_small, 0);
    TEST_ASSERT_NULL(cno_len.buf);
    TEST_ASSERT_EQUAL_size_t(0u, cno_len.len);
    TEST_ASSERT_FALSE(span.cok(cno_len));

    // A write into the empty region stores nothing and dereferences nothing.
    bytes.put(&no_buf, 0xAA);
    bytes.raw(&no_buf, g_big, 16);
    TEST_ASSERT_NULL(no_buf.buf);
    TEST_ASSERT_TRUE(no_buf.overflow);
}

// The backward direction: after an overflow pos keeps counting, so it reports the capacity the
// payload needed rather than only that it did not fit.
void test_pos_reports_the_capacity_the_payload_needed(void)
{
    protocore_span s = span.from(g_small, SMALL_N);
    memset(g_small, 0, sizeof g_small);

    for (unsigned i = 0; i < SMALL_N; i++)
    {
        bytes.put(&s, (uint8_t)(0x40u + i));
    }
    TEST_ASSERT_TRUE(span.ok(s));
    TEST_ASSERT_EQUAL_size_t(SMALL_N, span.len(s));
    TEST_ASSERT_EQUAL_size_t(0u, span.room(s));
    TEST_ASSERT_EQUAL_HEX8(0x40, g_small[0]);
    TEST_ASSERT_EQUAL_HEX8(0x40 + SMALL_N - 1, g_small[SMALL_N - 1]);

    // Five more than it holds: nothing is stored, the flag latches, and pos names the size wanted.
    for (unsigned i = 0; i < 5; i++)
    {
        bytes.put(&s, 0xFF);
    }
    TEST_ASSERT_TRUE(s.overflow);
    TEST_ASSERT_FALSE(span.ok(s));
    TEST_ASSERT_TRUE(span.has_storage(s)); // the storage is still there; only the write failed
    TEST_ASSERT_EQUAL_size_t(SMALL_N + 5u, span.len(s));
    TEST_ASSERT_EQUAL_size_t(0u, span.room(s));
    TEST_ASSERT_EQUAL_HEX8(0x40 + SMALL_N - 1, g_small[SMALL_N - 1]); // not written past
}

// Rewinding keeps the storage and clears the sticky flag, so the same region can be produced into
// again without rebinding it.
void test_reset_rewinds_and_clears_the_overflow(void)
{
    protocore_span s = span.from(g_small, SMALL_N);
    bytes.raw(&s, g_big, SMALL_N + 3u);
    TEST_ASSERT_TRUE(s.overflow);

    span.reset(&s);
    TEST_ASSERT_EQUAL_size_t(0u, s.pos);
    TEST_ASSERT_FALSE(s.overflow);
    TEST_ASSERT_TRUE(span.ok(s));
    TEST_ASSERT_EQUAL_PTR(g_small, s.buf);
    TEST_ASSERT_EQUAL_size_t(SMALL_N, s.cap);
    TEST_ASSERT_EQUAL_size_t(SMALL_N, span.room(s));
}

// A sub-region past the parent's end is empty rather than a pointer past the allocation.
void test_after_clamps_rather_than_pointing_past_the_allocation(void)
{
    protocore_span s = span.from(g_small, SMALL_N);

    protocore_span tail = span.after(s, 3);
    TEST_ASSERT_EQUAL_PTR(&g_small[3], tail.buf);
    TEST_ASSERT_EQUAL_size_t(SMALL_N - 3u, tail.cap);
    TEST_ASSERT_EQUAL_size_t(0u, tail.pos); // a fresh cursor over the tail

    protocore_span at_end = span.after(s, SMALL_N);
    TEST_ASSERT_NULL(at_end.buf);
    TEST_ASSERT_EQUAL_size_t(0u, at_end.cap);

    protocore_span past = span.after(s, SMALL_N + 1000u);
    TEST_ASSERT_NULL(past.buf);
    TEST_ASSERT_EQUAL_size_t(0u, past.cap);

    protocore_span from_empty = span.after(span.from(NULL, 0), 0);
    TEST_ASSERT_NULL(from_empty.buf);
}

// The first n bytes, clamped to what the parent actually holds.
void test_first_clamps_to_what_the_parent_holds(void)
{
    protocore_span s = span.from(g_small, SMALL_N);

    protocore_span head = span.first(s, 3);
    TEST_ASSERT_EQUAL_PTR(g_small, head.buf);
    TEST_ASSERT_EQUAL_size_t(3u, head.cap);

    protocore_span whole = span.first(s, SMALL_N);
    TEST_ASSERT_EQUAL_size_t(SMALL_N, whole.cap);

    protocore_span asked_too_much = span.first(s, SMALL_N + 1000u);
    TEST_ASSERT_EQUAL_size_t(SMALL_N, asked_too_much.cap); // clamped, not granted

    protocore_span none = span.first(s, 0);
    TEST_ASSERT_NULL(none.buf);
    TEST_ASSERT_EQUAL_size_t(0u, none.cap);
}

// The reader's length comes from the span's own cursor, so it is never handed a length that
// disagrees with the bytes. An overflowed span produced nothing consistent, so it yields nothing.
void test_produced_is_the_cursor_and_is_empty_after_an_overflow(void)
{
    protocore_span s = span.from(g_small, SMALL_N);
    bytes.put(&s, 0x01);
    bytes.put(&s, 0x02);
    bytes.put(&s, 0x03);

    protocore_cspan out = span.produced(s);
    TEST_ASSERT_EQUAL_PTR(g_small, out.buf);
    TEST_ASSERT_EQUAL_size_t(3u, out.len);
    TEST_ASSERT_TRUE(span.cok(out));

    // Nothing produced yet is an empty view, not a view of the whole capacity.
    protocore_span fresh = span.from(g_small, SMALL_N);
    protocore_cspan empty = span.produced(fresh);
    TEST_ASSERT_NULL(empty.buf);
    TEST_ASSERT_EQUAL_size_t(0u, empty.len);

    bytes.raw(&s, g_big, SMALL_N);
    TEST_ASSERT_TRUE(s.overflow);
    protocore_cspan after_overflow = span.produced(s);
    TEST_ASSERT_NULL(after_overflow.buf);
    TEST_ASSERT_EQUAL_size_t(0u, after_overflow.len);
}

// A read view is clamped to the capacity, whatever length is asked for.
void test_read_clamps_to_the_capacity(void)
{
    protocore_span s = span.from(g_small, SMALL_N);

    protocore_cspan part = span.read(s, 4);
    TEST_ASSERT_EQUAL_PTR(g_small, part.buf);
    TEST_ASSERT_EQUAL_size_t(4u, part.len);

    protocore_cspan too_much = span.read(s, SMALL_N + 1000u);
    TEST_ASSERT_EQUAL_size_t(SMALL_N, too_much.len);

    protocore_cspan of_empty = span.read(span.from(NULL, 0), 8);
    TEST_ASSERT_NULL(of_empty.buf);
    TEST_ASSERT_EQUAL_size_t(0u, of_empty.len);
}

// The bytes a bounded write produced come back through the paired reader with the same values in
// the same order, so the region survives a round trip through the two accessors.
void test_the_region_round_trips_through_the_paired_verbs(void)
{
    protocore_span w = span.from(g_big, 16);
    bytes.put_be(&w, 0x0102030405060708ull, 8);
    bytes.put_be(&w, 0xA1B2C3D4u, 4);
    TEST_ASSERT_TRUE(span.ok(w));
    TEST_ASSERT_EQUAL_size_t(12u, span.len(w));

    protocore_cspan r = span.produced(w);
    TEST_ASSERT_EQUAL_size_t(12u, r.len);
    uint64_t v = 0;
    TEST_ASSERT_TRUE(bytes.take_be(&r, 8, &v));
    TEST_ASSERT_EQUAL_HEX64(0x0102030405060708ull, v);
    TEST_ASSERT_TRUE(bytes.take_be(&r, 4, &v));
    TEST_ASSERT_EQUAL_HEX64(0xA1B2C3D4ull, v);

    // Past the end sets the sticky err and leaves the cursor where it was.
    const size_t at = r.pos;
    TEST_ASSERT_FALSE(bytes.take_be(&r, 1, &v));
    TEST_ASSERT_EQUAL_size_t(at, r.pos);
    TEST_ASSERT_TRUE(r.err);
    TEST_ASSERT_FALSE(span.cok(r));
}

// The table names twelve accessors and several share a signature, so a swapped pair type-checks and
// links; only pointer identity catches it.
void test_the_table_is_wired_to_the_named_accessors(void)
{
    TEST_ASSERT_EQUAL_PTR(protocore_span_from, span.from);
    TEST_ASSERT_EQUAL_PTR(protocore_span_ok, span.ok);
    TEST_ASSERT_EQUAL_PTR(protocore_span_has_storage, span.has_storage);
    TEST_ASSERT_EQUAL_PTR(protocore_span_len, span.len);
    TEST_ASSERT_EQUAL_PTR(protocore_span_room, span.room);
    TEST_ASSERT_EQUAL_PTR(protocore_span_reset, span.reset);
    TEST_ASSERT_EQUAL_PTR(protocore_span_after, span.after);
    TEST_ASSERT_EQUAL_PTR(protocore_span_first, span.first);
    TEST_ASSERT_EQUAL_PTR(protocore_span_produced, span.produced);
    TEST_ASSERT_EQUAL_PTR(protocore_span_read, span.read);
    TEST_ASSERT_EQUAL_PTR(protocore_cspan_from, span.cfrom);
    TEST_ASSERT_EQUAL_PTR(protocore_cspan_ok, span.cok);
}
