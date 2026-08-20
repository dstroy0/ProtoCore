// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file control.c
 * @brief PID control law (pure single-precision float; FPU-accelerated on ESP32 / ESP32-S3).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_CONTROL

#include "mmgr/protomem/protomem.h"
#include "services/system/control/control.h"

PROTOCORE_BEGIN_DECLS

// clamp helper is control_clamp() from control.h (inline) - reused here, not re-declared.

// The entries this file calls before reaching their definitions.
// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void control_pid_reset(uint8_t *restrict work);

static void control_pid_init(uint8_t *restrict work)
{
    Pid *p = Control.pid_init_args.p;
    float kp = Control.pid_init_args.kp;
    float ki = Control.pid_init_args.ki;
    float kd = Control.pid_init_args.kd;

    if (!p)
    {
        return;
    }
    p->kp = kp;
    p->ki = ki;
    p->kd = kd;
    p->kff = 0.0f;
    p->out_min = -CONTROL_UNBOUNDED;
    p->out_max = CONTROL_UNBOUNDED;
    p->integ_min = -CONTROL_UNBOUNDED;
    p->integ_max = CONTROL_UNBOUNDED;
    p->d_alpha = 0.0f;
    p->dt = 0.0f;
    p->inv_dt = 0.0f;
    Control.pid_reset_args.p = p;
    control_pid_reset(work);
}

static void control_pid_set_rate(uint8_t *restrict work)
{
    (void)work;
    Pid *p = Control.pid_set_rate_args.p;
    float dt = Control.pid_set_rate_args.dt;

    if (p && dt > 0.0f)
    {
        p->dt = dt;
        p->inv_dt = 1.0f / dt;
    }
}

static void control_pid_set_output_limits(uint8_t *restrict work)
{
    (void)work;
    Pid *p = Control.pid_set_output_limits_args.p;
    float lo = Control.pid_set_output_limits_args.lo;
    float hi = Control.pid_set_output_limits_args.hi;

    if (p)
    {
        p->out_min = lo;
        p->out_max = hi;
    }
}

static void control_pid_set_integral_limits(uint8_t *restrict work)
{
    (void)work;
    Pid *p = Control.pid_set_integral_limits_args.p;
    float lo = Control.pid_set_integral_limits_args.lo;
    float hi = Control.pid_set_integral_limits_args.hi;

    if (p)
    {
        p->integ_min = lo;
        p->integ_max = hi;
    }
}

static void control_pid_set_derivative_filter(uint8_t *restrict work)
{
    (void)work;
    Pid *p = Control.pid_set_derivative_filter_args.p;
    float alpha = Control.pid_set_derivative_filter_args.alpha;

    if (p)
    {
        p->d_alpha = alpha;
    }
}

static void control_pid_set_feedforward(uint8_t *restrict work)
{
    (void)work;
    Pid *p = Control.pid_set_feedforward_args.p;
    float kff = Control.pid_set_feedforward_args.kff;

    if (p)
    {
        p->kff = kff;
    }
}

static void control_pid_reset(uint8_t *restrict work)
{
    (void)work;
    Pid *p = Control.pid_reset_args.p;

    if (!p)
    {
        return;
    }
    p->integ = 0.0f;
    p->prev_meas = 0.0f;
    p->d_filt = 0.0f;
    p->primed = PROTO_FALSE;
}

// pid_update() is defined inline in control.h (zero call overhead); this TU just uses it below.

static void control_pid_update_n(uint8_t *restrict work)
{
    (void)work;
    Pid *p = Control.pid_update_n_args.p;
    const float *setpoint = Control.pid_update_n_args.setpoint;
    const float *measurement = Control.pid_update_n_args.measurement;
    float dt = Control.pid_update_n_args.dt;
    float *out = Control.pid_update_n_args.out;
    uint8_t n = Control.pid_update_n_args.n;

    if (!p || !setpoint || !measurement || !out)
    {
        return;
    }
    for (uint8_t i = 0; i < n; i++)
    {
        out[i] = pid_update(&p[i], setpoint[i], measurement[i], dt);
    }
}

// --- dense-binary log packers (little-endian; see the PID_LOG_* format in control.h) ---

static size_t put_u16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
    return 2;
}

static size_t put_u32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
    return 4;
}

static size_t put_f32le(uint8_t *p, float v)
{
    uint32_t u;
    mem.cpy(&u, &v, 4); // reinterpret the IEEE-754 bits, then emit little-endian
    return put_u32le(p, u);
}

static void control_pid_log_header(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Control.pid_log_header_args.buf;
    size_t cap = Control.pid_log_header_args.cap;
    const Pid *p = Control.pid_log_header_args.p;
    float dt = Control.pid_log_header_args.dt;

    if (!buf || !p || cap < PID_LOG_HEADER_LEN)
    {
        Control.n = 0;
        return;
    }
    size_t o = 0;
    mem.cpy(buf, PID_LOG_MAGIC, 4);
    o += 4;
    buf[o++] = PID_LOG_VERSION;
    buf[o++] = 0; // flags (reserved)
    o += put_u16le(buf + o, 0);
    o += put_f32le(buf + o, dt);
    o += put_f32le(buf + o, p->kp);
    o += put_f32le(buf + o, p->ki);
    o += put_f32le(buf + o, p->kd);
    o += put_f32le(buf + o, p->kff);
    o += put_f32le(buf + o, p->out_min);
    o += put_f32le(buf + o, p->out_max);
    Control.n = o; // == PID_LOG_HEADER_LEN
}

static void control_pid_log_record(uint8_t *restrict work)
{
    (void)work;
    uint8_t *buf = Control.pid_log_record_args.buf;
    size_t cap = Control.pid_log_record_args.cap;
    float setpoint = Control.pid_log_record_args.setpoint;
    float measurement = Control.pid_log_record_args.measurement;
    float output = Control.pid_log_record_args.output;
    proto_bool saturated = Control.pid_log_record_args.saturated;

    if (!buf || cap < PID_LOG_RECORD_LEN)
    {
        Control.n = 0;
        return;
    }
    size_t o = 0;
    o += put_f32le(buf + o, setpoint);
    o += put_f32le(buf + o, measurement);
    o += put_f32le(buf + o, output);
    o += put_u32le(buf + o, saturated ? PID_LOG_STATUS_SATURATED : 0u);
    Control.n = o; // == PID_LOG_RECORD_LEN
}

ControlNs Control = {
    .pid_init = control_pid_init,
    .pid_set_output_limits = control_pid_set_output_limits,
    .pid_set_integral_limits = control_pid_set_integral_limits,
    .pid_set_derivative_filter = control_pid_set_derivative_filter,
    .pid_set_feedforward = control_pid_set_feedforward,
    .pid_set_rate = control_pid_set_rate,
    .pid_reset = control_pid_reset,
    .pid_update_n = control_pid_update_n,
    .pid_log_header = control_pid_log_header,
    .pid_log_record = control_pid_log_record,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CONTROL
