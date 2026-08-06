// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the OIDC RS256 ID-token verifier (services/security/oidc): the pure
// relying-party path that parses a compact JWT, resolves the signing key from a JWKS document, and
// verifies the RSASSA-PKCS1-v1.5 SHA-256 signature. Four pure operations are timed:
//   * pc_oidc_token_kid        - base64url-decode the header and scan out the `kid` (cheap, no crypto)
//   * pc_oidc_jwks_find        - scan the JWKS JSON and base64url-decode the RSA modulus/exponent
//   * pc_oidc_verify_with_key  - the headline op: real RSA-2048 modexp (pc_rsa_verify, mbedTLS-
//                                 accelerated on ESP32) over a pre-resolved key + all claim checks
//   * pc_oidc_verify           - the end-to-end entry point (jwks_find + verify_with_key combined)
// Everything here is deterministic CPU work over fixed BSS/scratch buffers - the verifier never
// fetches anything (JWKS/discovery retrieval over HTTPS is the caller's job and is deliberately out
// of scope on this rig, which has no network attached). The cheap parse ops get a large N; the two
// crypto-bearing verify ops get a small N (mirroring ssh_rsa_2048_verify's N=8 in the crypto rig),
// since each does a full RSA public-key operation.
//
// Vectors (token, JWKS, iss/aud/now) are copied verbatim from test/test_oidc/test_oidc.cpp - real
// openssl-signed RSA-2048 RS256 material, known-good and spec-conformant (K_TOK_VALID verifies
// against K_JWKS at NOW).
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   pio run -d performance_benching/device/oidc -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "mmgr/plaintext.h" // pc_plaintext_reset() (the verify path borrows from this arena)
#include "services/security/oidc/oidc.h"
#include <Arduino.h>

// --- known-good RS256 vectors, copied verbatim from test/test_oidc/test_oidc.cpp ---
// A valid RSA-2048 RS256 ID token (kid "test-key-1") whose signature verifies against K_JWKS.
static const char *K_TOK_VALID =
    "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCIsImtpZCI6InRlc3Qta2V5LTEifQ.eyJpc3MiOiJodHRwczovL2lzc3Vlci5leGFtcGxlIiwiYXVkIj"
    "oiY2xpZW50LTEyMyIsInN1YiI6InVzZXItNDIiLCJlbWFpbCI6ImFsaWNlQGV4YW1wbGUuY29tIiwiaWF0IjoxNzAwMDAwMDAwLCJleHAiOjQxMDI0"
    "NDQ4MDB9.QBKta3RxQh8K5FiNUL6ZJemBvIGmDsgZFhF5FbEkjcmbgNsbPZsLl70N0UFwGbsAY5sGoUZxPhWdl-_VT2nXQnwbCVqIrztVibBj4wlwL"
    "e_jY28M5OLf9t3mT5YaxWmV2FWUS_2yTYBEY96CweeoEeX_oSUSOJZyxZ_e8vbET9RcQU4zSx1sDwK6X9dUHZvs8QmrVCdqFP3OAUq0aqguj87Wcor"
    "YvCmB8nBVwYqmNupvRPP8bBPh9nTISEkP6uTQKVJKrnp5C6JN2puWS06G-YzX6HJW3nS2lHj3iI7_6xqD_pI0eXh720ZwzYSK6PPbn9EHiiw0WuaBt"
    "6G9QwR-Rg";
// The matching JWKS: one RSA key carrying n/e for kid "test-key-1".
static const char *K_JWKS =
    "{\"keys\":[{\"kty\":\"RSA\",\"use\":\"sig\",\"kid\":\"test-key-1\",\"alg\":\"RS256\","
    "\"n\":\"owVI9J0dgUuZi6j45vERbQdj4OuB7vY-o7k6IWGdckzCfaNaeEdbaCvK7j2OpEKyKWHsgYAcqQJXbSCnQQuIvbJPNJu8iWAqwZsakKmS0Y"
    "0aP9HIaPvlMi8pN6ihVH8lutfeWVwhn-5_V_4Y5UWRJSzZXNUplH5TML2fVWlkU1XqkbZVnkyrAqK4N8ic1Is80g4JDJvMWNRR2cltCj1hjudBiLza"
    "xwCMRp6lvXV0YsYUL-Hw3gFSSDp4MiH0zkvivV1t3qq8OG9tBUcvZtrW0D7f5FP2YqV30XJko1PaCfnwl3KsiJwno4wK0hybd9ujjDSLW7DhKE88oC"
    "W0Y8YMRQ\",\"e\":\"AQAB\"}]}";

static const char *ISS = "https://issuer.example";
static const char *AUD = "client-123";
static const uint32_t NOW = 1700000100; // after iat(1700000000), before exp(4102444800)

static void oidc_bench_task(void *)
{
    pc_plaintext_reset(); // start from an empty per-dispatch arena (verify borrows ~2.6 KB from it)

    // Pre-resolve the signing key once so pc_oidc_verify_with_key benches the crypto path in
    // isolation (the JWKS scan/decode is timed separately by pc_oidc_jwks_find below).
    pc_oidc_key key;
    key.loaded = false;
    bool key_ok = pc_oidc_jwks_find(K_JWKS, "test-key-1", &key);

    const size_t tok_len = strlen(K_TOK_VALID);
    char kid[PC_OIDC_KID_LEN];

    for (;;)
    {
        Serial.printf("DB ==== oidc device microbench start (CCOUNT @ %u MHz) ====\n", (unsigned)getCpuFrequencyMhz());
        Serial.printf("DB key_resolved=%d (0 would mean the verify blocks measure the fail path)\n", (int)key_ok);

        volatile int sink = 0;

        // Cheap parse ops (no crypto): large N.
        DBENCH_OP("pc_oidc_token_kid", 20000, sink += (int)pc_oidc_token_kid(K_TOK_VALID, tok_len, kid, sizeof(kid)));
        DBENCH_OP("pc_oidc_jwks_find", 5000, sink += (int)pc_oidc_jwks_find(K_JWKS, "test-key-1", &key));

        // Crypto-bearing verify ops: real RSA-2048 public-key modexp -> small N (cf. ssh_rsa verify).
        DBENCH_OP("pc_oidc_verify_with_key", 64,
                  sink += (int)pc_oidc_verify_with_key(K_TOK_VALID, tok_len, &key, ISS, AUD, NOW, nullptr));
        DBENCH_OP("pc_oidc_verify (end-to-end)", 64,
                  sink += (int)pc_oidc_verify(K_TOK_VALID, tok_len, K_JWKS, ISS, AUD, NOW, nullptr));

        (void)sink;
        Serial.println("DB ==== DONE ====");
        vTaskDelay(5000 / portTICK_PERIOD_MS);
    }
}

void setup()
{
    Serial.begin(115200);
    delay(2500); // let USB-CDC enumerate so the banner is captured
    Serial.println("\nDB boot: oidc device microbench");
    xTaskCreatePinnedToCore(oidc_bench_task, "dbench", 16384, nullptr, 24, nullptr, 1);
}

void loop()
{
    delay(1000);
}
