// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the SSH server's hot cryptographic ops: the curve25519 KEX
// (x25519 ephemeral gen + shared secret), the ssh-ed25519 host-key signature over the exchange hash
// (the one-time-per-connection handshake cost), and the chacha20-poly1305@openssh.com record layer
// (the per-packet, steady-state throughput op). Pure (no socket / no lwIP), but it pulls in the field
// arithmetic + SHA-512 + ChaCha20 + Poly1305, so link those.
//
// NOTE: on host, curve25519/ed25519 field inversion runs in software (Fermat a^(p-2)); on the ESP32 it
// is offloaded to the MPI/RSA accelerator, so the DEVICE figures come from the rig /bench ssh ops and
// this host ns/op is a RELATIVE baseline. Build + run:
//   g++ -O2 -std=c++17 -Isrc -Itest/mocks -Itest/support -DPC_ENABLE_SSH=1
//   performance_benching/network_drivers/presentation/ssh/host.cpp \
//       src/network_drivers/presentation/ssh/crypto/pc_curve25519.cpp \
//       src/network_drivers/presentation/ssh/crypto/pc_ed25519.cpp \
//       src/network_drivers/presentation/ssh/crypto/ssh_sha512.cpp \
//       src/network_drivers/presentation/ssh/crypto/pc_chacha20.cpp \
//       src/network_drivers/presentation/ssh/crypto/pc_poly1305.cpp \
//       src/network_drivers/presentation/ssh/crypto/pc_chachapoly.cpp -o /tmp/bssh && /tmp/bssh

#define PC_ENABLE_SSH 1
#include "crypto/aead/chachapoly.h"
#include "crypto/asymmetric/curve25519.h"
#include "crypto/asymmetric/ed25519.h"

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
    printf("| %-12s | %-30s | %10.1f | %9.1f |\n", feature, op, ns_per_op, mbps);
}

int main()
{
    volatile uint32_t sink = 0;

    printf("| Feature      | Operation                      |     ns/op |    MB/s |\n");
    printf("|--------------|--------------------------------|-----------|---------|\n");

    // KEX: X25519 ephemeral public key = scalarmult(sk, base). One per connection.
    uint8_t sk[32], pk[32], peer_pk[32], shared[32];
    memset(sk, 0x11, sizeof(sk));
    memset(peer_pk, 0x22, sizeof(peer_pk));
    {
        double ns = bench_ns(4000, [&] {
            pc_x25519_base(pk, sk);
            sink += pk[0];
        });
        row("ssh", "x25519_base (KEX ephemeral)", ns, 0);
    }

    // KEX: X25519 shared secret = scalarmult(sk, Q_C). One per connection.
    {
        double ns = bench_ns(4000, [&] {
            pc_x25519(shared, sk, peer_pk);
            sink += shared[0];
        });
        row("ssh", "x25519 (KEX shared secret)", ns, 0);
    }

    // Field arithmetic: the radix-2^16 schoolbook multiply / square (pc_gf = int64[16]). This is the
    // innermost hot op of the Montgomery ladder (~9 field mul/sq per ladder step x 255 steps) and thus the
    // dominant cost of every X25519 / ed25519 scalar multiplication - the target for S3 vector (QACC) SIMD.
    pc_gf ga, gb, go;
    for (int k = 0; k < 16; k++)
    {
        ga[k] = 0x5a5a + k;
        gb[k] = 0x1234 - k;
    }
    {
        double ns = bench_ns(200000, [&] {
            pc_gf_mul(go, ga, gb);
            sink += (uint32_t)go[0];
        });
        row("ssh", "gf_mul (field 16x16 mul)", ns, 0);
    }
    {
        double ns = bench_ns(200000, [&] {
            pc_gf_sq(go, ga);
            sink += (uint32_t)go[0];
        });
        row("ssh", "gf_sq (field square)", ns, 0);
    }

    // Host key: ssh-ed25519 signature over the 32-byte exchange hash H. One per connection.
    uint8_t seed[32], sig[64], hash[32];
    memset(seed, 0x33, sizeof(seed));
    memset(hash, 0x44, sizeof(hash));
    {
        double ns = bench_ns(2000, [&] {
            pc_ed25519_sign(sig, hash, sizeof(hash), seed);
            sink += sig[0];
        });
        row("ssh", "ed25519_sign (host key)", ns, 0);
    }

    // Record layer: chacha20-poly1305@openssh.com encrypt of a full 1 KB data packet. The per-packet
    // steady-state op, so its throughput (MB/s over the payload) is the interesting figure.
    const uint32_t plen = 1024;
    uint8_t key[PC_CHACHAPOLY_KEY_LEN];
    memset(key, 0x55, sizeof(key));
    static uint8_t src[4 + 1024];
    static uint8_t dst[4 + 1024 + PC_CHACHAPOLY_TAG_LEN];
    src[0] = 0;
    src[1] = 0;
    src[2] = (uint8_t)(plen >> 8);
    src[3] = (uint8_t)plen;
    memset(src + 4, 0x66, plen);
    {
        uint32_t seq = 0;
        double ns = bench_ns(40000, [&] {
            pc_chachapoly_encrypt(key, seq++, dst, src, plen);
            sink += dst[0];
        });
        row("ssh", "chacha20-poly1305 enc (1 KB)", ns, (double)plen);
    }

    (void)sink;
    return 0;
}
