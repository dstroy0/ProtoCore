// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file chacha20.cpp
 * @brief ChaCha20 (RFC 8439) - implementation. See pc_chacha20.h.
 */

#include "crypto/cipher/chacha20.h"
#include "crypto/crypto_opt.h"
#include "mmgr/secure.h" // the secure pool: nested-cipher working state, wiped on release

// ChaCha20 is a hot, pure-integer (add/xor/rotate) keystream generator. The ESP32-S3 has no usable
// vector path (its PIE unit has only a *saturating* 32-bit add, `ee.vadds.s32`; ChaCha needs modular
// wrap-around, so it cannot be vectorized). The real lever is optimization level: the library ships at
// the arduino framework's -Os, and ChaCha runs ~2.36x faster at -O2 (measured on-device, CCOUNT). It is
// constant-time by structure (no secret-dependent branches), so forcing a higher level is side-channel
// safe - see the caveats in crypto_opt.h. Byte-exact; the SIMD investigation is in docs/FEATURE_PERFORMANCE.md.
//
// Measured (crypto bench): ChaCha20's additional ~8.8% S3 win at -O3 is carried entirely by -funswitch-loops
// (bisected on-device); pin just that transform on the -O2 floor. The P4 takes full -O3 via the per-die default
// (its win is -O3's inline/unroll parameter budget, not one flag).
#if defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3
PC_CRYPTO_HOT_UNSWITCH
#else
PC_CRYPTO_HOT
#endif

namespace
{
uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
uint32_t rotl32(uint32_t v, int c)
{
    return (v << c) | (v >> (32 - c));
}

#define QR(a, b, c, d)                                                                                                 \
    a += b;                                                                                                            \
    d ^= a;                                                                                                            \
    d = rotl32(d, 16);                                                                                                 \
    c += d;                                                                                                            \
    b ^= c;                                                                                                            \
    b = rotl32(b, 12);                                                                                                 \
    a += b;                                                                                                            \
    d ^= a;                                                                                                            \
    d = rotl32(d, 8);                                                                                                  \
    c += d;                                                                                                            \
    b ^= c;                                                                                                            \
    b = rotl32(b, 7)

// ChaCha20 working set (input state + keystream block + round state), borrowed from the secure pool
// per op and wiped on release. It runs nested under chachapoly, whose own borrow is still live, so the
// pool hands this one a separate address by construction - no region assignment needed.
struct Chacha20Work
{
    uint32_t st[16]; // input state (key / nonce / counter)
    uint8_t ks[64];  // keystream block
    uint32_t x[16];  // round state
};
static_assert(sizeof(Chacha20Work) <= PC_WORK_CHACHA20,
              "Chacha20Work outgrew PC_WORK_CHACHA20 - raise it in protocore_config.h, which derives "
              "PC_SECURE_ARENA_SIZE from it");

// The ChaCha20 core: 20 rounds over w->st, add the original state, serialize LE into @p out (64 bytes).
void chacha_core(Chacha20Work *w, uint8_t out[64])
{
    for (int i = 0; i < 16; i++)
    {
        w->x[i] = w->st[i];
    }
    for (int i = 0; i < 10; i++) // 10 double-rounds = 20 rounds
    {
        QR(w->x[0], w->x[4], w->x[8], w->x[12]);
        QR(w->x[1], w->x[5], w->x[9], w->x[13]);
        QR(w->x[2], w->x[6], w->x[10], w->x[14]);
        QR(w->x[3], w->x[7], w->x[11], w->x[15]);
        QR(w->x[0], w->x[5], w->x[10], w->x[15]);
        QR(w->x[1], w->x[6], w->x[11], w->x[12]);
        QR(w->x[2], w->x[7], w->x[8], w->x[13]);
        QR(w->x[3], w->x[4], w->x[9], w->x[14]);
    }
    for (int i = 0; i < 16; i++)
    {
        uint32_t v = w->x[i] + w->st[i];
        out[4 * i + 0] = (uint8_t)v;
        out[4 * i + 1] = (uint8_t)(v >> 8);
        out[4 * i + 2] = (uint8_t)(v >> 16);
        out[4 * i + 3] = (uint8_t)(v >> 24);
    }
}

// "expand 32-byte k"
const uint32_t SIGMA0 = 0x61707865;
const uint32_t SIGMA1 = 0x3320646e;
const uint32_t SIGMA2 = 0x79622d32;
const uint32_t SIGMA3 = 0x6b206574;
} // namespace

void pc_chacha20_xor(const uint8_t key[PC_CHACHA20_KEY_LEN], const uint8_t iv[8], uint64_t counter, const uint8_t *in,
                     uint8_t *out, size_t len)
{
    SecureBorrow ws_b(sizeof(Chacha20Work), alignof(Chacha20Work));
    const pc_span &ws = ws_b.span();
    if (!pc_span_ok(ws))
    {
        return; // pool exhausted: an empty borrow is the failure signal, as with malloc
    }
    Chacha20Work *w = reinterpret_cast<Chacha20Work *>(ws.buf);
    w->st[0] = SIGMA0;
    w->st[1] = SIGMA1;
    w->st[2] = SIGMA2;
    w->st[3] = SIGMA3;
    for (int i = 0; i < 8; i++)
    {
        w->st[4 + i] = rd_le32(key + 4 * i);
    }
    w->st[14] = rd_le32(iv + 0); // OpenSSH layout: 64-bit nonce in words 14-15
    w->st[15] = rd_le32(iv + 4);
    size_t off = 0;
    while (off < len)
    {
        w->st[12] = (uint32_t)(counter & 0xffffffffu); // 64-bit little-endian counter in words 12-13
        w->st[13] = (uint32_t)(counter >> 32);
        chacha_core(w, w->ks);
        size_t n = (len - off < 64) ? (len - off) : 64;
        for (size_t i = 0; i < n; i++)
        {
            out[off + i] = (uint8_t)((in ? in[off + i] : 0) ^ w->ks[i]);
        }
        off += n;
        counter++;
    }
}

void pc_chacha20_block_ietf(const uint8_t key[PC_CHACHA20_KEY_LEN], uint32_t counter, const uint8_t nonce[12],
                            uint8_t out[PC_CHACHA20_BLOCK_LEN])
{
    SecureBorrow ws_b(sizeof(Chacha20Work), alignof(Chacha20Work));
    const pc_span &ws = ws_b.span();
    if (!pc_span_ok(ws))
    {
        return; // pool exhausted: an empty borrow is the failure signal, as with malloc
    }
    Chacha20Work *w = reinterpret_cast<Chacha20Work *>(ws.buf);
    w->st[0] = SIGMA0;
    w->st[1] = SIGMA1;
    w->st[2] = SIGMA2;
    w->st[3] = SIGMA3;
    for (int i = 0; i < 8; i++)
    {
        w->st[4 + i] = rd_le32(key + 4 * i);
    }
    w->st[12] = counter; // RFC 8439 layout: 32-bit counter, 96-bit nonce
    w->st[13] = rd_le32(nonce + 0);
    w->st[14] = rd_le32(nonce + 4);
    w->st[15] = rd_le32(nonce + 8);
    chacha_core(w, out);
}
