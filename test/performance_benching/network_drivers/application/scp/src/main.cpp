// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SCP/RCP control-line codec (network_drivers/application/scp): parse and
// build the `C<mode> <size> <name>` transfer control line (octal mode, decimal size, name). Pure
// string logic - the SSH channel/file plumbing is elsewhere; only the per-file control-line codec
// is benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/scp -t upload --upload-port COM7
#include "device_bench.h"
#include "network_drivers/application/scp/scp.h"
#include <Arduino.h>

static void scp_bench_task(void *)
{
    static const char cline[] = "C0644 262144 firmware.bin\n";

    for (;;)
    {
        Serial.printf("DB ==== scp device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        uint32_t mode;
        uint64_t size;
        char name[64];
        DBENCH_OP("pc_scp_parse_cline", 200000,
                  sink += pc_scp_parse_cline(cline, sizeof(cline) - 1, &mode, &size, name, sizeof(name)));
        static char out[64];
        DBENCH_OP("pc_scp_build_cline", 200000,
                  sink += pc_scp_build_cline(0644, 262144, "firmware.bin", out, sizeof(out)));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: scp device microbench");
    xTaskCreatePinnedToCore(scp_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
