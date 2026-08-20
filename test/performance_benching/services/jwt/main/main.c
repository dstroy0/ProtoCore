// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for the JWT HS256 bearer-auth codec (services/security/jwt): the whole
// verifier is pure crypto/codec - Jwt.verify_mac() splits the compact token, checks the
// header "alg", recomputes base64url(HMAC-SHA-256(secret, "header.payload")) and compares it
// constant-time against the signature; the claim parsers base64url-decode the payload and scan the
// JSON. No sockets, no sessions, no heap - every call here runs the real production path (like
// performance_benching/device/modbus, a pure protocol codec, and unlike performance_benching/device/ads1115 where a bus
// transaction is stubbed; JWT touches no peripheral, so nothing needs stubbing). The reference token, secret, and
// expected claim values are copied verbatim from test/test_jwt/test_jwt.cpp (known-good, HMAC-signed with Python for
// secret "s3cr3t-key").
//
// Build/flash (JTAG-capable S3 over its USB-Serial/JTAG port):
//   idf.py -C test/performance_benching/jwt -t upload --upload-port COM7
// then open the port to capture the repeating "DB ..." lines (each run repeats every ~5 s, so a
// capture opened at any time still catches a full cycle).
#include "device_bench.h"
#include "services/security/jwt/jwt.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t jwt_work[16]; // the borrow an entry takes; Jwt never reads it

// Reference token + secret straight out of test/test_jwt/test_jwt.cpp: HS256 over payload
// {"sub":"alice","role":"admin","exp":2000000000,"iat":1700000000} with secret "s3cr3t-key".
static const char *SECRET = "s3cr3t-key";
static const char *TOKEN = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
                           "eyJzdWIiOiJhbGljZSIsInJvbGUiOiJhZG1pbiIsImV4cCI6MjAwMDAwMDAwMCwiaWF0IjoxNzAwMDAwMDAwfQ."
                           "oaEaMu7USfUlYDaLYQlogmRd_1ZPBr7cKrPIo5lXdxc";

void dbench_run(void)
{
    const uint8_t *secret = (const uint8_t *)SECRET;
    const size_t secret_len = strlen(SECRET);
    const size_t token_len = strlen(TOKEN);

    // Full Authorization header value for the bearer-path bench.
    static char auth_hdr[PROTOCORE_AUTH_HDR_CAP_JWT];
    snprintf(auth_hdr, sizeof(auth_hdr), "Bearer %s", TOKEN);

    // A representative space-separated OAuth2 scope claim for the pure-string allow check.
    static const char *scope_claim = "read write admin";

    for (;;)
    {
        DBENCH_BANNER("jwt");
        volatile int sink = 0;
        volatile long lsink = 0;
        char strbuf[32];

        // --- Crypto path: real HMAC-SHA-256 over ~100 bytes + base64url encode/compare (small N). ---
        Jwt.token.jws = TOKEN;
        Jwt.token.jws_len = token_len;
        Jwt.key.secret = secret;
        Jwt.key.secret_len = secret_len;
        DBENCH_OP("Jwt.verify_mac", 2000, Jwt.verify_mac(jwt_work); sink += Jwt.ok ? 1 : 0);

        Jwt.token.credentials = auth_hdr;
        DBENCH_OP("Jwt.verify_bearer", 2000, Jwt.verify_bearer(jwt_work); sink += Jwt.ok ? 1 : 0);

        // --- Codec path: base64url-decode the payload + scan JSON for a claim (medium N). ---
        Jwt.claim.name = "exp";
        DBENCH_OP("Jwt.claim_int", 10000, Jwt.claim_int(jwt_work); lsink = Jwt.num; sink += Jwt.ok ? 1 : 0);

        Jwt.claim.name = "sub";
        Jwt.claim.out = strbuf;
        Jwt.claim.out_cap = sizeof(strbuf);
        DBENCH_OP("Jwt.claim_str", 10000, Jwt.claim_str(jwt_work); sink += Jwt.ok ? 1 : 0);

        // --- Pure string path: whole-token scope match, no decode (large N). ---
        Jwt.scope.claim = scope_claim;
        Jwt.scope.required = "admin";
        DBENCH_OP("Jwt.scope_allows", 50000, Jwt.scope_allows(jwt_work); sink += Jwt.ok ? 1 : 0);

        (void)sink;
        (void)lsink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("jwt")
