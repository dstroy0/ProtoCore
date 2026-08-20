// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the PID control law (services/system/control): a single
// pid_update() step (P + I + derivative-on-measurement with its optional single-pole low-pass +
// feed-forward + output clamp + anti-windup by conditional integration), the zero-divide
// pid_update_fixed() fast path used by fixed-rate loops, the batched pid_update_n() multi-axis
// update (several drives off one control tick), and the dense-binary PID log packers
// (pid_log_header() / pid_log_record()) used to stream tuning data to tools/pid_tune.py. All of
// this is pure single-precision float arithmetic over a plain struct - no heap, no I/O. Worked
// example for performance_benching/device/<service>/ pure compute: unlike performance_benching/device/modbus (a pure
// protocol codec) or performance_benching/device/ads1115 (a peripheral driver with the bus stubbed),
// services/system/control has no hardware or transport dependency to stub at all - this rig has no plant/sensor
// attached, so the "measurement" fed into each call below is a fixed stand-in value, not a real sample, but every call
// is otherwise the real production code path.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/control -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/system/control/control.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t control_work[16]; // the borrow an entry takes; Control never reads it

void dbench_run(void)
{
    // Variable-rate loop: pid_update() computes 1/dt itself each call.
    Pid p;
    ControlV.pid_init_args.p = &p;
    ControlV.pid_init_args.kp = 1.5f;
    ControlV.pid_init_args.ki = 4.0f;
    ControlV.pid_init_args.kd = 0.05f;
    Control.pid_init(control_work);
    ControlV.pid_set_output_limits_args.p = &p;
    ControlV.pid_set_output_limits_args.lo = -10.0f;
    ControlV.pid_set_output_limits_args.hi = 10.0f;
    Control.pid_set_output_limits(control_work);
    ControlV.pid_set_integral_limits_args.p = &p;
    ControlV.pid_set_integral_limits_args.lo = -5.0f;
    ControlV.pid_set_integral_limits_args.hi = 5.0f;
    Control.pid_set_integral_limits(control_work);
    ControlV.pid_set_derivative_filter_args.p = &p;
    ControlV.pid_set_derivative_filter_args.alpha = 0.2f;
    Control.pid_set_derivative_filter(control_work);
    ControlV.pid_set_feedforward_args.p = &p;
    ControlV.pid_set_feedforward_args.kff = 0.1f;
    Control.pid_set_feedforward(control_work);

    // Fixed-rate loop: pid_set_rate() caches dt/inv_dt once, so pid_update_fixed() is all
    // multiplies (no per-call divide).
    Pid pf;
    ControlV.pid_init_args.p = &pf;
    ControlV.pid_init_args.kp = 1.5f;
    ControlV.pid_init_args.ki = 4.0f;
    ControlV.pid_init_args.kd = 0.05f;
    Control.pid_init(control_work);
    ControlV.pid_set_output_limits_args.p = &pf;
    ControlV.pid_set_output_limits_args.lo = -10.0f;
    ControlV.pid_set_output_limits_args.hi = 10.0f;
    Control.pid_set_output_limits(control_work);
    ControlV.pid_set_derivative_filter_args.p = &pf;
    ControlV.pid_set_derivative_filter_args.alpha = 0.2f;
    Control.pid_set_derivative_filter(control_work);
    ControlV.pid_set_rate_args.p = &pf;
    ControlV.pid_set_rate_args.dt = 0.01f;
    Control.pid_set_rate(control_work);

    // Batched multi-axis update (a motion master driving 4 axes off one control tick).
    Pid axes[4];
    for (uint8_t i = 0; i < 4; i++)
    {
        ControlV.pid_init_args.p = &axes[i];
        ControlV.pid_init_args.kp = 1.0f + (float)i;
        ControlV.pid_init_args.ki = 0.0f;
        ControlV.pid_init_args.kd = 0.0f;
        Control.pid_init(control_work);
    }
    static const float sp4[4] = {5.0f, 2.0f, -3.0f, 1.0f};
    static const float meas4[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    static float out4[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    // Shared scratch buffer for the dense-binary log packers (36 B header, 16 B record).
    static uint8_t logbuf[PID_LOG_HEADER_LEN > PID_LOG_RECORD_LEN ? PID_LOG_HEADER_LEN : PID_LOG_RECORD_LEN];

    for (;;)
    {
        DBENCH_BANNER("control");
        volatile float sinkf = 0.0f;
        volatile size_t sinksz = 0;

        DBENCH_OP("pid_update", 100000, sinkf += pid_update(&p, 3.0f, 0.5f, 0.01f));
        DBENCH_OP("pid_update_fixed", 100000, sinkf += pid_update_fixed(&pf, 3.0f, 0.5f));
        DBENCH_OP("Control.pid_update_n x4", 50000, {
            ControlV.pid_update_n_args.p = axes;
            ControlV.pid_update_n_args.setpoint = sp4;
            ControlV.pid_update_n_args.measurement = meas4;
            ControlV.pid_update_n_args.dt = 0.01f;
            ControlV.pid_update_n_args.out = out4;
            ControlV.pid_update_n_args.n = 4;
            Control.pid_update_n(control_work);
        });
        DBENCH_OP("Control.pid_log_header", 100000, {
            ControlV.pid_log_header_args.buf = logbuf;
            ControlV.pid_log_header_args.cap = sizeof(logbuf);
            ControlV.pid_log_header_args.p = &p;
            ControlV.pid_log_header_args.dt = 0.01f;
            Control.pid_log_header(control_work);
            sinksz += ControlV.n;
        });
        DBENCH_OP("Control.pid_log_record", 100000, {
            ControlV.pid_log_record_args.buf = logbuf;
            ControlV.pid_log_record_args.cap = sizeof(logbuf);
            ControlV.pid_log_record_args.setpoint = 3.0f;
            ControlV.pid_log_record_args.measurement = 0.5f;
            ControlV.pid_log_record_args.output = out4[0];
            ControlV.pid_log_record_args.saturated = false;
            Control.pid_log_record(control_work);
            sinksz += ControlV.n;
        });

        (void)sinkf;
        (void)sinksz;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("control")
