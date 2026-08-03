// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for services/failsafe: the software watchdog / deadlock detector. Uses the explicit
// *_at(now) API with a synthetic clock, so no time mock is needed.

#include "server/failsafe.h"
#include <string.h>
#include <unity.h>

static int s_fired_id = -1;
static const char *s_fired_name = nullptr;
static int s_fire_count = 0;

static void on_breach(int id, const char *name, void *arg)
{
    (void)arg;
    s_fired_id = id;
    s_fired_name = name;
    s_fire_count++;
}

void setUp(void)
{
    pc_failsafe_reset();
    pc_failsafe_on_breach(on_breach, nullptr);
    s_fired_id = -1;
    s_fired_name = nullptr;
    s_fire_count = 0;
}
void tearDown(void)
{
}

// The wrap-safe overdue predicate.
void test_overdue_predicate(void)
{
    TEST_ASSERT_FALSE(pc_lifeline_overdue(1000, 900, 200)); // 100 <= 200
    TEST_ASSERT_FALSE(pc_lifeline_overdue(1000, 800, 200)); // 200, not > 200
    TEST_ASSERT_TRUE(pc_lifeline_overdue(1000, 700, 200));  // 300 > 200
    // Across a millis() rollover: last_feed just before wrap, now just after.
    TEST_ASSERT_FALSE(pc_lifeline_overdue(50, 0xFFFFFF00u, 1000));         // gap 306 <= 1000
    TEST_ASSERT_TRUE(pc_lifeline_overdue(0x00000400u, 0xFFFFFF00u, 1000)); // gap 1284 > 1000
}

void test_register_and_not_overdue_when_fresh(void)
{
    int id = pc_failsafe_register_at("worker", 500, 1000);
    TEST_ASSERT_EQUAL_INT(0, id);
    // Just under the deadline: not overdue, no fire.
    TEST_ASSERT_EQUAL_UINT32(0, pc_failsafe_check_at(1400));
    TEST_ASSERT_EQUAL_INT(0, s_fire_count);
}

void test_breach_fires_once_then_clears_on_feed(void)
{
    int a = pc_failsafe_register_at("a", 500, 1000);
    // b has a huge deadline so it never trips during this test - a stays the only overdue lifeline.
    int b = pc_failsafe_register_at("b", 1000000, 1000);
    TEST_ASSERT_EQUAL_INT(0, a);
    TEST_ASSERT_EQUAL_INT(1, b);

    // Only a is overdue at t=1600 (age 600 > 500).
    uint32_t mask = pc_failsafe_check_at(1600);
    TEST_ASSERT_EQUAL_UINT32(1u << 0, mask); // only a
    TEST_ASSERT_EQUAL_INT(1, s_fire_count);
    TEST_ASSERT_EQUAL_INT(a, s_fired_id);
    TEST_ASSERT_EQUAL_STRING("a", s_fired_name);

    // Still overdue at 1700: stays in the mask but does NOT re-fire.
    mask = pc_failsafe_check_at(1700);
    TEST_ASSERT_EQUAL_UINT32(1u << 0, mask);
    TEST_ASSERT_EQUAL_INT(1, s_fire_count); // no re-fire

    // Feeding a clears the breach; a later miss fires again (a fresh episode).
    pc_failsafe_feed_at(a, 1700);
    TEST_ASSERT_EQUAL_UINT32(0, pc_failsafe_check_at(1800));       // fed, healthy
    TEST_ASSERT_EQUAL_UINT32(1u << 0, pc_failsafe_check_at(2300)); // overdue again
    TEST_ASSERT_EQUAL_INT(2, s_fire_count);
}

void test_registry_full(void)
{
    for (int i = 0; i < PC_FAILSAFE_MAX_LIFELINES; i++)
    {
        TEST_ASSERT_EQUAL_INT(i, pc_failsafe_register_at("x", 100, 0));
    }
    TEST_ASSERT_EQUAL_INT(-1, pc_failsafe_register_at("overflow", 100, 0));
}

void test_feed_bad_id(void)
{
    TEST_ASSERT_FALSE(pc_failsafe_feed_at(-1, 0));
    TEST_ASSERT_FALSE(pc_failsafe_feed_at(3, 0));                         // not armed
    TEST_ASSERT_FALSE(pc_failsafe_feed_at(PC_FAILSAFE_MAX_LIFELINES, 0)); // id >= registry size
}

// A breach with no callback installed still marks the lifeline breached and reports it in the mask;
// it just has nothing to call.
void test_breach_without_callback(void)
{
    pc_failsafe_on_breach(nullptr, nullptr); // overrides the setUp() default
    int a = pc_failsafe_register_at("solo", 500, 1000);
    TEST_ASSERT_EQUAL_INT(0, a);
    uint32_t mask = pc_failsafe_check_at(1600); // overdue (age 600 > 500)
    TEST_ASSERT_EQUAL_UINT32(1u << 0, mask);
    TEST_ASSERT_EQUAL_INT(-1, s_fired_id);  // unchanged: no callback ran
    TEST_ASSERT_EQUAL_INT(0, s_fire_count); // unchanged: no callback ran
}

void test_json(void)
{
    int a = pc_failsafe_register_at("motor", 500, 1000);
    (void)a;
    char buf[128];
    int n = pc_failsafe_json_at(1200, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"name\":\"motor\""));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"overdue\":false"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"age_ms\":200"));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"deadline_ms\":500"));
    // After the deadline, overdue flips true.
    pc_failsafe_json_at(2000, buf, sizeof(buf));
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"overdue\":true"));
}

// Guard clauses: a null output buffer or a zero capacity both bail out immediately.
void test_json_null_out_and_zero_cap(void)
{
    char buf[8];
    TEST_ASSERT_EQUAL_INT(0, pc_failsafe_json_at(0, nullptr, sizeof(buf)));
    TEST_ASSERT_EQUAL_INT(0, pc_failsafe_json_at(0, buf, 0));
}

// A lifeline registered with a null name serializes as an empty "name" field instead of dereferencing
// the null pointer.
void test_json_unnamed_lifeline(void)
{
    pc_failsafe_register_at(nullptr, 500, 1000);
    char buf[128];
    int n = pc_failsafe_json_at(1200, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_NOT_NULL(strstr(buf, "\"name\":\"\","));
}

// A capacity too small to hold even the opening literal exercises the truncation path in both fs_put
// (the plain-text copier) and fs_put_u32 (the decimal-digit copier): both must stop writing exactly at
// cap - 1 and leave the buffer NUL-terminated, never overrunning it.
void test_json_truncated_buffer(void)
{
    pc_failsafe_register_at("motor", 500, 1000);
    char buf[3];
    int n = pc_failsafe_json_at(1200, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_CHAR('{', buf[0]);
    TEST_ASSERT_EQUAL_CHAR('"', buf[1]);
    TEST_ASSERT_EQUAL_CHAR('\0', buf[2]);
}

void test_millis_wrappers_and_json()
{
    pc_failsafe_reset();
    int id = pc_failsafe_register("alpha", 1000); // clock-reading wrapper
    TEST_ASSERT_TRUE(id >= 0);
    TEST_ASSERT_TRUE(pc_failsafe_feed(id)); // clock-reading wrapper
    pc_failsafe_check();                    // clock-reading wrapper
    // A second lifeline drives the JSON comma separator.
    pc_failsafe_register("beta", 1000);
    char buf[256];
    TEST_ASSERT_TRUE(pc_failsafe_json_at(0, buf, sizeof(buf)) > 0);
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_overdue_predicate);
    RUN_TEST(test_register_and_not_overdue_when_fresh);
    RUN_TEST(test_breach_fires_once_then_clears_on_feed);
    RUN_TEST(test_registry_full);
    RUN_TEST(test_feed_bad_id);
    RUN_TEST(test_breach_without_callback);
    RUN_TEST(test_json);
    RUN_TEST(test_json_null_out_and_zero_cap);
    RUN_TEST(test_json_unnamed_lifeline);
    RUN_TEST(test_json_truncated_buffer);
    RUN_TEST(test_millis_wrappers_and_json);
    return UNITY_END();
}
