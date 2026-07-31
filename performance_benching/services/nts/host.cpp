// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the NTS framing codecs (RFC 8915): the NTS-KE record builder/parser (the
// TLS-1.3 key-establishment exchange on :4460) and the NTP extension-field builder (per-packet). All pure
// framing - no TLS, no AEAD, no socket - so it links standalone; the crypto (AES-SIV-CMAC-256 + the TLS
// exporter) sits on top and is not part of these hot ops. The device figure comes from the rig /bench op;
// this host ns/op + MB/s is a RELATIVE baseline. Build + run:
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support -DPC_ENABLE_NTS=1 performance_benching/services/nts/host.cpp \
//       src/services/timing_position/nts/nts.cpp -o /tmp/bn && /tmp/bn

#define PC_ENABLE_NTS 1
#include "services/timing_position/nts/nts.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>

using clk = std::chrono::steady_clock;

template <typename F> static double bench_ns(uint64_t iters, F fn)
{
    auto t0 = clk::now();
    for (uint64_t i = 0; i < iters; i++)
        fn();
    auto t1 = clk::now();
    double ns = std::chrono::duration<double, std::nano>(t1 - t0).count();
    return ns / (double)iters;
}

static void row(const char *feature, const char *op, double ns_per_op, double bytes_per_op)
{
    double mbps = bytes_per_op > 0 ? (bytes_per_op / (ns_per_op * 1e-9)) / 1e6 : 0.0;
    printf("| %-12s | %-26s | %10.1f | %9.1f |\n", feature, op, ns_per_op, mbps);
}

static void ke_count_cb(bool, uint16_t, const uint8_t *, size_t, void *arg)
{
    (*(size_t *)arg)++;
}

int main()
{
    uint8_t out[512];

    // A realistic NTS-KE server response: next-protocol NTPv4, the AES-SIV-CMAC-256 AEAD, one 32-byte
    // cookie, and the end-of-message record - the stream the client parses on every key establishment.
    uint8_t resp[512];
    size_t rl = 0;
    const uint8_t next_proto[2] = {0x00, Nts::NTS_NEXT_PROTO_NTPV4};
    const uint8_t aead[2] = {0x00, (uint8_t)Nts::NTS_AEAD_AES_SIV_CMAC_256};
    uint8_t cookie[32];
    for (int i = 0; i < 32; i++)
        cookie[i] = (uint8_t)(i * 7 + 3);
    rl += pc_nts_ke_record(true, Nts::NTS_KE_NEXT_PROTOCOL, next_proto, 2, resp + rl, sizeof(resp) - rl);
    rl += pc_nts_ke_record(true, Nts::NTS_KE_AEAD_ALGORITHM, aead, 2, resp + rl, sizeof(resp) - rl);
    rl += pc_nts_ke_record(false, Nts::NTS_KE_COOKIE, cookie, sizeof(cookie), resp + rl, sizeof(resp) - rl);
    rl += pc_nts_ke_record(true, Nts::NTS_KE_END_OF_MESSAGE, nullptr, 0, resp + rl, sizeof(resp) - rl);

    uint8_t nonce[16];
    for (int i = 0; i < 16; i++)
        nonce[i] = (uint8_t)(i * 11 + 1);

    printf("| Feature      | Operation                  |     ns/op |    MB/s |\n");
    printf("|--------------|----------------------------|-----------|---------|\n");

    // Build the NTS-KE request (client hello: next-protocol + AEAD offer). Once per key establishment.
    {
        size_t req_len = pc_nts_ke_request(out, sizeof(out));
        volatile size_t sink = 0;
        double ns = bench_ns(2000000, [&] { sink += pc_nts_ke_request(out, sizeof(out)); });
        row("nts", "ke_request (build)", ns, (double)req_len);
        (void)sink;
    }

    // Parse the NTS-KE server response stream (walk every record to EOM). The client receive hot op.
    {
        volatile size_t sink = 0;
        double ns = bench_ns(2000000, [&] {
            size_t n = 0;
            if (pc_nts_ke_parse(resp, rl, ke_count_cb, &n))
                sink += n;
        });
        row("nts", "ke_parse (server response)", ns, (double)rl);
        (void)sink;
    }

    // Build a Unique-Identifier EF (RFC 8915 5.3) - on every NTS-protected NTP request the client sends.
    {
        size_t ef_len = pc_nts_ef_unique_id(nonce, sizeof(nonce), out, sizeof(out));
        volatile size_t sink = 0;
        double ns = bench_ns(5000000, [&] { sink += pc_nts_ef_unique_id(nonce, sizeof(nonce), out, sizeof(out)); });
        row("nts", "ef_unique_id (build)", ns, (double)ef_len);
        (void)sink;
    }

    // Build a Cookie EF - carried on every NTS-protected NTP request (one cookie spent per exchange).
    {
        size_t ck_len = pc_nts_ef_cookie(cookie, sizeof(cookie), out, sizeof(out));
        volatile size_t sink = 0;
        double ns = bench_ns(5000000, [&] { sink += pc_nts_ef_cookie(cookie, sizeof(cookie), out, sizeof(out)); });
        row("nts", "ef_cookie (build)", ns, (double)ck_len);
        (void)sink;
    }

    return 0;
}
