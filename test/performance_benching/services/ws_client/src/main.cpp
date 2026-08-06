// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the WebSocket client codec (services/net/ws_client): the RFC 6455
// handshake builder, the Sec-WebSocket-Accept derivation, and the masked frame build/parse. Pure;
// the lwIP/TLS transport is out of scope.
//
// Build/flash:  pio run -d performance_benching/device/ws_client -t upload --upload-port COM7
#include "device_bench.h"
#include "services/net/ws_client/ws_client.h"
#include <Arduino.h>

static void ws_client_bench_task(void *)
{
    static const uint8_t mask[4] = {0x37, 0xFA, 0x21, 0x3D};
    static const uint8_t payload[32] = {'h', 'e', 'l', 'l', 'o', '-', 'f', 'r', 'o', 'm', '-', 'd', 'w', 's', '-', 'r',
                                        'i', 'g', '-', 'w', 's', 'c', 'l', 'i', 'e', 'n', 't', '!', '!', '!', '!', '!'};
    static uint8_t frame[64];
    size_t flen =
        ws_client_build_frame(frame, sizeof(frame), WsClientOpcode::WSC_OP_TEXT, payload, sizeof(payload), mask);

    for (;;)
    {
        Serial.printf("DB ==== ws_client device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        static uint8_t out[256];
        DBENCH_OP("ws_client_build_handshake", 200000,
                  sink += ws_client_build_handshake(out, sizeof(out), "pc.example", "/ws",
                                                    "dGhlIHNhbXBsZSBub25jZQ==", nullptr));
        static char acc[32];
        DBENCH_OP("ws_client_accept_for_key", 200000, {
            ws_client_accept_for_key("dGhlIHNhbXBsZSBub25jZQ==", acc, sizeof(acc));
            sink += acc[0];
        });
        DBENCH_OP("ws_client_build_frame (masked)", 200000,
                  sink +=
                  ws_client_build_frame(out, sizeof(out), WsClientOpcode::WSC_OP_TEXT, payload, sizeof(payload), mask));
        DBENCH_OP("ws_client_parse_frame", 200000, {
            uint8_t op;
            bool fin;
            size_t poff;
            size_t plen;
            size_t consumed;
            sink += ws_client_parse_frame(frame, flen, &op, &fin, &poff, &plen, &consumed) ? plen : 0;
        });
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: ws_client device microbench");
    xTaskCreatePinnedToCore(ws_client_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
