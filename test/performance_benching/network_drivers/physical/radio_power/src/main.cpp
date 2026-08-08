// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for services/radio_power. NOTE: this is a thin Wi-Fi power-save
// *control* service - its real work is esp_wifi_set_ps() radio calls (pc_radio_power_apply /
// busy_hold / busy_release), which are hardware side effects, not a deterministic CPU codec. The
// only genuinely pure, side-effect-free operations are the power-save mode -> name lookup and the
// current-mode getter, so those are all that is benched here (kept for coverage/consistency across
// the performance_benching/device suite; the figure is not a meaningful throughput number).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/radio_power -t upload --upload-port COM7
#include "device_bench.h"
#include "network_drivers/physical/radio_power.h"
#include <Arduino.h>

static void radio_power_bench_task(void *)
{
    for (;;)
    {
        Serial.printf("DB ==== radio_power device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uintptr_t sink = 0;
        DBENCH_OP("pc_radio_ps_name", 200000, sink += (uintptr_t)pc_radio_ps_name(pc_radio_ps::PC_PHY_PS_MAX_MODEM));
        DBENCH_OP("pc_radio_ps_get", 200000, sink += pc_radio_ps_get());
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: radio_power device microbench");
    xTaskCreatePinnedToCore(radio_power_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
