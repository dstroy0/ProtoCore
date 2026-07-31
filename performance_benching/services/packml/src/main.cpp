// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the PackML / OMAC packaging-machine state model
// (services/machine_tool/packml, ISA-TR88.00.02): the pure 17-state transition engine and the owned PackTags
// service. Everything benched here is a pure, fixed-BSS state transition or a tag snapshot - no heap,
// no I/O, no transport - so, like performance_benching/device/modbus (a pure protocol codec), every call exercises the
// real production code path. The engine's only dependency is the header-only monotonic clock
// (services/system/clock.h), which defaults to the platform millis() on device, so nothing is stubbed.
//
// Benched (the performance-relevant pure ops the model surfaces):
//   - pc_packml_command          : apply a control command, return the resulting state (the core
//                                   transition table + the universal Stop/Abort branches)
//   - pc_packml_state_complete   : advance an acting state to its target (the State-Complete edge)
//   - pc_packml_command_valid    : is a command legal in a state (runs the table, compares)
//   - pc_packml_execute_complete : production-done edge, Execute -> Completing
//   - pc_packml_svc_status       : fill the full Status/Admin PackTags snapshot (reads the clock)
// Deliberately out of scope: OPC UA / any transport carrying these tags - none is part of the pure
// state model, and this rig has no network attached.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/packml -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/machine_tool/packml/packml.h"
#include <Arduino.h>

static void packml_bench_task(void *)
{
    pc_packml_svc_init(PackMlMode::PRODUCING); // owned service: Stopped, counters cleared
    PackMlStatus st;

    for (;;)
    {
        Serial.printf("DB ==== packml device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink = 0;

        // Core transition table: a real production edge (Execute + Hold -> Holding).
        DBENCH_OP("pc_packml_command Ex+Hold", 200000,
                  sink += (uint32_t)pc_packml_command(PackMlState::EXECUTE, PackMlCommand::HOLD));
        // State-Complete edge: an acting state auto-advances (Starting -> Execute).
        DBENCH_OP("pc_packml_state_complete", 200000,
                  sink += (uint32_t)pc_packml_state_complete(PackMlState::STARTING));
        // Command validity: runs the transition table and compares (Execute + Hold is legal).
        DBENCH_OP("pc_packml_command_valid", 200000,
                  sink += pc_packml_command_valid(PackMlState::EXECUTE, PackMlCommand::HOLD) ? 1u : 0u);
        // Production-done edge: Execute -> Completing.
        DBENCH_OP("pc_packml_execute_complete", 200000,
                  sink += (uint32_t)pc_packml_execute_complete(PackMlState::EXECUTE));
        // Full PackTags Status/Admin snapshot (reads the monotonic clock for the timer tags).
        DBENCH_OP("pc_packml_svc_status", 100000, (pc_packml_svc_status(&st), sink += (uint32_t)st.state_current));

        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: packml device microbench");
    xTaskCreatePinnedToCore(packml_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
