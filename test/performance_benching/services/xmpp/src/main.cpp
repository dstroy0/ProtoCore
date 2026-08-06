// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the XMPP stanza codec (services/iot/xmpp): the XML escaper and
// the stanza builders (stream open, message, presence, iq). Pure string logic; no TCP/TLS.
//
// Build/flash:  pio run -d performance_benching/device/xmpp -t upload --upload-port COM7
#include "device_bench.h"
#include "services/iot/xmpp/xmpp.h"
#include <Arduino.h>

static void xmpp_bench_task(void *)
{
    for (;;)
    {
        Serial.printf("DB ==== xmpp device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        static char out[512];
        DBENCH_OP("pc_xmpp_escape", 200000, sink += pc_xmpp_escape("a<b>&\"c'd", 9, out, sizeof(out)));
        DBENCH_OP("pc_xmpp_stream_open", 200000, sink += pc_xmpp_stream_open("rig@pc", "pc.example", out, sizeof(out)));
        DBENCH_OP("pc_xmpp_message", 200000,
                  sink += pc_xmpp_message("ops@pc", "rig@pc", "chat", "temp 84C over threshold", out, sizeof(out)));
        DBENCH_OP("pc_xmpp_iq", 200000,
                  sink += pc_xmpp_iq("get", "q1", "<ping xmlns='urn:xmpp:ping'/>", out, sizeof(out)));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: xmpp device microbench");
    xTaskCreatePinnedToCore(xmpp_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
