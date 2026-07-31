// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sha1.cpp
 * @brief SHA-1 implementation (FIPS 180-4).
 *
 * On Arduino (ESP32) targets, delegates to mbedtls_sha1() which uses the hardware SHA accelerator. On
 * native (x86) test targets, uses a portable software implementation so unit tests run without mbedTLS.
 */

#include "crypto/hash/sha1.h"
#include "crypto/crypto_opt.h"
#include <string.h>
#ifdef ARDUINO
#include "mbedtls/sha1.h" // hardware-accelerated SHA-1 on ESP32
#else
#include "shared_primitives/endian.h" // native software SHA-1
#endif
PC_CRYPTO_HOT

#ifdef ARDUINO

// --- ESP32 / Arduino: use mbedTLS (hardware-accelerated on ESP32) ----------

void pc_sha1(const uint8_t *data, size_t len, uint8_t digest[PC_SHA1_DIGEST_LEN])
{
    (void)mbedtls_sha1(data, len, digest);
}

#else

// --- Native / test: software SHA-1, no external dependencies ---------------

static inline uint32_t rot32(uint32_t x, int n)
{
    return (x << n) | (x >> (32 - n));
}

// Process one 64-byte block into the running state h[0..4] (FIPS 180-4 §6.1.2).
static void sha1_block(uint32_t h[5], const uint8_t block[64])
{
    // Message schedule: 16 big-endian words from the block, extended to 80 by XOR-and-rotate (the
    // SHA-1 recurrence; the rotate-by-1 is what SHA-0 lacked).
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
    {
        w[i] = pc_rd32be(block + i * 4);
    }
    for (int i = 16; i < 80; i++)
    {
        w[i] = rot32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    }

    uint32_t a = h[0];
    uint32_t b = h[1];
    uint32_t c = h[2];
    uint32_t d = h[3];
    uint32_t e = h[4];

    // 80 rounds in four 20-round regimes, each with its own mixing function f and constant k.
    for (int i = 0; i < 80; i++)
    {
        uint32_t f;
        uint32_t k;
        if (i < 20)
        {
            f = (b & c) | (~b & d); // choice
            k = 0x5A827999u;
        }
        else if (i < 40)
        {
            f = b ^ c ^ d; // parity
            k = 0x6ED9EBA1u;
        }
        else if (i < 60)
        {
            f = (b & c) | (b & d) | (c & d); // majority
            k = 0x8F1BBCDCu;
        }
        else
        {
            f = b ^ c ^ d; // parity
            k = 0xCA62C1D6u;
        }

        // Round update; b is rotated 30 as it shifts into c.
        uint32_t tmp = rot32(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rot32(b, 30);
        b = a;
        a = tmp;
    }

    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

void pc_sha1(const uint8_t *data, size_t len, uint8_t digest[PC_SHA1_DIGEST_LEN])
{
    uint32_t h[5] = {0x67452301u, 0xEFCDAB89u, 0x98BADCFEu, 0x10325476u, 0xC3D2E1F0u};

    // Process full 64-byte blocks
    size_t blocks = len / 64;
    for (size_t i = 0; i < blocks; i++)
    {
        sha1_block(h, data + i * 64);
    }

    // Build the padded final block(s)
    uint8_t pad[128] = {};
    size_t tail = len - blocks * 64;
    memcpy(pad, data + blocks * 64, tail);
    pad[tail] = 0x80;

    // Bit-length goes in the last 8 bytes of the final block
    uint64_t bit_len = (uint64_t)len * 8;
    uint8_t *bl = (tail < 56) ? pad + 56 : pad + 120;
    for (int i = 7; i >= 0; i--, bit_len >>= 8)
    {
        bl[i] = (uint8_t)bit_len;
    }

    sha1_block(h, pad);
    if (tail >= 56)
    {
        sha1_block(h, pad + 64);
    }

    for (int i = 0; i < 5; i++)
    {
        pc_wr32be(digest + i * 4, h[i]);
    }
}

#endif // ARDUINO
