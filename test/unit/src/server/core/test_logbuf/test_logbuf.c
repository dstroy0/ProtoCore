// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the rotating log ring (server/core/logbuf.h).
//
// No standard governs an in-RAM log ring, so every expectation here is a PROPERTY of what the
// module publishes, stated as such: the ring keeps the newest PROTOCORE_LOG_LINES lines and prunes
// the oldest, a lookup indexes oldest-first, a line is its severity letter then a space then the
// message, and the trap fires for a level at or above its threshold.
//
// test_the_oldest_is_pruned_on_overflow is the load-bearing case: a fixed ring is only useful if
// what it drops is the oldest line and what it keeps is contiguous and in order. A wrong cursor
// there reads back a line that was already overwritten, so a /logs panel shows the failure that
// preceded the one being investigated.

#include "server/core/logbuf/logbuf.h"
#include <string.h>

#include <unity.h>

void setUp(void)
{
    Logbuf.reset(protocore_logbuf_span());
    LogbufV.trap.threshold = 0xFF; // the trap outlives a reset, so each case starts with it off
    LogbufV.trap.cb = NULL;
    Logbuf.set_trap(protocore_logbuf_span());
}
void tearDown(void)
{
}

static void put(uint8_t level, const char *msg)
{
    LogbufV.line.level = level;
    LogbufV.line.msg = msg;
    Logbuf.put(protocore_logbuf_span());
}

static uint16_t held(void)
{
    LogbufV.held(protocore_logbuf_span());
    return LogbufV.count;
}

static const char *at(uint16_t i)
{
    LogbufV.read.i = i;
    Logbuf.at(protocore_logbuf_span());
    return LogbufV.text;
}

static char g_out[4096];

static int dump_into(char *out, size_t cap)
{
    LogbufV.read.out = out;
    LogbufV.read.cap = cap;
    Logbuf.dump(protocore_logbuf_span());
    return LogbufV.n;
}

static const char *dump(void)
{
    (void)dump_into(g_out, sizeof(g_out));
    return g_out;
}

static int g_trap_fires;
static uint8_t g_trap_level;
static char g_trap_line[PROTOCORE_LOG_LINE_LEN];

static void on_trap(uint8_t level, const char *line)
{
    g_trap_fires++;
    g_trap_level = level;
    g_trap_line[0] = '\0';
    if (line)
    {
        size_t n = strlen(line);
        if (n >= sizeof(g_trap_line))
        {
            n = sizeof(g_trap_line) - 1;
        }
        memcpy(g_trap_line, line, n);
        g_trap_line[n] = '\0';
    }
}

static void arm_trap(uint8_t threshold)
{
    g_trap_fires = 0;
    g_trap_level = 0xFF;
    g_trap_line[0] = '\0';
    LogbufV.trap.threshold = threshold;
    LogbufV.trap.cb = on_trap;
    Logbuf.set_trap(protocore_logbuf_span());
}

// A stored line is its severity letter, one space, then the message, so a reader can sort or filter
// on the first octet without parsing the rest.
void test_the_severity_letter_leads_the_line(void)
{
    put(PROTOCORE_LOG_DEBUG, "d");
    put(PROTOCORE_LOG_INFO, "i");
    put(PROTOCORE_LOG_WARN, "w");
    put(PROTOCORE_LOG_ERROR, "e");
    TEST_ASSERT_EQUAL_STRING("D d", at(0));
    TEST_ASSERT_EQUAL_STRING("I i", at(1));
    TEST_ASSERT_EQUAL_STRING("W w", at(2));
    TEST_ASSERT_EQUAL_STRING("E e", at(3));
}

// The levels are compared with >=, so they have to be ordered low to high with no ties.
void test_the_levels_are_ordered(void)
{
    TEST_ASSERT_TRUE(PROTOCORE_LOG_DEBUG < PROTOCORE_LOG_INFO);
    TEST_ASSERT_TRUE(PROTOCORE_LOG_INFO < PROTOCORE_LOG_WARN);
    TEST_ASSERT_TRUE(PROTOCORE_LOG_WARN < PROTOCORE_LOG_ERROR);
}

// Index 0 is the oldest held line and count-1 the newest, whichever slots they occupy.
void test_lines_come_back_oldest_first(void)
{
    TEST_ASSERT_EQUAL_UINT16(0, held());
    put(PROTOCORE_LOG_INFO, "first");
    put(PROTOCORE_LOG_INFO, "second");
    put(PROTOCORE_LOG_INFO, "third");
    TEST_ASSERT_EQUAL_UINT16(3, held());
    TEST_ASSERT_EQUAL_STRING("I first", at(0));
    TEST_ASSERT_EQUAL_STRING("I second", at(1));
    TEST_ASSERT_EQUAL_STRING("I third", at(2));
}

// Past the capacity the ring holds exactly PROTOCORE_LOG_LINES lines, and the ones it holds are the
// newest, still in the order they were logged.
void test_the_oldest_is_pruned_on_overflow(void)
{
    // one letter per line, so each is "I <c>" and its own identity
    for (unsigned i = 0; i < PROTOCORE_LOG_LINES + 3u; i++)
    {
        char msg[2] = {(char)('a' + (i % 26u)), '\0'};
        put(PROTOCORE_LOG_INFO, msg);
    }
    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_LOG_LINES, held());
    for (unsigned i = 0; i < PROTOCORE_LOG_LINES; i++)
    {
        // the first three (a, b, c) were pruned, so the oldest held is the fourth logged
        char want[4] = {'I', ' ', (char)('a' + ((i + 3u) % 26u)), '\0'};
        TEST_ASSERT_EQUAL_STRING(want, at((uint16_t)i));
    }
}

// A lookup outside the held range names no line rather than a stale slot.
void test_a_lookup_past_the_end_reports_nothing(void)
{
    TEST_ASSERT_NULL(at(0));
    put(PROTOCORE_LOG_INFO, "only");
    TEST_ASSERT_NOT_NULL(at(0));
    TEST_ASSERT_NULL(at(1));
    TEST_ASSERT_NULL(at(PROTOCORE_LOG_LINES));
}

// A line with no message still carries its severity, so the entry is not lost.
void test_a_null_message_renders_the_letter_alone(void)
{
    put(PROTOCORE_LOG_ERROR, NULL);
    TEST_ASSERT_EQUAL_UINT16(1, held());
    TEST_ASSERT_EQUAL_STRING("E ", at(0));
}

// A message wider than the line is not truncated into something that reads as a complete entry: the
// slot is left empty, and it still counts as a line.
void test_a_line_that_does_not_fit_is_empty(void)
{
    char big[PROTOCORE_LOG_LINE_LEN + 8];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    put(PROTOCORE_LOG_WARN, big);
    TEST_ASSERT_EQUAL_UINT16(1, held());
    TEST_ASSERT_EQUAL_STRING("", at(0));
}

// A dump is the held lines oldest-first with one newline between them, and no trailing newline.
void test_dump_joins_the_held_lines_with_a_newline(void)
{
    put(PROTOCORE_LOG_ERROR, "a");
    put(PROTOCORE_LOG_ERROR, "b");
    put(PROTOCORE_LOG_ERROR, "c");
    TEST_ASSERT_EQUAL_STRING("E a\nE b\nE c", dump());
    TEST_ASSERT_EQUAL_INT(11, LogbufV.n);
    TEST_ASSERT_EQUAL_INT((int)strlen(g_out), LogbufV.n);
}

// The whole dump plus its terminator is what fits; one octet less writes nothing, because a partial
// dump would read as a complete log that is missing its most recent lines.
void test_dump_fails_closed_when_a_line_would_not_fit(void)
{
    put(PROTOCORE_LOG_ERROR, "a");
    put(PROTOCORE_LOG_ERROR, "b");
    put(PROTOCORE_LOG_ERROR, "c");
    char exact[12]; // "E a\nE b\nE c" is 11 characters plus the terminator
    TEST_ASSERT_EQUAL_INT(11, dump_into(exact, sizeof(exact)));
    TEST_ASSERT_EQUAL_STRING("E a\nE b\nE c", exact);

    char one_short[11];
    TEST_ASSERT_EQUAL_INT(0, dump_into(one_short, sizeof(one_short)));
    TEST_ASSERT_EQUAL_STRING("", one_short);
}

void test_dump_of_an_empty_ring_is_an_empty_string(void)
{
    TEST_ASSERT_EQUAL_INT(0, dump_into(g_out, sizeof(g_out)));
    TEST_ASSERT_EQUAL_STRING("", g_out);
}

void test_dump_refuses_null_and_zero_capacity(void)
{
    put(PROTOCORE_LOG_INFO, "a");
    char buf[8];
    buf[0] = 'x';
    TEST_ASSERT_EQUAL_INT(0, dump_into(NULL, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(0, dump_into(buf, 0));
    TEST_ASSERT_EQUAL_CHAR('x', buf[0]);
}

// The trap is the forwarding hook, so it fires for a line at its threshold and for anything above
// it, and for nothing below.
void test_the_trap_fires_at_or_above_its_threshold(void)
{
    arm_trap(PROTOCORE_LOG_WARN);
    put(PROTOCORE_LOG_DEBUG, "quiet");
    put(PROTOCORE_LOG_INFO, "quiet");
    TEST_ASSERT_EQUAL_INT(0, g_trap_fires);

    put(PROTOCORE_LOG_WARN, "at the threshold");
    TEST_ASSERT_EQUAL_INT(1, g_trap_fires);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_LOG_WARN, g_trap_level);
    TEST_ASSERT_EQUAL_STRING("W at the threshold", g_trap_line);

    put(PROTOCORE_LOG_ERROR, "above it");
    TEST_ASSERT_EQUAL_INT(2, g_trap_fires);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_LOG_ERROR, g_trap_level);
    TEST_ASSERT_EQUAL_STRING("E above it", g_trap_line);
}

// The trap sees the stored line, so what is forwarded is what /logs would show.
void test_the_trap_sees_the_stored_line(void)
{
    arm_trap(PROTOCORE_LOG_DEBUG);
    put(PROTOCORE_LOG_ERROR, "disk gone");
    TEST_ASSERT_EQUAL_INT(1, g_trap_fires);
    TEST_ASSERT_EQUAL_STRING(at(0), g_trap_line);
}

// A threshold of 0xFF is above every level, so it disables the trap without removing it, and a null
// callback leaves it off whatever the threshold.
void test_the_trap_can_be_turned_off(void)
{
    arm_trap(0xFF);
    put(PROTOCORE_LOG_ERROR, "loud");
    TEST_ASSERT_EQUAL_INT(0, g_trap_fires);

    arm_trap(PROTOCORE_LOG_DEBUG);
    LogbufV.trap.threshold = PROTOCORE_LOG_DEBUG;
    LogbufV.trap.cb = NULL;
    Logbuf.set_trap(protocore_logbuf_span());
    put(PROTOCORE_LOG_ERROR, "loud");
    TEST_ASSERT_EQUAL_INT(0, g_trap_fires);
}

// A reset empties the ring; the next line is the oldest again.
void test_reset_empties_the_ring(void)
{
    put(PROTOCORE_LOG_INFO, "before");
    Logbuf.reset(protocore_logbuf_span());
    TEST_ASSERT_EQUAL_UINT16(0, held());
    TEST_ASSERT_NULL(at(0));
    put(PROTOCORE_LOG_INFO, "after");
    TEST_ASSERT_EQUAL_UINT16(1, held());
    TEST_ASSERT_EQUAL_STRING("I after", at(0));
}
