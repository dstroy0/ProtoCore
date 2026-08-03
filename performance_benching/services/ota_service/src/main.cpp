// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for server/update/ota_service - EXCEPT there is nothing pure to
// benchmark here, so this sketch deliberately benches nothing and exists only to keep
// server/update/ota_service under the same performance_benching/device/<service>/ umbrella and to prove the real
// production ota_service.cpp still compiles and links on-device (built with -DPC_ENABLE_OTA=1).
//
// Why nothing is benched (a NOTE-4 service): ota_service.h exposes exactly one public function,
// pc_ota_begin(server, path, user, pass). That call is a one-time, side-effectful REGISTRATION - it
// stores the Basic-auth credentials, installs the HTTP parser's streaming-body hooks
// (http_parser_set_stream_hooks) and adds a POST route (server.on) - not a deterministic, loopable
// codec. Everything ota_service actually DOES is real hardware/transport that this peripheral-less
// rig must never touch: it streams the uploaded image straight into the ESP32 `Update` flash API
// (Update.begin / Update.write / Update.end / Update.abort), replies over the socket (server.send),
// and on success reboots (ESP.restart()). The only pure CPU-side logic in the module is the HTTP
// Basic-auth check (ota_check_auth), which is a static internal function that just delegates to the
// shared pc_base64_decode primitive (RFC 7617 credential decode) - that primitive is benched under
// its own umbrella, and it is not a separable part of ota_service's own surface. Unlike
// performance_benching/device/ads1115 - where the I2C transaction is stubbed but a deterministic config-word /
// conversion codec remains to bench - ota_service has no such codec to isolate, so the honest result
// is an empty benchmark (same call as performance_benching/device/i2c and performance_benching/device/mdns_service). We
// still #include the header and take (never call) the address of pc_ota_begin() so the compiler/linker prove the real
// production symbol is valid in this context.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/ota_service -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "server/update/ota_service.h"
#include <Arduino.h>

static void ota_service_bench_task(void *)
{
    // Reference the real production symbol so it is compiled/linked, but NEVER call it: invoking
    // pc_ota_begin() would mutate the live server's route table + parser stream hooks and wire up
    // the ESP32 `Update` flash path - side-effectful setup, not a pure microbench, and out of scope
    // for this peripheral-less rig. (PC is only forward-declared in ota_service.h; a function
    // pointer to a function taking `PC &` needs no complete type.)
    void (*volatile fn)(PC &, const char *, const char *, const char *) = &pc_ota_begin;
    (void)fn;

    for (;;)
    {
        Serial.printf("DB ==== ota_service device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        // NOTE-4 service: ota_service is a one-time registration over the ESP32 `Update` flash API
        // (real flash I/O + reboot), with no pure, hardware-independent codec to time. Nothing is
        // benched here on purpose - see the file header for the full rationale.
        Serial.println("DB (no pure op to bench: ota_service is Update-flash registration only)");
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: ota_service device microbench (no-op: Update-flash registration only)");
    xTaskCreatePinnedToCore(ota_service_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
