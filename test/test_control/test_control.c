// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Unit tests for the PID control law (services/system/control): P / I / D terms, output clamping,
// anti-windup (conditional integration), derivative-on-measurement, feed-forward, the batched
// update, and the inline control-law primitives. Pure host tests.

#include "services/system/control/control.h"
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <unity.h>

void setUp()
{
}
void tearDown()
{
}

static bool near_f(float a, float b)
{
    return fabsf(a - b) < 1e-4f;
}

// --- little-endian readers for the dense-binary PID log format (mirror the packers in control.cpp) ---
static uint16_t rd_u16le(const uint8_t *b)
{
    return (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
}
static uint32_t rd_u32le(const uint8_t *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static float rd_f32le(const uint8_t *b)
{
    uint32_t u = rd_u32le(b);
    float f = 0.0f;
    memcpy(&f, &u, 4);
    return f;
}

void test_proportional_only()
{
    Pid p;
    pid_init(&p, 2.0f, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(near_f(pid_update(&p, 10.0f, 0.0f, 0.1f), 20.0f)); // kp*error = 2*10
}

void test_integral_accumulates()
{
    Pid p;
    pid_init(&p, 1.0f, 10.0f, 0.0f);
    TEST_ASSERT_TRUE(near_f(pid_update(&p, 1.0f, 0.0f, 0.1f), 2.0f)); // P=1, I=10*1*0.1=1
    TEST_ASSERT_TRUE(near_f(pid_update(&p, 1.0f, 0.0f, 0.1f), 3.0f)); // P=1, I=2
}

void test_feedforward()
{
    Pid p;
    pid_init(&p, 0.0f, 0.0f, 0.0f);
    pid_set_feedforward(&p, 0.5f);
    TEST_ASSERT_TRUE(near_f(pid_update(&p, 8.0f, 0.0f, 0.1f), 4.0f)); // kff*setpoint = 0.5*8
}

void test_output_clamp_and_antiwindup()
{
    Pid p;
    pid_init(&p, 1.0f, 10.0f, 0.0f);
    pid_set_output_limits(&p, 0.0f, 5.0f);
    TEST_ASSERT_TRUE(near_f(pid_update(&p, 100.0f, 0.0f, 0.1f), 5.0f)); // saturates high
    float integ1 = p.integ;
    TEST_ASSERT_TRUE(near_f(pid_update(&p, 100.0f, 0.0f, 0.1f), 5.0f));
    TEST_ASSERT_TRUE(p.integ == integ1);     // integrator frozen while saturated (no windup)
    TEST_ASSERT_TRUE(fabsf(p.integ) < 1.0f); // did not accumulate
}

void test_antiwindup_recovers()
{
    // Once the error reverses, the (un-wound) integrator resumes normally.
    Pid p;
    pid_init(&p, 1.0f, 10.0f, 0.0f);
    pid_set_output_limits(&p, 0.0f, 5.0f);
    pid_update(&p, 100.0f, 0.0f, 0.1f);            // saturate, integrator frozen at 0
    float out = pid_update(&p, 0.0f, 10.0f, 0.1f); // error now -10, should be able to leave the rail
    TEST_ASSERT_TRUE(out < 5.0f);
}

void test_derivative_on_measurement()
{
    Pid p;
    pid_init(&p, 0.0f, 0.0f, 1.0f);
    TEST_ASSERT_TRUE(near_f(pid_update(&p, 0.0f, 0.0f, 0.1f), 0.0f));   // first step primes, no D
    TEST_ASSERT_TRUE(near_f(pid_update(&p, 0.0f, 1.0f, 0.1f), -10.0f)); // d=-(1-0)/0.1
}

void test_setpoint_step_no_kick()
{
    // A setpoint step must NOT produce a derivative kick (D acts on measurement only).
    Pid p;
    pid_init(&p, 0.0f, 0.0f, 5.0f);
    pid_update(&p, 0.0f, 0.0f, 0.1f);                                  // prime
    TEST_ASSERT_TRUE(near_f(pid_update(&p, 99.0f, 0.0f, 0.1f), 0.0f)); // setpoint jumped, meas flat -> D=0
}

void test_derivative_filter()
{
    Pid p;
    pid_init(&p, 0.0f, 0.0f, 1.0f);
    pid_set_derivative_filter(&p, 0.5f); // EMA alpha
    pid_update(&p, 0.0f, 0.0f, 0.1f);    // prime, d_filt=0
    // raw d = -(1-0)/0.1 = -10; filtered = 0 + 0.5*(-10 - 0) = -5
    TEST_ASSERT_TRUE(near_f(pid_update(&p, 0.0f, 1.0f, 0.1f), -5.0f));
}

void test_reset_and_guards()
{
    Pid p;
    pid_init(&p, 1.0f, 10.0f, 0.0f);
    pid_update(&p, 5.0f, 0.0f, 0.1f);
    TEST_ASSERT_TRUE(p.integ != 0.0f);
    pid_reset(&p);
    TEST_ASSERT_TRUE(p.integ == 0.0f);
    TEST_ASSERT_FALSE(p.primed);
    TEST_ASSERT_TRUE(near_f(pid_update(&p, 1.0f, 0.0f, 0.0f), 0.0f)); // dt<=0 returns 0
    TEST_ASSERT_TRUE(near_f(pid_update(NULL, 1.0f, 0.0f, 0.1f), 0.0f));
}

void test_batched_update()
{
    Pid axes[2];
    pid_init(&axes[0], 1.0f, 0.0f, 0.0f);
    pid_init(&axes[1], 3.0f, 0.0f, 0.0f);
    float sp[2] = {5.0f, 2.0f};
    float meas[2] = {0.0f, 0.0f};
    float out[2] = {0.0f, 0.0f};
    pid_update_n(axes, sp, meas, 0.1f, out, 2);
    TEST_ASSERT_TRUE(near_f(out[0], 5.0f));
    TEST_ASSERT_TRUE(near_f(out[1], 6.0f));
}

void test_fixed_rate_matches()
{
    // pid_update_fixed(sp, meas) must equal pid_update(sp, meas, dt) once pid_set_rate caches dt.
    Pid a;
    Pid b;
    pid_init(&a, 1.5f, 4.0f, 0.05f);
    pid_init(&b, 1.5f, 4.0f, 0.05f);
    pid_set_derivative_filter(&b, 0.2f);
    pid_set_derivative_filter(&a, 0.2f);
    pid_set_rate(&b, 0.01f);
    for (int i = 0; i < 25; i++)
    {
        float sp = (i < 5) ? 0.0f : 10.0f;
        float meas = (float)i * 0.37f;
        float va = pid_update(&a, sp, meas, 0.01f);
        float vb = pid_update_fixed(&b, sp, meas);
        TEST_ASSERT_TRUE(near_f(va, vb));
    }
    // no rate cached -> returns 0 (fails closed)
    Pid c;
    pid_init(&c, 1.0f, 0.0f, 0.0f);
    TEST_ASSERT_TRUE(near_f(pid_update_fixed(&c, 1.0f, 0.0f), 0.0f));
}

void test_control_primitives()
{
    TEST_ASSERT_TRUE(near_f(control_clamp(7.0f, 0.0f, 5.0f), 5.0f));
    TEST_ASSERT_TRUE(near_f(control_clamp(-1.0f, 0.0f, 5.0f), 0.0f));
    TEST_ASSERT_TRUE(near_f(control_deadband(0.3f, 0.5f), 0.0f));
    TEST_ASSERT_TRUE(near_f(control_deadband(1.5f, 0.5f), 1.0f));
    TEST_ASSERT_TRUE(near_f(control_deadband(-1.5f, 0.5f), -1.0f));
    TEST_ASSERT_TRUE(near_f(control_slew(10.0f, 0.0f, 2.0f), 2.0f));
    TEST_ASSERT_TRUE(near_f(control_slew(1.0f, 0.0f, 2.0f), 1.0f)); // within step -> reach target
    TEST_ASSERT_TRUE(near_f(control_lpf(0.0f, 10.0f, 0.25f), 2.5f));
}

// Every setter / reset must no-op on a null Pid (the false side of each guard) without crashing,
// and pid_set_rate must ignore a non-positive dt (leaving no rate cached).
void test_setter_null_guards()
{
    pid_init(NULL, 1.0f, 2.0f, 3.0f);           // pid_init early return
    pid_set_rate(NULL, 0.01f);                  // p false
    pid_set_output_limits(NULL, 0.0f, 5.0f);    // p false
    pid_set_integral_limits(NULL, -1.0f, 1.0f); // p false
    pid_set_derivative_filter(NULL, 0.5f);      // p false
    pid_set_feedforward(NULL, 0.5f);            // p false
    pid_reset(NULL);                            // early return

    Pid p;
    pid_init(&p, 1.0f, 0.0f, 0.0f);
    pid_set_rate(&p, 0.0f);  // dt not > 0: does not cache
    pid_set_rate(&p, -1.0f); // dt not > 0 again
    TEST_ASSERT_TRUE(p.dt == 0.0f);
    TEST_ASSERT_TRUE(near_f(pid_update_fixed(&p, 1.0f, 0.0f), 0.0f)); // no rate cached -> fails closed
}

// pid_set_integral_limits clamps the accumulator (the secondary anti-windup bound): with a tight
// integral cap and unbounded output, the integrator saturates at the cap and no higher.
void test_integral_limits_take_effect()
{
    Pid p;
    pid_init(&p, 1.0f, 10.0f, 0.0f);
    pid_set_integral_limits(&p, -0.5f, 0.5f);
    TEST_ASSERT_TRUE(near_f(p.integ_min, -0.5f)); // setter wrote the fields
    TEST_ASSERT_TRUE(near_f(p.integ_max, 0.5f));
    pid_update(&p, 1.0f, 0.0f, 0.1f); // ki*error*dt = 10*1*0.1 = 1.0, clamped to 0.5
    TEST_ASSERT_TRUE(near_f(p.integ, 0.5f));
    pid_update(&p, 1.0f, 0.0f, 0.1f);
    TEST_ASSERT_TRUE(near_f(p.integ, 0.5f)); // stays at the cap
}

// The batched update rejects a null pointer for any of its four arrays (the guard's OR arms),
// leaving the output buffer untouched, then runs normally when all four are valid.
void test_pid_update_n_null_guards()
{
    Pid axes[2];
    pid_init(&axes[0], 1.0f, 0.0f, 0.0f);
    pid_init(&axes[1], 1.0f, 0.0f, 0.0f);
    float sp[2] = {5.0f, 5.0f};
    float meas[2] = {0.0f, 0.0f};
    float out[2] = {123.0f, 123.0f};
    pid_update_n(NULL, sp, meas, 0.1f, out, 2); // !p
    TEST_ASSERT_TRUE(out[0] == 123.0f);
    pid_update_n(axes, NULL, meas, 0.1f, out, 2); // !setpoint
    TEST_ASSERT_TRUE(out[0] == 123.0f);
    pid_update_n(axes, sp, NULL, 0.1f, out, 2); // !measurement
    TEST_ASSERT_TRUE(out[0] == 123.0f);
    pid_update_n(axes, sp, meas, 0.1f, NULL, 2); // !out
    TEST_ASSERT_TRUE(out[0] == 123.0f);
    pid_update_n(axes, sp, meas, 0.1f, out, 2); // all valid -> writes
    TEST_ASSERT_TRUE(near_f(out[0], 5.0f));
    TEST_ASSERT_TRUE(near_f(out[1], 5.0f));
}

// pid_log_header emits the self-describing 36-octet dense-binary header: magic, version, flags,
// reserved, then dt / kp / ki / kd / kff / out_min / out_max as little-endian f32. Byte-exact.
void test_pid_log_header_bytes()
{
    Pid p;
    pid_init(&p, 1.5f, 2.5f, 0.75f);
    pid_set_feedforward(&p, 0.25f);
    pid_set_output_limits(&p, -5.0f, 5.0f);
    uint8_t buf[PID_LOG_HEADER_LEN];
    size_t n = pid_log_header(buf, sizeof(buf), &p, 0.01f);
    TEST_ASSERT_EQUAL_size_t(PID_LOG_HEADER_LEN, n);
    TEST_ASSERT_EQUAL_HEX8('D', buf[0]);
    TEST_ASSERT_EQUAL_HEX8('P', buf[1]);
    TEST_ASSERT_EQUAL_HEX8('I', buf[2]);
    TEST_ASSERT_EQUAL_HEX8('D', buf[3]);
    TEST_ASSERT_EQUAL_HEX8(PID_LOG_VERSION, buf[4]);
    TEST_ASSERT_EQUAL_HEX8(0, buf[5]);                   // flags (reserved)
    TEST_ASSERT_EQUAL_HEX16(0, rd_u16le(buf + 6));       // reserved u16
    TEST_ASSERT_TRUE(near_f(rd_f32le(buf + 8), 0.01f));  // dt
    TEST_ASSERT_TRUE(near_f(rd_f32le(buf + 12), 1.5f));  // kp
    TEST_ASSERT_TRUE(near_f(rd_f32le(buf + 16), 2.5f));  // ki
    TEST_ASSERT_TRUE(near_f(rd_f32le(buf + 20), 0.75f)); // kd
    TEST_ASSERT_TRUE(near_f(rd_f32le(buf + 24), 0.25f)); // kff
    TEST_ASSERT_TRUE(near_f(rd_f32le(buf + 28), -5.0f)); // out_min
    TEST_ASSERT_TRUE(near_f(rd_f32le(buf + 32), 5.0f));  // out_max
    // guards: null buf, null Pid, capacity too small -> 0.
    TEST_ASSERT_EQUAL_size_t(0, pid_log_header(NULL, sizeof(buf), &p, 0.01f));
    TEST_ASSERT_EQUAL_size_t(0, pid_log_header(buf, sizeof(buf), NULL, 0.01f));
    TEST_ASSERT_EQUAL_size_t(0, pid_log_header(buf, PID_LOG_HEADER_LEN - 1, &p, 0.01f));
}

// pid_log_record emits the 16-octet record: setpoint / measurement / output f32 + a u32 status
// word carrying the SATURATED bit. Byte-check both the saturated and un-saturated status.
void test_pid_log_record_bytes()
{
    uint8_t buf[PID_LOG_RECORD_LEN];
    size_t n = pid_log_record(buf, sizeof(buf), 3.0f, 2.0f, 4.5f, true);
    TEST_ASSERT_EQUAL_size_t(PID_LOG_RECORD_LEN, n);
    TEST_ASSERT_TRUE(near_f(rd_f32le(buf + 0), 3.0f)); // setpoint
    TEST_ASSERT_TRUE(near_f(rd_f32le(buf + 4), 2.0f)); // measurement
    TEST_ASSERT_TRUE(near_f(rd_f32le(buf + 8), 4.5f)); // output
    TEST_ASSERT_EQUAL_HEX32(PID_LOG_STATUS_SATURATED, rd_u32le(buf + 12));
    n = pid_log_record(buf, sizeof(buf), 0.0f, 0.0f, 0.0f, false);
    TEST_ASSERT_EQUAL_size_t(PID_LOG_RECORD_LEN, n);
    TEST_ASSERT_EQUAL_HEX32(0u, rd_u32le(buf + 12)); // status clear when not saturated
    // guards: null buf, capacity too small -> 0.
    TEST_ASSERT_EQUAL_size_t(0, pid_log_record(NULL, sizeof(buf), 0.0f, 0.0f, 0.0f, false));
    TEST_ASSERT_EQUAL_size_t(0, pid_log_record(buf, PID_LOG_RECORD_LEN - 1, 0.0f, 0.0f, 0.0f, false));
}

// control_slew clamps a falling step too, not just a rising one, and pid_update_fixed rejects a
// null Pid before it dereferences one (the short-circuit arm the dt<=0 guard never reaches).
void test_slew_down_and_fixed_null()
{
    TEST_ASSERT_TRUE(near_f(control_slew(-10.0f, 0.0f, 2.0f), -2.0f)); // falling, clamped to -step
    TEST_ASSERT_TRUE(near_f(control_slew(-1.0f, 0.0f, 2.0f), -1.0f));  // within step -> reach target
    TEST_ASSERT_TRUE(near_f(pid_update_fixed(NULL, 1.0f, 0.0f), 0.0f));
}

// Anti-windup freezes the accumulator only when the error would drive the output DEEPER into the
// rail. Saturated with the error already reversed, integration must resume - that is the
// (saturated, error-of-the-opposite-sign) arm of each worsen_* test, which unwinds the integral.
void test_antiwindup_integrates_while_saturated_reversing()
{
    // Anti-windup itself stops the integral growing once the output rails, so build the integral
    // with the limits wide open and THEN narrow them - that reaches "saturated high" holding an
    // integral big enough to stay saturated even as the error reverses.
    Pid p;
    pid_init(&p, 1.0f, 1.0f, 0.0f);
    pid_set_output_limits(&p, -100.0f, 100.0f);
    for (int i = 0; i < 50; ++i)
    {
        (void)pid_update(&p, 1.0f, 0.0f, 0.1f); // integ -> ~5, never saturated
    }
    pid_set_output_limits(&p, -1.0f, 1.0f);
    float integ_hi = p.integ;
    TEST_ASSERT_TRUE(integ_hi > 1.0f);
    float out = pid_update(&p, 0.0f, 0.5f, 0.1f); // error = -0.5, unclamped ~4.45 still > out_max
    TEST_ASSERT_TRUE(near_f(out, 1.0f));
    TEST_ASSERT_TRUE(p.integ < integ_hi); // unwound, not frozen

    // Low rail, positive error: the mirror image.
    Pid q;
    pid_init(&q, 1.0f, 1.0f, 0.0f);
    pid_set_output_limits(&q, -100.0f, 100.0f);
    for (int i = 0; i < 50; ++i)
    {
        (void)pid_update(&q, 0.0f, 1.0f, 0.1f); // integ -> ~-5
    }
    pid_set_output_limits(&q, -1.0f, 1.0f);
    float integ_lo = q.integ;
    TEST_ASSERT_TRUE(integ_lo < -1.0f);
    out = pid_update(&q, 0.5f, 0.0f, 0.1f); // error = +0.5, unclamped ~-4.45 still < out_min
    TEST_ASSERT_TRUE(near_f(out, -1.0f));
    TEST_ASSERT_TRUE(q.integ > integ_lo); // unwound, not frozen
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_proportional_only);
    RUN_TEST(test_integral_accumulates);
    RUN_TEST(test_feedforward);
    RUN_TEST(test_output_clamp_and_antiwindup);
    RUN_TEST(test_antiwindup_recovers);
    RUN_TEST(test_derivative_on_measurement);
    RUN_TEST(test_setpoint_step_no_kick);
    RUN_TEST(test_derivative_filter);
    RUN_TEST(test_reset_and_guards);
    RUN_TEST(test_batched_update);
    RUN_TEST(test_fixed_rate_matches);
    RUN_TEST(test_control_primitives);
    RUN_TEST(test_setter_null_guards);
    RUN_TEST(test_integral_limits_take_effect);
    RUN_TEST(test_pid_update_n_null_guards);
    RUN_TEST(test_pid_log_header_bytes);
    RUN_TEST(test_pid_log_record_bytes);
    RUN_TEST(test_slew_down_and_fixed_null);
    RUN_TEST(test_antiwindup_integrates_while_saturated_reversing);
    return UNITY_END();
}
