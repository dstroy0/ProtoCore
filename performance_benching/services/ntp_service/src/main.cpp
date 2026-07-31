// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the SNTP wall-clock service (services/timing_position/ntp_service). The
// service is a thin wrapper over the ESP-IDF SNTP client: the network half (configTzTime / the
// async pool.ntp.org sync in pc_ntp_begin) is real transport and is deliberately OUT OF SCOPE on
// this rig - we never start SNTP, so nothing here touches WiFi or a socket. What IS benched is the
// one pure, deterministic CPU path the service owns: formatting the current epoch as the RFC 7231
// IMF-fixdate HTTP `Date` header (pc_ntp_http_date -> shared pc_http_date -> gmtime_r + strftime),
// plus the cheap epoch/synced poll accessors and the time_source registry adapter.
//
// Time seam: PC_ENABLE_NTP defaults to 0 (and test_matrix.json's flag set for this service does not
// enable it), so the host branch of ntp_service.cpp is what compiles here - it exposes the
// pc_ntp_set_test_epoch() seam the unit tests use to inject a wall clock. We forward-declare it
// (the header only declares it for !ARDUINO) and seed a realistic modern epoch once at task start,
// so pc_ntp_http_date() runs its full gmtime_r + strftime path instead of the epoch==0 fast-out.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/ntp_service -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/timing_position/ntp_service/ntp_service.h"
#include "shared_primitives/http_date.h" // pc_http_date() - the shared IMF-fixdate formatter
#include <Arduino.h>

// Host/test time seam (see file header): defined by the host branch of ntp_service.cpp, which is the
// branch that compiles when PC_ENABLE_NTP is 0. The public header hides its declaration behind
// !defined(ARDUINO), so forward-declare it here (matching C++ linkage) to inject a wall clock.
extern void pc_ntp_set_test_epoch(time_t epoch);

static void ntp_service_bench_task(void *)
{
    // Seed a realistic modern wall clock (2023-11-14 22:13:20 UTC) so the Date formatter has real
    // broken-down-time work to do; no SNTP is ever started.
    pc_ntp_set_test_epoch((time_t)1700000000);

    // RFC 7231's own canonical example epoch: "Sun, 06 Nov 1994 08:49:37 GMT" - a known-good,
    // spec-conformant literal for the direct-formatter bench (isolates gmtime_r + strftime cost).
    static const time_t rfc_epoch = (time_t)784111777;
    static char datebuf[40]; // RFC IMF-fixdate is 29 chars + NUL; 40 is comfortable headroom

    for (;;)
    {
        Serial.printf("DB ==== ntp_service device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sinkz = 0;
        volatile uint32_t sinku = 0;
        volatile int sinkb = 0;

        // The service's Date-header path end to end: read the (seeded) epoch, break it down, strftime.
        DBENCH_OP("pc_ntp_http_date", 20000, sinkz += pc_ntp_http_date(datebuf, sizeof(datebuf)));
        // The shared formatter directly with a fixed spec epoch: pure gmtime_r + strftime, no clock read.
        DBENCH_OP("pc_http_date", 20000, sinkz += pc_http_date(rfc_epoch, datebuf, sizeof(datebuf)));
        // Cheap poll accessors the app calls to gate the Date header on / read the wall clock.
        DBENCH_OP("pc_ntp_epoch", 200000, sinku += (uint32_t)pc_ntp_epoch());
        DBENCH_OP("pc_ntp_synced", 200000, sinkb += pc_ntp_synced() ? 1 : 0);
        // The multi-source time registry adapter (services/timing_position/time_source feeds the Date header from
        // this).
        DBENCH_OP("pc_ntp_time_source", 200000, sinku += pc_ntp_time_source());

        (void)sinkz;
        (void)sinku;
        (void)sinkb;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: ntp_service device microbench");
    xTaskCreatePinnedToCore(ntp_service_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
