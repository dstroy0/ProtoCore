// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the WAMP codec (services/iot/wamp): the JSON message builders
// (HELLO / SUBSCRIBE / GOODBYE) and the array-element parser used to decode inbound messages. Pure;
// the WebSocket transport is elsewhere.
//
// Build/flash:  pio run -d performance_benching/device/wamp -t upload --upload-port COM7
#include "device_bench.h"
#include "services/iot/wamp/wamp.h"
#include <Arduino.h>

static void wamp_bench_task(void *)
{
    static const char welcome[] = "[2,9129137332,{\"roles\":{\"broker\":{}}}]";

    for (;;)
    {
        Serial.printf("DB ==== wamp device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        static char buf[256];
        DBENCH_OP("pc_wamp_build_hello", 200000,
                  sink += pc_wamp_build_hello(buf, sizeof(buf), "realm1", "{\"roles\":{\"subscriber\":{}}}"));
        DBENCH_OP("pc_wamp_build_subscribe", 200000,
                  sink += pc_wamp_build_subscribe(buf, sizeof(buf), 713845233ull, "com.pc.telemetry", nullptr));
        DBENCH_OP("pc_wamp_build_goodbye", 200000,
                  sink += pc_wamp_build_goodbye(buf, sizeof(buf), "wamp.close.normal", nullptr));
        int type = 0;
        DBENCH_OP("pc_wamp_get_type (parse)", 200000, sink += pc_wamp_get_type(welcome, &type));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: wamp device microbench");
    xTaskCreatePinnedToCore(wamp_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
