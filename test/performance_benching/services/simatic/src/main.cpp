// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Siemens 3964R codec (services/fieldbus/simatic): the BCC checksum
// and the DLE-stuffed block build/parse (the pure framing under the stateful 3964R engine). The
// stateful RX/TX engine (pc_3964r_send/rx_byte/tick) drives a UART and is out of scope; only the
// deterministic per-block codec is benched.
//
// Build/flash:  pio run -d performance_benching/device/simatic -t upload --upload-port COM7
#include "device_bench.h"
#include "services/fieldbus/simatic/simatic.h"
#include <Arduino.h>

static void simatic_bench_task(void *)
{
    static const uint8_t data[32] = {0x10, 0x02, 0x03, 0x10, 0x03, 0x11, 0x22, 0x33, 0x44, 0x55, 0x10,
                                     0x66, 0x77, 0x88, 0x99, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x01,
                                     0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B};
    static uint8_t block[80];
    size_t blen = pc_3964r_build_block(block, sizeof(block), data, sizeof(data), true);

    for (;;)
    {
        Serial.printf("DB ==== simatic device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        DBENCH_BULK("pc_3964r_bcc", 200000, sizeof(data), sink += pc_3964r_bcc(data, sizeof(data)));
        static uint8_t out[80];
        DBENCH_OP("pc_3964r_build_block (BCC)", 200000,
                  sink += pc_3964r_build_block(out, sizeof(out), data, sizeof(data), true));
        DBENCH_OP("pc_3964r_parse_block", 200000, {
            uint8_t p[64];
            size_t plen = 0;
            sink += pc_3964r_parse_block(block, blen, true, p, sizeof(p), &plen) ? plen : 0;
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
    Serial.println("\nDB boot: simatic device microbench");
    xTaskCreatePinnedToCore(simatic_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
