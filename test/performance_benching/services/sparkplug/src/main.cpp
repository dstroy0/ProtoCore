// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Sparkplug B codec (services/iot/sparkplug): the topic string
// builder, a single protobuf metric encode, and a full NDATA payload encode. Pure; the MQTT
// transport is elsewhere.
//
// Build/flash:  pio run -d performance_benching/device/sparkplug -t upload --upload-port COM7
#include "device_bench.h"
#include "services/iot/sparkplug/sparkplug.h"
#include <Arduino.h>

static void sparkplug_bench_task(void *)
{
    static SpbMetric metrics[3];
    for (int i = 0; i < 3; i++)
    {
        metrics[i] = SpbMetric{};
        metrics[i].name = "line1/temp";
        metrics[i].datatype = SPB_DT_DOUBLE;
        metrics[i].kind = SpbMetricKind::SPB_M_DOUBLE;
        metrics[i].double_value = 21.4 + i;
    }

    for (;;)
    {
        Serial.printf("DB ==== sparkplug device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        static char topic[128];
        DBENCH_OP("pc_spb_build_topic", 200000,
                  sink += pc_spb_build_topic(topic, sizeof(topic), "Group1", "NDATA", "edge1", "dev1"));
        static uint8_t buf[256];
        DBENCH_OP("pc_spb_build_metric", 200000, sink += pc_spb_build_metric(buf, sizeof(buf), &metrics[0]));
        DBENCH_OP("pc_spb_build_payload (3 metrics)", 200000,
                  sink += pc_spb_build_payload(buf, sizeof(buf), 1720700000000ull, 1, metrics, 3));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: sparkplug device microbench");
    xTaskCreatePinnedToCore(sparkplug_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
