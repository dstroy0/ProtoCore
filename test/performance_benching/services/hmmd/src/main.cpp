// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Waveshare HMMD 24GHz mmWave micro-motion radar codec
// (services/peripherals/hmmd): the LD2410-family little-endian framing. Everything benched here is the pure,
// deterministic CPU-side codec -
//   * pc_hmmd_parse_report  - decode a whole 45-octet report frame (detect flag, distance, all 16
//                              gate energies), validating header/footer/length (bulk, so MB/s too);
//   * pc_hmmd_stream_push   - drive one full frame, octet-by-octet, through the resyncing stream
//                              reassembler (reset + 45 pushes per op);
//   * pc_hmmd_cmd_open      - build a full FD FC FB FA .. 04 03 02 01 command frame;
//   * pc_hmmd_parse_ack     - decode one command-ACK frame.
// The UART half (pc_hmmd_begin/poll/last, Serial2 on real hardware) and the module's bare GPIO OUT
// presence line are deliberately OUT OF SCOPE: this rig has no HMMD radar wired up, so no real UART
// transaction is ever issued - only the codec that runs on captured bytes is timed.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/hmmd -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/peripherals/hmmd/hmmd.h"
#include <Arduino.h>

// Build one spec-conformant report frame in place (mirrors test/test_hmmd build_report): a target
// detected at `dist` cm with gate energies gate0..gate0+15. len = 35 = detect(1)+dist(2)+16*2.
static void hmmd_build_report(uint8_t *f, uint8_t detect, uint16_t dist, uint16_t gate0)
{
    memset(f, 0, PC_HMMD_FRAME_MAX);
    f[0] = 0xF4;
    f[1] = 0xF3;
    f[2] = 0xF2;
    f[3] = 0xF1;
    f[4] = (uint8_t)(PC_HMMD_REPORT_LEN & 0xFF);
    f[5] = (uint8_t)(PC_HMMD_REPORT_LEN >> 8);
    f[6] = detect;
    f[7] = (uint8_t)(dist & 0xFF);
    f[8] = (uint8_t)(dist >> 8);
    for (int i = 0; i < PC_HMMD_GATES; i++)
    {
        uint16_t e = (uint16_t)(gate0 + i);
        f[9 + 2 * i] = (uint8_t)(e & 0xFF);
        f[10 + 2 * i] = (uint8_t)(e >> 8);
    }
    f[41] = 0xF8;
    f[42] = 0xF7;
    f[43] = 0xF6;
    f[44] = 0xF5;
}

// Drive one whole frame through the byte-by-byte reassembler; returns frames completed (0 or 1).
static int hmmd_reassemble(const uint8_t *frame, HmmdReport *out)
{
    HmmdStream s;
    pc_hmmd_stream_reset(&s);
    int n = 0;
    for (int i = 0; i < PC_HMMD_FRAME_MAX; i++)
    {
        if (pc_hmmd_stream_push(&s, frame[i], out))
        {
            n++;
        }
    }
    return n;
}

static void hmmd_bench_task(void *)
{
    // A known-good report frame (target at 137 cm, gate energies 100..115), same as test_hmmd.
    static uint8_t REPORT[PC_HMMD_FRAME_MAX];
    hmmd_build_report(REPORT, 0x01, 137, 100);

    // A known-good command-ACK frame (reply to read-config: word 0x0108, two data octets), from
    // test/test_hmmd/test_hmmd.cpp - already spec-conformant.
    static const uint8_t ACK[14] = {0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0x08, 0x01, 0xAB, 0xCD, 0x04, 0x03, 0x02, 0x01};
    static uint8_t cmd[32];

    HmmdReport r;
    HmmdAck a;

    for (;;)
    {
        Serial.printf("DB ==== hmmd device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        volatile bool bsink = false;

        // parse a whole report frame (header/footer/length checks + 16 gate energies) - bulk over 45B
        DBENCH_BULK("pc_hmmd_parse_report", 100000, sizeof(REPORT),
                    bsink ^= pc_hmmd_parse_report(REPORT, sizeof(REPORT), &r));
        // reassemble one full frame octet-by-octet through the resyncing stream (reset + 45 pushes)
        DBENCH_OP("pc_hmmd_stream_push x45", 20000, sink += hmmd_reassemble(REPORT, &r));
        // build a full open-command-mode frame (FD FC FB FA .. 04 03 02 01)
        DBENCH_OP("pc_hmmd_cmd_open", 100000, sink += pc_hmmd_cmd_open(cmd, sizeof(cmd)));
        // decode one command-ACK frame
        DBENCH_OP("pc_hmmd_parse_ack", 100000, bsink ^= pc_hmmd_parse_ack(ACK, sizeof(ACK), &a));

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
    Serial.println("\nDB boot: hmmd device microbench");
    xTaskCreatePinnedToCore(hmmd_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
