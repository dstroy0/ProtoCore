// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file control.c
 * @brief PID control law (pure single-precision float; FPU-accelerated on ESP32 / ESP32-S3).
 */

#include "services/system/control/control.h"

#if PC_ENABLE_CONTROL

#include <string.h>

// clamp helper is control_clamp() from control.h (inline) - reused here, not re-declared.

void pid_init(Pid *p, float kp, float ki, float kd)
{
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
    pid_reset(p);
}

void pid_set_rate(Pid *p, float dt)
{
    if (p && dt > 0.0f)
    {
        p->dt = dt;
        p->inv_dt = 1.0f / dt;
    }
}

void pid_set_output_limits(Pid *p, float lo, float hi)
{
    if (p)
    {
        p->out_min = lo;
        p->out_max = hi;
    }
}

void pid_set_integral_limits(Pid *p, float lo, float hi)
{
    if (p)
    {
        p->integ_min = lo;
        p->integ_max = hi;
    }
}

void pid_set_derivative_filter(Pid *p, float alpha)
{
    if (p)
    {
        p->d_alpha = alpha;
    }
}

void pid_set_feedforward(Pid *p, float kff)
{
    if (p)
    {
        p->kff = kff;
    }
}

void pid_reset(Pid *p)
{
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

void pid_update_n(Pid *p, const float *setpoint, const float *measurement, float dt, float *out, uint8_t n)
{
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
    memcpy(&u, &v, 4); // reinterpret the IEEE-754 bits, then emit little-endian
    return put_u32le(p, u);
}

size_t pid_log_header(uint8_t *buf, size_t cap, const Pid *p, float dt)
{
    if (!buf || !p || cap < PID_LOG_HEADER_LEN)
    {
        return 0;
    }
    size_t o = 0;
    memcpy(buf, PID_LOG_MAGIC, 4);
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
    return o; // == PID_LOG_HEADER_LEN
}

size_t pid_log_record(uint8_t *buf, size_t cap, float setpoint, float measurement, float output, proto_bool saturated)
{
    if (!buf || cap < PID_LOG_RECORD_LEN)
    {
        return 0;
    }
    size_t o = 0;
    o += put_f32le(buf + o, setpoint);
    o += put_f32le(buf + o, measurement);
    o += put_f32le(buf + o, output);
    o += put_u32le(buf + o, saturated ? PID_LOG_STATUS_SATURATED : 0u);
    return o; // == PID_LOG_RECORD_LEN
}

#endif // PC_ENABLE_CONTROL
