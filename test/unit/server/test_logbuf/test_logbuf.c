// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the rotating log ring (server/logbuf): append order, the
// level prefix, oldest-pruned rotation, dump formatting, and the severity trap.

#include "server/system/logbuf.h"
#include <stdio.h>

#include <unity.h>

void setUp()
{
    protocore_logbuf_reset();
    protocore_log_set_trap(0xFF, NULL); // disable trap between tests
}
void tearDown()
{
}

void test_append_and_order()
{
    protocore_log(PROTOCORE_LOG_INFO, "first");
    protocore_log(PROTOCORE_LOG_WARN, "second");
    TEST_ASSERT_EQUAL_UINT16(2, protocore_log_count());
    TEST_ASSERT_EQUAL_STRING("I first", protocore_log_at(0));
    TEST_ASSERT_EQUAL_STRING("W second", protocore_log_at(1));
    TEST_ASSERT_NULL(protocore_log_at(2));
}

void test_dump()
{
    protocore_log(PROTOCORE_LOG_ERROR, "boom");
    protocore_log(PROTOCORE_LOG_DEBUG, "trace");
    char buf[128];
    int n = protocore_log_dump(buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_STRING("E boom\nD trace", buf);
}

void test_rotation_drops_oldest()
{
    char msg[16];
    for (int i = 0; i < PROTOCORE_LOG_LINES + 3; i++) // overflow by 3
    {
        snprintf(msg, sizeof(msg), "n%d", i);
        protocore_log(PROTOCORE_LOG_INFO, msg);
    }
    TEST_ASSERT_EQUAL_UINT16(PROTOCORE_LOG_LINES, protocore_log_count());
    // The 3 oldest (n0,n1,n2) were pruned; oldest now is n3, newest is the last.
    TEST_ASSERT_EQUAL_STRING("I n3", protocore_log_at(0));
    char expect_last[16];
    snprintf(expect_last, sizeof(expect_last), "I n%d", PROTOCORE_LOG_LINES + 2);
    TEST_ASSERT_EQUAL_STRING(expect_last, protocore_log_at(PROTOCORE_LOG_LINES - 1));
}

static int g_traps = 0;
static uint8_t g_last_level = 0;
static void trap(uint8_t level, const char *)
{
    g_traps++;
    g_last_level = level;
}

void test_trap_threshold()
{
    g_traps = 0;
    protocore_log_set_trap(PROTOCORE_LOG_WARN, trap);
    protocore_log(PROTOCORE_LOG_INFO, "ignored"); // below threshold
    protocore_log(PROTOCORE_LOG_DEBUG, "ignored");
    TEST_ASSERT_EQUAL_INT(0, g_traps);
    protocore_log(PROTOCORE_LOG_WARN, "warned"); // at threshold
    protocore_log(PROTOCORE_LOG_ERROR, "errored");
    TEST_ASSERT_EQUAL_INT(2, g_traps);
    TEST_ASSERT_EQUAL_UINT8(PROTOCORE_LOG_ERROR, g_last_level);
}

void test_log_null_message()
{
    // A null message must not crash and must fall back to an empty string body.
    protocore_log(PROTOCORE_LOG_INFO, NULL);
    TEST_ASSERT_EQUAL_UINT16(1, protocore_log_count());
    TEST_ASSERT_EQUAL_STRING("I ", protocore_log_at(0));
}

void test_dump_guards()
{
    char out[64];
    TEST_ASSERT_EQUAL_INT(0, protocore_log_dump(NULL, sizeof(out))); // null out
    TEST_ASSERT_EQUAL_INT(0, protocore_log_dump(out, 0));            // zero cap
    // A dump buffer too small for the logged line fails closed.
    protocore_logbuf_reset();
    protocore_log(0, "a fairly long log line that will not fit a tiny dump buffer");
    TEST_ASSERT_EQUAL_INT(0, protocore_log_dump(out, 8));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_append_and_order);
    RUN_TEST(test_dump);
    RUN_TEST(test_rotation_drops_oldest);
    RUN_TEST(test_trap_threshold);
    RUN_TEST(test_log_null_message);
    RUN_TEST(test_dump_guards);
    return UNITY_END();
}
