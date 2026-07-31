// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the Haas Machine Data Collection (MDC) Q-command codec
// (services/machine_tool/haas_mdc): building the `?Q###` numbered queries and the `?Q600 <var>` macro read, then
// parsing the framed responses - locating the payload between STX (0x02) and ETB (0x17), splitting the
// comma-delimited CSV into trimmed fields, and running the typed Q500 (program/status/parts) and Q600
// (macro) decoders plus the unframed DPRNT push de-multiplexer. Every op here is pure (no RS-232/TCP,
// no heap) so this is a pure-protocol-codec bench like performance_benching/device/modbus: the actual production code
// path runs each iteration. The RS-232/socket transport (Setting 143) is deliberately out of scope -
// this rig has no serial link to a Haas control attached, and the codec never touches one.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/haas_mdc -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/machine_tool/haas_mdc/haas_mdc.h"
#include <Arduino.h>

static void haas_mdc_bench_task(void *)
{
    // Byte-exact response vectors lifted from test/test_haas_mdc (known-good, spec-conformant framing:
    // payload between STX 0x02 and ETB 0x17, then CR LF and a '>' prompt). Kept as separate literal
    // pieces so the \x.. escapes don't greedily swallow the next payload character.
    static const char frame_status[] = "\x02"
                                       "PROGRAM, O00010, IDLE, PARTS, 42"
                                       "\x17"
                                       "\r\n>";
    static const char frame_macro[] = "\x02"
                                      "MACRO, 5021,       -234.567"
                                      "\x17"
                                      "\r\n>";
    static const char dprnt[] = "\x12"
                                "COUNT 5\r\n"
                                "\x14"; // a pushed DPRNT line bracketed by POPEN/PCLOS (no STX/ETB)

    static char qbuf[32]; // query-builder scratch

    // Pre-parse the response frames once; the typed decoders (parse_status / parse_macro) are benched
    // against these already-split field views so each op times only its own decode work.
    static HaasMdcResp r_status;
    static HaasMdcResp r_macro;
    pc_haas_mdc_parse(frame_status, sizeof(frame_status) - 1, &r_status);
    pc_haas_mdc_parse(frame_macro, sizeof(frame_macro) - 1, &r_macro);

    for (;;)
    {
        Serial.printf("DB ==== haas_mdc device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;

        // Query builders (snprintf-backed `?Q###\r` / `?Q600 <var>\r`).
        DBENCH_OP("pc_haas_mdc_build_q Q500", 50000,
                  sink += pc_haas_mdc_build_q(qbuf, sizeof(qbuf), HAAS_Q_PROGRAM_STATUS));
        DBENCH_OP("pc_haas_mdc_build_var 5021", 50000, sink += pc_haas_mdc_build_var(qbuf, sizeof(qbuf), 5021));

        // Frame parse: STX/ETB scan + CSV field split (Q500 5-field payload).
        HaasMdcResp r;
        DBENCH_OP("pc_haas_mdc_parse Q500 frame", 20000,
                  sink += pc_haas_mdc_parse(frame_status, sizeof(frame_status) - 1, &r) ? 1 : 0);

        // Typed decoders over the pre-split field views.
        HaasMdcStatus st;
        DBENCH_OP("pc_haas_mdc_parse_status", 50000, sink += pc_haas_mdc_parse_status(&r_status, &st) ? 1 : 0);
        uint32_t var = 0;
        const char *val = nullptr;
        size_t vl = 0;
        DBENCH_OP("pc_haas_mdc_parse_macro", 50000, sink += pc_haas_mdc_parse_macro(&r_macro, &var, &val, &vl) ? 1 : 0);

        // Unframed DPRNT push strip/de-mux.
        const char *txt = nullptr;
        size_t txt_len = 0;
        DBENCH_OP("pc_haas_mdc_dprnt_line", 50000,
                  sink += pc_haas_mdc_dprnt_line(dprnt, sizeof(dprnt) - 1, &txt, &txt_len) ? 1 : 0);

        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: haas_mdc device microbench");
    xTaskCreatePinnedToCore(haas_mdc_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
