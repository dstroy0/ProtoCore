// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the NEMA TS 2 traffic-cabinet SDLC frame codec
// (services/transportation/nema_ts2): the CRC-16/X-25 frame check sequence, the frame build
// ([address][control][frame-type][data...][FCS lo][FCS hi]) and the parse+FCS-validate path - all
// pure (no heap, no I/O). Like performance_benching/device/modbus, this is a pure protocol codec with no hardware
// involved, so every call here exercises the real production code path. The synchronous SDLC serial
// PHY and the BIU/MMU/detector bus timing are the hardware-gated half of TS 2 and are deliberately
// out of scope - this rig has no cabinet bus attached, so only the deterministic CPU-side codec is
// ever benched. Sample bytes are the known-good, spec-conformant vectors from
// test/test_nema_ts2/test_nema_ts2.cpp (the CRC-16/X-25 check value 0x906E and a command frame).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/nema_ts2 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/transportation/nema_ts2/nema_ts2.h"
#include <Arduino.h>

static void nema_ts2_bench_task(void *)
{
    // CRC-16/X-25 canonical check vector "123456789" (must yield 0x906E) - the checksum throughput case.
    static const uint8_t chk[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};

    // A controller -> BIU load-switch command frame (address 0x05, control 0x10, 3 data bytes), the
    // same shape the host test builds and parses.
    static const uint8_t cmd_data[3] = {0x01, 0x02, 0x03};
    static uint8_t frame[16];
    size_t frame_len = pc_nema_ts2_build(0x05, 0x10, NemaTs2::NEMA_TS2_FT_CMD_LOADSWITCH, cmd_data, sizeof(cmd_data),
                                         frame, sizeof(frame));

    static uint8_t out[16];

    for (;;)
    {
        Serial.printf("DB ==== nema_ts2 device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile uint16_t sink16 = 0;
        volatile size_t sinksz = 0;
        volatile bool sinkb = false;
        NemaTs2Frame parsed;

        // FCS-16 (CRC-16/X-25) over the 9-byte check vector - reports MB/s.
        DBENCH_BULK("pc_nema_ts2_crc x25 (9B)", 50000, sizeof(chk), sink16 += pc_nema_ts2_crc(chk, sizeof(chk)));
        // Build a full command frame (header + memcpy(data) + FCS-16).
        DBENCH_OP("pc_nema_ts2_build cmd+3B", 50000,
                  sinksz += pc_nema_ts2_build(0x05, 0x10, NemaTs2::NEMA_TS2_FT_CMD_LOADSWITCH, cmd_data,
                                              sizeof(cmd_data), out, sizeof(out)));
        // Validate the FCS and parse a well-formed command frame.
        DBENCH_OP("pc_nema_ts2_parse cmd+3B", 50000, sinkb ^= pc_nema_ts2_parse(frame, frame_len, &parsed));
        (void)sink16;
        (void)sinksz;
        (void)sinkb;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: nema_ts2 device microbench");
    xTaskCreatePinnedToCore(nema_ts2_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
