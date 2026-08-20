// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file control.h
 * @brief Closed-loop control law (PROTOCORE_ENABLE_CONTROL) - a zero-heap PID controller plus a
 *        handful of inline control-law primitives, for driving an actuator toward a setpoint.
 *
 * The PID is the textbook parallel form with the corrections that matter on real hardware:
 *   - derivative-on-measurement (the derivative acts on the measurement, not the error, so a
 *     step change in the setpoint does not produce a "derivative kick"),
 *   - an optional single-pole low-pass on the derivative (measurement noise otherwise dominates
 *     the D term),
 *   - output clamping to the actuator's range, and
 *   - anti-windup by conditional integration (the integrator is frozen while the output is
 *     saturated and integrating would push it deeper into the rail, so it never winds up past
 *     what the actuator can deliver), plus a hard integral clamp as a secondary bound,
 *   - a feed-forward term (kff * setpoint) for the part of the command known in advance.
 *
 * pid_update() is defined inline (below): the whole law folds into the caller with no function-
 * call overhead, and on the ESP32 / ESP32-S3 the single-precision-float math maps to the FPU
 * (single-cycle add/mul + `madd.s` fused multiply-add, never the soft-float double path). Put your
 * control loop in IRAM if you need deterministic latency free of flash-cache stalls.
 *
 * Pure float arithmetic - no heap, no <math.h>. Pair it with a plant it can command (a CiA 402
 * drive via `services/cia402`, a dshot ESC, a heater PWM) and a sensor it can read back.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CONTROL_H
#define PROTOCORE_CONTROL_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_CONTROL

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

#define CONTROL_UNBOUNDED 1e30f ///< sentinel for "no clamp" (well outside any real actuator range)

// PID-run log for offline tuning with tools/pid_tune.py, in two interchangeable formats:
//
//  1. CSV (human / serial friendly) - one row per control step, these columns:
#define CONTROL_LOG_HEADER "t_s,setpoint,measurement,output,dt_s"

//
//  2. Dense binary (little-endian) for high-rate loops - self-describing (the header carries the
//     gains, limits, and sample period the run used, so the tuner needs no flags) and 16 B/sample:
//       header (PID_LOG_HEADER_LEN): "DPID" | ver u8=1 | flags u8 | reserved u16 |
//         dt_s f32 | kp f32 | ki f32 | kd f32 | kff f32 | out_min f32 | out_max f32
//       record (PID_LOG_RECORD_LEN): setpoint f32 | measurement f32 | output f32 |
//         status u32 (bit 0 = output was saturated this step, so the tuner can drop it from the
//         plant identification fit).
#define PID_LOG_MAGIC "DPID"
#define PID_LOG_VERSION 1
#define PID_LOG_HEADER_LEN 36
#define PID_LOG_RECORD_LEN 16
#define PID_LOG_STATUS_SATURATED 0x1u

/**
 * @brief A single-loop PID controller. Zero its bytes or call pid_init() before first use; the
 *        runtime-state fields below the gains are owned by pid_update() / pid_reset().
 */
typedef struct
{
    // gains
    float kp;  ///< proportional gain
    float ki;  ///< integral gain (per second)
    float kd;  ///< derivative gain (seconds)
    float kff; ///< feed-forward gain applied to the setpoint
    // limits
    float out_min;   ///< output lower clamp
    float out_max;   ///< output upper clamp
    float integ_min; ///< integral accumulator lower clamp
    float integ_max; ///< integral accumulator upper clamp
    float d_alpha;   ///< derivative low-pass smoothing in [0,1); 0 = raw (unfiltered) derivative
    // fixed-rate cache (set by pid_set_rate; lets pid_update_fixed() run with no divide)
    float dt;     ///< cached sample period, 0 until pid_set_rate()
    float inv_dt; ///< cached 1/dt
    // runtime state (owned by pid_update / pid_reset)
    float integ;       ///< integral accumulator
    float prev_meas;   ///< previous measurement (for derivative-on-measurement)
    float d_filt;      ///< filtered derivative
    proto_bool primed; ///< false until the first update supplies prev_meas (no derivative on step 1)
} Pid;

/// Clamp @p v to [lo, hi].
static inline float control_clamp(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

/// Deadband: return 0 within +/- @p band, else @p v shifted toward 0 by @p band (continuous).
static inline float control_deadband(float v, float band)
{
    if (v > band)
    {
        return v - band;
    }
    if (v < -band)
    {
        return v + band;
    }
    return 0.0f;
}

/// Slew-rate limit: move @p current toward @p target by at most @p max_step this call.
static inline float control_slew(float target, float current, float max_step)
{
    float d = target - current;
    if (d > max_step)
    {
        return current + max_step;
    }
    if (d < -max_step)
    {
        return current - max_step;
    }
    return target;
}

/// One step of a single-pole low-pass: blend @p sample into @p prev by @p alpha in [0,1].
static inline float control_lpf(float prev, float sample, float alpha)
{
    return prev + alpha * (sample - prev);
}

/// Internal shared step, used by pid_update() and pid_update_fixed(): the whole control law with
/// @p dt and its reciprocal @p inv_dt supplied, so there is no divide inside. Call an entry point
/// below, not this directly.
static inline float pid_step_(Pid *p, float setpoint, float measurement, float dt, float inv_dt)
{
    float error = setpoint - measurement;

    // Derivative on measurement (no setpoint-change "kick"): d(error)/dt = -d(measurement)/dt when
    // the setpoint is held. Skip the first update (no prev_meas yet), optionally low-pass filter it.
    float deriv = 0.0f;
    if (p->primed)
    {
        deriv = -(measurement - p->prev_meas) * inv_dt; // multiply by 1/dt, no divide
        p->d_filt = (p->d_alpha > 0.0f) ? p->d_filt + p->d_alpha * (deriv - p->d_filt) : deriv;
    }
    p->prev_meas = measurement;
    p->primed = PROTO_TRUE;

    // Tentative integration, hard-clamped to the accumulator bounds (a secondary safety limit).
    float integ_next = control_clamp(p->integ + p->ki * error * dt, p->integ_min, p->integ_max);

    // FMA chain -> madd.s on the FPU: kp*error + integ + kd*d_filt + kff*setpoint.
    float unclamped = p->kp * error + integ_next + p->kd * p->d_filt + p->kff * setpoint;
    float out = control_clamp(unclamped, p->out_min, p->out_max);

    // Anti-windup by conditional integration: commit the new integral unless the output is
    // saturated AND integrating further this direction would push it deeper into the rail - then
    // freeze the accumulator instead, so it never winds up past what the actuator can deliver.
    proto_bool worsen_high = (unclamped > p->out_max) && (error > 0.0f);
    proto_bool worsen_low = (unclamped < p->out_min) && (error < 0.0f);
    if (!worsen_high && !worsen_low)
    {
        p->integ = integ_next;
    }

    return out;
}

/**
 * @brief Advance the loop one step: returns the (clamped) control output for @p setpoint given the
 *        measured process value @p measurement over the elapsed time @p dt seconds (dt <= 0 -> 0).
 *
 * Inline (no call overhead) and FPU-accelerated (single-precision, FMA-folded). Variable-rate: it
 * computes 1/dt each call. For a fixed-rate loop use pid_update_fixed() to skip that divide.
 */
static inline float pid_update(Pid *p, float setpoint, float measurement, float dt)
{
    if (!p || dt <= 0.0f)
    {
        return 0.0f;
    }
    return pid_step_(p, setpoint, measurement, dt, 1.0f / dt);
}

/**
 * @brief Zero-divide fixed-rate step: same law as pid_update() but uses the dt / 1-over-dt cached
 *        by pid_set_rate(), so the hot path is all multiplies (the fastest form). Returns 0 until
 *        pid_set_rate() has supplied a positive dt.
 */
static inline float pid_update_fixed(Pid *p, float setpoint, float measurement)
{
    if (!p || p->dt <= 0.0f)
    {
        return 0.0f;
    }
    return pid_step_(p, setpoint, measurement, p->dt, p->inv_dt);
}

/** @brief What pid_init takes: p, kp, ki, kd. */
typedef struct
{
    Pid *p;
    float kp;
    float ki;
    float kd;
} ControlPidInitArgs;

/** @brief What pid_set_output_limits takes: p, lo, hi. */
typedef struct
{
    Pid *p;
    float lo;
    float hi;
} ControlPidSetOutputLimitsArgs;

/** @brief What pid_set_integral_limits takes: p, lo, hi. */
typedef struct
{
    Pid *p;
    float lo;
    float hi;
} ControlPidSetIntegralLimitsArgs;

/** @brief What pid_set_derivative_filter takes: p, alpha. */
typedef struct
{
    Pid *p;
    float alpha;
} ControlPidSetDerivativeFilterArgs;

/** @brief What pid_set_feedforward takes: p, kff. */
typedef struct
{
    Pid *p;
    float kff;
} ControlPidSetFeedforwardArgs;

/** @brief What pid_set_rate takes: p, dt. */
typedef struct
{
    Pid *p;
    float dt;
} ControlPidSetRateArgs;

/** @brief What pid_reset takes: p. */
typedef struct
{
    Pid *p;
} ControlPidResetArgs;

/** @brief What pid_update_n takes: p, setpoint, measurement, dt, out, ... */
typedef struct
{
    Pid *p;
    const float *setpoint;
    const float *measurement;
    float dt;
    float *out;
    uint8_t n;
} ControlPidUpdateNArgs;

/** @brief What pid_log_header takes: buf, cap, p, dt. */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    const Pid *p;
    float dt;
} ControlPidLogHeaderArgs;

/** @brief What pid_log_record takes: buf, cap, setpoint, measurement, ... */
typedef struct
{
    uint8_t *buf;
    size_t cap;
    float setpoint;
    float measurement;
    float output;
    proto_bool saturated;
} ControlPidLogRecordArgs;

/**
 * @brief Closed-loop control law (PROTOCORE_ENABLE_CONTROL) - a zero-heap PID controller plus a handful of inline
 * control-law primitives, for driving an actuator toward a setpoint.
 *
 * A caller sets the members a call takes, invokes it through ::Control with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Control.pid_init_args.p = ...;
 *   Control.pid_init_args.kp = ...;
 *   Control.pid_init_args.ki = ...;
 *   Control.pid_init_args.kd = ...;
 *   Control.pid_init(work);
 *
 * @var ControlNs::pid_init_args  what pid_init takes: p, kp, ki, kd
 * @var ControlNs::pid_set_output_limits_args  what pid_set_output_limits takes: p, lo, hi
 * @var ControlNs::pid_set_integral_limits_args  what pid_set_integral_limits takes: p, lo, hi
 * @var ControlNs::pid_set_derivative_filter_args  what pid_set_derivative_filter takes: p, alpha
 * @var ControlNs::pid_set_feedforward_args  what pid_set_feedforward takes: p, kff
 * @var ControlNs::pid_set_rate_args  what pid_set_rate takes: p, dt
 * @var ControlNs::pid_reset_args  what pid_reset takes: p
 * @var ControlNs::pid_update_n_args  what pid_update_n takes: p, setpoint, measurement, dt, out,
 * @var ControlNs::pid_log_header_args  what pid_log_header takes: buf, cap, p, dt
 * @var ControlNs::pid_log_record_args  what pid_log_record takes: buf, cap, setpoint, measurement,
 * @var ControlNs::ok  a call's true/false outcome
 * @var ControlNs::n  the count a call reports
 * @var ControlNs::pid_init  pid_init
 * @var ControlNs::pid_set_output_limits  pid_set_output_limits
 * @var ControlNs::pid_set_integral_limits  pid_set_integral_limits
 * @var ControlNs::pid_set_derivative_filter  pid_set_derivative_filter
 * @var ControlNs::pid_set_feedforward  pid_set_feedforward
 * @var ControlNs::pid_set_rate  pid_set_rate
 * @var ControlNs::pid_reset  pid_reset
 * @var ControlNs::pid_update_n  pid_update_n
 * @var ControlNs::pid_log_header  pid_log_header
 * @var ControlNs::pid_log_record  pid_log_record
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    ControlPidInitArgs pid_init_args;
    ControlPidSetOutputLimitsArgs pid_set_output_limits_args;
    ControlPidSetIntegralLimitsArgs pid_set_integral_limits_args;
    ControlPidSetDerivativeFilterArgs pid_set_derivative_filter_args;
    ControlPidSetFeedforwardArgs pid_set_feedforward_args;
    ControlPidSetRateArgs pid_set_rate_args;
    ControlPidResetArgs pid_reset_args;
    ControlPidUpdateNArgs pid_update_n_args;
    ControlPidLogHeaderArgs pid_log_header_args;
    ControlPidLogRecordArgs pid_log_record_args;

    proto_bool ok;
    size_t n;

    void (*const pid_init)(uint8_t *restrict work);
    void (*const pid_set_output_limits)(uint8_t *restrict work);
    void (*const pid_set_integral_limits)(uint8_t *restrict work);
    void (*const pid_set_derivative_filter)(uint8_t *restrict work);
    void (*const pid_set_feedforward)(uint8_t *restrict work);
    void (*const pid_set_rate)(uint8_t *restrict work);
    void (*const pid_reset)(uint8_t *restrict work);
    void (*const pid_update_n)(uint8_t *restrict work);
    void (*const pid_log_header)(uint8_t *restrict work);
    void (*const pid_log_record)(uint8_t *restrict work);
} ControlNs;

/** @brief The one symbol this module exports. */
extern ControlNs Control;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CONTROL

#endif // PROTOCORE_CONTROL_H
