// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Host-side microbenchmark for the base64 codec (network_drivers/presentation/codec/base64): encode +
// decode of a 1 KiB payload. A deterministic ns/op + MB/s baseline that complements the on-device
// ESP32-S3 numbers; the host figure is a relative baseline (a fast desktop/RPi core), not the device
// cost. Build + run:
//   gcc -O2 -std=c11 -I. -Isrc -Itest/performance_benching/common
//   test/performance_benching/network_drivers/presentation/base64/host.c
//   src/network_drivers/presentation/codec/base64/base64.c -o /tmp/hb && /tmp/hb

#include "network_drivers/presentation/codec/base64/base64.h"

#include "host_bench.h"
#include <stdint.h>
#include <string.h>

int main(void)
{
    hbench_header();

    const size_t N = 1024;
    uint8_t src[N];
    for (size_t i = 0; i < N; i++)
    {
        src[i] = (uint8_t)(i * 31 + 7);
    }
    char enc[((N + 2) / 3) * 4 + 1];
    uint8_t dec[N];
    volatile size_t sink = 0;

    double ns_e = 0.0;
    HBENCH_NS(200000, Base64.encode(src, N, enc), ns_e);
    hbench_row("base64", "encode 1 KiB", ns_e, (double)N);

    double ns_d = 0.0;
    HBENCH_NS(200000, sink += Base64.decode(enc, dec, sizeof(dec)), ns_d);
    hbench_row("base64", "decode 1 KiB", ns_d, (double)N);

    (void)sink;
    return 0;
}
