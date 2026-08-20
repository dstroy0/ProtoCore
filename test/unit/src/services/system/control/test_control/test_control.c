// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host tests for the PID control law (services/system/control/control.h).
//
// No standards body publishes test vectors for a PID controller, so every expectation here is
// PROPERTIES plus arithmetic derived from the difference equations control.h states, with the
// derivation written out beside each case. Gains, sample periods and signals are chosen to be exact
// in binary32 so the arithmetic is exact too. test_anti_windup_freezes_and_releases is the
// load-bearing case: conditional integration is the one part of the law whose behavior cannot be
// read off the parallel form, and it is what keeps a saturated actuator from winding up.

#include "services/system/control/control.h"
#include <string.h>

#include <unity.h>

static uint8_t control_work[16]; // the borrow an entry takes; Control never reads it

void setUp(void)
{
}
void tearDown(void)
{
}

static float absf(float v)
{
    return v < 0.0f ? -v : v;
}

static void close_to(float want, float got)
{
    TEST_ASSERT_TRUE(absf(want - got) <= 1e-3f * (1.0f + absf(want)));
}

// u = kp * error with every other term zeroed: error = 10 - 4 = 6, so out = 2 * 6 = 12.
void test_proportional_term(void)
{
    Pid p;
    ControlV.pid_init_args.p = &p;
    ControlV.pid_init_args.kp = 2.0f;
    ControlV.pid_init_args.ki = 0.0f;
    ControlV.pid_init_args.kd = 0.0f;
    ControlV.pid_init(control_work);
    TEST_ASSERT_TRUE(pid_update(&p, 10.0f, 4.0f, 0.5f) == 12.0f);
    // The output tracks the error only: halving it halves the command.
    TEST_ASSERT_TRUE(pid_update(&p, 7.0f, 4.0f, 0.5f) == 6.0f);
    // Zero error is zero command.
    TEST_ASSERT_TRUE(pid_update(&p, 4.0f, 4.0f, 0.5f) == 0.0f);
}

// The integral accumulates ki * error * dt each step: with ki = 4, error = 2 and dt = 0.25 each
// step adds 4 * 2 * 0.25 = 2, so the output walks 2, 4, 6.
void test_integral_term_accumulates(void)
{
    Pid p;
    ControlV.pid_init_args.p = &p;
    ControlV.pid_init_args.kp = 0.0f;
    ControlV.pid_init_args.ki = 4.0f;
    ControlV.pid_init_args.kd = 0.0f;
    ControlV.pid_init(control_work);
    TEST_ASSERT_TRUE(pid_update(&p, 2.0f, 0.0f, 0.25f) == 2.0f);
    TEST_ASSERT_TRUE(pid_update(&p, 2.0f, 0.0f, 0.25f) == 4.0f);
    TEST_ASSERT_TRUE(pid_update(&p, 2.0f, 0.0f, 0.25f) == 6.0f);
    // An error of the opposite sign unwinds it by the same rule.
    TEST_ASSERT_TRUE(pid_update(&p, -2.0f, 0.0f, 0.25f) == 4.0f);
}

// Derivative on measurement: d = -(measurement - previous) / dt. The first step has no previous
// measurement, so it contributes nothing; the second sees (3 - 1) / 0.5 = 4, negated, times kd = 2.
void test_derivative_acts_on_the_measurement(void)
{
    Pid p;
    ControlV.pid_init_args.p = &p;
    ControlV.pid_init_args.kp = 0.0f;
    ControlV.pid_init_args.ki = 0.0f;
    ControlV.pid_init_args.kd = 2.0f;
    ControlV.pid_init(control_work);
    TEST_ASSERT_TRUE(pid_update(&p, 0.0f, 1.0f, 0.5f) == 0.0f);
    TEST_ASSERT_TRUE(pid_update(&p, 0.0f, 3.0f, 0.5f) == -8.0f);
    // A held measurement has no derivative.
    TEST_ASSERT_TRUE(pid_update(&p, 0.0f, 3.0f, 0.5f) == 0.0f);
    // Falling measurement gives the opposite sign.
    TEST_ASSERT_TRUE(pid_update(&p, 0.0f, 1.0f, 0.5f) == 8.0f);
}

// A setpoint step must not produce a derivative kick: with the derivative taken on the measurement,
// two controllers fed identical measurements differ only by kp * (setpoint difference).
void test_setpoint_step_produces_no_derivative_kick(void)
{
    Pid held;
    Pid stepped;
    ControlV.pid_init_args.p = &held;
    ControlV.pid_init_args.kp = 1.0f;
    ControlV.pid_init_args.ki = 0.0f;
    ControlV.pid_init_args.kd = 4.0f;
    ControlV.pid_init(control_work);
    ControlV.pid_init_args.p = &stepped;
    ControlV.pid_init_args.kp = 1.0f;
    ControlV.pid_init_args.ki = 0.0f;
    ControlV.pid_init_args.kd = 4.0f;
    ControlV.pid_init(control_work);

    TEST_ASSERT_TRUE(pid_update(&held, 0.0f, 1.0f, 0.5f) == pid_update(&stepped, 0.0f, 1.0f, 0.5f));

    float a = pid_update(&held, 0.0f, 2.0f, 0.5f);     // setpoint held
    float b = pid_update(&stepped, 16.0f, 2.0f, 0.5f); // setpoint stepped by 16
    TEST_ASSERT_TRUE((b - a) == 16.0f);                // kp * 16, nothing from D
}

// The single-pole low-pass blends each new derivative in by alpha: with alpha = 0.5 and a raw
// derivative of -4, the filtered value goes 0 -> -2 -> -3 -> -3.5.
void test_derivative_low_pass(void)
{
    Pid p;
    ControlV.pid_init_args.p = &p;
    ControlV.pid_init_args.kp = 0.0f;
    ControlV.pid_init_args.ki = 0.0f;
    ControlV.pid_init_args.kd = 1.0f;
    ControlV.pid_init(control_work);
    ControlV.pid_set_derivative_filter_args.p = &p;
    ControlV.pid_set_derivative_filter_args.alpha = 0.5f;
    ControlV.pid_set_derivative_filter(control_work);
    TEST_ASSERT_TRUE(pid_update(&p, 0.0f, 0.0f, 0.5f) == 0.0f); // priming step
    TEST_ASSERT_TRUE(pid_update(&p, 0.0f, 2.0f, 0.5f) == -2.0f);
    TEST_ASSERT_TRUE(pid_update(&p, 0.0f, 4.0f, 0.5f) == -3.0f);
    TEST_ASSERT_TRUE(pid_update(&p, 0.0f, 6.0f, 0.5f) == -3.5f);
}

// The feed-forward term is kff * setpoint, independent of the measurement.
void test_feedforward_term(void)
{
    Pid p;
    ControlV.pid_init_args.p = &p;
    ControlV.pid_init_args.kp = 0.0f;
    ControlV.pid_init_args.ki = 0.0f;
    ControlV.pid_init_args.kd = 0.0f;
    ControlV.pid_init(control_work);
    ControlV.pid_set_feedforward_args.p = &p;
    ControlV.pid_set_feedforward_args.kff = 3.0f;
    ControlV.pid_set_feedforward(control_work);
    TEST_ASSERT_TRUE(pid_update(&p, 2.0f, 0.0f, 1.0f) == 6.0f);
    TEST_ASSERT_TRUE(pid_update(&p, 2.0f, 100.0f, 1.0f) == 6.0f);
}

// The output is clamped to the actuator's range in both directions.
void test_output_clamping(void)
{
    Pid p;
    ControlV.pid_init_args.p = &p;
    ControlV.pid_init_args.kp = 1.0f;
    ControlV.pid_init_args.ki = 0.0f;
    ControlV.pid_init_args.kd = 0.0f;
    ControlV.pid_init(control_work);
    ControlV.pid_set_output_limits_args.p = &p;
    ControlV.pid_set_output_limits_args.lo = -1.0f;
    ControlV.pid_set_output_limits_args.hi = 1.0f;
    ControlV.pid_set_output_limits(control_work);
    TEST_ASSERT_TRUE(pid_update(&p, 10.0f, 0.0f, 1.0f) == 1.0f);
    TEST_ASSERT_TRUE(pid_update(&p, -10.0f, 0.0f, 1.0f) == -1.0f);
    TEST_ASSERT_TRUE(pid_update(&p, 0.5f, 0.0f, 1.0f) == 0.5f); // inside the range, untouched
}

// The load-bearing case. With ki = 1, dt = 1 and out_max = 5 the integrator adds the error each
// step. Step 1: 0 + 3 = 3, output 3, not saturated, so the accumulator commits. Step 2 would make
// it 6 and the output 6 > 5 with the error still positive - integrating further only pushes deeper
// into the rail, so the accumulator is frozen at 3 and the output clamps to 5. Once the error
// reverses, integrating moves the output back toward the range and the accumulator commits again:
// 3 + (-1) = 2.
void test_anti_windup_freezes_and_releases(void)
{
    Pid p;
    ControlV.pid_init_args.p = &p;
    ControlV.pid_init_args.kp = 0.0f;
    ControlV.pid_init_args.ki = 1.0f;
    ControlV.pid_init_args.kd = 0.0f;
    ControlV.pid_init(control_work);
    ControlV.pid_set_output_limits_args.p = &p;
    ControlV.pid_set_output_limits_args.lo = -5.0f;
    ControlV.pid_set_output_limits_args.hi = 5.0f;
    ControlV.pid_set_output_limits(control_work);

    TEST_ASSERT_TRUE(pid_update(&p, 3.0f, 0.0f, 1.0f) == 3.0f);
    TEST_ASSERT_TRUE(p.integ == 3.0f);

    for (int i = 0; i < 20; i++)
    {
        TEST_ASSERT_TRUE(pid_update(&p, 3.0f, 0.0f, 1.0f) == 5.0f);
        TEST_ASSERT_TRUE(p.integ == 3.0f); // frozen, not wound up over twenty steps
    }

    TEST_ASSERT_TRUE(pid_update(&p, -1.0f, 0.0f, 1.0f) == 2.0f);
    TEST_ASSERT_TRUE(p.integ == 2.0f);

    // The same freeze applies at the lower rail with a negative error.
    ControlV.pid_reset_args.p = &p;
    ControlV.pid_reset(control_work);
    TEST_ASSERT_TRUE(pid_update(&p, -3.0f, 0.0f, 1.0f) == -3.0f);
    for (int i = 0; i < 20; i++)
    {
        TEST_ASSERT_TRUE(pid_update(&p, -3.0f, 0.0f, 1.0f) == -5.0f);
        TEST_ASSERT_TRUE(p.integ == -3.0f);
    }
}

// The hard integral clamp is a second, unconditional bound: with limits [-2, 2] the accumulator
// stops there even when the output is nowhere near its own limits.
void test_integral_hard_clamp(void)
{
    Pid p;
    ControlV.pid_init_args.p = &p;
    ControlV.pid_init_args.kp = 0.0f;
    ControlV.pid_init_args.ki = 1.0f;
    ControlV.pid_init_args.kd = 0.0f;
    ControlV.pid_init(control_work);
    ControlV.pid_set_integral_limits_args.p = &p;
    ControlV.pid_set_integral_limits_args.lo = -2.0f;
    ControlV.pid_set_integral_limits_args.hi = 2.0f;
    ControlV.pid_set_integral_limits(control_work);
    TEST_ASSERT_TRUE(pid_update(&p, 3.0f, 0.0f, 1.0f) == 2.0f);
    TEST_ASSERT_TRUE(pid_update(&p, 3.0f, 0.0f, 1.0f) == 2.0f);
    TEST_ASSERT_TRUE(p.integ == 2.0f);
    TEST_ASSERT_TRUE(pid_update(&p, -3.0f, 0.0f, 1.0f) == -1.0f); // 2 + (-3) = -1, inside the clamp
    TEST_ASSERT_TRUE(pid_update(&p, -3.0f, 0.0f, 1.0f) == -2.0f); // -1 + (-3) = -4, clamped to -2
}

// All four terms at once: kp = 2, ki = 4, kd = 1, kff = 0.5, dt = 0.5.
//   step 1: error = 8 - 0 = 8, no derivative yet.
//           integ = 4 * 8 * 0.5 = 16, out = 2*8 + 16 + 0 + 0.5*8 = 36
//   step 2: measurement 2, error = 6, deriv = -(2 - 0)/0.5 = -4
//           integ = 16 + 4*6*0.5 = 28, out = 2*6 + 28 + 1*(-4) + 0.5*8 = 40
void test_all_four_terms_together(void)
{
    Pid p;
    ControlV.pid_init_args.p = &p;
    ControlV.pid_init_args.kp = 2.0f;
    ControlV.pid_init_args.ki = 4.0f;
    ControlV.pid_init_args.kd = 1.0f;
    ControlV.pid_init(control_work);
    ControlV.pid_set_feedforward_args.p = &p;
    ControlV.pid_set_feedforward_args.kff = 0.5f;
    ControlV.pid_set_feedforward(control_work);
    TEST_ASSERT_TRUE(pid_update(&p, 8.0f, 0.0f, 0.5f) == 36.0f);
    TEST_ASSERT_TRUE(pid_update(&p, 8.0f, 2.0f, 0.5f) == 40.0f);
}

// A non-positive sample period is not a step: the law divides by dt, so it returns 0 and leaves the
// controller untouched.
void test_non_positive_dt_is_not_a_step(void)
{
    Pid p;
    ControlV.pid_init_args.p = &p;
    ControlV.pid_init_args.kp = 1.0f;
    ControlV.pid_init_args.ki = 1.0f;
    ControlV.pid_init_args.kd = 0.0f;
    ControlV.pid_init(control_work);
    TEST_ASSERT_TRUE(pid_update(&p, 5.0f, 0.0f, 0.0f) == 0.0f);
    TEST_ASSERT_TRUE(pid_update(&p, 5.0f, 0.0f, -1.0f) == 0.0f);
    TEST_ASSERT_TRUE(p.integ == 0.0f);
    TEST_ASSERT_FALSE(p.primed);
    TEST_ASSERT_TRUE(pid_update(NULL, 5.0f, 0.0f, 1.0f) == 0.0f);
}

// The fixed-rate entry point is the same law with the cached period, and it refuses to run before
// pid_set_rate has supplied one.
void test_fixed_rate_matches_the_variable_rate_law(void)
{
    Pid fixed;
    Pid variable;
    ControlV.pid_init_args.p = &fixed;
    ControlV.pid_init_args.kp = 2.0f;
    ControlV.pid_init_args.ki = 4.0f;
    ControlV.pid_init_args.kd = 1.0f;
    ControlV.pid_init(control_work);
    ControlV.pid_init_args.p = &variable;
    ControlV.pid_init_args.kp = 2.0f;
    ControlV.pid_init_args.ki = 4.0f;
    ControlV.pid_init_args.kd = 1.0f;
    ControlV.pid_init(control_work);

    TEST_ASSERT_TRUE(pid_update_fixed(&fixed, 8.0f, 0.0f) == 0.0f); // no rate yet
    TEST_ASSERT_FALSE(fixed.primed);

    ControlV.pid_set_rate_args.p = &fixed;
    ControlV.pid_set_rate_args.dt = 0.5f;
    ControlV.pid_set_rate(control_work);
    TEST_ASSERT_TRUE(fixed.dt == 0.5f);
    TEST_ASSERT_TRUE(fixed.inv_dt == 2.0f);
    ControlV.pid_set_rate_args.p = &fixed;
    ControlV.pid_set_rate_args.dt = 0.0f;
    ControlV.pid_set_rate(control_work); // a non-positive rate is ignored, not stored
    TEST_ASSERT_TRUE(fixed.dt == 0.5f);

    static const float MEAS[5] = {0.0f, 2.0f, 3.0f, 3.0f, 1.0f};
    for (int i = 0; i < 5; i++)
    {
        TEST_ASSERT_TRUE(pid_update_fixed(&fixed, 8.0f, MEAS[i]) == pid_update(&variable, 8.0f, MEAS[i], 0.5f));
    }
    TEST_ASSERT_TRUE(pid_update_fixed(NULL, 1.0f, 0.0f) == 0.0f);
}

// A reset clears the integrator, the derivative memory and the prime flag, so the next step behaves
// exactly like a first step. The gains and limits survive it.
void test_reset_clears_only_the_runtime_state(void)
{
    Pid p;
    ControlV.pid_init_args.p = &p;
    ControlV.pid_init_args.kp = 2.0f;
    ControlV.pid_init_args.ki = 4.0f;
    ControlV.pid_init_args.kd = 1.0f;
    ControlV.pid_init(control_work);
    ControlV.pid_set_output_limits_args.p = &p;
    ControlV.pid_set_output_limits_args.lo = -100.0f;
    ControlV.pid_set_output_limits_args.hi = 100.0f;
    ControlV.pid_set_output_limits(control_work);
    (void)pid_update(&p, 8.0f, 0.0f, 0.5f);
    (void)pid_update(&p, 8.0f, 2.0f, 0.5f);
    TEST_ASSERT_TRUE(p.integ != 0.0f);
    TEST_ASSERT_TRUE(p.primed);

    ControlV.pid_reset_args.p = &p;
    ControlV.pid_reset(control_work);
    TEST_ASSERT_TRUE(p.integ == 0.0f);
    TEST_ASSERT_TRUE(p.prev_meas == 0.0f);
    TEST_ASSERT_TRUE(p.d_filt == 0.0f);
    TEST_ASSERT_FALSE(p.primed);
    TEST_ASSERT_TRUE(p.kp == 2.0f);
    TEST_ASSERT_TRUE(p.out_max == 100.0f);

    Pid fresh;
    ControlV.pid_init_args.p = &fresh;
    ControlV.pid_init_args.kp = 2.0f;
    ControlV.pid_init_args.ki = 4.0f;
    ControlV.pid_init_args.kd = 1.0f;
    ControlV.pid_init(control_work);
    ControlV.pid_set_output_limits_args.p = &fresh;
    ControlV.pid_set_output_limits_args.lo = -100.0f;
    ControlV.pid_set_output_limits_args.hi = 100.0f;
    ControlV.pid_set_output_limits(control_work);
    TEST_ASSERT_TRUE(pid_update(&p, 8.0f, 0.0f, 0.5f) == pid_update(&fresh, 8.0f, 0.0f, 0.5f));
}

// pid_init leaves the loop unbounded, so nothing clamps until limits are set.
void test_init_defaults(void)
{
    Pid p;
    ControlV.pid_init_args.p = &p;
    ControlV.pid_init_args.kp = 1.0f;
    ControlV.pid_init_args.ki = 2.0f;
    ControlV.pid_init_args.kd = 3.0f;
    ControlV.pid_init(control_work);
    TEST_ASSERT_TRUE(p.kp == 1.0f);
    TEST_ASSERT_TRUE(p.ki == 2.0f);
    TEST_ASSERT_TRUE(p.kd == 3.0f);
    TEST_ASSERT_TRUE(p.kff == 0.0f);
    TEST_ASSERT_TRUE(p.d_alpha == 0.0f);
    TEST_ASSERT_TRUE(p.out_min == -CONTROL_UNBOUNDED);
    TEST_ASSERT_TRUE(p.out_max == CONTROL_UNBOUNDED);
    TEST_ASSERT_TRUE(p.integ_min == -CONTROL_UNBOUNDED);
    TEST_ASSERT_TRUE(p.integ_max == CONTROL_UNBOUNDED);
    TEST_ASSERT_TRUE(p.dt == 0.0f);
    TEST_ASSERT_FALSE(p.primed);
}

// The batched pass must give, axis by axis, exactly what the single-loop entry point gives.
void test_batched_update_matches_the_single_loop(void)
{
    Pid batch[3];
    Pid one[3];
    for (int i = 0; i < 3; i++)
    {
        ControlV.pid_init_args.p = &batch[i];
        ControlV.pid_init_args.kp = (float)(i + 1);
        ControlV.pid_init_args.ki = 1.0f;
        ControlV.pid_init_args.kd = 0.5f;
        ControlV.pid_init(control_work);
        ControlV.pid_init_args.p = &one[i];
        ControlV.pid_init_args.kp = (float)(i + 1);
        ControlV.pid_init_args.ki = 1.0f;
        ControlV.pid_init_args.kd = 0.5f;
        ControlV.pid_init(control_work);
    }
    static const float SP[3] = {4.0f, 8.0f, -2.0f};
    float meas[3] = {0.0f, 1.0f, 0.5f};
    float out[3] = {0.0f, 0.0f, 0.0f};

    for (int step = 0; step < 4; step++)
    {
        ControlV.pid_update_n_args.p = batch;
        ControlV.pid_update_n_args.setpoint = SP;
        ControlV.pid_update_n_args.measurement = meas;
        ControlV.pid_update_n_args.dt = 0.25f;
        ControlV.pid_update_n_args.out = out;
        ControlV.pid_update_n_args.n = 3;
        ControlV.pid_update_n(control_work);
        for (int i = 0; i < 3; i++)
        {
            TEST_ASSERT_TRUE(out[i] == pid_update(&one[i], SP[i], meas[i], 0.25f));
            meas[i] += 0.5f;
        }
    }
    // A null array is a no-op rather than a write through it.
    out[0] = 42.0f;
    ControlV.pid_update_n_args.p = NULL;
    ControlV.pid_update_n_args.setpoint = SP;
    ControlV.pid_update_n_args.measurement = meas;
    ControlV.pid_update_n_args.dt = 0.25f;
    ControlV.pid_update_n_args.out = out;
    ControlV.pid_update_n_args.n = 3;
    ControlV.pid_update_n(control_work);
    ControlV.pid_update_n_args.p = batch;
    ControlV.pid_update_n_args.setpoint = NULL;
    ControlV.pid_update_n_args.measurement = meas;
    ControlV.pid_update_n_args.dt = 0.25f;
    ControlV.pid_update_n_args.out = out;
    ControlV.pid_update_n_args.n = 3;
    ControlV.pid_update_n(control_work);
    ControlV.pid_update_n_args.p = batch;
    ControlV.pid_update_n_args.setpoint = SP;
    ControlV.pid_update_n_args.measurement = NULL;
    ControlV.pid_update_n_args.dt = 0.25f;
    ControlV.pid_update_n_args.out = out;
    ControlV.pid_update_n_args.n = 3;
    ControlV.pid_update_n(control_work);
    ControlV.pid_update_n_args.p = batch;
    ControlV.pid_update_n_args.setpoint = SP;
    ControlV.pid_update_n_args.measurement = meas;
    ControlV.pid_update_n_args.dt = 0.25f;
    ControlV.pid_update_n_args.out = NULL;
    ControlV.pid_update_n_args.n = 3;
    ControlV.pid_update_n(control_work);
    TEST_ASSERT_TRUE(out[0] == 42.0f);
}

// The inline primitives, each straight from its definition.
void test_control_primitives(void)
{
    TEST_ASSERT_TRUE(control_clamp(5.0f, -1.0f, 1.0f) == 1.0f);
    TEST_ASSERT_TRUE(control_clamp(-5.0f, -1.0f, 1.0f) == -1.0f);
    TEST_ASSERT_TRUE(control_clamp(0.25f, -1.0f, 1.0f) == 0.25f);
    TEST_ASSERT_TRUE(control_clamp(-1.0f, -1.0f, 1.0f) == -1.0f); // the bounds are inclusive
    TEST_ASSERT_TRUE(control_clamp(1.0f, -1.0f, 1.0f) == 1.0f);

    // Deadband is continuous at the band edge: it returns 0 inside and v shifted toward 0 outside.
    TEST_ASSERT_TRUE(control_deadband(0.5f, 1.0f) == 0.0f);
    TEST_ASSERT_TRUE(control_deadband(-0.5f, 1.0f) == 0.0f);
    TEST_ASSERT_TRUE(control_deadband(1.0f, 1.0f) == 0.0f);
    TEST_ASSERT_TRUE(control_deadband(-1.0f, 1.0f) == 0.0f);
    TEST_ASSERT_TRUE(control_deadband(1.5f, 1.0f) == 0.5f);
    TEST_ASSERT_TRUE(control_deadband(-1.5f, 1.0f) == -0.5f);

    // Slew moves at most max_step toward the target, and lands exactly on it once within reach.
    TEST_ASSERT_TRUE(control_slew(10.0f, 0.0f, 2.0f) == 2.0f);
    TEST_ASSERT_TRUE(control_slew(-10.0f, 0.0f, 2.0f) == -2.0f);
    TEST_ASSERT_TRUE(control_slew(1.0f, 0.0f, 2.0f) == 1.0f);
    TEST_ASSERT_TRUE(control_slew(0.0f, 0.0f, 2.0f) == 0.0f);

    // The low-pass blends by alpha; alpha 0 holds and alpha 1 takes the sample.
    TEST_ASSERT_TRUE(control_lpf(0.0f, 8.0f, 0.5f) == 4.0f);
    TEST_ASSERT_TRUE(control_lpf(4.0f, 8.0f, 0.5f) == 6.0f);
    TEST_ASSERT_TRUE(control_lpf(4.0f, 8.0f, 0.0f) == 4.0f);
    TEST_ASSERT_TRUE(control_lpf(4.0f, 8.0f, 1.0f) == 8.0f);
}

// A slew of max_step reaches a target n steps away in exactly n calls: the primitive is monotone
// and never overshoots.
void test_slew_reaches_the_target_without_overshoot(void)
{
    float v = 0.0f;
    for (int i = 0; i < 5; i++)
    {
        v = control_slew(10.0f, v, 2.0f);
        TEST_ASSERT_TRUE(v <= 10.0f);
    }
    TEST_ASSERT_TRUE(v == 10.0f);
    TEST_ASSERT_TRUE(control_slew(10.0f, v, 2.0f) == 10.0f); // idempotent at the target
}

// Little-endian float readback, so the log assertions can be stated in the values that were logged.
static float f32le(const uint8_t *p)
{
    uint32_t u = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    float f;
    memcpy(&f, &u, 4);
    return f;
}

static uint32_t u32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

// The dense-binary log header, field by field against the layout control.h publishes:
// "DPID" | ver u8 = 1 | flags u8 | reserved u16 | dt | kp | ki | kd | kff | out_min | out_max,
// all floats little-endian, 36 octets in total.
void test_log_header_layout(void)
{
    Pid p;
    ControlV.pid_init_args.p = &p;
    ControlV.pid_init_args.kp = 1.5f;
    ControlV.pid_init_args.ki = 0.25f;
    ControlV.pid_init_args.kd = 0.125f;
    ControlV.pid_init(control_work);
    ControlV.pid_set_feedforward_args.p = &p;
    ControlV.pid_set_feedforward_args.kff = 2.0f;
    ControlV.pid_set_feedforward(control_work);
    ControlV.pid_set_output_limits_args.p = &p;
    ControlV.pid_set_output_limits_args.lo = -8.0f;
    ControlV.pid_set_output_limits_args.hi = 8.0f;
    ControlV.pid_set_output_limits(control_work);

    uint8_t buf[PID_LOG_HEADER_LEN + 4];
    memset(buf, 0xAA, sizeof(buf));
    ControlV.pid_log_header_args.buf = buf;
    ControlV.pid_log_header_args.cap = sizeof(buf);
    ControlV.pid_log_header_args.p = &p;
    ControlV.pid_log_header_args.dt = 0.5f;
    ControlV.pid_log_header(control_work);
    TEST_ASSERT_EQUAL_UINT32(PID_LOG_HEADER_LEN, (uint32_t)ControlV.n);
    TEST_ASSERT_EQUAL_UINT32(36u, (uint32_t)PID_LOG_HEADER_LEN);

    TEST_ASSERT_EQUAL_MEMORY(PID_LOG_MAGIC, buf, 4);
    TEST_ASSERT_EQUAL_UINT8(PID_LOG_VERSION, buf[4]);
    TEST_ASSERT_EQUAL_UINT8(1, buf[4]);
    TEST_ASSERT_EQUAL_UINT8(0, buf[5]); // flags
    TEST_ASSERT_EQUAL_UINT8(0, buf[6]); // reserved u16
    TEST_ASSERT_EQUAL_UINT8(0, buf[7]);
    TEST_ASSERT_TRUE(f32le(buf + 8) == 0.5f);
    TEST_ASSERT_TRUE(f32le(buf + 12) == 1.5f);
    TEST_ASSERT_TRUE(f32le(buf + 16) == 0.25f);
    TEST_ASSERT_TRUE(f32le(buf + 20) == 0.125f);
    TEST_ASSERT_TRUE(f32le(buf + 24) == 2.0f);
    TEST_ASSERT_TRUE(f32le(buf + 28) == -8.0f);
    TEST_ASSERT_TRUE(f32le(buf + 32) == 8.0f);
    TEST_ASSERT_EQUAL_UINT8(0xAA, buf[36]); // nothing written past the header

    // 0.5f is IEEE-754 0x3F000000, so a little-endian float ends with 0x3F.
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[8]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[9]);
    TEST_ASSERT_EQUAL_HEX8(0x00, buf[10]);
    TEST_ASSERT_EQUAL_HEX8(0x3F, buf[11]);

    // A buffer one octet short of the fixed width writes nothing.
    memset(buf, 0xAA, sizeof(buf));
    ControlV.pid_log_header_args.buf = buf;
    ControlV.pid_log_header_args.cap = PID_LOG_HEADER_LEN - 1;
    ControlV.pid_log_header_args.p = &p;
    ControlV.pid_log_header_args.dt = 0.5f;
    ControlV.pid_log_header(control_work);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)ControlV.n);
    TEST_ASSERT_EQUAL_UINT8(0xAA, buf[0]);
    ControlV.pid_log_header_args.buf = NULL;
    ControlV.pid_log_header_args.cap = sizeof(buf);
    ControlV.pid_log_header_args.p = &p;
    ControlV.pid_log_header_args.dt = 0.5f;
    ControlV.pid_log_header(control_work);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)ControlV.n);
    ControlV.pid_log_header_args.buf = buf;
    ControlV.pid_log_header_args.cap = sizeof(buf);
    ControlV.pid_log_header_args.p = NULL;
    ControlV.pid_log_header_args.dt = 0.5f;
    ControlV.pid_log_header(control_work);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)ControlV.n);
}

// One log record: setpoint | measurement | output | status, 16 octets, with bit 0 of status marking
// a saturated step.
void test_log_record_layout(void)
{
    uint8_t buf[PID_LOG_RECORD_LEN + 4];
    memset(buf, 0xAA, sizeof(buf));
    ControlV.pid_log_record_args.buf = buf;
    ControlV.pid_log_record_args.cap = sizeof(buf);
    ControlV.pid_log_record_args.setpoint = 4.0f;
    ControlV.pid_log_record_args.measurement = -1.5f;
    ControlV.pid_log_record_args.output = 2.25f;
    ControlV.pid_log_record_args.saturated = PROTO_TRUE;
    ControlV.pid_log_record(control_work);
    TEST_ASSERT_EQUAL_UINT32(PID_LOG_RECORD_LEN, (uint32_t)ControlV.n);
    TEST_ASSERT_EQUAL_UINT32(16u, (uint32_t)PID_LOG_RECORD_LEN);
    TEST_ASSERT_TRUE(f32le(buf + 0) == 4.0f);
    TEST_ASSERT_TRUE(f32le(buf + 4) == -1.5f);
    TEST_ASSERT_TRUE(f32le(buf + 8) == 2.25f);
    TEST_ASSERT_EQUAL_HEX32(PID_LOG_STATUS_SATURATED, u32le(buf + 12));
    TEST_ASSERT_EQUAL_HEX32(0x1u, PID_LOG_STATUS_SATURATED);
    TEST_ASSERT_EQUAL_UINT8(0xAA, buf[16]);

    ControlV.pid_log_record_args.buf = buf;
    ControlV.pid_log_record_args.cap = sizeof(buf);
    ControlV.pid_log_record_args.setpoint = 4.0f;
    ControlV.pid_log_record_args.measurement = -1.5f;
    ControlV.pid_log_record_args.output = 2.25f;
    ControlV.pid_log_record_args.saturated = PROTO_FALSE;
    ControlV.pid_log_record(control_work);
    TEST_ASSERT_EQUAL_UINT32(PID_LOG_RECORD_LEN, (uint32_t)ControlV.n);
    TEST_ASSERT_EQUAL_HEX32(0u, u32le(buf + 12));

    memset(buf, 0xAA, sizeof(buf));
    ControlV.pid_log_record_args.buf = buf;
    ControlV.pid_log_record_args.cap = PID_LOG_RECORD_LEN - 1;
    ControlV.pid_log_record_args.setpoint = 0.0f;
    ControlV.pid_log_record_args.measurement = 0.0f;
    ControlV.pid_log_record_args.output = 0.0f;
    ControlV.pid_log_record_args.saturated = PROTO_FALSE;
    ControlV.pid_log_record(control_work);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)ControlV.n);
    TEST_ASSERT_EQUAL_UINT8(0xAA, buf[0]);
    ControlV.pid_log_record_args.buf = NULL;
    ControlV.pid_log_record_args.cap = sizeof(buf);
    ControlV.pid_log_record_args.setpoint = 0.0f;
    ControlV.pid_log_record_args.measurement = 0.0f;
    ControlV.pid_log_record_args.output = 0.0f;
    ControlV.pid_log_record_args.saturated = PROTO_FALSE;
    ControlV.pid_log_record(control_work);
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)ControlV.n);
}

// A closed loop over a first-order plant must settle at the setpoint: with an integral term the
// steady-state error goes to zero, which is the property the I term exists for.
void test_closed_loop_settles_on_the_setpoint(void)
{
    Pid p;
    ControlV.pid_init_args.p = &p;
    ControlV.pid_init_args.kp = 0.5f;
    ControlV.pid_init_args.ki = 2.0f;
    ControlV.pid_init_args.kd = 0.0f;
    ControlV.pid_init(control_work);
    ControlV.pid_set_output_limits_args.p = &p;
    ControlV.pid_set_output_limits_args.lo = -10.0f;
    ControlV.pid_set_output_limits_args.hi = 10.0f;
    ControlV.pid_set_output_limits(control_work);

    float plant = 0.0f;
    const float setpoint = 3.0f;
    for (int i = 0; i < 2000; i++)
    {
        float u = pid_update(&p, setpoint, plant, 0.01f);
        plant += 0.05f * (u - plant); // first-order lag toward the command
    }
    close_to(setpoint, plant);

    // A pure proportional loop leaves the offset the I term removes.
    Pid pp;
    ControlV.pid_init_args.p = &pp;
    ControlV.pid_init_args.kp = 0.5f;
    ControlV.pid_init_args.ki = 0.0f;
    ControlV.pid_init_args.kd = 0.0f;
    ControlV.pid_init(control_work);
    ControlV.pid_set_output_limits_args.p = &pp;
    ControlV.pid_set_output_limits_args.lo = -10.0f;
    ControlV.pid_set_output_limits_args.hi = 10.0f;
    ControlV.pid_set_output_limits(control_work);
    float plant_p = 0.0f;
    for (int i = 0; i < 2000; i++)
    {
        float u = pid_update(&pp, setpoint, plant_p, 0.01f);
        plant_p += 0.05f * (u - plant_p);
    }
    TEST_ASSERT_TRUE(plant_p < setpoint);
    TEST_ASSERT_TRUE(plant_p > 0.0f);
}
