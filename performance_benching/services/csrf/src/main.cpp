// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the stateless HMAC-signed CSRF token (services/security/csrf):
// pc_csrf_issue() builds a fresh `<nonce_hex>.<sig_hex>` token (HMAC-SHA256 over a 6-byte nonce,
// truncated + hex-encoded) and pc_csrf_verify() recomputes that HMAC and constant-time compares
// it - both pure (no Arduino, no sockets, no heap). Also bench the shared hex codec
// (shared_primitives/hex.h) the token layer builds on, since encode/decode of the nonce and
// signature bytes is on the same hot path as issue/verify. Worked example for performance_benching/device/<service>/:
// a pure protocol codec with no hardware involved, so every call here exercises the real production
// code path (contrast with performance_benching/device/ads1115, a peripheral driver where the bus transaction itself
// is stubbed). Out of scope: the hardware-RNG secret seeding a real begin() does once at boot - the
// bench seeds a fixed secret instead, exactly like test/test_csrf/test_csrf.cpp, so issue/verify are
// deterministic and comparable run to run.
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/csrf -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/security/csrf/csrf.h"
#include "shared_primitives/hex.h"
#include <Arduino.h>

// Same fixed secret used by test/test_csrf/test_csrf.cpp - deterministic, known-good.
static const uint8_t SECRET[32] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa,
                                   0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x10, 0x32, 0x54, 0x76, 0x98, 0xba,
                                   0xdc, 0xfe, 0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef};

static void csrf_bench_task(void *)
{
    pc_csrf_set_secret(SECRET, sizeof(SECRET));

    static char token[CSRF_TOKEN_BUF];
    int tlen = pc_csrf_issue(token, sizeof(token)); // seed a valid token for the verify bench
    (void)tlen;

    static const uint8_t raw6[CSRF_NONCE_BYTES] = {0xde, 0xad, 0xbe, 0xef, 0x01, 0x02};
    static char hex_out[CSRF_NONCE_BYTES * 2 + 1];
    pc_hex_encode(raw6, sizeof(raw6), hex_out);
    static uint8_t bin_out[CSRF_NONCE_BYTES];

    for (;;)
    {
        Serial.printf("DB ==== csrf device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile int sinki = 0;
        volatile bool sinkb = false;

        DBENCH_OP("pc_csrf_issue", 20000, sinki += pc_csrf_issue(token, sizeof(token)));
        DBENCH_OP("pc_csrf_verify", 20000, sinkb = pc_csrf_verify(token));
        DBENCH_OP("pc_hex_encode (6B nonce)", 50000, pc_hex_encode(raw6, sizeof(raw6), hex_out));
        DBENCH_OP("pc_hex_decode (6B nonce)", 50000,
                  sinki += pc_hex_decode(hex_out, sizeof(hex_out) - 1, bin_out, sizeof(bin_out)));

        (void)sinki;
        (void)sinkb;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: csrf device microbench");
    xTaskCreatePinnedToCore(csrf_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
