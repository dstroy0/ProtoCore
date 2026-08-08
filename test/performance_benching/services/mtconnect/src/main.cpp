// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the MTConnect agent response codec (services/machine_tool/mtconnect,
// ANSI/MTC1.4): the four XML document builders an agent answers HTTP requests with, all pure
// text-framing over a caller buffer (zero heap, no stdlib, values XML-escaped) - so like
// performance_benching/device/modbus this is a pure protocol codec and every call here runs the real production
// path, no hardware to stub out. Benched:
//   - MTConnectStreams (the `current`/`sample` response): begin header + Samples/Events/Condition
//     observations + end.
//   - MTConnectDevices (the `probe` response): the device model - a <Device> with its <DataItems>.
//   - MTConnectAssets (the `asset` response): a <CuttingTool> with its <CuttingToolLifeCycle>.
//   - MTConnectError (a request error): header + <Errors><Error errorCode=..>.
//   - pc_mtc_sample_query: replay a from/count sub-window out of the rolling observation ring
//     into an MTConnectStreams document (the long-poll `sample` cursor).
// Out of scope: the HTTP transport (sockets/AsyncWebServer) that carries these documents - only the
// deterministic CPU-side document framing is timed. Sample data is copied verbatim from the
// known-good, spec-conformant vectors in test/test_mtconnect/test_mtconnect.cpp.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/mtconnect -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/machine_tool/mtconnect/mtconnect.h"
#include <Arduino.h>

static void mtconnect_bench_task(void *)
{
    static char buf[1024];
    pc_mtc_streams s;

    // A populated rolling observation ring for the `sample` long-poll query (built once).
    static pc_mtc_sample_buffer ring;
    pc_mtc_sample_buffer_init(&ring, 1);
    pc_mtc_sample_buffer_add(&ring, pc_mtc_category::PC_MTC_SAMPLE, "Position", "xpos", "T1", "1.0");
    pc_mtc_sample_buffer_add(&ring, pc_mtc_category::PC_MTC_SAMPLE, "Position", "xpos", "T2", "2.0");
    pc_mtc_sample_buffer_add(&ring, pc_mtc_category::PC_MTC_EVENT, "Execution", "exec", "T3", "ACTIVE");

    for (;;)
    {
        Serial.printf("DB ==== mtconnect device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;

        // MTConnectStreams (`current`/`sample`): header + one Event, one Sample, one Condition.
        DBENCH_OP("pc_mtc_streams build", 20000, pc_mtc_streams_begin(&s, buf, sizeof(buf), 1500, 42, "cnc1");
                  pc_mtc_streams_add(&s, pc_mtc_category::PC_MTC_EVENT, "Availability", "avail", 40,
                                     "2026-07-06T00:00:00Z", "AVAILABLE");
                  pc_mtc_streams_add(&s, pc_mtc_category::PC_MTC_SAMPLE, "Position", "xpos", 41, "2026-07-06T00:00:01Z",
                                     "12.5");
                  pc_mtc_streams_add(&s, pc_mtc_category::PC_MTC_CONDITION, "SystemCondition", "sys", 42,
                                     "2026-07-06T00:00:02Z", "Fault");
                  sink += pc_mtc_streams_end(&s));

        // MTConnectDevices (`probe`): the device model with three DataItems.
        DBENCH_OP(
            "pc_mtc_devices probe build", 20000,
            pc_mtc_devices_begin(&s, buf, sizeof(buf), 1500, "dev1", "cnc1", "uuid-abc");
            pc_mtc_devices_add_item(&s, pc_mtc_category::PC_MTC_EVENT, "avail", "Availability", nullptr, nullptr);
            pc_mtc_devices_add_item(&s, pc_mtc_category::PC_MTC_SAMPLE, "xpos", "Position", "Xabs", "MILLIMETER");
            pc_mtc_devices_add_item(&s, pc_mtc_category::PC_MTC_CONDITION, "sys", "SystemCondition", nullptr, nullptr);
            sink += pc_mtc_devices_end(&s));

        // MTConnectAssets (`asset`): one CuttingTool with a ToolLife.
        DBENCH_OP("pc_mtc_assets build", 20000, pc_mtc_assets_begin(&s, buf, sizeof(buf), 1500, 2, 1024);
                  pc_mtc_assets_cutting_tool_begin(&s, "tool-1", "SN-42", "T17", "uuid-abc", "2026-07-09T00:00:00Z");
                  pc_mtc_assets_tool_life(&s, "MINUTES", "DOWN", "100", "42"); pc_mtc_assets_cutting_tool_end(&s);
                  sink += pc_mtc_assets_end(&s));

        // MTConnectError: header + one Error element.
        DBENCH_OP("pc_mtc_error build", 50000, sink += pc_mtc_error(1500, "UNSUPPORTED", "bad path", buf, sizeof(buf)));

        // Long-poll `sample` cursor: replay the whole retained window as an MTConnectStreams document.
        DBENCH_OP("pc_mtc_sample_query", 20000,
                  sink += pc_mtc_sample_query(&ring, buf, sizeof(buf), 1500, "cnc1", 1, 10));

        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: mtconnect device microbench");
    xTaskCreatePinnedToCore(mtconnect_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
