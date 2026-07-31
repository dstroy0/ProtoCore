// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the webhook client (services/net/webhook): the IFTTT Maker URL
// builder and the JSON payload builder. Pure string logic; the HTTPS POST (pc_webhook_post /
// pc_ifttt_trigger) is out of scope.
//
// Build/flash:  pio run -d performance_benching/device/webhook -t upload --upload-port COM7
#include "device_bench.h"
#include "services/net/webhook/webhook.h"
#include <Arduino.h>

static void webhook_bench_task(void *)
{
    for (;;)
    {
        Serial.printf("DB ==== webhook device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile int sink = 0;
        static char out[256];
        DBENCH_OP("pc_ifttt_url", 200000, sink += pc_ifttt_url("temp_alert", "bXlrZXktMTIzNDU", out, sizeof(out)));
        DBENCH_OP("pc_ifttt_payload", 200000, sink += pc_ifttt_payload("84.0", "threshold", "rig-1", out, sizeof(out)));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: webhook device microbench");
    xTaskCreatePinnedToCore(webhook_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
