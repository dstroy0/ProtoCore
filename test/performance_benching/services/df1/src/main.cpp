// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Allen-Bradley DF1 full-duplex frame codec
// (services/fieldbus/df1): the BCC and CRC-16/ARC checksums, the frame builder (DLE STX + DLE
// byte-stuffing + DLE ETX + check), and the validating, un-stuffing parser - all pure (no
// heap, no UART). Worked example for performance_benching/device/<service>/: a pure protocol codec with no
// hardware involved, so every call here exercises the real production code path (contrast
// with performance_benching/device/ads1115, a peripheral driver where the bus transaction itself is
// stubbed). Vectors below are copied straight out of test/test_df1/test_df1.cpp
// (test_bcc_vector, test_crc_vector, test_round_trip_bcc, test_round_trip_crc) - already
// known-good against AB pub. 1770-6.5.16; out of scope: the serial UART transport itself,
// which DF1 rides on but this codec never touches.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/df1 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/df1/df1.h"
#include <Arduino.h>

static void df1_bench_task(void *)
{
    // test_bcc_vector: 0x07 + 0x19 = 0x20 -> BCC 0xE0.
    static const uint8_t bcc_data[] = {0x07, 0x19};
    // test_crc_vector: CRC-16/ARC of "123456789" is the standard check-value string.
    static const uint8_t crc_data[] = "123456789";

    // test_round_trip_bcc: two embedded DLE (0x10) bytes, exercises byte-stuffing.
    static const uint8_t bcc_frame_data[] = {0x10, 0x05, 0x10, 0xAB};
    // test_round_trip_crc: includes one embedded DLE (0x10) byte.
    static const uint8_t crc_frame_data[] = {0x00, 0x05, 0x0F, 0x00, 0x10, 0x42};

    static uint8_t bcc_frame[32];
    static uint8_t crc_frame[32];
    size_t bcc_frame_len = pc_df1_build_frame(bcc_frame, sizeof(bcc_frame), bcc_frame_data, sizeof(bcc_frame_data),
                                              Df1Check::DF1_CHECK_BCC);
    size_t crc_frame_len = pc_df1_build_frame(crc_frame, sizeof(crc_frame), crc_frame_data, sizeof(crc_frame_data),
                                              Df1Check::DF1_CHECK_CRC);

    static uint8_t out[32];

    for (;;)
    {
        Serial.printf("DB ==== df1 device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        volatile uint16_t sink16 = 0;
        size_t out_len;

        DBENCH_BULK("pc_df1_bcc", 200000, sizeof(bcc_data), sink += pc_df1_bcc(bcc_data, sizeof(bcc_data)));

        DBENCH_BULK("pc_df1_crc", 100000, sizeof(crc_data) - 1, sink16 += pc_df1_crc(crc_data, sizeof(crc_data) - 1));

        DBENCH_OP("pc_df1_build_frame BCC", 50000,
                  sink += pc_df1_build_frame(bcc_frame, sizeof(bcc_frame), bcc_frame_data, sizeof(bcc_frame_data),
                                             Df1Check::DF1_CHECK_BCC));

        DBENCH_OP("pc_df1_build_frame CRC", 50000,
                  sink += pc_df1_build_frame(crc_frame, sizeof(crc_frame), crc_frame_data, sizeof(crc_frame_data),
                                             Df1Check::DF1_CHECK_CRC));

        DBENCH_OP("pc_df1_parse_frame BCC", 50000,
                  sink +=
                  pc_df1_parse_frame(bcc_frame, bcc_frame_len, Df1Check::DF1_CHECK_BCC, out, sizeof(out), &out_len));

        DBENCH_OP("pc_df1_parse_frame CRC", 50000,
                  sink +=
                  pc_df1_parse_frame(crc_frame, crc_frame_len, Df1Check::DF1_CHECK_CRC, out, sizeof(out), &out_len));

        (void)sink;
        (void)sink16;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: df1 device microbench");
    xTaskCreatePinnedToCore(df1_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
