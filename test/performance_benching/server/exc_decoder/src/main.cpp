// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the ESP32 panic / exception decoder (server/exc_decoder):
// pc_exc_parse() scans a captured Guru Meditation dump (cause, register-dump PC + EXCVADDR, and the
// "Backtrace: pc:sp pc:sp ..." frame list) into a structured ExcInfo using hand-rolled hex/decimal
// parsing (no stdlib, no heap), and pc_exc_json() serializes that struct for a live "/exception"
// panel - both pure text-in/struct-or-text-out code with no hardware involved, so every call here
// exercises the real production code path (worked example: a pure protocol codec, same category as
// performance_benching/device/modbus). Out of scope: the core-dump-partition half of this service
// (pc_exc_coredump_present/summary/read/save/erase in exc_coredump.cpp) - those read a flash
// partition via esp_partition_read()/esp_core_dump_*(), which is hardware I/O this physical rig
// cannot exercise meaningfully, so they are never called here.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/exc_decoder -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/exc_decoder.h"
#include <Arduino.h>

static void exc_decoder_bench_task(void *)
{
    // Full Guru Meditation dump (cause + register PC/EXCVADDR + 3-frame backtrace), lifted straight
    // out of test/test_exc_decoder/test_exc_decoder.cpp (test_parse_full / test_json) - already
    // known-good, spec-conformant fixture text.
    static const char *PANIC = "Guru Meditation Error: Core  1 panic'ed (LoadProhibited). Exception was unhandled.\n"
                               "\n"
                               "Core  1 register dump:\n"
                               "PC      : 0x400d1234  PS      : 0x00060730  A0      : 0x800d5678  A1      : "
                               "0x3ffb2200\n"
                               "A2      : 0x00000000  A3      : 0x00000001  A4      : 0x00000000  A5      : "
                               "0x00000000\n"
                               "EXCVADDR: 0x0000002a  LBEG    : 0x40085d5c  LEND    : 0x40085d6a  LCOUNT  : "
                               "0x00000000\n"
                               "\n"
                               "Backtrace: 0x400d1234:0x3ffb2200 0x400d5678:0x3ffb2220 0x400d9abc:0x3ffb2240\n";
    static const size_t PANIC_LEN = strlen(PANIC);

    // Backtrace-only text (no register dump): exercises the "PC falls back to frames[0]" path -
    // lifted from test_backtrace_only_and_corrupted, including the trailing corruption marker.
    static const char *BT_ONLY = "Backtrace: 0x400e1111:0x3ffc0000 0x400e2222:0x3ffc0020 |<-CORRUPTED\n";

    ExcInfo info_full;
    pc_exc_parse(PANIC, &info_full);
    static char json_buf[512];

    for (;;)
    {
        Serial.printf("DB ==== exc_decoder device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());

        volatile bool sinkb = false;
        volatile size_t sinksz = 0;

        DBENCH_BULK("pc_exc_parse (full dump)", 20000, PANIC_LEN, sinkb ^= pc_exc_parse(PANIC, &info_full));

        DBENCH_OP("pc_exc_parse (backtrace-only)", 20000, sinkb ^= pc_exc_parse(BT_ONLY, &info_full));

        DBENCH_OP("pc_exc_json (full dump)", 20000, sinksz += pc_exc_json(&info_full, json_buf, sizeof(json_buf)));

        (void)sinkb;
        (void)sinksz;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: exc_decoder device microbench");
    xTaskCreatePinnedToCore(exc_decoder_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
