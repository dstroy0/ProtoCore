// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the pluggable monotonic clock (services/pc_clock): the platform
// default, a custom clock divided down to the internal 1000 Hz, and revert.

#include "server/clock/clock.h"
#include <Arduino.h> // set_millis (host mock)
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
    pc_set_clock(nullptr, 0); // start each test on the platform default
    pc_set_micros_clock(nullptr, 0);
}
void tearDown()
{
    pc_set_clock(nullptr, 0);
    pc_set_micros_clock(nullptr, 0);
}

void test_default_is_platform_millis()
{
    set_millis(5000);
    TEST_ASSERT_EQUAL_UINT32(5000, pc_millis());
    set_millis(12345);
    TEST_ASSERT_EQUAL_UINT32(12345, pc_millis());
}

void test_custom_clock_divides_to_1000hz()
{
    pc_set_clock(fake_clock, 8000); // 8 kHz source -> divide by 8
    g_fake = 8000;
    TEST_ASSERT_EQUAL_UINT32(1000, pc_millis());
    g_fake = 16000;
    TEST_ASSERT_EQUAL_UINT32(2000, pc_millis());

    g_fake = 1000000;
    pc_set_clock(fake_clock, 1000000); // 1 MHz source -> divide by 1000
    TEST_ASSERT_EQUAL_UINT32(1000, pc_millis());
}

void test_sub_khz_source_not_divided()
{
    pc_set_clock(fake_clock, 500); // < 1000: cannot divide up, used as-is
    g_fake = 1234;
    TEST_ASSERT_EQUAL_UINT32(1234, pc_millis());
}

void test_revert_to_default()
{
    pc_set_clock(fake_clock, 1000);
    g_fake = 42;
    TEST_ASSERT_EQUAL_UINT32(42, pc_millis());
    pc_set_clock(nullptr, 0);
    set_millis(777);
    TEST_ASSERT_EQUAL_UINT32(777, pc_millis());
}

// --- v5 clock-awareness: microsecond base + latency budgeting -----------------

void test_micros_custom_divides_to_1mhz()
{
    pc_set_micros_clock(fake_us, 80000000u); // 80 MHz source -> divide by 80
    g_fake_us = 80000000u;
    TEST_ASSERT_EQUAL_UINT32(1000000u, pc_micros());
    g_fake_us = 160u;
    TEST_ASSERT_EQUAL_UINT32(2u, pc_micros()); // 160 / 80
}

void test_latency_stat_records_and_budgets()
{
    pc_set_micros_clock(fake_us, 1000000u); // 1 MHz -> microseconds == counter
    pc_latency_stat s;
    pc_lat_reset(&s);
    TEST_ASSERT_EQUAL_UINT32(0, pc_lat_avg_us(&s)); // no samples yet -> 0 (count==0 branch)

    g_fake_us = 1000;
    uint32_t t = pc_lat_begin();
    g_fake_us = 1010; // 10 us span (budget 50: ok)
    pc_lat_end(&s, t, 50);

    g_fake_us = 2000;
    t = pc_lat_begin();
    g_fake_us = 2100; // 100 us span (budget 50: OVER)
    pc_lat_end(&s, t, 50);

    g_fake_us = 3000;
    t = pc_lat_begin();
    g_fake_us = 3020; // 20 us span
    pc_lat_end(&s, t, 50);

    TEST_ASSERT_EQUAL_UINT32(3, s.count);
    TEST_ASSERT_EQUAL_UINT32(10, s.min_us);
    TEST_ASSERT_EQUAL_UINT32(100, s.max_us);
    TEST_ASSERT_EQUAL_UINT32((10 + 100 + 20) / 3, pc_lat_avg_us(&s));
    TEST_ASSERT_EQUAL_UINT32(1, s.over_budget); // only the 100 us sample
}

void test_latency_budget_zero_disables()
{
    pc_set_micros_clock(fake_us, 1000000u);
    pc_latency_stat s;
    pc_lat_reset(&s);
    g_fake_us = 0;
    uint32_t t = pc_lat_begin();
    g_fake_us = 99999; // huge span, but budget 0 = disabled
    pc_lat_end(&s, t, 0);
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
