// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for JWT HS256 bearer-auth verification: pc_jwt_verify_hs256() splits the compact
// token, enforces alg==HS256 (rejecting alg=none / RS256 / HS384), HMAC-SHA256s the signing input,
// base64url-encodes the MAC, and constant-time compares it - the per-request auth hot op. Pure (no socket),
// but it pulls in base64 + HMAC-SHA256 + SHA-256, so link those. The device figure comes from the rig /bench
// pc_jwt_verify_hs256 op; this host ns/op is a RELATIVE baseline. Build + run:
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support -DPC_ENABLE_JWT=1 performance_benching/services/jwt/host.cpp \
//       src/services/security/jwt/jwt.cpp src/network_drivers/presentation/codec/base64/base64.cpp \
//       src/network_drivers/tls/pc_hmac_sha256.cpp \
//       src/network_drivers/tls/pc_sha256.cpp -o /tmp/bjwt && /tmp/bjwt

#define PC_ENABLE_JWT 1
#include "services/security/jwt/jwt.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

using clk = std::chrono::steady_clock;

template <typename F> static double bench_ns(uint64_t iters, F fn)
{
    auto t0 = clk::now();
    for (uint64_t i = 0; i < iters; i++)
    {
        fn();
    }
    auto t1 = clk::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    return ns / (double)iters;
}

static void row(const char *feature, const char *op, double ns_per_op, double bytes_per_op)
{
    double mbps = bytes_per_op > 0 ? (bytes_per_op / (ns_per_op * 1e-9)) / 1e6 : 0.0;
    printf("| %-12s | %-24s | %10.1f | %9.1f |\n", feature, op, ns_per_op, mbps);
}

int main()
{
    // A well-formed HS256 token (alg=HS256 header) so the full verify path runs (split + alg check + HMAC +
    // base64url + ct-compare). The signature need not match to time the work.
    const char *tok = "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9."
                      "eyJzdWIiOiJyaWciLCJyb2xlIjoiYWRtaW4iLCJleHAiOjE5MDAwMDAwMDB9."
                      "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    const size_t tlen = strlen(tok);
    const uint8_t secret[] = "pc-rig-jwt-secret-2026";

    printf("| Feature      | Operation                |     ns/op |    MB/s |\n");
    printf("|--------------|--------------------------|-----------|---------|\n");

    // verify one bearer token (split + alg check + HMAC-SHA256 + base64url + constant-time compare).
    {
        volatile int sink = 0;
        double ns =
            bench_ns(2000000, [&] { sink += pc_jwt_verify_hs256(tok, tlen, secret, sizeof(secret) - 1) ? 1 : 0; });
        row("jwt", "pc_jwt_verify_hs256", ns, (double)tlen);
        (void)sink;
    }

    return 0;
}
