// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Wi-Fi sniffer core (services/radio/wifi_sniffer): the 802.11
// frame parse, the running stats update, and the roam-hysteresis decision - the per-frame CPU path
// the promiscuous rx callback runs. The channel-hop scan and the radio are out of scope.
//
// Build/flash:  pio run -d performance_benching/device/wifi_sniffer -t upload --upload-port COM7
#include "device_bench.h"
#include "services/radio/wifi_sniffer/wifi_sniffer.h"
#include <Arduino.h>

static void wifi_sniffer_bench_task(void *)
{
    // A 24-byte 802.11 beacon MAC header (mgmt/beacon, no DS bits) + a little body.
    static uint8_t frame[40];
    memset(frame, 0, sizeof(frame));
    frame[0] = 0x80; // mgmt, beacon
    for (int i = 4; i < 22; i++)
    {
        frame[i] = (uint8_t)(i * 7 + 1);
    }

    for (;;)
    {
        Serial.printf("DB ==== wifi_sniffer device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint32_t sink = 0;
        WifiFrame wf;
        WifiStats st;
        pc_wifi_stats_reset(&st);
        DBENCH_OP("pc_wifi_parse", 200000, sink += pc_wifi_parse(frame, sizeof(frame), &wf));
        pc_wifi_parse(frame, sizeof(frame), &wf);
        DBENCH_OP("pc_wifi_stats_add", 200000, {
            pc_wifi_stats_add(&st, &wf);
            sink += 1;
        });
        DBENCH_OP("pc_wifi_should_roam", 200000, sink += pc_wifi_should_roam(-72, -58, 8));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: wifi_sniffer device microbench");
    xTaskCreatePinnedToCore(wifi_sniffer_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
