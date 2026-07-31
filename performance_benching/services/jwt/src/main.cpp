// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the JWT HS256 bearer-auth codec (services/security/jwt): the whole
// verifier is pure crypto/codec - pc_jwt_verify_hs256() splits the compact token, checks the
// header "alg", recomputes base64url(HMAC-SHA-256(secret, "header.payload")) and compares it
// constant-time against the signature; the claim parsers base64url-decode the payload and scan the
// JSON. No sockets, no sessions, no heap - every call here runs the real production path (like
// performance_benching/device/modbus, a pure protocol codec, and unlike performance_benching/device/ads1115 where a bus
// transaction is stubbed; JWT touches no peripheral, so nothing needs stubbing). The reference token, secret, and
// expected claim values are copied verbatim from test/test_jwt/test_jwt.cpp (known-good, HMAC-signed with Python for
// secret "s3cr3t-key").
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/jwt -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/security/jwt/jwt.h"
#include <Arduino.h>
#include <string.h>

// Reference token + secret straight out of test/test_jwt/test_jwt.cpp: HS256 over payload
// {"sub":"alice","role":"admin","exp":2000000000,"iat":1700000000} with secret "s3cr3t-key".
static const char *SECRET = "s3cr3t-key";
static const char *TOKEN = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
                           "eyJzdWIiOiJhbGljZSIsInJvbGUiOiJhZG1pbiIsImV4cCI6MjAwMDAwMDAwMCwiaWF0IjoxNzAwMDAwMDAwfQ."
                           "oaEaMu7USfUlYDaLYQlogmRd_1ZPBr7cKrPIo5lXdxc";

static void jwt_bench_task(void *)
{
    const uint8_t *secret = (const uint8_t *)SECRET;
    const size_t secret_len = strlen(SECRET);
    const size_t token_len = strlen(TOKEN);

    // Full Authorization header value for the bearer-path bench.
    static char auth_hdr[PC_AUTH_HDR_CAP_JWT];
    snprintf(auth_hdr, sizeof(auth_hdr), "Bearer %s", TOKEN);

    // A representative space-separated OAuth2 scope claim for the pure-string allow check.
    static const char *scope_claim = "read write admin";

    for (;;)
    {
        Serial.printf("DB ==== jwt device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        volatile int sink = 0;
        volatile long lsink = 0;
        char strbuf[32];

        // --- Crypto path: real HMAC-SHA-256 over ~100 bytes + base64url encode/compare (small N). ---
        DBENCH_OP("pc_jwt_verify_hs256", 2000, sink += pc_jwt_verify_hs256(TOKEN, token_len, secret, secret_len));
        DBENCH_OP("pc_jwt_bearer_valid", 2000, sink += pc_jwt_bearer_valid(auth_hdr, secret, secret_len));

        // --- Codec path: base64url-decode the payload + scan JSON for a claim (medium N). ---
        DBENCH_OP("pc_jwt_claim_int", 10000, sink += pc_jwt_claim_int(TOKEN, token_len, "exp", (long *)&lsink));
        DBENCH_OP("pc_jwt_claim_str", 10000, sink += pc_jwt_claim_str(TOKEN, token_len, "sub", strbuf, sizeof(strbuf)));

        // --- Pure string path: whole-token scope match, no decode (large N). ---
        DBENCH_OP("pc_jwt_scope_allows", 50000, sink += pc_jwt_scope_allows(scope_claim, "admin"));

        (void)sink;
        (void)lsink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: jwt device microbench");
    xTaskCreatePinnedToCore(jwt_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
