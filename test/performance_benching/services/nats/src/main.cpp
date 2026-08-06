// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the NATS client protocol codec (services/iot/nats): the
// line-oriented pub/sub builders (pc_nats_build_pub / _sub / _connect) and the inbound message
// parser (pc_nats_parse over MSG / control lines). Like performance_benching/device/modbus, this is a pure
// protocol codec with no hardware involved - the builders write into a caller buffer and the parser
// decodes one inbound message at the buffer head, so every call here exercises the real production
// code path (no sockets, no heap, no transport). The outbound client transport that a real device
// would pump these bytes over is deliberately out of scope; only the deterministic CPU-side codec is
// benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/nats -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/iot/nats/nats.h"
#include <Arduino.h>

static void nats_bench_task(void *)
{
    static char out[128];

    // PUB payload (known-good literal from test/test_nats/test_nats.cpp).
    static const uint8_t payload[] = {'h', 'e', 'l', 'l', 'o'};

    // Two inbound streams straight out of the host tests (already spec-conformant / known-good):
    // a MSG with subject/sid/payload, and a MSG carrying an explicit reply-to token.
    static const char msg_raw[] = "MSG foo 1 5\r\nhello\r\n";
    static const char msg_reply_raw[] = "MSG foo 1 _INBOX.7 5\r\nhello\r\n";
    // A server INFO control line (JSON arg parse path).
    static const char info_raw[] = "INFO {\"server_id\":\"x\"}\r\n";

    for (;;)
    {
        Serial.printf("DB ==== nats device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        NatsMsg m;
        size_t consumed;

        // --- builders ---
        DBENCH_OP("pc_nats_build_pub", 50000,
                  sink += pc_nats_build_pub(out, sizeof(out), "foo", nullptr, payload, sizeof(payload)));
        DBENCH_OP("pc_nats_build_sub", 50000, sink += pc_nats_build_sub(out, sizeof(out), "foo", "workers", "9"));
        DBENCH_OP("pc_nats_build_connect", 50000,
                  sink += pc_nats_build_connect(out, sizeof(out), "{\"verbose\":false}"));

        // --- parser ---
        DBENCH_OP("pc_nats_parse MSG", 50000, sink += pc_nats_parse(msg_raw, sizeof(msg_raw) - 1, &m, &consumed));
        DBENCH_OP("pc_nats_parse MSG+reply", 50000,
                  sink += pc_nats_parse(msg_reply_raw, sizeof(msg_reply_raw) - 1, &m, &consumed));
        DBENCH_OP("pc_nats_parse INFO", 50000, sink += pc_nats_parse(info_raw, sizeof(info_raw) - 1, &m, &consumed));

        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: nats device microbench");
    xTaskCreatePinnedToCore(nats_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
