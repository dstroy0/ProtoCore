// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the telemetry math helpers (services/iot/telemetry): moving-window
// stats, rate-of-change, and the totalizer. Pure computation - the host drives a
// synthetic millisecond clock and known datasets.

#include "services/iot/telemetry/telemetry.h"
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

// Classic dataset {2,4,4,4,5,5,7,9}: mean 5, population variance 4, stddev 2.
void test_window_classic_stats()
{
    float buf[8];
    pc_window w;
    pc_window_init(&w, buf, 8);
    const float samples[8] = {2, 4, 4, 4, 5, 5, 7, 9};
    for (int i = 0; i < 8; i++)
    {
        pc_window_push(&w, samples[i]);
    }
    TEST_ASSERT_EQUAL_UINT16(8, pc_window_count(&w));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 5.0f, pc_window_mean(&w));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 4.0f, pc_window_variance(&w));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.0f, pc_window_stddev(&w));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.0f, pc_window_min(&w));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 9.0f, pc_window_max(&w));
}

// An empty window reports zeros, not garbage.
void test_window_empty()
{
    float buf[4];
    pc_window w;
    pc_window_init(&w, buf, 4);
    TEST_ASSERT_EQUAL_UINT16(0, pc_window_count(&w));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pc_window_mean(&w));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pc_window_variance(&w));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pc_window_min(&w));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pc_window_max(&w));
}

// A single sample: mean is the sample, variance 0.
void test_window_single_sample()
{
    float buf[4];
    pc_window w;
    pc_window_init(&w, buf, 4);
    pc_window_push(&w, 42.0f);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 42.0f, pc_window_mean(&w));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, pc_window_variance(&w));
}

// Pushing past capacity evicts the oldest sample (rolling window).
void test_window_eviction()
{
    float buf[3];
    pc_window w;
    pc_window_init(&w, buf, 3);
    pc_window_push(&w, 1);
    pc_window_push(&w, 2);
    pc_window_push(&w, 3);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.0f, pc_window_mean(&w)); // {1,2,3}
    pc_window_push(&w, 4);                                     // evicts 1 -> {2,3,4}
    TEST_ASSERT_EQUAL_UINT16(3, pc_window_count(&w));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 3.0f, pc_window_mean(&w));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 2.0f, pc_window_min(&w));
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 4.0f, pc_window_max(&w));
}

// A zero-capacity window or an unbound (NULL) buffer is a defensive no-op:
// push must not touch count/head/sums, and it must not crash.
void test_window_push_guards()
{
    float buf[4];

    // cap == 0, buf non-NULL.
    pc_window w_zero_cap;
    pc_window_init(&w_zero_cap, buf, 0);
    pc_window_push(&w_zero_cap, 1.0f);
    TEST_ASSERT_EQUAL_UINT16(0, pc_window_count(&w_zero_cap));

    // buf == NULL, cap non-zero.
    pc_window w_null_buf;
    pc_window_init(&w_null_buf, NULL, 4);
    pc_window_push(&w_null_buf, 1.0f);
    TEST_ASSERT_EQUAL_UINT16(0, pc_window_count(&w_null_buf));
}

// Variance clamps a tiny negative result caused by floating-point rounding in
// the running sums (sum_sq/count - mean*mean can dip just below 0). Construct
// that state directly since it's not reliably reachable via real float pushes.
void test_window_variance_clamps_negative_rounding()
{
    float buf[4];
    pc_window w;
    pc_window_init(&w, buf, 4);
    w.count = 2;
    w.sum = 2.0;
    w.sum_sq = 1.999999; // slightly under mean*mean*count == 1.0*1.0*2 == 2.0
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pc_window_variance(&w));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, pc_window_stddev(&w)); // sqrt of clamped 0, not NaN
}

// Rate of change: first sample yields 0, then units per second.
void test_rate_basic()
{
    pc_rate r;
    pc_rate_init(&r);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, pc_rate_update(&r, 10.0f, 0));     // first
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 10.0f, pc_rate_update(&r, 20.0f, 1000)); // +10 / 1s
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, -5.0f, pc_rate_update(&r, 10.0f, 3000)); // -10 / 2s
}

// A zero elapsed time yields 0 (no divide-by-zero).
void test_rate_zero_dt()
{
    pc_rate r;
    pc_rate_init(&r);
    pc_rate_update(&r, 5.0f, 100);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, pc_rate_update(&r, 9.0f, 100));
}

// Constant rate of 2/s for 2 s totals 4.
void test_totalizer_constant_rate()
{
    pc_totalizer t;
    pc_totalizer_init(&t);
    pc_totalizer_add(&t, 2.0f, 0);    // seed
    pc_totalizer_add(&t, 2.0f, 1000); // +2
    double total = pc_totalizer_add(&t, 2.0f, 2000);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 4.0f, (float)total);
}

// Trapezoidal rule: ramp 0 -> 10 over 1 s totals 5; reset clears it.
void test_totalizer_trapezoid_and_reset()
{
    pc_totalizer t;
    pc_totalizer_init(&t);
    pc_totalizer_add(&t, 0.0f, 0);
    double total = pc_totalizer_add(&t, 10.0f, 1000);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 5.0f, (float)total);
    pc_totalizer_reset(&t);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, (float)pc_totalizer_total(&t));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_window_classic_stats);
    RUN_TEST(test_window_empty);
    RUN_TEST(test_window_single_sample);
    RUN_TEST(test_window_eviction);
    RUN_TEST(test_window_push_guards);
    RUN_TEST(test_window_variance_clamps_negative_rounding);
    RUN_TEST(test_rate_basic);
    RUN_TEST(test_rate_zero_dt);
    RUN_TEST(test_totalizer_constant_rate);
    RUN_TEST(test_totalizer_trapezoid_and_reset);
    return UNITY_END();
}
