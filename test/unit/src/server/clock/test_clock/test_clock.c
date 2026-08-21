// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
#include "server/clock/clock.h"
#include <Arduino.h>
#include <unity.h>

// The library's microsecond clock, read through the namespace.
static uint32_t clock_us(void)
{
    Clock.micros(Clock.internal);
    return Clock.us;
}

// The library's millisecond clock, read through the namespace.
static uint32_t clock_ms(void)
{
    Clock.millis(Clock.internal);
    return Clock.ms;
}

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
    Clock.src.fn = NULL;
    Clock.src.ticks_per_second = 0;
    Clock.set_ms(Clock.internal);
    Clock.src.fn = NULL;
    Clock.src.ticks_per_second = 0;
    Clock.set_us(Clock.internal);
}
void tearDown()
{
    Clock.src.fn = NULL;
    Clock.src.ticks_per_second = 0;
    Clock.set_ms(Clock.internal);
    Clock.src.fn = NULL;
    Clock.src.ticks_per_second = 0;
    Clock.set_us(Clock.internal);
}

void test_default_is_platform_millis()
{
    set_millis(5000);
    TEST_ASSERT_EQUAL_UINT32(5000, clock_ms());
    set_millis(12345);
    TEST_ASSERT_EQUAL_UINT32(12345, clock_ms());
}

void test_custom_clock_divides_to_1000hz()
{
    Clock.src.fn = fake_clock;
    Clock.src.ticks_per_second = 8000;
    Clock.set_ms(Clock.internal);
    g_fake = 8000;
    TEST_ASSERT_EQUAL_UINT32(1000, clock_ms());
    g_fake = 16000;
    TEST_ASSERT_EQUAL_UINT32(2000, clock_ms());

    g_fake = 1000000;
    Clock.src.fn = fake_clock;
    Clock.src.ticks_per_second = 1000000;
    Clock.set_ms(Clock.internal);
    TEST_ASSERT_EQUAL_UINT32(1000, clock_ms());
}

void test_sub_khz_source_not_divided()
{
    Clock.src.fn = fake_clock;
    Clock.src.ticks_per_second = 500;
    Clock.set_ms(Clock.internal);
    g_fake = 1234;
    TEST_ASSERT_EQUAL_UINT32(1234, clock_ms());
}

void test_revert_to_default()
{
    Clock.src.fn = fake_clock;
    Clock.src.ticks_per_second = 1000;
    Clock.set_ms(Clock.internal);
    g_fake = 42;
    TEST_ASSERT_EQUAL_UINT32(42, clock_ms());
    Clock.src.fn = NULL;
    Clock.src.ticks_per_second = 0;
    Clock.set_ms(Clock.internal);
    set_millis(777);
    TEST_ASSERT_EQUAL_UINT32(777, clock_ms());
}

void test_micros_custom_divides_to_1mhz()
{
    Clock.src.fn = fake_us;
    Clock.src.ticks_per_second = 80000000u;
    Clock.set_us(Clock.internal);
    g_fake_us = 80000000u;
    TEST_ASSERT_EQUAL_UINT32(1000000u, clock_us());
    g_fake_us = 160u;
    TEST_ASSERT_EQUAL_UINT32(2u, clock_us());
}

void test_latency_stat_records_and_budgets()
{
    Clock.src.fn = fake_us;
    Clock.src.ticks_per_second = 1000000u;
    Clock.set_us(Clock.internal);
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
    Clock.src.fn = fake_us;
    Clock.src.ticks_per_second = 1000000u;
    Clock.set_us(Clock.internal);
    protocore_latency_stat s;
    protocore_lat_reset(&s);
    g_fake_us = 0;
    uint32_t t = protocore_lat_begin();
    g_fake_us = 99999;
    protocore_lat_end(&s, t, 0);
    TEST_ASSERT_EQUAL_UINT32(1, s.count);
    TEST_ASSERT_EQUAL_UINT32(0, s.over_budget);
}
