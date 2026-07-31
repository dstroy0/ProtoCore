// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SCPI codec (services/instrumentation/scpi): the command builder, the
// real-number formatter, the number parser, and the header pattern matcher (SCPI short/long-form
// matching) - all pure string logic (no instrument link), the hot path a SCPI-over-LAN endpoint
// runs per command.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/scpi -t upload --upload-port COM7
#include "device_bench.h"
#include "services/instrumentation/scpi/scpi.h"
#include <Arduino.h>
#include <string.h>

static void scpi_bench_task(void *)
{
    static const char *args2[] = {"1", "MAX"};

    for (;;)
    {
        Serial.printf("DB ==== scpi device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        static char buf[64];
        DBENCH_OP("pc_scpi_build (2 args)", 200000, sink += pc_scpi_build(buf, sizeof(buf), "SOUR:VOLT", args2, 2));
        DBENCH_OP("pc_scpi_fmt_real", 200000, sink += pc_scpi_fmt_real(buf, sizeof(buf), 3.14159265));
        double d = 0;
        DBENCH_OP("pc_scpi_parse_number", 200000, sink += pc_scpi_parse_number("1.2345E+3", 9, &d));
        DBENCH_OP("pc_scpi_match (short/long)", 200000, sink += pc_scpi_match("SOUR:VOLT", 9, "SOURce:VOLTage"));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: scpi device microbench");
    xTaskCreatePinnedToCore(scpi_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
