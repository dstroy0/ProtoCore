// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Sigfox codec (services/radio/sigfox): pc_sigfox_build_uplink()
// formats a payload as the AT$SF hex uplink frame. Pure; the UART link to the Sigfox modem is out
// of scope.
//
// Build/flash:  pio run -d performance_benching/device/sigfox -t upload --upload-port COM7
#include "device_bench.h"
#include "services/radio/sigfox/sigfox.h"
#include <Arduino.h>

static void sigfox_bench_task(void *)
{
    static const uint8_t payload[12] = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01, 0x21, 0x40, 0x30, 0x00, 0xAB, 0xCD};

    for (;;)
    {
        Serial.printf("DB ==== sigfox device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        static char out[64];
        DBENCH_OP("pc_sigfox_build_uplink (12B)", 200000,
                  sink += pc_sigfox_build_uplink(payload, sizeof(payload), out, sizeof(out)));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: sigfox device microbench");
    xTaskCreatePinnedToCore(sigfox_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
