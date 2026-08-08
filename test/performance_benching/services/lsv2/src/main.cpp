// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Heidenhain LSV/2 telegram codec (services/machine_tool/lsv2): the
// framer (4-byte big-endian payload-length prefix + 4-char mnemonic + payload), the typed request
// builders (login A_LG, filename command R_FL, run-info R_RI), the stream parser that slices one
// complete telegram off a byte buffer, and the error-response decoder (T_ER class+code). All pure
// (no heap, no sockets), so every call here exercises the real production code path - like
// performance_benching/device/modbus, a pure protocol codec with no hardware involved. The TCP/serial link to the
// control (default port 19000) is the application's, so nothing here does any I/O: out of scope on
// this rig, which has no peripherals attached.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/lsv2 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/machine_tool/lsv2/lsv2.h"
#include <Arduino.h>

static void lsv2_bench_task(void *)
{
    static uint8_t buf[512];

    // A T_ER error response with the 2-byte error-class + error-code payload (test golden vector),
    // parsed once so the error decoder is benched on a ready telegram (as a caller would hold it).
    static const uint8_t err_frame[] = {0x00, 0x00, 0x00, 0x02, 'T', '_', 'E', 'R', 0x01, 0x31};
    // An S_RI run-info data reply carrying 3 payload bytes (test golden vector) for the parser.
    static const uint8_t sri_frame[] = {0x00, 0x00, 0x00, 0x03, 'S', '_', 'R', 'I', 0xAA, 0xBB, 0xCC};
    static Lsv2Telegram err_t;
    pc_lsv2_parse(err_frame, sizeof(err_frame), &err_t, nullptr);

    // A 256-byte payload for a bulk framing pass (e.g. a C_FL file-data block): exercises the
    // header write + memcpy throughput the framer is dominated by for real transfers.
    static uint8_t big_payload[256];
    for (size_t i = 0; i < sizeof(big_payload); i++)
    {
        big_payload[i] = (uint8_t)i;
    }

    for (;;)
    {
        Serial.printf("DB ==== lsv2 device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        volatile uint8_t bsink = 0;

        // ── request builders (cheap: header write + a small copy) ──────────────────────────────
        DBENCH_OP("pc_lsv2_build_run_info", 200000,
                  sink += pc_lsv2_build_run_info(buf, sizeof(buf), LSV2_RI_PGM_STATE));
        DBENCH_OP("pc_lsv2_build_login", 100000,
                  sink += pc_lsv2_build_login(buf, sizeof(buf), PC_LSV2_LOGIN_INSPECT, nullptr));
        DBENCH_OP("pc_lsv2_build_filename", 100000,
                  sink += pc_lsv2_build_filename(buf, sizeof(buf), PC_LSV2_CMD_FILE_LOAD, "PGM.H"));

        // ── framer throughput over a 256-byte payload block ────────────────────────────────────
        DBENCH_BULK("pc_lsv2_build 256B", 100000, sizeof(big_payload),
                    sink += pc_lsv2_build(buf, sizeof(buf), PC_LSV2_CMD_FILE_SEND, big_payload, sizeof(big_payload)));

        // ── parse + response readers ───────────────────────────────────────────────────────────
        {
            Lsv2Telegram t;
            size_t consumed = 0;
            DBENCH_OP("pc_lsv2_parse S_RI", 200000,
                      sink += pc_lsv2_parse(sri_frame, sizeof(sri_frame), &t, &consumed) ? consumed : 0);
        }
        {
            uint8_t ec = 0, code = 0;
            DBENCH_OP("pc_lsv2_error decode", 200000,
                      bsink += pc_lsv2_error(&err_t, &ec, &code) ? (uint8_t)(ec + code) : 0);
        }

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
    Serial.println("\nDB boot: lsv2 device microbench");
    xTaskCreatePinnedToCore(lsv2_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
