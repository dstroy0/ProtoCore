// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the TPKT (RFC 1006) + COTP X.224 class-0 frame codec
// (services/fieldbus/cotp): the TPKT envelope build/parse, the COTP Data TPDU builder, the Connection
// Request builder (plain, and with S7-style src/dst TSAP parameters appended), and the COTP
// parser - all pure (no sockets, no heap). This is the ISO-on-TCP foundation under S7comm /
// IEC 61850 MMS. Worked example for performance_benching/device/<service>/: a pure protocol codec with no
// hardware involved, so every call here exercises the real production code path (contrast
// with performance_benching/device/ads1115, a peripheral driver where the bus transaction itself is stubbed).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/cotp -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/cotp/cotp.h"
#include <Arduino.h>

static void cotp_bench_task(void *)
{
    // TPKT: wrap a 3-octet payload (mirrors test_tpkt_bytes).
    static const uint8_t tpkt_payload[] = {0xAA, 0xBB, 0xCC};
    static uint8_t tpkt_buf[16];

    // COTP Data TPDU: 3-octet user data, EOT set (mirrors test_cotp_dt_bytes).
    static const uint8_t dt_data[] = {0x41, 0x42, 0x43}; // "ABC"
    static uint8_t dt_buf[16];
    size_t dt_len = pc_cotp_build_dt(dt_buf, sizeof(dt_buf), dt_data, sizeof(dt_data), true);

    // COTP Connection Request: plain (mirrors test_cotp_cr_bytes).
    static uint8_t cr_buf[32];

    // COTP Connection Request with S7-style src/dst TSAP parameters appended (mirrors
    // test_cotp_cr_with_tsaps).
    static const uint8_t tsaps[] = {0xC1, 0x02, 0x01, 0x00, 0xC2, 0x02, 0x01, 0x02};
    static uint8_t cr_tsap_buf[32];

    for (;;)
    {
        Serial.printf("DB ==== cotp device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;

        DBENCH_OP("pc_tpkt_build", 100000,
                  sink += pc_tpkt_build(tpkt_buf, sizeof(tpkt_buf), tpkt_payload, sizeof(tpkt_payload)));

        {
            const uint8_t *payload;
            size_t payload_len, consumed;
            const size_t tpkt_len = TPKT_HEADER_SIZE + sizeof(tpkt_payload); // 4 + 3 = 7
            DBENCH_OP("pc_tpkt_parse", 100000,
                      sink += pc_tpkt_parse(tpkt_buf, tpkt_len, &payload, &payload_len, &consumed));
        }

        DBENCH_OP("pc_cotp_build_dt", 100000,
                  sink += pc_cotp_build_dt(dt_buf, sizeof(dt_buf), dt_data, sizeof(dt_data), true));

        DBENCH_OP("pc_cotp_build_cr", 100000,
                  sink += pc_cotp_build_cr(cr_buf, sizeof(cr_buf), 0x0001, 0x0A, nullptr, 0));

        DBENCH_OP("pc_cotp_build_cr+tsaps", 100000,
                  sink += pc_cotp_build_cr(cr_tsap_buf, sizeof(cr_tsap_buf), 0x0002, 0x0A, tsaps, sizeof(tsaps)));

        {
            CotpHeader h;
            DBENCH_OP("pc_cotp_parse (DT)", 100000, sink += pc_cotp_parse(dt_buf, dt_len, &h));
        }

        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: cotp device microbench");
    xTaskCreatePinnedToCore(cotp_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
