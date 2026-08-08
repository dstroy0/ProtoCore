// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the WebDAV multi-status builder (network_drivers/application/webdav): the
// PROPFIND 207 Multi-Status response assembly (pc_webdav_ms_begin / _entry / _end) and the XML
// escaper. Pure string logic - the FS traversal is elsewhere; only the per-response codec is benched.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/webdav -t upload --upload-port COM7
#include "device_bench.h"
#include "network_drivers/application/webdav/webdav.h"
#include <Arduino.h>

static void webdav_bench_task(void *)
{
    static const char *mtime = "Mon, 07 Jul 2026 12:00:00 GMT";

    for (;;)
    {
        Serial.printf("DB ==== webdav device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;
        static char buf[4096];
        DBENCH_OP("pc_webdav_ms_entry (1 file)", 100000,
                  sink += pc_webdav_ms_entry(buf, sizeof(buf), 0, "/dav/report.txt", false, 4096, mtime, "text/plain"));
        DBENCH_OP("pc_webdav propfind (dir+2)", 100000, {
            size_t len = pc_webdav_ms_begin(buf, sizeof(buf), 0);
            len = pc_webdav_ms_entry(buf, sizeof(buf), len, "/dav/", true, 0, mtime, "");
            len = pc_webdav_ms_entry(buf, sizeof(buf), len, "/dav/sensor-log.csv", false, 12800, mtime, "text/csv");
            len = pc_webdav_ms_end(buf, sizeof(buf), len);
            sink += len;
        });
        static char esc[256];
        DBENCH_OP("pc_webdav_xml_escape", 200000,
                  sink += pc_webdav_xml_escape(esc, sizeof(esc), "/dav/a&b<c>\"d'e.txt"));
        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500);
    Serial.println("\nDB boot: webdav device microbench");
    xTaskCreatePinnedToCore(webdav_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
