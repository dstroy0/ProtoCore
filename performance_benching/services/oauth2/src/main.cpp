// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the OAuth2 token-endpoint client core (services/security/oauth2):
// building the application/x-www-form-urlencoded request bodies for the authorization_code and
// refresh_token grants (with proper percent-encoding), and parsing the JSON token response into
// pc_o_auth2_tokens (reusing the library's zero-heap JSON reader). All three are pure - no heap, no
// stdlib, no sockets. A pure protocol codec like performance_benching/device/modbus, so every call here exercises
// the real production code path.
//
// Deliberately out of scope: the ESP32 HTTP(S) exchange convenience layer
// (pc_oauth2_exchange_code / pc_oauth2_refresh) is guarded by PC_ENABLE_HTTP_CLIENT, which is NOT
// enabled here - it POSTs over a real HTTP client (network I/O), which this rig cannot do, so only
// the deterministic CPU-side codec is benched (no transport is compiled in, hence nothing to stub).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/oauth2 -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/security/oauth2/oauth2.h"
#include <Arduino.h>

static void oauth2_bench_task(void *)
{
    // Realistic authorization_code parameters (redirect_uri needs percent-encoding of ':' and '/',
    // client_secret exercises the '!' -> %21 path) - shape copied from test/test_oauth2.
    static const char *kCode = "auth-code-123";
    static const char *kRedirect = "https://app.example/cb";
    static const char *kClientId = "client-42";
    static const char *kSecret = "s3cr!t";
    static const char *kVerifier = "verifier_xyz-123~ABCdef.0987";
    static const char *kRefresh = "rt-token-abcdef0123456789";

    // A spec-conformant token-endpoint JSON response (literal reused from test/test_oauth2).
    static const char *kJson = "{\"access_token\":\"AT123\",\"token_type\":\"Bearer\",\"expires_in\":3600,"
                               "\"refresh_token\":\"RT456\",\"id_token\":\"eyJ.x.y\"}";

    static char body[512];
    static pc_o_auth2_tokens tok;

    for (;;)
    {
        Serial.printf("DB ==== oauth2 device microbench start (CCOUNT @ %u MHz) ====\n",
                      (unsigned)getCpuFrequencyMhz());
        volatile int sinki = 0;
        volatile bool sinkb = false;

        // Confidential client: build authorization_code form body (percent-encodes redirect_uri + secret).
        DBENCH_OP("pc_oauth2_build_code_request", 100000,
                  sinki +=
                  pc_oauth2_build_code_request(kCode, kRedirect, kClientId, kSecret, nullptr, body, sizeof(body)));
        // Public client with PKCE: no secret, appends &code_verifier.
        DBENCH_OP("pc_oauth2_build_code_pkce", 100000,
                  sinki +=
                  pc_oauth2_build_code_request(kCode, kRedirect, kClientId, nullptr, kVerifier, body, sizeof(body)));
        // refresh_token grant form body.
        DBENCH_OP("pc_oauth2_build_refresh_request", 100000,
                  sinki += pc_oauth2_build_refresh_request(kRefresh, kClientId, kSecret, body, sizeof(body)));
        // Parse the JSON token response (reuses the zero-heap JSON reader).
        DBENCH_OP("pc_oauth2_parse_token_response", 50000, sinkb ^= pc_oauth2_parse_token_response(kJson, &tok));

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
    Serial.println("\nDB boot: oauth2 device microbench");
    xTaskCreatePinnedToCore(oauth2_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
