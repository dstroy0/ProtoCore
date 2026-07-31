// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Wi-SUN FAN codec (services/radio/wisun): the RFC 7252 CoAP
// request builder (the per-request hot op over the mesh) and the pure node-registry bookkeeping
// (register / find). Pure; no radio.
//
// Build/flash:  pio run -d performance_benching/device/wisun -t upload --upload-port COM7
#include "device_bench.h"
#include "services/radio/wisun/wisun.h"
#include <Arduino.h>

static void wisun_bench_task(void *)
{
    static const uint8_t v6[16] = {0xfd, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1};
    pc_ip br = pc_ip_from_v6_bytes(v6);
    static WisunNode storage[8];
    static WisunFan fan;

    for (;;)
    {
        Serial.printf("DB ==== wisun device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        static const uint8_t body[4] = {0x01, 0x02, 0x03, 0x04};
        static uint8_t out[128];
        DBENCH_OP("pc_wisun_build_coap (CON PUT)", 200000,
                  sink += pc_wisun_build_coap(WisunCoap::WISUN_COAP_CON, WisunCoap::WISUN_COAP_PUT, 0x1234, nullptr, 0,
                                              "led", body, sizeof(body), out, sizeof(out)));
        pc_wisun_init(&fan, &br, storage, 8);
        uint8_t na[16] = {0xfd, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 2};
        DBENCH_OP("pc_wisun_node_register", 100000, {
            na[15] = (uint8_t)(sink & 0x7F) + 2;
            pc_ip addr = pc_ip_from_v6_bytes(na);
            sink += (size_t)(pc_wisun_node_register(&fan, &addr, 0) >= 0 ? 1 : 0);
        });
        pc_ip find = pc_ip_from_v6_bytes(na);
        size_t idx;
        DBENCH_OP("pc_wisun_node_find", 200000, sink += pc_wisun_node_find(&fan, &find, &idx));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: wisun device microbench");
    xTaskCreatePinnedToCore(wisun_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
