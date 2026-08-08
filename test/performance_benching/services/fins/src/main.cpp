// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Omron FINS frame codec (services/fieldbus/fins): the command
// builder, the Memory Area Read convenience, and the command / response parsers - all pure (no
// heap, no sockets). FINS/UDP carries this frame directly (UDP provides integrity, so there is no
// checksum to bench); the send itself is the app's concern and is out of scope here. Worked example
// for performance_benching/device/<service>/: a pure protocol codec with no hardware involved, so every call here
// exercises the real production code path (contrast with performance_benching/device/ads1115, a peripheral driver
// where the bus transaction itself is stubbed).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/fins -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/fins/fins.h"
#include <Arduino.h>

static FinsHeader make_header()
{
    FinsHeader h = {};
    h.icf = FINS_ICF_COMMAND; // 0x80
    h.rsv = 0x00;
    h.gct = 0x02;
    h.dna = 0x00;
    h.da1 = 0x01; // destination node 1
    h.da2 = 0x00;
    h.sna = 0x00;
    h.sa1 = 0x02; // source node 2
    h.sa2 = 0x00;
    h.sid = 0x2A;
    return h;
}

static void fins_bench_task(void *)
{
    static const FinsHeader h = make_header();
    static const uint8_t params[] = {0xAB, 0xCD};
    static uint8_t cmd_buf[32];
    static uint8_t mar_buf[32];

    // Pre-build a command frame once so the parse benches have real, known-good bytes to chew on
    // (same bytes as test_build_command_bytes / test_parse_command in test/test_fins).
    static uint8_t parse_src[32];
    size_t parse_src_len = pc_fins_build_command(parse_src, sizeof(parse_src), &h, 0x01, 0x01, params, sizeof(params));

    // A normal-completion response frame (same bytes as test_parse_response_ok).
    static const uint8_t resp_ok[] = {
        0xC0, 0x00, 0x02, 0x00, 0x02, 0x00, 0x00, 0x01, 0x00, 0x2A, // response header
        0x01, 0x01,                                                 // echoed MRC/SRC
        0x00, 0x00,                                                 // MRES/SRES = normal
        0x12, 0x34, 0x56, 0x78                                      // 2 data words
    };

    for (;;)
    {
        Serial.printf("DB ==== fins device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        FinsCommand cmd_out;
        FinsResponse resp_out;

        DBENCH_OP("pc_fins_build_command", 50000,
                  sink += pc_fins_build_command(cmd_buf, sizeof(cmd_buf), &h, 0x05, 0x01, params, sizeof(params)));
        DBENCH_OP("pc_fins_build_memory_area_read", 50000,
                  sink += pc_fins_build_memory_area_read(mar_buf, sizeof(mar_buf), &h, 0xB0, 100, 0, 10));
        DBENCH_OP("pc_fins_parse_command", 50000,
                  sink += pc_fins_parse_command(parse_src, parse_src_len, &cmd_out) ? 1 : 0);
        DBENCH_OP("pc_fins_parse_response", 50000,
                  sink += pc_fins_parse_response(resp_ok, sizeof(resp_ok), &resp_out) ? 1 : 0);
        (void)sink;

        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: fins device microbench");
    xTaskCreatePinnedToCore(fins_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
