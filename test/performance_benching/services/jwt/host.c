// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for JWT HS256 bearer-auth verification: pc_jwt_verify_hs256() splits the compact
// token, enforces alg==HS256 (rejecting alg=none / RS256 / HS384), HMAC-SHA256s the signing input,
// base64url-encodes the MAC, and constant-time compares it - the per-request auth hot op. Pure (no socket),
// but it pulls in base64 + HMAC-SHA256 + SHA-256, so link those. The device figure comes from the rig /bench
// pc_jwt_verify_hs256 op; this host ns/op is a relative baseline. Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/mocks -Itest/support -Itest/performance_benching/common
//   -DPROTOCORE_ENABLE_JWT=1 test/performance_benching/services/jwt/host.c
//   src/services/security/jwt/jwt.c src/network_drivers/presentation/codec/base64/base64.c
//   src/crypto/mac/hmac_sha256.c src/crypto/hash/sha256.c src/mmgr/secure.c src/mmgr/arena.c
//   src/mmgr/protomem.c src/mmgr/protostr.c -o /tmp/bjwt && /tmp/bjwt

#define PROTOCORE_ENABLE_JWT 1
#include "services/security/jwt/jwt.h"

#include "host_bench.h"
#include <stdint.h>
#include <string.h>

int main(void)
{
    // A well-formed HS256 token (alg=HS256 header) so the full verify path runs (split + alg check + HMAC +
    // base64url + ct-compare). The signature need not match to time the work.
    const char *tok = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
                      "eyJzdWIiOiJyaWciLCJyb2xlIjoiYWRtaW4iLCJleHAiOjE5MDAwMDAwMDB9."
                      "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    const size_t tlen = strlen(tok);
    const uint8_t secret[] = "pc-rig-jwt-secret-2026";

    hbench_header();

    // verify one bearer token (split + alg check + HMAC-SHA256 + base64url + constant-time compare).
    {
        volatile int sink = 0;
        double ns = 0.0;
        HBENCH_NS(2000000, sink += pc_jwt_verify_hs256(tok, tlen, secret, sizeof(secret) - 1) ? 1 : 0, ns);
        hbench_row("jwt", "pc_jwt_verify_hs256", ns, (double)tlen);
        (void)sink;
    }

    return 0;
}
