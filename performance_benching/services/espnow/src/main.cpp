// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the ESP-NOW envelope codec + peer registry
// (services/radio/espnow): pc_espnow_encode/decode (typed [magic][type][len] + payload framing) and
// the bounded peer registry (pc_espnow_peer_add/has), all pure - no heap, no radio. Per
// test_matrix.json, the envelope codec + peer registry are host-tested here while the esp_now
// radio binding is ESP32-only; this rig benches only the shared pure core. Deliberately out of
// scope: pc_espnow_begin/add_peer/send/broadcast, which call esp_now_init()/esp_now_add_peer()/
// esp_now_send() against real WiFi/radio state this rig does not have configured - exercising
// them here would be real radio I/O, not a deterministic CPU-side microbenchmark.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/espnow -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/radio/espnow/espnow.h"
#include <Arduino.h>

static void espnow_bench_task(void *)
{
    pc_espnow_peers_reset();

    // Small envelope, mirrors test_espnow.cpp's roundtrip fixture.
    static const uint8_t payload_small[] = {1, 2, 3, 4, 5};
    static uint8_t frame_small[PC_ESPNOW_HDR + sizeof(payload_small)];
    size_t n_small = pc_espnow_encode(42, payload_small, sizeof(payload_small), frame_small, sizeof(frame_small));

    // Max-size envelope (radio MTU minus header) for the bulk/throughput numbers.
    static uint8_t payload_max[PC_ESPNOW_MAX_PAYLOAD];
    for (size_t i = 0; i < sizeof(payload_max); i++)
        payload_max[i] = (uint8_t)i;
    static uint8_t frame_max[PC_ESPNOW_HDR + PC_ESPNOW_MAX_PAYLOAD];
    size_t n_max = pc_espnow_encode(7, payload_max, sizeof(payload_max), frame_max, sizeof(frame_max));

    // One registered peer to exercise the registry's lookup/add-idempotent paths.
    static const uint8_t mac_a[6] = {0x01, 0x00, 0x00, 0x00, 0x00, 0xAA};
    pc_espnow_peer_add(mac_a);

    static uint8_t g_type_out = 0;
    static const uint8_t *g_payload_out = nullptr;
    static size_t g_plen_out = 0;

    for (;;)
    {
        Serial.printf("DB ==== espnow device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        volatile bool bsink = false;

        DBENCH_OP("pc_espnow_encode", 100000,
                  sink += pc_espnow_encode(42, payload_small, sizeof(payload_small), frame_small, sizeof(frame_small)));
        DBENCH_OP("pc_espnow_decode", 100000,
                  bsink = pc_espnow_decode(frame_small, n_small, &g_type_out, &g_payload_out, &g_plen_out));
        DBENCH_BULK("pc_espnow_encode_max", 20000, PC_ESPNOW_MAX_PAYLOAD,
                    sink += pc_espnow_encode(7, payload_max, sizeof(payload_max), frame_max, sizeof(frame_max)));
        DBENCH_BULK("pc_espnow_decode_max", 20000, PC_ESPNOW_MAX_PAYLOAD,
                    bsink = pc_espnow_decode(frame_max, n_max, &g_type_out, &g_payload_out, &g_plen_out));
        DBENCH_OP("pc_espnow_peer_has", 100000, bsink = pc_espnow_peer_has(mac_a));
        DBENCH_OP("pc_espnow_peer_add", 100000, bsink = pc_espnow_peer_add(mac_a)); // idempotent re-add

        (void)sink;
        (void)bsink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: espnow device microbench");
    xTaskCreatePinnedToCore(espnow_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
