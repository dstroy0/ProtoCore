// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// On-device CCOUNT microbenchmark for representative SSH crypto primitives (network_drivers/
// crypto): SHA-256 and the ChaCha20 stream cipher (bulk). The full crypto suite is
// exercised in depth by penetration_testing/rig_firmware/main_cryptobench; this is the performance_benching/
// counterpart. Build/flash: pio run -d performance_benching/network_drivers/presentation/ssh -t upload
#include "crypto/cipher/chacha20.h"
#include "crypto/hash/sha256.h"
#include "device_bench.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static uint8_t tw[4096]; // test-side working bytes for the crypto entry points

void dbench_run(void)
{
    static uint8_t buf[1024];
    memset(buf, 0xA5, sizeof(buf));
    static const uint8_t key[PROTOCORE_CHACHA20_KEY_LEN] = {0};
    static const uint8_t iv[8] = {0};
    for (;;)
    {
        DBENCH_BANNER("ssh");
        volatile uint32_t sink = 0;
        uint8_t digest[PROTOCORE_SHA256_DIGEST_LEN];
        DBENCH_BULK("Sha256.hash (1 KiB)", 2000, 1024, {
            Sha256.hash_args.data = buf;
            Sha256.hash_args.len = 1024;
            Sha256.hash_args.out = digest;
            Sha256.hash(tw);
            sink += digest[0];
        });
        DBENCH_BULK("Chacha20.xor_ (1 KiB)", 1000, 1024, {
            Chacha20.xor_args.key = key;
            Chacha20.xor_args.iv = iv;
            Chacha20.xor_args.counter = 1;
            Chacha20.xor_args.in = buf;
            Chacha20.xor_args.out = buf;
            Chacha20.xor_args.len = 1024;
            Chacha20.xor_(tw);
            sink += buf[0];
        });
        (void)sink;
        DBENCH_DONE();
    }
}

DBENCH_MAIN("ssh")
