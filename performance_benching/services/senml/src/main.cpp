// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SenML codec (services/iot/senml): building a SenML-JSON pack
// and a SenML-CBOR pack from a record array. Pure; no transport.
//
// Build/flash:  pio run -d performance_benching/device/senml -t upload --upload-port COM7
#include "device_bench.h"
#include "services/iot/senml/senml.h"
#include <Arduino.h>

static void senml_bench_task(void *)
{
    static const SenmlRecord recs[3] = {
        {"urn:dev:ow:10e2073a01080063:", true, 1720700000.0, "temp", "Cel", SenmlValueKind::SENML_V_FLOAT, 21.4,
         nullptr, false, false, 0},
        {nullptr, false, 0, "humidity", "%RH", SenmlValueKind::SENML_V_FLOAT, 48.0, nullptr, false, true, 10.0},
        {nullptr, false, 0, "status", nullptr, SenmlValueKind::SENML_V_STRING, 0, "ok", false, false, 0},
    };

    for (;;)
    {
        Serial.printf("DB ==== senml device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        static char jbuf[512];
        static uint8_t cbuf[512];
        DBENCH_OP("pc_senml_json_build (3 recs)", 200000, sink += pc_senml_json_build(jbuf, sizeof(jbuf), recs, 3));
        DBENCH_OP("pc_senml_cbor_build (3 recs)", 200000, sink += pc_senml_cbor_build(cbuf, sizeof(cbuf), recs, 3));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: senml device microbench");
    xTaskCreatePinnedToCore(senml_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
