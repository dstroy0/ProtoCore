// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the IEEE 2030.5 (SEP2) codec (services/energy/sep2): the XML
// resource builders - DeviceCapability, EndDevice, and a DERControl event. Pure string logic;
// no transport.
//
// Build/flash:  pio run -d performance_benching/device/sep2 -t upload --upload-port COM7
#include "device_bench.h"
#include "services/energy/sep2/sep2.h"
#include <Arduino.h>

static void sep2_bench_task(void *)
{
    for (;;)
    {
        Serial.printf("DB ==== sep2 device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        static char out[512];
        DBENCH_OP("pc_sep2_device_capability", 200000,
                  sink += pc_sep2_device_capability(300, "/edev", "/derp", out, sizeof(out)));
        DBENCH_OP("pc_sep2_end_device", 200000,
                  sink += pc_sep2_end_device(0x0123456789ABull, "3E4F...LFDI", "/edev/1", out, sizeof(out)));
        DBENCH_OP("pc_sep2_der_control", 200000,
                  sink += pc_sep2_der_control("D7A1B2C3", 1720700000u, 3600u, -1500, out, sizeof(out)));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: sep2 device microbench");
    xTaskCreatePinnedToCore(sep2_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
