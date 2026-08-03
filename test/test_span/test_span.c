// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// pc_span: the run length must be bound in BOTH directions.
//
//   forward   cap  - sizes the storage and bounds every write
//   backward  pos  - what the payload actually needed, still counting after an overflow, so an
//                    undersized run-length constant reports the size it should have been
//
// The size is never deduced or recovered - every buffer already has a declared constant. What the
// span fixes is the *restating* of it: rule 19 turns an array into a borrowed region, and the naive
// rewrite repeats the length at each use where the copies can drift apart (or become a 4-byte
// sizeof on a pointer). Stated once, carried thereafter.
//
// The field names are the bytes.h write-cursor convention on purpose, so pc_bw_* drives a pc_span
// with no second byte-append API; that pairing is exercised here too.

#include "shared_primitives/bytes.h"
#include "shared_primitives/span.h"
#include <string.h>
#include <unity.h>

#define SMALL_N 7
#define BIG_N 2048

void setUp(void)
{
}
void tearDown(void)
{
}

// --- forward: the constant is stated once and travels with the storage ---

static void test_capacity_is_the_constant_it_was_built_from(void)
{
    uint8_t small[SMALL_N];
    uint8_t big[BIG_N];
    TEST_ASSERT_EQUAL_UINT32(SMALL_N, pc_span_from(small, SMALL_N).cap);
    TEST_ASSERT_EQUAL_UINT32(BIG_N, pc_span_from(big, BIG_N).cap);
    TEST_ASSERT_EQUAL_PTR(small, pc_span_from(small, SMALL_N).buf);
    TEST_ASSERT_EQUAL_PTR(big, pc_span_from(big, BIG_N).buf);
}

// The regression this type exists to prevent: through a pointer, sizeof() reports the pointer
// width (4 or 8) instead of the run length. Carried in a span, the run length survives.
static void test_span_survives_what_sizeof_loses(void)
{
    uint8_t buf[BIG_N];
    uint8_t *as_ptr = buf;
    pc_span as_span = pc_span_from(buf, BIG_N);

    TEST_ASSERT_EQUAL_UINT32(sizeof(void *), sizeof(as_ptr)); // the trap: 4 or 8, never 2048
    TEST_ASSERT_EQUAL_UINT32(BIG_N, as_span.cap);             // the fix
    TEST_ASSERT_TRUE(as_span.cap != sizeof(as_ptr));
}

static void test_a_fresh_span_is_empty_and_ok(void)
{
    uint8_t buf[16];
    pc_span s = pc_span_from(buf, 16);
    TEST_ASSERT_TRUE(pc_span_ok(s));
    TEST_ASSERT_EQUAL_UINT32(0, pc_span_len(s));
    TEST_ASSERT_EQUAL_UINT32(16, pc_span_room(s));
    TEST_ASSERT_FALSE(s.overflow);
}

// --- backward: pos reports what was needed, past cap on overflow ---

static void test_produced_length_rides_back_with_the_buffer(void)
{
    uint8_t buf[16];
    pc_span s = pc_span_from(buf, 16);
    pc_bw_put(&s, 0xAA);
    pc_bw_put_be(&s, 0x1122334455667788ull, 8);
    TEST_ASSERT_TRUE(pc_span_ok(s));
    TEST_ASSERT_EQUAL_UINT32(9, pc_span_len(s)); // no out_len parameter needed
    TEST_ASSERT_EQUAL_UINT32(7, pc_span_room(s));
    TEST_ASSERT_EQUAL_UINT8(0xAA, buf[0]);
    TEST_ASSERT_EQUAL_UINT8(0x11, buf[1]);
    TEST_ASSERT_EQUAL_UINT8(0x88, buf[8]);
}

// The point of the backward direction: an overflow does not merely fail, it reports the capacity
// the run-length constant should have had.
static void test_overflow_keeps_counting_the_required_size(void)
{
    uint8_t buf[4];
    pc_span s = pc_span_from(buf, 4);
    for (int i = 0; i < 10; i++)
    {
        pc_bw_put(&s, (uint8_t)i);
    }
    TEST_ASSERT_FALSE(pc_span_ok(s));             // it did not fit
    TEST_ASSERT_TRUE(s.overflow);                 // and says so
    TEST_ASSERT_EQUAL_UINT32(10, pc_span_len(s)); // ...and 10 is what cap should have been
    TEST_ASSERT_EQUAL_UINT32(0, pc_span_room(s)); // no room reported past the end
    TEST_ASSERT_EQUAL_UINT8(0, buf[0]);           // the bytes that fit still landed
    TEST_ASSERT_EQUAL_UINT8(3, buf[3]);
}

static void test_reset_rewinds_and_clears_overflow(void)
{
    uint8_t buf[2];
    pc_span s = pc_span_from(buf, 2);
    pc_bw_put(&s, 1);
    pc_bw_put(&s, 2);
    pc_bw_put(&s, 3); // overflows
    TEST_ASSERT_FALSE(pc_span_ok(s));
    pc_span_reset(&s);
    TEST_ASSERT_TRUE(pc_span_ok(s));
    TEST_ASSERT_EQUAL_UINT32(0, pc_span_len(s));
    TEST_ASSERT_EQUAL_UINT32(2, pc_span_room(s));
}

// --- fail-closed normalization: {NULL, n} must be unconstructible ---

static void test_null_pointer_yields_zero_capacity(void)
{
    pc_span s = pc_span_from(NULL, 4096);
    TEST_ASSERT_NULL(s.buf);
    TEST_ASSERT_EQUAL_UINT32(0, s.cap); // NOT 4096: an omitted check must write nothing
    TEST_ASSERT_FALSE(pc_span_ok(s));
    TEST_ASSERT_FALSE(pc_span_has_storage(s));
}

static void test_zero_capacity_yields_null_pointer(void)
{
    uint8_t buf[4];
    pc_span s = pc_span_from(buf, 0);
    TEST_ASSERT_NULL(s.buf);
    TEST_ASSERT_EQUAL_UINT32(0, s.cap);
    TEST_ASSERT_FALSE(pc_span_ok(s));
}

// A write into a failed allocation must be a no-op, not a null dereference.
static void test_writing_a_failed_allocation_is_a_noop(void)
{
    pc_span s = pc_span_from(NULL, 4096);
    pc_bw_put(&s, 0xFF);
    pc_bw_put_be(&s, 0xDEADBEEF, 4);
    TEST_ASSERT_FALSE(pc_span_ok(s));
    TEST_ASSERT_EQUAL_UINT32(5, pc_span_len(s)); // still reports what was wanted
}

static void test_cspan_null_and_zero_normalize(void)
{
    const uint8_t buf[4] = {0};
    TEST_ASSERT_FALSE(pc_cspan_ok(pc_cspan_from(NULL, 99)));
    TEST_ASSERT_EQUAL_UINT32(0, pc_cspan_from(NULL, 99).len);
    TEST_ASSERT_FALSE(pc_cspan_ok(pc_cspan_from(buf, 0)));
    TEST_ASSERT_TRUE(pc_cspan_ok(pc_cspan_from(buf, 4)));
}

// --- sub-spans clamp instead of running past the allocation ---

static void test_after_advances_and_shrinks(void)
{
    uint8_t buf[10];
    pc_span s = pc_span_after(pc_span_from(buf, 10), 3);
    TEST_ASSERT_EQUAL_PTR(buf + 3, s.buf);
    TEST_ASSERT_EQUAL_UINT32(7, s.cap);          // capacity tracks the advance, so it cannot over-report
    TEST_ASSERT_EQUAL_UINT32(0, pc_span_len(s)); // a fresh cursor over the tail
}

static void test_after_past_the_end_is_empty_not_out_of_bounds(void)
{
    uint8_t buf[10];
    TEST_ASSERT_FALSE(pc_span_has_storage(pc_span_after(pc_span_from(buf, 10), 10)));
    TEST_ASSERT_FALSE(pc_span_has_storage(pc_span_after(pc_span_from(buf, 10), 11)));
    TEST_ASSERT_FALSE(pc_span_has_storage(pc_span_after(pc_span_from(buf, 10), (size_t)-1)));
    TEST_ASSERT_EQUAL_UINT32(0, pc_span_after(pc_span_from(buf, 10), 999).cap);
}

static void test_first_clamps_to_what_exists(void)
{
    uint8_t buf[10];
    TEST_ASSERT_EQUAL_UINT32(4, pc_span_first(pc_span_from(buf, 10), 4).cap);
    TEST_ASSERT_EQUAL_UINT32(10, pc_span_first(pc_span_from(buf, 10), 10).cap);
    TEST_ASSERT_EQUAL_UINT32(10, pc_span_first(pc_span_from(buf, 10), 99).cap); // clamped
    TEST_ASSERT_FALSE(pc_span_has_storage(pc_span_first(pc_span_from(buf, 10), 0)));
}

// --- handing the produced bytes on, without a separate length ---

static void test_produced_view_uses_the_spans_own_cursor(void)
{
    uint8_t buf[16];
    pc_span s = pc_span_from(buf, 16);
    pc_bw_put(&s, 'h');
    pc_bw_put(&s, 'i');
    pc_cspan v = pc_span_produced(s);
    TEST_ASSERT_TRUE(pc_cspan_ok(v));
    TEST_ASSERT_EQUAL_PTR(buf, v.buf);
    TEST_ASSERT_EQUAL_UINT32(2, v.len); // the length cannot disagree with the bytes
}

// A partially written frame must never be handed on as though it were whole.
static void test_produced_view_of_an_overflowed_span_is_empty(void)
{
    uint8_t buf[2];
    pc_span s = pc_span_from(buf, 2);
    pc_bw_put(&s, 1);
    pc_bw_put(&s, 2);
    pc_bw_put(&s, 3); // overflow
    TEST_ASSERT_FALSE(pc_cspan_ok(pc_span_produced(s)));
    TEST_ASSERT_EQUAL_UINT32(0, pc_span_produced(s).len);
}

static void test_read_narrows_to_a_given_length(void)
{
    uint8_t buf[10];
    memset(buf, 0xAB, sizeof(buf));
    pc_cspan c = pc_span_read(pc_span_from(buf, 10), 6);
    TEST_ASSERT_TRUE(pc_cspan_ok(c));
    TEST_ASSERT_EQUAL_UINT32(6, c.len);
    TEST_ASSERT_EQUAL_UINT8(0xAB, c.buf[5]);
    TEST_ASSERT_EQUAL_UINT32(10, pc_span_read(pc_span_from(buf, 10), 99).len); // clamped
    TEST_ASSERT_FALSE(pc_cspan_ok(pc_span_read(pc_span_from(NULL, 0), 4)));
}

// --- the read cursor from bytes.h drives a pc_cspan unchanged ---

static void test_bytes_read_cursor_drives_a_cspan(void)
{
    const uint8_t frame[4] = {0xDE, 0xAD, 0xBE, 0xEF}; // a plain 4-byte big-endian value
    pc_cspan r = pc_cspan_from(frame, 4);
    uint64_t v = 0;
    TEST_ASSERT_TRUE(pc_br_take_be(&r, 4, &v));
    TEST_ASSERT_EQUAL_HEX64(0xDEADBEEFull, v);
    TEST_ASSERT_TRUE(pc_cspan_ok(r));
    TEST_ASSERT_EQUAL_UINT32(4, r.pos); // reads at the cursor: no tag byte is skipped

    uint64_t again = 0;
    TEST_ASSERT_FALSE(pc_br_take_be(&r, 4, &again)); // past the end
    TEST_ASSERT_FALSE(pc_cspan_ok(r));               // sticky error
}

// A length prefix of 0xFFFFFFFF must be rejected, not wrapped into a small number. On a 32-bit
// size_t (esp32, c2000) the old `*off + n > len` form summed to 7 and passed. A 64-bit host cannot
// reproduce that, so this asserts the contract rather than the arithmetic.
static void test_a_wire_length_cannot_overflow_the_bound(void)
{
    const uint8_t frame[12] = {0, 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF, 0, 0, 0, 0};
    size_t off = 4; // sitting on the length prefix
    const uint8_t *out = NULL;
    uint32_t slen = 0;
    TEST_ASSERT_FALSE(pc_rd_str(frame, sizeof(frame), &off, &out, &slen));
    TEST_ASSERT_EQUAL_UINT32(4, off); // a failed read leaves the offset on its own field
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_capacity_is_the_constant_it_was_built_from);
    RUN_TEST(test_span_survives_what_sizeof_loses);
    RUN_TEST(test_a_fresh_span_is_empty_and_ok);
    RUN_TEST(test_produced_length_rides_back_with_the_buffer);
    RUN_TEST(test_overflow_keeps_counting_the_required_size);
    RUN_TEST(test_reset_rewinds_and_clears_overflow);
    RUN_TEST(test_null_pointer_yields_zero_capacity);
    RUN_TEST(test_zero_capacity_yields_null_pointer);
    RUN_TEST(test_writing_a_failed_allocation_is_a_noop);
    RUN_TEST(test_cspan_null_and_zero_normalize);
    RUN_TEST(test_after_advances_and_shrinks);
    RUN_TEST(test_after_past_the_end_is_empty_not_out_of_bounds);
    RUN_TEST(test_first_clamps_to_what_exists);
    RUN_TEST(test_produced_view_uses_the_spans_own_cursor);
    RUN_TEST(test_produced_view_of_an_overflowed_span_is_empty);
    RUN_TEST(test_read_narrows_to_a_given_length);
    RUN_TEST(test_bytes_read_cursor_drives_a_cspan);
    RUN_TEST(test_a_wire_length_cannot_overflow_the_bound);
    return UNITY_END();
}
