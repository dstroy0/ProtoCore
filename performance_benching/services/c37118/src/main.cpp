// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the IEEE C37.118.2 synchrophasor frame codec
// (services/energy/c37118): the CRC-CCITT, the frame builder (Command frame + an arbitrary-payload
// Data frame), and the CRC-validating parser (frame header fields + the Command-frame data
// word) - all pure (no heap, no sockets). Worked example for performance_benching/device/<service>/: a pure
// protocol codec with no hardware involved (contrast with performance_benching/device/ads1115, a peripheral
// driver where the bus transaction itself is stubbed), so every call here exercises the real
// production code path. Out of scope: PMU/PDC transport (this rig has no serial/UDP link to a
// real PMU) and the message-type-specific contents of Data/Config frames beyond the fixed
// header this codec frames.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/c37118 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/energy/c37118/c37118.h"
#include <Arduino.h>

static void c37118_bench_task(void *)
{
    // Command frame fields, straight out of test/test_c37118/test_c37118.cpp
    // (test_command_round_trip): idcode 0x1234, soc 0x5F5E1100, fracsec 0x00ABCDEF,
    // cmd C37118_CMD_SEND_CFG2 -> an 18-octet frame.
    static uint8_t cmd_buf[32];
    size_t cmd_len =
        pc_c37118_build_command(cmd_buf, sizeof(cmd_buf), 0x1234, 0x5F5E1100, 0x00ABCDEF, C37118_CMD_SEND_CFG2);

    // Data frame payload (STAT + a 4-byte value), straight out of test_data_frame_payload().
    static const uint8_t data_payload[] = {0x00, 0x00, 0x12, 0x34, 0x56, 0x78};
    static uint8_t data_buf[64];

    // A parsed Command frame, used to bench pc_c37118_parse_command() in isolation.
    C37118Frame parsed_cmd;
    pc_c37118_parse_frame(cmd_buf, cmd_len, &parsed_cmd);

    for (;;)
    {
        Serial.printf("DB ==== c37118 device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());

        volatile uint16_t sink16 = 0;
        volatile size_t sinksz = 0;
        volatile bool sinkb = false;

        DBENCH_BULK("pc_c37118_crc", 100000, (size_t)(cmd_len - 2), sink16 += pc_c37118_crc(cmd_buf, cmd_len - 2));

        DBENCH_OP("pc_c37118_build_command", 50000,
                  sinksz += pc_c37118_build_command(cmd_buf, sizeof(cmd_buf), 0x1234, 0x5F5E1100, 0x00ABCDEF,
                                                    C37118_CMD_SEND_CFG2));

        DBENCH_OP("pc_c37118_build_frame (data)", 50000,
                  sinksz += pc_c37118_build_frame(data_buf, sizeof(data_buf), C37118_TYPE_DATA, C37118_VERSION_2011, 60,
                                                  100, 200, data_payload, sizeof(data_payload)));

        DBENCH_OP("pc_c37118_parse_frame (cmd)", 50000, sinkb = pc_c37118_parse_frame(cmd_buf, cmd_len, &parsed_cmd));

        uint16_t cmd_out = 0;
        DBENCH_OP("pc_c37118_parse_command", 200000, sinkb = pc_c37118_parse_command(&parsed_cmd, &cmd_out));

        (void)sink16;
        (void)sinksz;
        (void)sinkb;
        (void)cmd_out;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: c37118 device microbench");
    xTaskCreatePinnedToCore(c37118_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
