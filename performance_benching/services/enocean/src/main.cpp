// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the EnOcean ESP3 serial codec (services/radio/enocean):
// pc_esp3_build() assembles a telegram (header + data + optional data + two CRC-8s),
// pc_esp3_parse() frames one back out of a byte stream, and pc_esp3_crc8() is the CRC-8
// (poly 0x07) both of those lean on. All three are pure (no UART, no radio) - this rig has no
// EnOcean TCM/USB gateway attached, so the transport side (reading bytes off a real serial
// port, driving a radio module) is out of scope everywhere; only the deterministic CPU-side
// codec is ever benched, matching performance_benching/device/modbus (a pure protocol codec, no hardware
// involved).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/enocean -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/radio/enocean/enocean.h"
#include <Arduino.h>

static void enocean_bench_task(void *)
{
    // RADIO_ERP1 telegram: RORG + payload + sender(4) + status, plus a 3-byte optional data
    // field (sub-telegram count + RSSI + security level) - straight out of
    // test_build_then_parse_round_trip (test/test_enocean/test_enocean.cpp), known-good and
    // spec-conformant. Builds to 17 bytes total (6 header/crc + 7 data + 3 opt + 1 crc).
    static const uint8_t data[7] = {0xF6, 0x50, 0x01, 0x02, 0x03, 0x04, 0x30};
    static const uint8_t opt[3] = {0x03, 0x00, 0x00};
    static uint8_t build_out[64];
    static uint8_t telegram[64];

    uint16_t tg_len =
        pc_esp3_build(pc_esp3_type::ESP3_RADIO_ERP1, data, sizeof(data), opt, sizeof(opt), telegram, sizeof(telegram));

    for (;;)
    {
        Serial.printf("DB ==== enocean device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        pc_esp3_packet pkt = {};

        DBENCH_OP("pc_esp3_build", 20000,
                  sink += pc_esp3_build(pc_esp3_type::ESP3_RADIO_ERP1, data, sizeof(data), opt, sizeof(opt), build_out,
                                        sizeof(build_out)));
        DBENCH_OP("pc_esp3_parse", 20000, sink += (size_t)pc_esp3_parse(telegram, tg_len, &pkt));
        DBENCH_BULK("pc_esp3_crc8", 20000, tg_len, sink += pc_esp3_crc8(telegram, tg_len));

        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: enocean device microbench");
    xTaskCreatePinnedToCore(enocean_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
