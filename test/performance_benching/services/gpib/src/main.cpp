// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the GPIB-over-LAN (Prologix-style) command codec
// (services/instrumentation/gpib): building the ++ controller commands (addr / read / spoll / eos), escaping a data
// line for the addressed instrument (a leading ESC before each CR / LF / ESC / '+' byte, then an
// unescaped '\n'), classifying a line as command-vs-data, and parsing the adapter's decimal /
// address / version responses - all pure, no sockets. Like performance_benching/device/modbus, this is a pure
// protocol codec with no hardware involved, so every call here runs the real production code path;
// the raw TCP-1234 socket / serial link into the Prologix adapter is the application's and is
// deliberately out of scope (this rig has no adapter attached).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/gpib -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/instrumentation/gpib/gpib.h"
#include <Arduino.h>

static void gpib_bench_task(void *)
{
    static char cbuf[64];    // command-builder destination
    static uint8_t dbuf[64]; // escaped-data destination

    // A worst-case data payload straight out of test/test_gpib (Prologix manual §8.1 example): every
    // one of CR/LF/ESC/'+' appears, so the escaper takes its escape branch on the maximum number of
    // bytes for this length - the honest throughput number for the escape hot path.
    static const uint8_t esc_src[] = {0, 1, 2, 13, 3, 10, 4, 27, 5, 43, 6};
    // A plain SCPI command (no special bytes) - the passthrough path the escaper takes most often.
    static const uint8_t idn_src[] = {'*', 'I', 'D', 'N', '?'};
    // A real "++ver" response and a serial-poll status byte, verbatim from test/test_gpib.
    static const char ver_resp[] = "Prologix GPIB-ETHERNET Controller version 1.6.6.0\r\n";
    static const char addr_resp[] = "9 96\r\n";
    static const char dec_resp[] = "64\r\n";

    for (;;)
    {
        Serial.printf("DB ==== gpib device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        volatile bool bsink = false;

        // ── command builders (snprintf-backed) ──
        DBENCH_OP("pc_gpib_addr", 50000, sink += pc_gpib_addr(cbuf, sizeof(cbuf), 9, 96));
        DBENCH_OP("pc_gpib_read eoi", 50000, sink += pc_gpib_read(cbuf, sizeof(cbuf), GpibRead::UNTIL_EOI, 0));
        DBENCH_OP("pc_gpib_spoll", 50000, sink += pc_gpib_spoll(cbuf, sizeof(cbuf), 9, 96));

        // ── data-line escaping (byte-throughput hot path) ──
        DBENCH_BULK("pc_gpib_build_data esc", 50000, sizeof(esc_src),
                    sink += pc_gpib_build_data(dbuf, sizeof(dbuf), esc_src, sizeof(esc_src)));
        DBENCH_BULK("pc_gpib_build_data plain", 50000, sizeof(idn_src),
                    sink += pc_gpib_build_data(dbuf, sizeof(dbuf), idn_src, sizeof(idn_src)));

        // ── classifier + response parsers ──
        DBENCH_OP("pc_gpib_is_command", 200000, bsink ^= pc_gpib_is_command("++mode 1", 8));
        DBENCH_OP("pc_gpib_parse_decimal", 100000,
                  bsink ^= pc_gpib_parse_decimal(dec_resp, sizeof(dec_resp) - 1, nullptr));
        DBENCH_OP("pc_gpib_parse_addr", 100000,
                  bsink ^= pc_gpib_parse_addr(addr_resp, sizeof(addr_resp) - 1, nullptr, nullptr));
        DBENCH_OP("pc_gpib_parse_version", 100000,
                  bsink ^= pc_gpib_parse_version(ver_resp, sizeof(ver_resp) - 1, nullptr, nullptr));

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
    Serial.println("\nDB boot: gpib device microbench");
    xTaskCreatePinnedToCore(gpib_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
