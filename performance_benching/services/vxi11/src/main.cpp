// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the VXI-11 codec (services/instrumentation/vxi11): the ONC RPC record mark and
// the VXI-11 request builders (GetPort via the portmapper, CreateLink). Pure XDR/RPC framing; the TCP
// socket is out of scope.
//
// Build/flash:  pio run -d performance_benching/device/vxi11 -t upload --upload-port COM7
#include "device_bench.h"
#include "services/instrumentation/vxi11/vxi11.h"
#include <Arduino.h>

static void vxi11_bench_task(void *)
{
    for (;;)
    {
        Serial.printf("DB ==== vxi11 device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        static uint8_t buf[128];
        DBENCH_OP("pc_rpc_record_mark", 200000, sink += pc_rpc_record_mark(buf, sizeof(buf), 64));
        bool last;
        uint32_t frag;
        DBENCH_OP("pc_rpc_parse_record_mark", 200000, sink += pc_rpc_parse_record_mark(buf, 4, &last, &frag));
        DBENCH_OP("pc_vxi11_build_getport", 200000,
                  sink += pc_vxi11_build_getport(buf, sizeof(buf), 0x0001, 0x0607AF, 1, 6));
        DBENCH_OP("pc_vxi11_build_create_link", 200000,
                  sink += pc_vxi11_build_create_link(buf, sizeof(buf), 0x0002, 42, false, 0, "inst0"));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: vxi11 device microbench");
    xTaskCreatePinnedToCore(vxi11_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
