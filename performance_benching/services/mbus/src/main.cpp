// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the wired M-Bus / EN 13757 codec (services/fieldbus/mbus): the
// short/long link-layer frame builders (start/stop octets, doubled length, 8-bit sum checksum),
// the frame parser (which re-verifies that checksum), and the EN 13757-3 variable-data record
// walker (DIF/VIF, DIFE/VIFE extension chains, LVAR strings). Every function here is pure and
// zero-heap - the physical two-wire M-Bus is reached over a UART through a level converter that
// is the *application's* responsibility, not this codec's, so there is no bus I/O to stub and no
// peripheral is touched (contrast performance_benching/device/ads1115, where the I2C half is out of scope). This
// is a pure-protocol-codec worked example just like performance_benching/device/modbus.
//
// The record body benched below is copied verbatim from test/test_mbus/test_mbus.cpp
// (test_record_walk) - a known-good, spec-conformant 4-record stream (INT32, INT16, INT32+DIFE,
// LVAR ASCII) - wrapped in a realistic RSP_UD (REQ_UD2 response) long frame.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/mbus -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/fieldbus/mbus/mbus.h"
#include <Arduino.h>

static void mbus_bench_task(void *)
{
    // 4-record EN 13757-3 variable-data body (INT32 value 42, INT16 0x2710, INT32 behind a DIFE,
    // LVAR ASCII "ABC") - lifted straight from test/test_mbus/test_mbus.cpp.
    static const uint8_t record_body[] = {
        0x04, 0x13, 0x2A, 0x00, 0x00, 0x00,       // DIF INT32, VIF 0x13, value 42
        0x02, 0x5A, 0x10, 0x27,                   // DIF INT16, VIF 0x5A, value 0x2710
        0x84, 0x01, 0x13, 0x01, 0x00, 0x00, 0x00, // DIF INT32 + DIFE 0x01, VIF 0x13, value 1
        0x0D, 0x7C, 0x03, 'A',  'B',  'C',        // DIF variable, VIF 0x7C, LVAR 3, "ABC"
    };

    // Wrap the record body in a real RSP_UD long frame once; parse benches this exact buffer.
    static uint8_t frame[64];
    size_t frame_len = pc_mbus_build_long(frame, sizeof(frame), MBUS_C_RSP_UD, 0x01, MBUS_CI_RSP_VARIABLE, record_body,
                                          (uint8_t)sizeof(record_body));
    static uint8_t out[16];

    for (;;)
    {
        Serial.printf("DB ==== mbus device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        MbusFrame f;
        size_t consumed = 0;

        // Short-frame builder (SND_NKE link reset): start/CS/stop over just C + A.
        DBENCH_OP("pc_mbus_build_snd_nke", 100000, sink += pc_mbus_build_snd_nke(out, sizeof(out), 0x05));

        // Long-frame builder: framing + 8-bit sum checksum over the 23-octet record body.
        DBENCH_OP("pc_mbus_build_long", 50000,
                  sink += pc_mbus_build_long(frame, sizeof(frame), MBUS_C_RSP_UD, 0x01, MBUS_CI_RSP_VARIABLE,
                                             record_body, (uint8_t)sizeof(record_body)));

        // Parser: validates start/stop, doubled length, and re-sums the checksum. Bulk => MB/s too.
        DBENCH_BULK("pc_mbus_parse (long)", 50000, frame_len, sink += pc_mbus_parse(frame, frame_len, &f, &consumed));

        // Variable-data record walker: one full pass over all 4 records (DIFE/VIFE + LVAR).
        DBENCH_OP(
            "pc_mbus_record_walk x4", 50000, do {
                size_t p = 0;
                MbusRecord r;
                while (pc_mbus_record_next(record_body, sizeof(record_body), &p, &r))
                    sink += r.data_len;
            } while (0));

        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: mbus device microbench");
    xTaskCreatePinnedToCore(mbus_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
