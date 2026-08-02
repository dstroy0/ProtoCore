// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file poly1305.c
 * @brief Poly1305 (RFC 8439) - implementation. See pc_poly1305.h.
 *
 * poly1305-donna 32-bit: the accumulator h and key part r are held as five 26-bit limbs; each
 * 16-byte block adds the message limb (with the 2^128 high bit), multiplies by r, and reduces
 * modulo 2^130 - 5. The final value is fully reduced, conditionally has p subtracted in constant
 * time, and s is added modulo 2^128.
 */

#include "crypto/mac/poly1305.h"
#include "crypto/crypto_opt.h"
#include "mmgr/secure.h" // the secure pool: nested-MAC working state, wiped on release
#include <string.h>

// Poly1305 is a hot, pure-integer MAC (the other half of chacha20-poly1305). Like ChaCha it has no vector
// path on the S3 and runs materially faster than the framework -Os; it is constant-time by structure
// (the final reduction is branchless), so a higher level for this TU is side-channel safe. Byte-exact.
// See the caveats in crypto_opt.h and the ChaCha note in pc_chacha20.cpp.
PC_CRYPTO_HOT

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void wr_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

// Absorb one 16-byte block into h: h = (h + block) * r mod (2^130 - 5). hibit is 2^24 for a full
// block (the implicit high 1 bit at position 128) or 0 for the padded final block.
static void poly_block(uint32_t h[5], const uint32_t r[5], const uint32_t sr[5], const uint8_t blk[16], uint32_t hibit)
{
    uint32_t t0 = rd_le32(blk + 0), t1 = rd_le32(blk + 4), t2 = rd_le32(blk + 8), t3 = rd_le32(blk + 12);
    h[0] += t0 & 0x3ffffff;
    h[1] += ((t0 >> 26) | (t1 << 6)) & 0x3ffffff;
    h[2] += ((t1 >> 20) | (t2 << 12)) & 0x3ffffff;
    h[3] += ((t2 >> 14) | (t3 << 18)) & 0x3ffffff;
    h[4] += (t3 >> 8) | hibit;

    uint64_t d0 = (uint64_t)h[0] * r[0] + (uint64_t)h[1] * sr[4] + (uint64_t)h[2] * sr[3] + (uint64_t)h[3] * sr[2] +
                  (uint64_t)h[4] * sr[1];
    uint64_t d1 = (uint64_t)h[0] * r[1] + (uint64_t)h[1] * r[0] + (uint64_t)h[2] * sr[4] + (uint64_t)h[3] * sr[3] +
                  (uint64_t)h[4] * sr[2];
    uint64_t d2 = (uint64_t)h[0] * r[2] + (uint64_t)h[1] * r[1] + (uint64_t)h[2] * r[0] + (uint64_t)h[3] * sr[4] +
                  (uint64_t)h[4] * sr[3];
    uint64_t d3 = (uint64_t)h[0] * r[3] + (uint64_t)h[1] * r[2] + (uint64_t)h[2] * r[1] + (uint64_t)h[3] * r[0] +
                  (uint64_t)h[4] * sr[4];
    uint64_t d4 = (uint64_t)h[0] * r[4] + (uint64_t)h[1] * r[3] + (uint64_t)h[2] * r[2] + (uint64_t)h[3] * r[1] +
                  (uint64_t)h[4] * r[0];

    uint32_t c;
    c = (uint32_t)(d0 >> 26);
    h[0] = (uint32_t)d0 & 0x3ffffff;
    d1 += c;
    c = (uint32_t)(d1 >> 26);
    h[1] = (uint32_t)d1 & 0x3ffffff;
    d2 += c;
    c = (uint32_t)(d2 >> 26);
    h[2] = (uint32_t)d2 & 0x3ffffff;
    d3 += c;
    c = (uint32_t)(d3 >> 26);
    h[3] = (uint32_t)d3 & 0x3ffffff;
    d4 += c;
    c = (uint32_t)(d4 >> 26);
    h[4] = (uint32_t)d4 & 0x3ffffff;
    h[0] += c * 5;
    c = h[0] >> 26;
    h[0] &= 0x3ffffff;
    h[1] += c;
}

// Poly1305 working limbs (key part r, its *5 form sr, accumulator h) + the partial-block buffer, in the
// shared crypto scratch at the poly1305 region (it runs under chachapoly, so cannot share the base span).
typedef struct
{
    uint32_t r[5];
    uint32_t sr[5];
    uint32_t h[5];
    uint8_t buf[16];
} Poly1305Work;
static_assert(sizeof(Poly1305Work) <= PC_WORK_POLY1305,
              "Poly1305Work outgrew PC_WORK_POLY1305 - raise it in protocore_config.h, which derives "
              "PC_SECURE_ARENA_SIZE from it");

void pc_poly1305(uint8_t tag[PC_POLY1305_TAG_LEN], const uint8_t *msg, size_t len,
                 const uint8_t key[PC_POLY1305_KEY_LEN])
{
    // Working limbs + the partial-block buffer are borrowed from the secure pool, never the stack.
    // No hand-assigned region: poly1305 runs nested under chachapoly, whose own borrow is still live,
    // so the pool hands this one a different address by construction. SecureScope wipes it on every
    // exit path, not just the one that falls off the end.
    size_t mark = pc_secure_mark();
    pc_span work = pc_secure_span(sizeof(Poly1305Work), _Alignof(Poly1305Work));
    if (!pc_span_ok(work))
    {
        pc_secure_release(mark);
        return; // pool exhausted: fail closed rather than tag with a half-built state
    }
    Poly1305Work *w = (Poly1305Work *)work.buf;
    uint32_t *r = w->r;
    uint32_t *sr = w->sr;
    uint32_t *h = w->h;
    uint32_t t0 = rd_le32(key + 0), t1 = rd_le32(key + 4), t2 = rd_le32(key + 8), t3 = rd_le32(key + 12);
    // Clamp r (RFC 8439 sec 2.5) folded into the limb split.
    r[0] = t0 & 0x3ffffff;
    r[1] = ((t0 >> 26) | (t1 << 6)) & 0x3ffff03;
    r[2] = ((t1 >> 20) | (t2 << 12)) & 0x3ffc0ff;
    r[3] = ((t2 >> 14) | (t3 << 18)) & 0x3f03fff;
    r[4] = (t3 >> 8) & 0x00fffff;
    sr[0] = 0;
    sr[1] = r[1] * 5;
    sr[2] = r[2] * 5;
    sr[3] = r[3] * 5;
    sr[4] = r[4] * 5;
    h[0] = 0;
    h[1] = 0;
    h[2] = 0;
    h[3] = 0;
    h[4] = 0;

    while (len >= 16)
    {
        poly_block(h, r, sr, msg, 1u << 24);
        msg += 16;
        len -= 16;
    }
    if (len)
    {
        memset(w->buf, 0, 16);
        for (size_t i = 0; i < len; i++)
        {
            w->buf[i] = msg[i];
        }
        w->buf[len] = 1; // the message-terminating high bit for the partial block
        poly_block(h, r, sr, w->buf, 0);
    }

    // Fully carry h.
    uint32_t c;
    c = h[1] >> 26;
    h[1] &= 0x3ffffff;
    h[2] += c;
    c = h[2] >> 26;
    h[2] &= 0x3ffffff;
    h[3] += c;
    c = h[3] >> 26;
    h[3] &= 0x3ffffff;
    h[4] += c;
    c = h[4] >> 26;
    h[4] &= 0x3ffffff;
    h[0] += c * 5;
    c = h[0] >> 26;
    h[0] &= 0x3ffffff;
    h[1] += c;

    // Compute h + -p (i.e. h - (2^130 - 5)).
    uint32_t g0 = h[0] + 5;
    c = g0 >> 26;
    g0 &= 0x3ffffff;
    uint32_t g1 = h[1] + c;
    c = g1 >> 26;
    g1 &= 0x3ffffff;
    uint32_t g2 = h[2] + c;
    c = g2 >> 26;
    g2 &= 0x3ffffff;
    uint32_t g3 = h[3] + c;
    c = g3 >> 26;
    g3 &= 0x3ffffff;
    uint32_t g4 = h[4] + c - (1u << 26);

    // Select h if h < p, else h + -p; branch-free.
    uint32_t mask = (g4 >> 31) - 1; // all-ones when g4 has no borrow (h >= p) -> pick g
    g0 &= mask;
    g1 &= mask;
    g2 &= mask;
    g3 &= mask;
    g4 &= mask;
    mask = ~mask;
    h[0] = (h[0] & mask) | g0;
    h[1] = (h[1] & mask) | g1;
    h[2] = (h[2] & mask) | g2;
    h[3] = (h[3] & mask) | g3;
    h[4] = (h[4] & mask) | g4;

    // Reassemble h into four 32-bit words (h mod 2^128).
    uint32_t f0 = (h[0]) | (h[1] << 26);
    uint32_t f1 = (h[1] >> 6) | (h[2] << 20);
    uint32_t f2 = (h[2] >> 12) | (h[3] << 14);
    uint32_t f3 = (h[3] >> 18) | (h[4] << 8);

    // tag = (h + s) mod 2^128, where s = key[16..32].
    uint64_t f = (uint64_t)f0 + rd_le32(key + 16);
    f0 = (uint32_t)f;
    f = (uint64_t)f1 + rd_le32(key + 20) + (f >> 32);
    f1 = (uint32_t)f;
    f = (uint64_t)f2 + rd_le32(key + 24) + (f >> 32);
    f2 = (uint32_t)f;
    f = (uint64_t)f3 + rd_le32(key + 28) + (f >> 32);
    f3 = (uint32_t)f;

    wr_le32(tag + 0, f0);
    wr_le32(tag + 4, f1);
    wr_le32(tag + 8, f2);
    wr_le32(tag + 12, f3);
    pc_secure_release(mark);
}
