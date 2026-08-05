// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the HiSLIP (IVI-6.1) message codec (services/instrumentation/hislip): the
// fixed 16-byte header build/parse (HS prologue + type + control + 32-bit MessageParameter + 64-bit
// PayloadLength, big-endian), the Initialize handshake build/parse, and the Data / DataEND framing
// that carries a SCPI payload. Every function here is a pure, zero-heap codec (the same class of
// service as performance_benching/device/modbus): the two TCP connections a real HiSLIP session runs over are the
// application's, so no sockets / transport are involved and every call exercises real production
// code. Sample vectors are the byte-exact IVI-6.1 worked examples lifted from test/test_hislip.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/hislip -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/instrumentation/hislip/hislip.h"
#include <Arduino.h>

static void hislip_bench_task(void *)
{
    // A known-good DataEND message (header + "*IDN?\n") for the parse path - the IVI-6.1 worked
    // example from test/test_hislip.cpp (test_build_dataend_vector).
    static const uint8_t dataend_msg[] = {'H',  'S',  0x07, 0x00, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x00,
                                          0x00, 0x00, 0x00, 0x00, 0x06, '*',  'I',  'D',  'N',  '?',  '\n'};
    // A known-good Initialize message (client offers v1.0, vendor "XY"=0x5859, sub-address "hislip0").
    static uint8_t init_msg[64];
    size_t init_len = pc_hislip_build_initialize(init_msg, sizeof(init_msg), PC_HISLIP_VERSION_1_0, 0x5859, "hislip0");

    // A 512-byte SCPI-ish payload for the throughput (MB/s) build path.
    static uint8_t big_payload[512];
    for (size_t i = 0; i < sizeof(big_payload); i++)
    {
        big_payload[i] = (uint8_t)('0' + (i % 10));
    }

    static uint8_t out[16 + sizeof(big_payload)];

    for (;;)
    {
        Serial.printf("DB ==== hislip device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        volatile bool bsink = false;

        // 16-byte header build (prologue + type + control + BE param + BE length).
        DBENCH_OP("pc_hislip_build_header", 100000,
                  sink += pc_hislip_build_header(out, sizeof(out), HislipMsg::DATA_END, 0x01, 0xDEADBEEF,
                                                 0x0102030405060708ULL));
        // 16-byte header parse (prologue check + BE field decode).
        HislipHeader h;
        DBENCH_OP("pc_hislip_parse_header", 100000,
                  bsink ^= pc_hislip_parse_header(dataend_msg, sizeof(dataend_msg), &h));

        // Initialize handshake: build (header + sub-address payload) and parse.
        DBENCH_OP("pc_hislip_build_initialize", 50000,
                  sink += pc_hislip_build_initialize(out, sizeof(out), PC_HISLIP_VERSION_1_0, 0x5859, "hislip0"));
        HislipInitialize init;
        DBENCH_OP("pc_hislip_parse_initialize", 50000, bsink ^= pc_hislip_parse_initialize(init_msg, init_len, &init));

        // DataEND framing carrying a short SCPI query ("*IDN?\n").
        DBENCH_OP("pc_hislip_build_dataend", 50000,
                  sink += pc_hislip_build_data(out, sizeof(out), true, 0, PC_HISLIP_MESSAGE_ID_INIT,
                                               (const uint8_t *)"*IDN?\n", 6));
        // Data framing over a 512-byte payload - reports MB/s of the header-build + memcpy path.
        DBENCH_BULK("pc_hislip_build_data 512B", 20000, sizeof(big_payload),
                    sink +=
                    pc_hislip_build_data(out, sizeof(out), false, 0, 0x00001000, big_payload, sizeof(big_payload)));

        (void)sink;
        (void)bsink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: hislip device microbench");
    xTaskCreatePinnedToCore(hislip_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
