// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the PN532 NFC frame codec (services/peripherals/pn532): the NXP PN532
// "normal information frame" (00 | 00 FF | LEN | LCS | TFI | PData | DCS | 00) build + parse, its
// two running checksums (LCS over LEN, DCS over TFI+PData), and the 6-byte ACK detect/build - all
// pure (pc_pn532_build_frame / parse_frame / is_ack / build_ack carry no I2C/SPI/HSU of their own;
// the caller moves the bytes over the bus). Like performance_benching/device/modbus, this is a pure protocol codec
// with no hardware involved, so every call here exercises the real production code path - the actual
// PN532 reader on I2C/SPI/UART is out of scope everywhere on this peripheral-less rig. Sample frames
// are the documented GetFirmwareVersion command + response KATs copied verbatim from
// test/test_pn532/test_pn532.cpp (already known-good, spec-conformant).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/pn532 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/peripherals/pn532/pn532.h"
#include <Arduino.h>

static void pn532_bench_task(void *)
{
    // Host -> PN532 GetFirmwareVersion (command 0x02); the documented frame is 00 00 FF 02 FE D4 02 2A 00.
    static const uint8_t cmd_gfv[1] = {0x02};
    // PN532 -> host GetFirmwareVersion response: 00 00 FF 06 FA D5 03 32 01 06 07 E8 00.
    static const uint8_t resp_gfv[13] = {0x00, 0x00, 0xFF, 0x06, 0xFA, 0xD5, 0x03, 0x32, 0x01, 0x06, 0x07, 0xE8, 0x00};
    // The 6-byte ACK frame (00 00 FF 00 FF 00).
    static const uint8_t ack[6] = {0x00, 0x00, 0xFF, 0x00, 0xFF, 0x00};
    static uint8_t out[32];

    for (;;)
    {
        Serial.printf("DB ==== pn532 device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        volatile int isink = 0;
        volatile bool bsink = false;

        // Build the GetFirmwareVersion command frame (LEN/LCS + TFI + DCS + postamble).
        DBENCH_OP("pc_pn532_build_frame gfv", 200000,
                  sink += pc_pn532_build_frame(PN532_TFI_HOST, cmd_gfv, 1, out, sizeof(out)));
        // Frame + verify the GetFirmwareVersion response (LCS + DCS checks over 5 PData bytes).
        DBENCH_OP("pc_pn532_parse_frame gfv resp", 200000, {
            uint8_t tfi = 0;
            const uint8_t *pd = nullptr;
            uint8_t pdlen = 0;
            isink += pc_pn532_parse_frame(resp_gfv, sizeof(resp_gfv), &tfi, &pd, &pdlen);
        });
        // ACK detect (6-byte compare) and ACK build.
        DBENCH_OP("pc_pn532_is_ack", 200000, bsink ^= pc_pn532_is_ack(ack, sizeof(ack)));
        DBENCH_OP("pc_pn532_build_ack", 200000, sink += pc_pn532_build_ack(out, sizeof(out)));

        (void)sink;
        (void)isink;
        (void)bsink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: pn532 device microbench");
    xTaskCreatePinnedToCore(pn532_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
