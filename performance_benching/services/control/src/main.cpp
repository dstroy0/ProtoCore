// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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
//   pio run -d performance_benching/device/control -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/system/control/control.h"
#include <Arduino.h>

static void control_bench_task(void *)
{
    // Variable-rate loop: pid_update() computes 1/dt itself each call.
    Pid p;
    pid_init(&p, 1.5f, 4.0f, 0.05f);
    pid_set_output_limits(&p, -10.0f, 10.0f);
    pid_set_integral_limits(&p, -5.0f, 5.0f);
    pid_set_derivative_filter(&p, 0.2f);
    pid_set_feedforward(&p, 0.1f);

    // Fixed-rate loop: pid_set_rate() caches dt/inv_dt once, so pid_update_fixed() is all
    // multiplies (no per-call divide).
    Pid pf;
    pid_init(&pf, 1.5f, 4.0f, 0.05f);
    pid_set_output_limits(&pf, -10.0f, 10.0f);
    pid_set_derivative_filter(&pf, 0.2f);
    pid_set_rate(&pf, 0.01f);

    // Batched multi-axis update (a motion master driving 4 axes off one control tick).
    Pid axes[4];
    for (uint8_t i = 0; i < 4; i++)
        pid_init(&axes[i], 1.0f + (float)i, 0.0f, 0.0f);
    static const float sp4[4] = {5.0f, 2.0f, -3.0f, 1.0f};
    static const float meas4[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    static float out4[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    // Shared scratch buffer for the dense-binary log packers (36 B header, 16 B record).
    static uint8_t logbuf[PID_LOG_HEADER_LEN > PID_LOG_RECORD_LEN ? PID_LOG_HEADER_LEN : PID_LOG_RECORD_LEN];

    for (;;)
    {
        Serial.printf("DB ==== control device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile float sinkf = 0.0f;
        volatile size_t sinksz = 0;

        DBENCH_OP("pid_update", 100000, sinkf += pid_update(&p, 3.0f, 0.5f, 0.01f));
        DBENCH_OP("pid_update_fixed", 100000, sinkf += pid_update_fixed(&pf, 3.0f, 0.5f));
        DBENCH_OP("pid_update_n x4", 50000, pid_update_n(axes, sp4, meas4, 0.01f, out4, 4));
        DBENCH_OP("pid_log_header", 100000, sinksz += pid_log_header(logbuf, sizeof(logbuf), &p, 0.01f));
        DBENCH_OP("pid_log_record", 100000,
                  sinksz += pid_log_record(logbuf, sizeof(logbuf), 3.0f, 0.5f, out4[0], false));

        (void)sinkf;
        (void)sinksz;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: control device microbench");
    xTaskCreatePinnedToCore(control_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
