// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the abstract logging layer (shared_primitives/log.h). Built at
// PC_LOG_LEVEL_INFO with PC_ENABLE_LOGBUF, so the interesting property is testable at runtime:
// DEBUG sits below the floor and must be *absent* from the binary, while INFO and above emit.
//
// The compile-time half of the guarantee (no code, no flash string for a discarded level) is not a
// runtime property, so it is asserted the only way it can be - by observing that a discarded call
// reaches neither the sink nor the ring, with the argument-evaluation probe below proving the
// arguments were never even evaluated.

#include "services/system/logbuf/logbuf.h"
#include "shared_primitives/log.h"
#include "shared_primitives/ring.h" // shared SPSC byte-ring primitive, exercised at the bottom
#include <stdio.h>
#include <string.h>
#include <unity.h>

static uint8_t s_last_level = 0xFF;
static char s_last_line[256];
static int s_sink_calls = 0;
static int s_eval_count = 0;

static void test_sink(uint8_t level, const char *line)
{
    s_last_level = level;
    snprintf(s_last_line, sizeof(s_last_line), "%s", line);
    s_sink_calls++;
}

// Named in a log argument; a discarded level must never call it.
static int counting_arg(void)
{
    s_eval_count++;
    return 42;
}

// Log frames under test. Each is the message shape the caller declares.
static const pc_field F_DEBUG[] = {{PC_FK_LIT, 0, 6, "debug "}, PC_I64, PC_END};
static const pc_field F_EXPENSIVE[] = {{PC_FK_LIT, 0, 10, "expensive "}, PC_I64, PC_END};
static const pc_field F_HELLO[] = {{PC_FK_LIT, 0, 6, "hello "}, PC_I64, PC_END};
static const pc_field F_WARN[] = {{PC_FK_LIT, 0, 5, "warn "}, PC_STR, PC_END};
static const pc_field F_ERR[] = {{PC_FK_LIT, 0, 4, "err "}, PC_U32, PC_END};
static const pc_field F_VALUE[] = {{PC_FK_LIT, 0, 6, "value "}, PC_I64, PC_END};

static const pc_field F_RING[] = {{PC_FK_LIT, 0, 11, "to the ring"}, PC_END};
static const pc_field F_I[] = {{PC_FK_LIT, 0, 1, "i"}, PC_END};
static const pc_field F_W[] = {{PC_FK_LIT, 0, 1, "w"}, PC_END};
static const pc_field F_E[] = {{PC_FK_LIT, 0, 1, "e"}, PC_END};
static const pc_field F_STILL[] = {{PC_FK_LIT, 0, 22, "still goes to the ring"}, PC_END};
static const pc_field F_STR[] = {PC_STR, PC_END};

void setUp()
{
    s_last_level = 0xFF;
    s_last_line[0] = '\0';
    s_sink_calls = 0;
    s_eval_count = 0;
    pc_logbuf_reset();
    pc_log_set_sink(test_sink);
}

void tearDown()
{
    pc_log_set_sink(nullptr);
}

// --- the floor ------------------------------------------------------------

void test_debug_is_below_the_floor_and_emits_nothing()
{
    PC_LOGD(F_DEBUG, (int64_t)counting_arg());
    TEST_ASSERT_EQUAL_INT(0, s_sink_calls);
    TEST_ASSERT_EQUAL_UINT16(0, pc_log_count());
}

void test_discarded_call_does_not_evaluate_its_arguments()
{
    // The whole point of a preprocessor filter rather than a runtime `if`: a discarded log must not
    // pay for building its own arguments either.
    PC_LOGD(F_EXPENSIVE, (int64_t)counting_arg());
    TEST_ASSERT_EQUAL_INT(0, s_eval_count);
}

void test_info_and_above_emit()
{
    PC_LOGI(F_HELLO, (int64_t)7);
    TEST_ASSERT_EQUAL_INT(1, s_sink_calls);
    TEST_ASSERT_EQUAL_UINT8(PC_LOG_LEVEL_INFO, s_last_level);
    TEST_ASSERT_EQUAL_STRING("hello 7", s_last_line);

    PC_LOGW(F_WARN, "here");
    TEST_ASSERT_EQUAL_UINT8(PC_LOG_LEVEL_WARN, s_last_level);
    TEST_ASSERT_EQUAL_STRING("warn here", s_last_line);

    PC_LOGE(F_ERR, (uint32_t)3);
    TEST_ASSERT_EQUAL_UINT8(PC_LOG_LEVEL_ERROR, s_last_level);
    TEST_ASSERT_EQUAL_STRING("err 3", s_last_line);

    TEST_ASSERT_EQUAL_INT(3, s_sink_calls);
}

void test_enabled_call_does_evaluate_its_arguments()
{
    PC_LOGI(F_VALUE, (int64_t)counting_arg());
    TEST_ASSERT_EQUAL_INT(1, s_eval_count);
    TEST_ASSERT_EQUAL_STRING("value 42", s_last_line);
}

// --- routing --------------------------------------------------------------

void test_emitted_line_also_reaches_the_logbuf_ring()
{
    PC_LOGW(F_RING);
    TEST_ASSERT_EQUAL_UINT16(1, pc_log_count());
    TEST_ASSERT_EQUAL_STRING("W to the ring", pc_log_at(0));
}

void test_levels_match_the_logbuf_letters()
{
    // The PC_LOG_LEVEL_* preprocessor values and pc_log_level's constexprs are two spellings of one
    // scale; if they ever drift, the stored letter is what goes wrong, so assert on that.
    PC_LOGI(F_I);
    PC_LOGW(F_W);
    PC_LOGE(F_E);
    TEST_ASSERT_EQUAL_STRING("I i", pc_log_at(0));
    TEST_ASSERT_EQUAL_STRING("W w", pc_log_at(1));
    TEST_ASSERT_EQUAL_STRING("E e", pc_log_at(2));
    TEST_ASSERT_EQUAL_INT(PC_LOG_LEVEL_INFO, (int)pc_log_level::PC_LOG_INFO);
    TEST_ASSERT_EQUAL_INT(PC_LOG_LEVEL_WARN, (int)pc_log_level::PC_LOG_WARN);
    TEST_ASSERT_EQUAL_INT(PC_LOG_LEVEL_ERROR, (int)pc_log_level::PC_LOG_ERROR);
    TEST_ASSERT_EQUAL_INT(PC_LOG_LEVEL_DEBUG, (int)pc_log_level::PC_LOG_DEBUG);
}

void test_no_sink_is_not_a_crash()
{
    pc_log_set_sink(nullptr);
    PC_LOGE(F_STILL);
    TEST_ASSERT_EQUAL_INT(0, s_sink_calls);
    TEST_ASSERT_EQUAL_UINT16(1, pc_log_count());
}

// --- formatting edges -----------------------------------------------------

void test_long_line_is_truncated_not_overflowed()
{
    char big[PC_LOG_LINE_LEN * 3];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    PC_LOGE(F_STR, big);
    TEST_ASSERT_EQUAL_INT(1, s_sink_calls);
    TEST_ASSERT_TRUE(strlen(s_last_line) < PC_LOG_LINE_LEN);
}

void test_null_spec_is_ignored()
{
    const pc_field *spec = nullptr;
    pc_log_frame(PC_LOG_LEVEL_ERROR, spec);
    TEST_ASSERT_EQUAL_INT(0, s_sink_calls);
    TEST_ASSERT_EQUAL_UINT16(0, pc_log_count());
}

void test_empty_message_is_still_a_line()
{
    PC_LOGI(F_STR, "");
    TEST_ASSERT_EQUAL_INT(1, s_sink_calls);
    TEST_ASSERT_EQUAL_STRING("", s_last_line);
    TEST_ASSERT_EQUAL_STRING("I ", pc_log_at(0));
}

// ---------------------------------------------------------------------------
// shared_primitives/ring.h - the single-producer / single-consumer byte-ring
// shared by both transports. It is header-only inline math with no .cpp of its
// own, so it is exercised directly here rather than through a transport.
// ---------------------------------------------------------------------------

// The pc_atomic wrapper must behave as a plain value through all four of its
// conversion/assignment paths.
void test_ring_atomic_wrapper_round_trips()
{
    pc_atomic<size_t> a;
    TEST_ASSERT_EQUAL_size_t(0, (size_t)a); // default-constructed is zero
    a = (size_t)7;                          // operator=(T): release store
    TEST_ASSERT_EQUAL_size_t(7, (size_t)a); // operator T(): acquire load

    pc_atomic<size_t> copied(a); // copy construction
    TEST_ASSERT_EQUAL_size_t(7, (size_t)copied);

    pc_atomic<size_t> assigned;
    assigned = copied; // copy assignment
    TEST_ASSERT_EQUAL_size_t(7, (size_t)assigned);

    pc_atomic<size_t> seeded((size_t)3); // value construction
    TEST_ASSERT_EQUAL_size_t(3, (size_t)seeded);
}

// Single-byte pops report emptiness rather than running past the head.
void test_ring_read_byte_and_available()
{
    uint8_t buf[8] = {'a', 'b', 'c', 0, 0, 0, 0, 0};
    pc_atomic<size_t> head((size_t)3);
    pc_atomic<size_t> tail((size_t)0);
    TEST_ASSERT_EQUAL_size_t(3, pc_ring_available(head, tail, sizeof(buf)));

    uint8_t out = 0;
    TEST_ASSERT_TRUE(pc_ring_read_byte(buf, sizeof(buf), head, tail, &out));
    TEST_ASSERT_EQUAL_HEX8('a', out);
    TEST_ASSERT_EQUAL_size_t(2, pc_ring_available(head, tail, sizeof(buf)));
    TEST_ASSERT_TRUE(pc_ring_read_byte(buf, sizeof(buf), head, tail, &out));
    TEST_ASSERT_EQUAL_HEX8('b', out);
    TEST_ASSERT_TRUE(pc_ring_read_byte(buf, sizeof(buf), head, tail, &out));
    TEST_ASSERT_EQUAL_HEX8('c', out);
    // Tail has caught the head: empty.
    TEST_ASSERT_FALSE(pc_ring_read_byte(buf, sizeof(buf), head, tail, &out));
    TEST_ASSERT_EQUAL_size_t(0, pc_ring_available(head, tail, sizeof(buf)));
}

// A bulk read stops at whichever comes first: the caller's limit or the head.
void test_ring_read_bulk_stops_at_head_and_maxn()
{
    uint8_t buf[8] = {'0', '1', '2', '3', '4', 0, 0, 0};
    pc_atomic<size_t> head((size_t)5);
    pc_atomic<size_t> tail((size_t)0);
    uint8_t dst[8] = {0};

    TEST_ASSERT_EQUAL_size_t(2, pc_ring_read(buf, sizeof(buf), head, tail, dst, 2)); // limited by maxn
    TEST_ASSERT_EQUAL_HEX8('0', dst[0]);
    TEST_ASSERT_EQUAL_HEX8('1', dst[1]);

    TEST_ASSERT_EQUAL_size_t(3, pc_ring_read(buf, sizeof(buf), head, tail, dst, sizeof(dst))); // limited by head
    TEST_ASSERT_EQUAL_HEX8('2', dst[0]);
    TEST_ASSERT_EQUAL_HEX8('4', dst[2]);

    TEST_ASSERT_EQUAL_size_t(0, pc_ring_read(buf, sizeof(buf), head, tail, dst, sizeof(dst))); // now empty
}

// Peek is wrap-aware and non-destructive; consume advances the tail modulo cap.
void test_ring_peek_and_consume_wrap()
{
    uint8_t buf[8] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};
    pc_atomic<size_t> tail((size_t)6);
    uint8_t dst[4] = {0};

    pc_ring_peek(buf, sizeof(buf), tail, 0, dst, 4); // 6, 7, then wraps to 0, 1
    TEST_ASSERT_EQUAL_HEX8('G', dst[0]);
    TEST_ASSERT_EQUAL_HEX8('H', dst[1]);
    TEST_ASSERT_EQUAL_HEX8('A', dst[2]);
    TEST_ASSERT_EQUAL_HEX8('B', dst[3]);
    TEST_ASSERT_EQUAL_size_t(6, (size_t)tail); // peeking consumed nothing

    pc_ring_peek(buf, sizeof(buf), tail, 2, dst, 2); // offset lands past the wrap
    TEST_ASSERT_EQUAL_HEX8('A', dst[0]);
    TEST_ASSERT_EQUAL_HEX8('B', dst[1]);

    pc_ring_consume(tail, sizeof(buf), 4);
    TEST_ASSERT_EQUAL_size_t(2, (size_t)tail); // (6 + 4) % 8
}

// Free space always reserves one slot so full is distinguishable from empty.
void test_ring_free_reserves_one_slot()
{
    pc_atomic<size_t> head((size_t)0);
    pc_atomic<size_t> tail((size_t)0);
    TEST_ASSERT_EQUAL_size_t(7, pc_ring_free(head, tail, 8)); // empty -> cap - 1
    head = (size_t)5;
    TEST_ASSERT_EQUAL_size_t(2, pc_ring_free(head, tail, 8));
    head = (size_t)7;
    TEST_ASSERT_EQUAL_size_t(0, pc_ring_free(head, tail, 8)); // full
}

// The producer span copy clamps to the wrap point and resumes at the buffer start.
void test_ring_write_span_wraps()
{
    uint8_t buf[8];
    memset(buf, 0, sizeof(buf));

    // Whole copy fits before the wrap point: the chunk is clamped to len.
    size_t head = pc_ring_write_span(buf, sizeof(buf), 0, (const uint8_t *)"abc", 3);
    TEST_ASSERT_EQUAL_size_t(3, head);
    TEST_ASSERT_EQUAL_MEMORY("abc", buf, 3);

    // Copy straddles the wrap: two spans, the first clamped to the buffer end.
    head = pc_ring_write_span(buf, sizeof(buf), 6, (const uint8_t *)"WXYZ", 4);
    TEST_ASSERT_EQUAL_size_t(2, head); // (6 + 4) % 8
    TEST_ASSERT_EQUAL_HEX8('W', buf[6]);
    TEST_ASSERT_EQUAL_HEX8('X', buf[7]);
    TEST_ASSERT_EQUAL_HEX8('Y', buf[0]);
    TEST_ASSERT_EQUAL_HEX8('Z', buf[1]);

    // Nothing to copy: the head is returned untouched.
    TEST_ASSERT_EQUAL_size_t(4, pc_ring_write_span(buf, sizeof(buf), 4, (const uint8_t *)"", 0));
}

int main(int, char **)
{
    UNITY_BEGIN();
    RUN_TEST(test_debug_is_below_the_floor_and_emits_nothing);
    RUN_TEST(test_discarded_call_does_not_evaluate_its_arguments);
    RUN_TEST(test_info_and_above_emit);
    RUN_TEST(test_enabled_call_does_evaluate_its_arguments);
    RUN_TEST(test_emitted_line_also_reaches_the_logbuf_ring);
    RUN_TEST(test_levels_match_the_logbuf_letters);
    RUN_TEST(test_no_sink_is_not_a_crash);
    RUN_TEST(test_long_line_is_truncated_not_overflowed);
    RUN_TEST(test_null_spec_is_ignored);
    RUN_TEST(test_empty_message_is_still_a_line);
    RUN_TEST(test_ring_atomic_wrapper_round_trips);
    RUN_TEST(test_ring_read_byte_and_available);
    RUN_TEST(test_ring_read_bulk_stops_at_head_and_maxn);
    RUN_TEST(test_ring_peek_and_consume_wrap);
    RUN_TEST(test_ring_free_reserves_one_slot);
    RUN_TEST(test_ring_write_span_wraps);
    return UNITY_END();
}
