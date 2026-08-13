// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the abstract logging layer (shared/log/log.h). Built at
// PROTOCORE_LOG_LEVEL_INFO with PROTOCORE_ENABLE_LOGBUF, so the interesting property is testable at runtime:
// DEBUG sits below the floor and must be *absent* from the binary, while INFO and above emit.
//
// The compile-time half of the guarantee (no code, no flash string for a discarded level) is not a
// runtime property, so it is asserted the only way it can be - by observing that a discarded call
// reaches neither the sink nor the ring, with the argument-evaluation probe below proving the
// arguments were never even evaluated.

#include "mmgr/protoframe.h" // the log frames below need the complete type
#include "mmgr/ring.h"       // shared SPSC byte-ring primitive, exercised at the bottom
#include "server/system/logbuf.h"
#include "shared/log/log.h"
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
static const protocore_field F_DEBUG[] = {{PROTOCORE_FK_LIT, 0, 6, "debug "}, PROTOCORE_I64, PROTOCORE_END};
static const protocore_field F_EXPENSIVE[] = {{PROTOCORE_FK_LIT, 0, 10, "expensive "}, PROTOCORE_I64, PROTOCORE_END};
static const protocore_field F_HELLO[] = {{PROTOCORE_FK_LIT, 0, 6, "hello "}, PROTOCORE_I64, PROTOCORE_END};
static const protocore_field F_WARN[] = {{PROTOCORE_FK_LIT, 0, 5, "warn "}, PROTOCORE_STR, PROTOCORE_END};
static const protocore_field F_ERR[] = {{PROTOCORE_FK_LIT, 0, 4, "err "}, PROTOCORE_U32, PROTOCORE_END};
static const protocore_field F_VALUE[] = {{PROTOCORE_FK_LIT, 0, 6, "value "}, PROTOCORE_I64, PROTOCORE_END};

static const protocore_field F_RING[] = {{PROTOCORE_FK_LIT, 0, 11, "to the ring"}, PROTOCORE_END};
static const protocore_field F_I[] = {{PROTOCORE_FK_LIT, 0, 1, "i"}, PROTOCORE_END};
static const protocore_field F_W[] = {{PROTOCORE_FK_LIT, 0, 1, "w"}, PROTOCORE_END};
static const protocore_field F_E[] = {{PROTOCORE_FK_LIT, 0, 1, "e"}, PROTOCORE_END};
static const protocore_field F_STILL[] = {{PROTOCORE_FK_LIT, 0, 22, "still goes to the ring"}, PROTOCORE_END};
static const protocore_field F_STR[] = {PROTOCORE_STR, PROTOCORE_END};

void setUp()
{
    s_last_level = 0xFF;
    s_last_line[0] = '\0';
    s_sink_calls = 0;
    s_eval_count = 0;
    protocore_logbuf_reset();
    protocore_log_set_sink(test_sink);
}

void tearDown()
{
    protocore_log_set_sink(NULL);
}

// --- the floor ------------------------------------------------------------

void test_debug_is_below_the_floor_and_emits_nothing()
{
    PROTOCORE_LOGD(F_DEBUG, ((const protocore_fval[]){PROTOCORE_VI64(counting_arg())}), 1);
    TEST_ASSERT_EQUAL_INT(0, s_sink_calls);
    TEST_ASSERT_EQUAL_UINT16(0, protocore_log_count());
}

void test_discarded_call_does_not_evaluate_its_arguments()
{
    // The whole point of a preprocessor filter rather than a runtime `if`: a discarded log must not
    // pay for building its own arguments either.
    PROTOCORE_LOGD(F_EXPENSIVE, ((const protocore_fval[]){PROTOCORE_VI64(counting_arg())}), 1);
    TEST_ASSERT_EQUAL_INT(0, s_eval_count);
}

void test_info_and_above_emit()
{
    PROTOCORE_LOGI(F_HELLO, ((const protocore_fval[]){PROTOCORE_VI64(7)}), 1);
    TEST_ASSERT_EQUAL_INT(1, s_sink_calls);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_LOG_LEVEL_INFO, s_last_level);
    TEST_ASSERT_EQUAL_STRING("hello 7", s_last_line);

    PROTOCORE_LOGW(F_WARN, ((const protocore_fval[]){PROTOCORE_VSTR("here")}), 1);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_LOG_LEVEL_WARN, s_last_level);
    TEST_ASSERT_EQUAL_STRING("warn here", s_last_line);

    PROTOCORE_LOGE(F_ERR, ((const protocore_fval[]){PROTOCORE_VU32(3u)}), 1);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_LOG_LEVEL_ERROR, s_last_level);
    TEST_ASSERT_EQUAL_STRING("err 3", s_last_line);

    TEST_ASSERT_EQUAL_INT(3, s_sink_calls);
}

void test_enabled_call_does_evaluate_its_arguments()
{
    PROTOCORE_LOGI(F_VALUE, ((const protocore_fval[]){PROTOCORE_VI64(counting_arg())}), 1);
    TEST_ASSERT_EQUAL_INT(1, s_eval_count);
    TEST_ASSERT_EQUAL_STRING("value 42", s_last_line);
}

// --- routing --------------------------------------------------------------

void test_emitted_line_also_reaches_the_logbuf_ring()
{
    PROTOCORE_LOGW(F_RING, NULL, 0);
    TEST_ASSERT_EQUAL_UINT16(1, protocore_log_count());
    TEST_ASSERT_EQUAL_STRING("W to the ring", protocore_log_at(0));
}

void test_levels_match_the_logbuf_letters()
{
    // The PROTOCORE_LOG_LEVEL_* preprocessor values and protocore_log_level's constexprs are two spellings of one
    // scale; if they ever drift, the stored letter is what goes wrong, so assert on that.
    PROTOCORE_LOGI(F_I, NULL, 0);
    PROTOCORE_LOGW(F_W, NULL, 0);
    PROTOCORE_LOGE(F_E, NULL, 0);
    TEST_ASSERT_EQUAL_STRING("I i", protocore_log_at(0));
    TEST_ASSERT_EQUAL_STRING("W w", protocore_log_at(1));
    TEST_ASSERT_EQUAL_STRING("E e", protocore_log_at(2));
    TEST_ASSERT_EQUAL_INT(PROTOCORE_LOG_LEVEL_INFO, (int)PROTOCORE_LOG_INFO);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_LOG_LEVEL_WARN, (int)PROTOCORE_LOG_WARN);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_LOG_LEVEL_ERROR, (int)PROTOCORE_LOG_ERROR);
    TEST_ASSERT_EQUAL_INT(PROTOCORE_LOG_LEVEL_DEBUG, (int)PROTOCORE_LOG_DEBUG);
}

void test_no_sink_is_not_a_crash()
{
    protocore_log_set_sink(NULL);
    PROTOCORE_LOGE(F_STILL, NULL, 0);
    TEST_ASSERT_EQUAL_INT(0, s_sink_calls);
    TEST_ASSERT_EQUAL_UINT16(1, protocore_log_count());
}

// --- formatting edges -----------------------------------------------------

// A frame that does not fit is refused, here as everywhere: the line comes out empty rather than
// half-written. Logging takes the same contract as the wire frames - there is only one.
void test_line_that_does_not_fit_is_refused()
{
    char big[PROTOCORE_LOG_LINE_LEN * 3];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    PROTOCORE_LOGE(F_STR, ((const protocore_fval[]){PROTOCORE_VSTR(big)}), 1);
    TEST_ASSERT_EQUAL_INT(1, s_sink_calls);
    TEST_ASSERT_EQUAL_STRING("", s_last_line);
}

void test_null_spec_is_ignored()
{
    const protocore_field *spec = NULL;
    protocore_log_frame(PROTOCORE_LOG_LEVEL_ERROR, spec, NULL, 0);
    TEST_ASSERT_EQUAL_INT(0, s_sink_calls);
    TEST_ASSERT_EQUAL_UINT16(0, protocore_log_count());
}

void test_empty_message_is_still_a_line()
{
    PROTOCORE_LOGI(F_STR, ((const protocore_fval[]){PROTOCORE_VSTR("")}), 1);
    TEST_ASSERT_EQUAL_INT(1, s_sink_calls);
    TEST_ASSERT_EQUAL_STRING("", s_last_line);
    TEST_ASSERT_EQUAL_STRING("I ", protocore_log_at(0));
}

// ---------------------------------------------------------------------------
// mmgr/ring.h - the single-producer / single-consumer byte-ring
// shared by both transports. It is header-only inline math with no .cpp of its
// own, so it is exercised directly here rather than through a transport.
// ---------------------------------------------------------------------------

// Single-byte pops report emptiness rather than running past the head.
void test_ring_read_byte_and_available()
{
    uint8_t buf[8] = {'a', 'b', 'c', 0, 0, 0, 0, 0};
    _Atomic size_t head = 3;
    _Atomic size_t tail = 0;
    TEST_ASSERT_EQUAL_size_t(3, protocore_ring_available(&head, &tail, sizeof(buf)));

    uint8_t out = 0;
    TEST_ASSERT_TRUE(protocore_ring_read_byte(buf, sizeof(buf), &head, &tail, &out));
    TEST_ASSERT_EQUAL_HEX8('a', out);
    TEST_ASSERT_EQUAL_size_t(2, protocore_ring_available(&head, &tail, sizeof(buf)));
    TEST_ASSERT_TRUE(protocore_ring_read_byte(buf, sizeof(buf), &head, &tail, &out));
    TEST_ASSERT_EQUAL_HEX8('b', out);
    TEST_ASSERT_TRUE(protocore_ring_read_byte(buf, sizeof(buf), &head, &tail, &out));
    TEST_ASSERT_EQUAL_HEX8('c', out);
    // Tail has caught the head: empty.
    TEST_ASSERT_FALSE(protocore_ring_read_byte(buf, sizeof(buf), &head, &tail, &out));
    TEST_ASSERT_EQUAL_size_t(0, protocore_ring_available(&head, &tail, sizeof(buf)));
}

// A bulk read stops at whichever comes first: the caller's limit or the head.
void test_ring_read_bulk_stops_at_head_and_maxn()
{
    uint8_t buf[8] = {'0', '1', '2', '3', '4', 0, 0, 0};
    _Atomic size_t head = 5;
    _Atomic size_t tail = 0;
    uint8_t dst[8] = {0};

    TEST_ASSERT_EQUAL_size_t(2, protocore_ring_read(buf, sizeof(buf), &head, &tail, dst, 2)); // limited by maxn
    TEST_ASSERT_EQUAL_HEX8('0', dst[0]);
    TEST_ASSERT_EQUAL_HEX8('1', dst[1]);

    TEST_ASSERT_EQUAL_size_t(3, protocore_ring_read(buf, sizeof(buf), &head, &tail, dst, sizeof(dst))); // limited by head
    TEST_ASSERT_EQUAL_HEX8('2', dst[0]);
    TEST_ASSERT_EQUAL_HEX8('4', dst[2]);

    TEST_ASSERT_EQUAL_size_t(0, protocore_ring_read(buf, sizeof(buf), &head, &tail, dst, sizeof(dst))); // now empty
}

// Peek is wrap-aware and non-destructive; consume advances the tail modulo cap.
void test_ring_peek_and_consume_wrap()
{
    uint8_t buf[8] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};
    _Atomic size_t tail = 6;
    uint8_t dst[4] = {0};

    protocore_ring_peek(buf, sizeof(buf), &tail, 0, dst, 4); // 6, 7, then wraps to 0, 1
    TEST_ASSERT_EQUAL_HEX8('G', dst[0]);
    TEST_ASSERT_EQUAL_HEX8('H', dst[1]);
    TEST_ASSERT_EQUAL_HEX8('A', dst[2]);
    TEST_ASSERT_EQUAL_HEX8('B', dst[3]);
    TEST_ASSERT_EQUAL_size_t(6, (size_t)tail); // peeking consumed nothing

    protocore_ring_peek(buf, sizeof(buf), &tail, 2, dst, 2); // offset lands past the wrap
    TEST_ASSERT_EQUAL_HEX8('A', dst[0]);
    TEST_ASSERT_EQUAL_HEX8('B', dst[1]);

    protocore_ring_consume(&tail, sizeof(buf), 4);
    TEST_ASSERT_EQUAL_size_t(2, (size_t)tail); // (6 + 4) % 8
}

// Free space always reserves one slot so full is distinguishable from empty.
void test_ring_free_reserves_one_slot()
{
    _Atomic size_t head = 0;
    _Atomic size_t tail = 0;
    TEST_ASSERT_EQUAL_size_t(7, protocore_ring_free(&head, &tail, 8)); // empty -> cap - 1
    head = (size_t)5;
    TEST_ASSERT_EQUAL_size_t(2, protocore_ring_free(&head, &tail, 8));
    head = (size_t)7;
    TEST_ASSERT_EQUAL_size_t(0, protocore_ring_free(&head, &tail, 8)); // full
}

// The producer span copy clamps to the wrap point and resumes at the buffer start.
void test_ring_write_span_wraps()
{
    uint8_t buf[8];
    memset(buf, 0, sizeof(buf));

    // Whole copy fits before the wrap point: the chunk is clamped to len.
    size_t head = protocore_ring_write_span(buf, sizeof(buf), 0, (const uint8_t *)"abc", 3);
    TEST_ASSERT_EQUAL_size_t(3, head);
    TEST_ASSERT_EQUAL_MEMORY("abc", buf, 3);

    // Copy straddles the wrap: two spans, the first clamped to the buffer end.
    head = protocore_ring_write_span(buf, sizeof(buf), 6, (const uint8_t *)"WXYZ", 4);
    TEST_ASSERT_EQUAL_size_t(2, head); // (6 + 4) % 8
    TEST_ASSERT_EQUAL_HEX8('W', buf[6]);
    TEST_ASSERT_EQUAL_HEX8('X', buf[7]);
    TEST_ASSERT_EQUAL_HEX8('Y', buf[0]);
    TEST_ASSERT_EQUAL_HEX8('Z', buf[1]);

    // Nothing to copy: the head is returned untouched.
    TEST_ASSERT_EQUAL_size_t(4, protocore_ring_write_span(buf, sizeof(buf), 4, (const uint8_t *)"", 0));
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_debug_is_below_the_floor_and_emits_nothing);
    RUN_TEST(test_discarded_call_does_not_evaluate_its_arguments);
    RUN_TEST(test_info_and_above_emit);
    RUN_TEST(test_enabled_call_does_evaluate_its_arguments);
    RUN_TEST(test_emitted_line_also_reaches_the_logbuf_ring);
    RUN_TEST(test_levels_match_the_logbuf_letters);
    RUN_TEST(test_no_sink_is_not_a_crash);
    RUN_TEST(test_line_that_does_not_fit_is_refused);
    RUN_TEST(test_null_spec_is_ignored);
    RUN_TEST(test_empty_message_is_still_a_line);
    RUN_TEST(test_ring_read_byte_and_available);
    RUN_TEST(test_ring_read_bulk_stops_at_head_and_maxn);
    RUN_TEST(test_ring_peek_and_consume_wrap);
    RUN_TEST(test_ring_free_reserves_one_slot);
    RUN_TEST(test_ring_write_span_wraps);
    return UNITY_END();
}
