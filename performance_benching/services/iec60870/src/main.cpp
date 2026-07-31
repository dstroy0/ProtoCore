// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the IEC 60870-5-101/-104 telecontrol codec
// (services/energy/iec60870). Like performance_benching/device/modbus, this is a pure protocol codec with no hardware
// involved, so every call here exercises the real production code path - there is no I2C/SPI/UART/
// socket to stub. What is benched, all deterministic and heap-free:
//   - pc_iec104_build_i / pc_iec104_parse - the -104 APCI (start + length + 4 control octets)
//     in its I-format, encode and decode.
//   - pc_iec_asdu_build_header - the shared 6-octet ASDU header (type id, VSQ, COT, common addr).
//   - pc_iec101_build_variable / pc_iec101_parse - the -101 FT1.2 variable-length link frame
//     (68 L L 68 C A <asdu> CS 16). These two walk the frame body to compute/verify the 8-bit sum
//     checksum, so they are timed as DBENCH_BULK to report ns/byte + MB/s over the whole frame.
//   - pc_iec_get_ioa - reading the 3-octet little-endian Information Object Address.
// Out of scope: the per-type-id information elements (single/double point, measured values,
// commands) are the application's, not this framing layer; and no real -104 TCP socket or -101
// UART transport is touched - this rig has no peripherals attached.
//
// Sample frames are the known-good, spec-conformant byte vectors from test/test_iec60870.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/iec60870 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/energy/iec60870/iec60870.h"
#include <Arduino.h>

static void iec60870_bench_task(void *)
{
    // A tiny spec-conformant ASDU (header only), straight out of test_iec60870.cpp.
    static const uint8_t asdu[6] = {0x09, 0x01, 0x03, 0x00, 0x0A, 0x00};

    // Pre-build the two frames we will parse in the loop, so parse timing is not polluted by build.
    static uint8_t i_frame[32];
    size_t i_len = pc_iec104_build_i(i_frame, sizeof(i_frame), 100, 50, asdu, sizeof(asdu));

    static uint8_t var_frame[32];
    size_t var_len =
        pc_iec101_build_variable(var_frame, sizeof(var_frame), IEC_FC_USER_DATA_CONFIRM, 0x01, asdu, sizeof(asdu));

    // A 3-octet IOA (little-endian 0x123456) for the read benchmark.
    static const uint8_t ioa_bytes[3] = {0x56, 0x34, 0x12};

    // Scratch output buffers for the encoders.
    static uint8_t out[64];

    // A populated ASDU header to encode.
    IecAsduHeader hdr;
    hdr.type_id = IEC_TYPE_M_ME_NC_1;
    hdr.sq = false;
    hdr.count = 3;
    hdr.test = false;
    hdr.negative = false;
    hdr.cot = IEC_COT_SPONTANEOUS;
    hdr.orig_addr = 0;
    hdr.common_addr = 0x000A;

    for (;;)
    {
        Serial.printf("DB ==== iec60870 device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        volatile uint32_t sink32 = 0;

        // -104 APCI encode/decode (I-format).
        DBENCH_OP("pc_iec104_build_i", 100000,
                  sink += pc_iec104_build_i(out, sizeof(out), 100, 50, asdu, sizeof(asdu)));
        {
            Iec104Apci a;
            size_t c;
            DBENCH_OP("pc_iec104_parse", 100000, sink += pc_iec104_parse(i_frame, i_len, &a, &c));
        }

        // Shared ASDU header encode.
        DBENCH_OP("pc_iec_asdu_build_header", 200000, sink += pc_iec_asdu_build_header(out, sizeof(out), &hdr));

        // -101 FT1.2 variable frame build + parse (both walk the body for the sum checksum).
        DBENCH_BULK("pc_iec101_build_variable", 50000, var_len,
                    sink += pc_iec101_build_variable(out, sizeof(out), IEC_FC_USER_DATA_CONFIRM, 0x01, asdu,
                                                     (uint8_t)sizeof(asdu)));
        {
            Iec101Frame f;
            size_t c;
            DBENCH_BULK("pc_iec101_parse (var)", 50000, var_len, sink += pc_iec101_parse(var_frame, var_len, &f, &c));
        }

        // 3-octet IOA read.
        DBENCH_OP("pc_iec_get_ioa", 200000, sink32 += pc_iec_get_ioa(ioa_bytes));

        (void)sink;
        (void)sink32;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: iec60870 device microbench");
    xTaskCreatePinnedToCore(iec60870_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
