// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the provisioning form-field parser (services/system/provisioning_service):
// pc_prov_form_field() locates a whole field name in an x-www-form-urlencoded body (matching only at
// the start or just after '&'), then copies its value while URL-decoding '+' -> space and '%XX' hex
// escapes into a caller buffer - pure, no heap, no I/O. This is the ONE non-trivial, always-compiled
// piece of the service and the only part the host test suite (test/test_matrix.json) exercises.
//
// Deliberately OUT OF SCOPE: the captive portal proper - softAP, the lwIP/UDP catch-all DNS responder,
// and NVS credential persistence (Preferences). Those live behind `#if PC_ENABLE_PROVISIONING &&
// defined(ARDUINO)` and need WiFi/transport/flash peripherals this bare S3 devkit has nothing wired to,
// so - exactly like the host test - we leave PC_ENABLE_PROVISIONING at its default 0 and bench only
// the parser (no stubbing required: the parser has no external dependency beyond the pure hex helper).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/provisioning_service -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/system/provisioning_service/provisioning_service.h"
#include <Arduino.h>

static void provisioning_service_bench_task(void *)
{
    // Realistic first-boot credentials POST body: an SSID that needs '+'-decoding and a passphrase
    // that needs '%XX'-decoding, with a trailing field so a lookup has to walk past a match too.
    // "My+Home+AP" -> "My Home AP", "p%40ssw0rd%21" -> "p@ssw0rd!".
    static const char body[] = "ssid=My+Home+AP&psk=p%40ssw0rd%21&save=1";
    static char ssid[33]; // matches prov_save_handler's on-target buffer
    static char psk[64];  // matches prov_save_handler's on-target buffer

    for (;;)
    {
        Serial.printf("DB ==== provisioning_service device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile size_t sink = 0;

        // First field, '+'-decoded value.
        DBENCH_OP("pc_prov_form_field ssid (+)", 100000, sink += pc_prov_form_field(body, "ssid", ssid, sizeof(ssid)));
        // Second field, '%XX'-decoded value (the hot path through pc_hex_val).
        DBENCH_OP("pc_prov_form_field psk (%XX)", 100000, sink += pc_prov_form_field(body, "psk", psk, sizeof(psk)));
        // Absent field: whole-body scan that never matches (worst-case lookup).
        DBENCH_OP("pc_prov_form_field miss", 100000, sink += pc_prov_form_field(body, "channel", ssid, sizeof(ssid)));
        (void)sink;

        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: provisioning_service device microbench");
    xTaskCreatePinnedToCore(provisioning_service_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
