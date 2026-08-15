// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the sample aggregators (services/iot/telemetry/telemetry.h).
//
// No specification governs this module: telemetry.h says so, and only the totalizer's meaning is
// borrowed, from SenML's Sum (RFC 8428 sec 4.2, "the integrated sum of the values over time").
// Every expected value below is therefore PROPERTIES plus arithmetic derived here from the
// definitions of mean, population variance, first difference and the trapezoidal rule, with the
// derivation written above the case. The load-bearing case is test_window_mean_variance_stddev:
// its eight samples make the mean, the variance and the standard deviation come out whole, so a
// sample-variance denominator or a dropped term changes the digits rather than the last bit.

#include "services/iot/telemetry/telemetry.h"

#include <unity.h>

void setUp(void)
{
}
void tearDown(void)
{
}

static TelemetryWindow g_w;
static float g_buf[8];
static TelemetryRate g_r;
static TelemetryTotalizer g_t;

static void window_init(uint16_t cap)
{
    Telemetry.window.w = &g_w;
    Telemetry.window.buf = g_buf;
    Telemetry.window.cap = cap;
    Telemetry.window_init(Telemetry.internal);
}

static void push(float sample)
{
    Telemetry.window.w = &g_w;
    Telemetry.window.sample = sample;
    Telemetry.window_push(Telemetry.internal);
}

static float rate(float value, uint32_t now_ms)
{
    Telemetry.rate.r = &g_r;
    Telemetry.rate.value = value;
    Telemetry.rate.now_ms = now_ms;
    Telemetry.rate_update(Telemetry.internal);
    return Telemetry.f32;
}

static double integrate(float r, uint32_t now_ms)
{
    Telemetry.totalizer.t = &g_t;
    Telemetry.totalizer.rate = r;
    Telemetry.totalizer.now_ms = now_ms;
    Telemetry.totalizer_add(Telemetry.internal);
    return Telemetry.f64;
}

// The samples 2 4 4 4 5 5 7 9.
//   sum      = 2+4+4+4+5+5+7+9                     = 40
//   mean     = 40 / 8                              =  5
//   sum_sq   = 4+16+16+16+25+25+49+81              = 232
//   variance = sum_sq/n - mean^2 = 232/8 - 25 = 29 - 25 = 4   (population, divisor n)
//   stddev   = sqrt(4)                             =  2
// A sample variance, divisor n-1, would be 32/7 = 4.571..., which these assertions reject.
void test_window_mean_variance_stddev(void)
{
    static const float S[8] = {2.0f, 4.0f, 4.0f, 4.0f, 5.0f, 5.0f, 7.0f, 9.0f};
    window_init(8);
    for (int i = 0; i < 8; i++)
    {
        push(S[i]);
    }

    Telemetry.window.w = &g_w;
    Telemetry.window_count(Telemetry.internal);
    TEST_ASSERT_TRUE(Telemetry.ok);
    TEST_ASSERT_EQUAL_UINT16(8u, Telemetry.u16);

    Telemetry.window_mean(Telemetry.internal);
    TEST_ASSERT_TRUE(Telemetry.ok);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, Telemetry.f32);

    Telemetry.window_variance(Telemetry.internal);
    TEST_ASSERT_EQUAL_FLOAT(4.0f, Telemetry.f32);

    Telemetry.window_stddev(Telemetry.internal);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, Telemetry.f32);

    Telemetry.window_min(Telemetry.internal);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, Telemetry.f32);

    Telemetry.window_max(Telemetry.internal);
    TEST_ASSERT_EQUAL_FLOAT(9.0f, Telemetry.f32);
}

// A window of capacity 4 fed five samples holds the last four, 2 3 4 5:
//   count stays 4, mean = (2+3+4+5)/4 = 14/4 = 3.5, min = 2, max = 5.
// The evicted 1 is folded back out of both running sums, so the mean cannot drift toward it.
void test_window_evicts_the_oldest_sample(void)
{
    window_init(4);
    push(1.0f);
    push(2.0f);
    push(3.0f);
    push(4.0f);

    Telemetry.window.w = &g_w;
    Telemetry.window_mean(Telemetry.internal);
    TEST_ASSERT_EQUAL_FLOAT(2.5f, Telemetry.f32); // (1+2+3+4)/4

    push(5.0f);
    Telemetry.window.w = &g_w;
    Telemetry.window_count(Telemetry.internal);
    TEST_ASSERT_EQUAL_UINT16(4u, Telemetry.u16);

    Telemetry.window_mean(Telemetry.internal);
    TEST_ASSERT_EQUAL_FLOAT(3.5f, Telemetry.f32);

    Telemetry.window_min(Telemetry.internal);
    TEST_ASSERT_EQUAL_FLOAT(2.0f, Telemetry.f32);

    Telemetry.window_max(Telemetry.internal);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, Telemetry.f32);
}

// The count grows one per push and stops at the capacity, never past it.
void test_window_count_stops_at_capacity(void)
{
    window_init(3);
    for (uint16_t i = 0; i < 10; i++)
    {
        push((float)i);
        Telemetry.window.w = &g_w;
        Telemetry.window_count(Telemetry.internal);
        const uint16_t want = (uint16_t)((i + 1u) < 3u ? (i + 1u) : 3u);
        TEST_ASSERT_EQUAL_UINT16(want, Telemetry.u16);
    }
}

// Every sample the same: the variance is zero by definition, and rounding must not carry it below
// zero, which would make the standard deviation a NaN.
void test_window_variance_of_a_constant_is_zero(void)
{
    window_init(8);
    for (int i = 0; i < 8; i++)
    {
        push(1000000.0f);
    }
    Telemetry.window.w = &g_w;
    Telemetry.window_variance(Telemetry.internal);
    TEST_ASSERT_TRUE(Telemetry.ok);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, Telemetry.f32);

    Telemetry.window_stddev(Telemetry.internal);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, Telemetry.f32);
}

// One sample is a whole window: the mean is that sample, the variance zero, min and max both it.
void test_window_of_one_sample(void)
{
    window_init(8);
    push(-3.5f);
    Telemetry.window.w = &g_w;
    Telemetry.window_mean(Telemetry.internal);
    TEST_ASSERT_EQUAL_FLOAT(-3.5f, Telemetry.f32);
    Telemetry.window_variance(Telemetry.internal);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, Telemetry.f32);
    Telemetry.window_min(Telemetry.internal);
    TEST_ASSERT_EQUAL_FLOAT(-3.5f, Telemetry.f32);
    Telemetry.window_max(Telemetry.internal);
    TEST_ASSERT_EQUAL_FLOAT(-3.5f, Telemetry.f32);
}

// An empty window has no mean to report, so every statistic refuses rather than dividing by zero.
void test_window_statistics_need_a_sample(void)
{
    window_init(8);
    Telemetry.window.w = &g_w;

    Telemetry.window_count(Telemetry.internal);
    TEST_ASSERT_TRUE(Telemetry.ok); // the count itself is known: it is zero
    TEST_ASSERT_EQUAL_UINT16(0u, Telemetry.u16);

    Telemetry.window_mean(Telemetry.internal);
    TEST_ASSERT_FALSE(Telemetry.ok);
    Telemetry.window_variance(Telemetry.internal);
    TEST_ASSERT_FALSE(Telemetry.ok);
    Telemetry.window_stddev(Telemetry.internal);
    TEST_ASSERT_FALSE(Telemetry.ok);
    Telemetry.window_min(Telemetry.internal);
    TEST_ASSERT_FALSE(Telemetry.ok);
    Telemetry.window_max(Telemetry.internal);
    TEST_ASSERT_FALSE(Telemetry.ok);
}

// An init re-empties a window that already held samples, so the previous run's sums cannot leak
// into the next one.
void test_window_init_empties_the_window(void)
{
    window_init(8);
    push(100.0f);
    push(200.0f);
    window_init(8);

    Telemetry.window.w = &g_w;
    Telemetry.window_count(Telemetry.internal);
    TEST_ASSERT_EQUAL_UINT16(0u, Telemetry.u16);

    push(1.0f);
    Telemetry.window.w = &g_w;
    Telemetry.window_mean(Telemetry.internal);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, Telemetry.f32);
}

// A push with no storage bound, and a zero capacity, are both refused: there is nowhere to put the
// sample and no window to summarize.
void test_window_push_needs_bound_storage(void)
{
    Telemetry.window.w = &g_w;
    Telemetry.window.buf = NULL;
    Telemetry.window.cap = 8;
    Telemetry.window_init(Telemetry.internal);
    push(1.0f);
    TEST_ASSERT_FALSE(Telemetry.ok);

    window_init(0);
    push(1.0f);
    TEST_ASSERT_FALSE(Telemetry.ok);
}

// The first difference over the elapsed seconds:
//   10 at t = 1000 ms is the first sample, so there is nothing to difference against: 0.
//   20 at t = 2000 ms: (20 - 10) / ((2000 - 1000)/1000) = 10 / 1 = 10 units per second.
//   20 at t = 2500 ms: (20 - 20) / 0.5                  =  0.
//   15 at t = 3500 ms: (15 - 20) / 1                    = -5, so a falling value reads negative.
void test_rate_is_the_first_difference_per_second(void)
{
    Telemetry.rate.r = &g_r;
    Telemetry.rate_init(Telemetry.internal);
    TEST_ASSERT_TRUE(Telemetry.ok);

    TEST_ASSERT_EQUAL_FLOAT(0.0f, rate(10.0f, 1000u));
    TEST_ASSERT_EQUAL_FLOAT(10.0f, rate(20.0f, 2000u));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, rate(20.0f, 2500u));
    TEST_ASSERT_EQUAL_FLOAT(-5.0f, rate(15.0f, 3500u));
}

// A quarter second is a quarter of the divisor: 5 over 0.25 s is 20 per second.
void test_rate_scales_a_sub_second_interval(void)
{
    Telemetry.rate.r = &g_r;
    Telemetry.rate_init(Telemetry.internal);
    (void)rate(0.0f, 0u);
    TEST_ASSERT_EQUAL_FLOAT(20.0f, rate(5.0f, 250u));
}

// Two samples at the same millisecond have no elapsed time to divide by, so the rate is reported
// as zero rather than an infinity.
void test_rate_of_a_zero_interval_is_zero(void)
{
    Telemetry.rate.r = &g_r;
    Telemetry.rate_init(Telemetry.internal);
    (void)rate(1.0f, 5000u);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, rate(9.0f, 5000u));
}

// The millisecond count is unsigned, so its difference is taken modulo 2^32 and a counter rollover
// subtracts correctly:
//   last = 2^32 - 500 = 4294966796, now = 500
//   (uint32)(500 - 4294966796) = 500 - 4294966796 + 4294967296 = 1000 ms
// so a rise of 10 over that interval is 10 per second, exactly as if the counter had not wrapped.
void test_rate_across_a_counter_rollover(void)
{
    Telemetry.rate.r = &g_r;
    Telemetry.rate_init(Telemetry.internal);
    (void)rate(100.0f, 4294966796u);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, rate(110.0f, 500u));
}

// An init drops the prior sample, so the next update primes rather than differencing against a
// value from the last run.
void test_rate_init_drops_the_prior_sample(void)
{
    Telemetry.rate.r = &g_r;
    Telemetry.rate_init(Telemetry.internal);
    (void)rate(0.0f, 0u);
    (void)rate(1000.0f, 1000u);

    Telemetry.rate.r = &g_r;
    Telemetry.rate_init(Telemetry.internal);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, rate(50.0f, 2000u));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, rate(50.0f, 3000u));
}

// The trapezoidal rule integrates a rate over time: each step adds the mean of its two endpoints
// multiplied by the elapsed seconds. Rates 0, 10, 10 one second apart:
//   step 1 (0 -> 10 over 1 s): (0 + 10)/2 * 1  =  5, total  5
//   step 2 (10 -> 10 over 1 s): (10 + 10)/2 * 1 = 10, total 15
// A rectangle rule taking the new endpoint alone would report 10 then 20.
// The total is SenML's Sum: the integrated sum of the values over time (RFC 8428 sec 4.2).
void test_totalizer_is_the_trapezoidal_integral(void)
{
    Telemetry.totalizer.t = &g_t;
    Telemetry.totalizer_init(Telemetry.internal);
    TEST_ASSERT_TRUE(Telemetry.ok);

    // The first sample only seeds an endpoint, so nothing is integrated yet.
    TEST_ASSERT_EQUAL_FLOAT(0.0f, (float)integrate(0.0f, 0u));
    TEST_ASSERT_EQUAL_FLOAT(5.0f, (float)integrate(10.0f, 1000u));
    TEST_ASSERT_EQUAL_FLOAT(15.0f, (float)integrate(10.0f, 2000u));

    Telemetry.totalizer.t = &g_t;
    Telemetry.totalizer_total(Telemetry.internal);
    TEST_ASSERT_TRUE(Telemetry.ok);
    TEST_ASSERT_EQUAL_FLOAT(15.0f, (float)Telemetry.f64);
}

// A rate held constant integrates to rate multiplied by elapsed time, whatever the step count:
// 100 per second over ten one-second steps is 1000, in rate units multiplied by seconds.
void test_totalizer_of_a_constant_rate(void)
{
    Telemetry.totalizer.t = &g_t;
    Telemetry.totalizer_init(Telemetry.internal);
    (void)integrate(100.0f, 0u);
    for (uint32_t i = 1; i <= 10u; i++)
    {
        (void)integrate(100.0f, i * 1000u);
    }
    Telemetry.totalizer.t = &g_t;
    Telemetry.totalizer_total(Telemetry.internal);
    TEST_ASSERT_EQUAL_FLOAT(1000.0f, (float)Telemetry.f64);
}

// A negative rate takes the total back down: 10 for a second then -10 for a second nets to zero.
//   step 1 (0 -> 10 over 1 s):  (0 + 10)/2   =  5
//   step 2 (10 -> -10 over 1 s): (10 - 10)/2 =  0, total 5
//   step 3 (-10 -> 0 over 1 s):  (-10 + 0)/2 = -5, total 0
void test_totalizer_integrates_a_negative_rate(void)
{
    Telemetry.totalizer.t = &g_t;
    Telemetry.totalizer_init(Telemetry.internal);
    (void)integrate(0.0f, 0u);
    TEST_ASSERT_EQUAL_FLOAT(5.0f, (float)integrate(10.0f, 1000u));
    TEST_ASSERT_EQUAL_FLOAT(5.0f, (float)integrate(-10.0f, 2000u));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, (float)integrate(0.0f, 3000u));
}

// A reset zeroes the running total and drops the prior rate sample, so the next add seeds an
// endpoint instead of integrating across the gap.
void test_totalizer_reset(void)
{
    Telemetry.totalizer.t = &g_t;
    Telemetry.totalizer_init(Telemetry.internal);
    (void)integrate(10.0f, 0u);
    (void)integrate(10.0f, 1000u);

    Telemetry.totalizer.t = &g_t;
    Telemetry.totalizer_reset(Telemetry.internal);
    TEST_ASSERT_TRUE(Telemetry.ok);

    Telemetry.totalizer.t = &g_t;
    Telemetry.totalizer_total(Telemetry.internal);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, (float)Telemetry.f64);

    TEST_ASSERT_EQUAL_FLOAT(0.0f, (float)integrate(10.0f, 9000u));
    TEST_ASSERT_EQUAL_FLOAT(10.0f, (float)integrate(10.0f, 10000u));
}

// The totalizer takes the same unsigned difference the rate does, so a counter rollover integrates
// one second, not 2^32 milliseconds:
//   last = 4294966796, now = 500 -> 1000 ms, at a constant 10 per second, adds 10.
void test_totalizer_across_a_counter_rollover(void)
{
    Telemetry.totalizer.t = &g_t;
    Telemetry.totalizer_init(Telemetry.internal);
    (void)integrate(10.0f, 4294966796u);
    TEST_ASSERT_EQUAL_FLOAT(10.0f, (float)integrate(10.0f, 500u));
}

// Every call names its accumulator through the handle; a null one is reported, not written through.
void test_calls_refuse_a_null_accumulator(void)
{
    Telemetry.window.w = NULL;
    Telemetry.window_init(Telemetry.internal);
    TEST_ASSERT_FALSE(Telemetry.ok);
    Telemetry.window_push(Telemetry.internal);
    TEST_ASSERT_FALSE(Telemetry.ok);
    Telemetry.window_count(Telemetry.internal);
    TEST_ASSERT_FALSE(Telemetry.ok);
    TEST_ASSERT_EQUAL_UINT16(0u, Telemetry.u16);

    Telemetry.rate.r = NULL;
    Telemetry.rate_init(Telemetry.internal);
    TEST_ASSERT_FALSE(Telemetry.ok);
    Telemetry.rate_update(Telemetry.internal);
    TEST_ASSERT_FALSE(Telemetry.ok);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, Telemetry.f32);

    Telemetry.totalizer.t = NULL;
    Telemetry.totalizer_init(Telemetry.internal);
    TEST_ASSERT_FALSE(Telemetry.ok);
    Telemetry.totalizer_add(Telemetry.internal);
    TEST_ASSERT_FALSE(Telemetry.ok);
    Telemetry.totalizer_total(Telemetry.internal);
    TEST_ASSERT_FALSE(Telemetry.ok);
}

// Two windows over separate storage do not share sums: the accumulator is the caller's, and the
// module holds nothing between calls.
void test_two_windows_are_independent(void)
{
    static TelemetryWindow other;
    static float other_buf[4];

    window_init(8);
    push(10.0f);
    push(20.0f);

    Telemetry.window.w = &other;
    Telemetry.window.buf = other_buf;
    Telemetry.window.cap = 4;
    Telemetry.window_init(Telemetry.internal);
    Telemetry.window.w = &other;
    Telemetry.window.sample = 1.0f;
    Telemetry.window_push(Telemetry.internal);

    Telemetry.window.w = &other;
    Telemetry.window_mean(Telemetry.internal);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, Telemetry.f32);

    Telemetry.window.w = &g_w;
    Telemetry.window_mean(Telemetry.internal);
    TEST_ASSERT_EQUAL_FLOAT(15.0f, Telemetry.f32);
}
