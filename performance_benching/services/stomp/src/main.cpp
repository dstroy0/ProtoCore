// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the STOMP 1.2 frame codec (services/iot/stomp): the zero-heap
// frame builder (command + escaped headers + NUL-terminated body) and the non-mutating parser.
// Pure; no socket.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/stomp -t upload --upload-port COM7
#include "device_bench.h"
#include "services/iot/stomp/stomp.h"
#include <Arduino.h>

static void stomp_bench_task(void *)
{
    static const char body[] = "hello-from-pc-rig";
    const size_t blen = sizeof(body) - 1;
    static const char *bk[] = {"destination", "content-length"};
    static const char *bv[] = {"/topic/pc", "20"};
    // A representative inbound MESSAGE frame (what a subscriber receives). Ends at the NUL.
    static const char msg[] = "MESSAGE\ndestination:/topic/pc\nmessage-id:007\nsubscription:0\n"
                              "content-length:18\n\nhello-from-pc-rig";
    const size_t mlen = sizeof(msg);

    for (;;)
    {
        Serial.printf("DB ==== stomp device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        static char frame[384];
        DBENCH_OP("pc_stomp_build_frame (SEND)", 200000,
                  sink += pc_stomp_build_frame(frame, sizeof(frame), "SEND", bk, bv, 2, body, blen));
        DBENCH_OP("pc_stomp_parse_frame (MESSAGE)", 200000, {
            StompFrame f;
            size_t used = 0;
            sink += pc_stomp_parse_frame(msg, mlen, &f, &used) ? used : 0;
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
    Serial.println("\nDB boot: stomp device microbench");
    xTaskCreatePinnedToCore(stomp_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
