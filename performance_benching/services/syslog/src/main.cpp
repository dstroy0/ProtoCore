// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the RFC 5424 syslog formatter (services/net/syslog):
// pc_syslog_format() builds one `<PRI>1 - HOST APP - - - MSG` line into a caller buffer - the
// per-log-line hot op before each UDP send. Pure; no socket.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/syslog -t upload --upload-port COM7
#include "device_bench.h"
#include "services/net/syslog/syslog.h"
#include <Arduino.h>

static void syslog_bench_task(void *)
{
    static const char *msg = "sensor=21.4C rh=48% link=up heap=131072";

    for (;;)
    {
        Serial.printf("DB ==== syslog device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        static char out[256];
        DBENCH_OP("pc_syslog_format (RFC 5424)", 200000,
                  sink += pc_syslog_format(out, sizeof(out), SyslogFacility::SYSLOG_FAC_LOCAL0,
                                           SyslogSeverity::SYSLOG_INFO, "pc-rig", "rig-app", msg));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: syslog device microbench");
    xTaskCreatePinnedToCore(syslog_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
