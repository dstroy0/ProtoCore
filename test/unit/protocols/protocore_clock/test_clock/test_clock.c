// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "server/clock/clock.h"
#include <Arduino.h>
#include <unity.h>

static uint32_t g_fake = 0;
static uint32_t fake_clock()
{
    return g_fake;
}
static uint32_t g_fake_us = 0;
static uint32_t fake_us()
{
    return g_fake_us;
}

void setUp()
{
    protocore_set_clock(NULL, 0);
    protocore_set_micros_clock(NULL, 0);
}
void tearDown()
{
    protocore_set_clock(NULL, 0);
    protocore_set_micros_clock(NULL, 0);
}

void test_default_is_platform_millis()
{
    set_millis(5000);
    TEST_ASSERT_EQUAL_UINT32(5000, protocore_millis());
    set_millis(12345);
    TEST_ASSERT_EQUAL_UINT32(12345, protocore_millis());
}

void test_custom_clock_divides_to_1000hz()
{
    protocore_set_clock(fake_clock, 8000);
    g_fake = 8000;
    TEST_ASSERT_EQUAL_UINT32(1000, protocore_millis());
    g_fake = 16000;
    TEST_ASSERT_EQUAL_UINT32(2000, protocore_millis());

    g_fake = 1000000;
    protocore_set_clock(fake_clock, 1000000);
    TEST_ASSERT_EQUAL_UINT32(1000, protocore_millis());
}

void test_sub_khz_source_not_divided()
{
    protocore_set_clock(fake_clock, 500);
    g_fake = 1234;
    TEST_ASSERT_EQUAL_UINT32(1234, protocore_millis());
}

void test_revert_to_default()
{
    protocore_set_clock(fake_clock, 1000);
    g_fake = 42;
    TEST_ASSERT_EQUAL_UINT32(42, protocore_millis());
    protocore_set_clock(NULL, 0);
    set_millis(777);
    TEST_ASSERT_EQUAL_UINT32(777, protocore_millis());
}

void test_micros_custom_divides_to_1mhz()
{
    protocore_set_micros_clock(fake_us, 80000000u);
    g_fake_us = 80000000u;
    TEST_ASSERT_EQUAL_UINT32(1000000u, protocore_micros());
    g_fake_us = 160u;
    TEST_ASSERT_EQUAL_UINT32(2u, protocore_micros());
}

void test_latency_stat_records_and_budgets()
{
    protocore_set_micros_clock(fake_us, 1000000u);
    protocore_latency_stat s;
    protocore_lat_reset(&s);
    TEST_ASSERT_EQUAL_UINT32(0, protocore_lat_avg_us(&s));

    g_fake_us = 1000;
    uint32_t t = protocore_lat_begin();
    g_fake_us = 1010;
    protocore_lat_end(&s, t, 50);

    g_fake_us = 2000;
    t = protocore_lat_begin();
    g_fake_us = 2100;
    protocore_lat_end(&s, t, 50);

    g_fake_us = 3000;
    t = protocore_lat_begin();
    g_fake_us = 3020;
    protocore_lat_end(&s, t, 50);

    TEST_ASSERT_EQUAL_UINT32(3, s.count);
    TEST_ASSERT_EQUAL_UINT32(10, s.min_us);
    TEST_ASSERT_EQUAL_UINT32(100, s.max_us);
    TEST_ASSERT_EQUAL_UINT32((10 + 100 + 20) / 3, protocore_lat_avg_us(&s));
    TEST_ASSERT_EQUAL_UINT32(1, s.over_budget);
}

void test_latency_budget_zero_disables()
{
    protocore_set_micros_clock(fake_us, 1000000u);
    protocore_latency_stat s;
    protocore_lat_reset(&s);
    g_fake_us = 0;
    uint32_t t = protocore_lat_begin();
    g_fake_us = 99999;
    protocore_lat_end(&s, t, 0);
    TEST_ASSERT_EQUAL_UINT32(1, s.count);
    TEST_ASSERT_EQUAL_UINT32(0, s.over_budget);
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_default_is_platform_millis);
    RUN_TEST(test_custom_clock_divides_to_1000hz);
    RUN_TEST(test_sub_khz_source_not_divided);
    RUN_TEST(test_revert_to_default);
    RUN_TEST(test_micros_custom_divides_to_1mhz);
    RUN_TEST(test_latency_stat_records_and_budgets);
    RUN_TEST(test_latency_budget_zero_disables);
    return UNITY_END();
}
