// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file md.cpp
 * @brief MD4 / MD5 / HMAC-MD5 implementation (see md.h). Little-endian word order.
 */

#include "crypto/hash/md.h"
#include "crypto/crypto_opt.h"
#include "mmgr/secure.h" // the secure pool: digest state, wiped on release
#include "shared_primitives/endian.h"
#include <string.h>
PC_CRYPTO_HOT

// The one definition of MdCtx - private to this TU (md.h forward-declares it). External callers hold it
// only via pointer and get their storage from pc_md_wants() below, so the size never leaves this file.
struct MdCtx
{
    uint32_t state[4];
    uint64_t bits;    ///< total message length in bits
    uint8_t buf[64];  ///< partial block
    uint32_t buf_len; ///< bytes currently in buf
};
static_assert(sizeof(MdCtx) <= PC_WORK_MD,
              "MdCtx outgrew PC_WORK_MD - raise it in protocore_config.h, which derives PC_SECURE_ARENA_SIZE from it");

MdCtx *pc_md_wants(void)
{
    pc_span ws = pc_secure_span(sizeof(MdCtx), alignof(MdCtx));
    return pc_span_ok(ws) ? reinterpret_cast<MdCtx *>(ws.buf) : nullptr;
}

static inline uint32_t rotl(uint32_t v, unsigned n)
{
    return (v << n) | (v >> (32 - n));
}

// --- MD5 (RFC 1321) --------------------------------------------------------

static const uint32_t MD5_K[64] = {
    0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
    0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
    0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
    0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
    0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
    0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
    0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
    0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};

static const uint8_t MD5_S[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                  5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                  4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                  6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};

static void pc_md5_compress(uint32_t s[4], const uint8_t block[64])
{
    uint32_t m[16];
    for (int i = 0; i < 16; i++)
    {
        m[i] = pc_rd32le(block + i * 4);
    }
    uint32_t a = s[0];
    uint32_t b = s[1];
    uint32_t c = s[2];
    uint32_t d = s[3];
    for (int i = 0; i < 64; i++)
    {
        uint32_t f;
        int g;
        if (i < 16)
        {
            f = (b & c) | (~b & d);
            g = i;
        }
        else if (i < 32)
        {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) & 15;
        }
        else if (i < 48)
        {
            f = b ^ c ^ d;
            g = (3 * i + 5) & 15;
        }
        else
        {
            f = c ^ (b | ~d);
            g = (7 * i) & 15;
        }
        f += a + MD5_K[i] + m[g];
        a = d;
        d = c;
        c = b;
        b += rotl(f, MD5_S[i]);
    }
    s[0] += a;
    s[1] += b;
    s[2] += c;
    s[3] += d;
}

void pc_md5_init(MdCtx *c)
{
    c->state[0] = 0x67452301;
    c->state[1] = 0xefcdab89;
    c->state[2] = 0x98badcfe;
    c->state[3] = 0x10325476;
    c->bits = 0;
    c->buf_len = 0;
}

// --- MD4 (RFC 1320) --------------------------------------------------------

static void pc_md4_compress(uint32_t s[4], const uint8_t block[64])
{
    uint32_t x[16];
    for (int i = 0; i < 16; i++)
    {
        x[i] = pc_rd32le(block + i * 4);
    }
    uint32_t a = s[0];
    uint32_t b = s[1];
    uint32_t c = s[2];
    uint32_t d = s[3];
#define F4(X, Y, Z) (((X) & (Y)) | (~(X) & (Z)))
#define G4(X, Y, Z) (((X) & (Y)) | ((X) & (Z)) | ((Y) & (Z)))
#define H4(X, Y, Z) ((X) ^ (Y) ^ (Z))
#define R1(A, B, C, D, K, S) A = rotl((uint32_t)(A + F4(B, C, D) + x[K]), S)
#define R2(A, B, C, D, K, S) A = rotl((uint32_t)(A + G4(B, C, D) + x[K] + 0x5a827999u), S)
#define R3(A, B, C, D, K, S) A = rotl((uint32_t)(A + H4(B, C, D) + x[K] + 0x6ed9eba1u), S)
    R1(a, b, c, d, 0, 3);
    R1(d, a, b, c, 1, 7);
    R1(c, d, a, b, 2, 11);
    R1(b, c, d, a, 3, 19);
    R1(a, b, c, d, 4, 3);
    R1(d, a, b, c, 5, 7);
    R1(c, d, a, b, 6, 11);
    R1(b, c, d, a, 7, 19);
    R1(a, b, c, d, 8, 3);
    R1(d, a, b, c, 9, 7);
    R1(c, d, a, b, 10, 11);
    R1(b, c, d, a, 11, 19);
    R1(a, b, c, d, 12, 3);
    R1(d, a, b, c, 13, 7);
    R1(c, d, a, b, 14, 11);
    R1(b, c, d, a, 15, 19);
    R2(a, b, c, d, 0, 3);
    R2(d, a, b, c, 4, 5);
    R2(c, d, a, b, 8, 9);
    R2(b, c, d, a, 12, 13);
    R2(a, b, c, d, 1, 3);
    R2(d, a, b, c, 5, 5);
    R2(c, d, a, b, 9, 9);
    R2(b, c, d, a, 13, 13);
    R2(a, b, c, d, 2, 3);
    R2(d, a, b, c, 6, 5);
    R2(c, d, a, b, 10, 9);
    R2(b, c, d, a, 14, 13);
    R2(a, b, c, d, 3, 3);
    R2(d, a, b, c, 7, 5);
    R2(c, d, a, b, 11, 9);
    R2(b, c, d, a, 15, 13);
    R3(a, b, c, d, 0, 3);
    R3(d, a, b, c, 8, 9);
    R3(c, d, a, b, 4, 11);
    R3(b, c, d, a, 12, 15);
    R3(a, b, c, d, 2, 3);
    R3(d, a, b, c, 10, 9);
    R3(c, d, a, b, 6, 11);
    R3(b, c, d, a, 14, 15);
    R3(a, b, c, d, 1, 3);
    R3(d, a, b, c, 9, 9);
    R3(c, d, a, b, 5, 11);
    R3(b, c, d, a, 13, 15);
    R3(a, b, c, d, 3, 3);
    R3(d, a, b, c, 11, 9);
    R3(c, d, a, b, 7, 11);
    R3(b, c, d, a, 15, 15);
#undef F4
#undef G4
#undef H4
#undef R1
#undef R2
#undef R3
    s[0] += a;
    s[1] += b;
    s[2] += c;
    s[3] += d;
}

void pc_md4_init(MdCtx *c)
{
    c->state[0] = 0x67452301;
    c->state[1] = 0xefcdab89;
    c->state[2] = 0x98badcfe;
    c->state[3] = 0x10325476;
    c->bits = 0;
    c->buf_len = 0;
}

// --- shared absorb / finish (MD4 and MD5 share the framing) ----------------

using md_compress_fn = void (*)(uint32_t[4], const uint8_t[64]);

static void md_absorb(MdCtx *c, const uint8_t *data, size_t len, md_compress_fn compress)
{
    c->bits += (uint64_t)len * 8;
    while (len > 0)
    {
        uint32_t take = 64 - c->buf_len;
        if ((size_t)take > len)
        {
            take = (uint32_t)len;
        }
        memcpy(c->buf + c->buf_len, data, take);
        c->buf_len += take;
        data += take;
        len -= take;
        if (c->buf_len == 64)
        {
            compress(c->state, c->buf);
            c->buf_len = 0;
        }
    }
}

static void md_finish(MdCtx *c, uint8_t out[16], md_compress_fn compress)
{
    uint64_t bits = c->bits;
    uint8_t pad = 0x80;
    md_absorb(c, &pad, 1, compress);
    uint8_t zero = 0x00;
    while (c->buf_len != 56)
    {
        md_absorb(c, &zero, 1, compress);
    }
    uint8_t lenbuf[8];
    for (int i = 0; i < 8; i++)
    {
        lenbuf[i] = (uint8_t)(bits >> (8 * i)); // little-endian bit length
    }
    md_absorb(c, lenbuf, 8, compress); // triggers the final compress
    for (int i = 0; i < 4; i++)
    {
        pc_wr32le(out + i * 4, c->state[i]);
    }
}

void pc_md5_update(MdCtx *c, const uint8_t *data, size_t len)
{
    md_absorb(c, data, len, pc_md5_compress);
}
void pc_md5_final(MdCtx *c, uint8_t out[16])
{
    md_finish(c, out, pc_md5_compress);
}
void pc_md4_update(MdCtx *c, const uint8_t *data, size_t len)
{
    md_absorb(c, data, len, pc_md4_compress);
}
void pc_md4_final(MdCtx *c, uint8_t out[16])
{
    md_finish(c, out, pc_md4_compress);
}

void md5(const uint8_t *data, size_t len, uint8_t out[16])
{
    MdCtx c;
    pc_md5_init(&c);
    pc_md5_update(&c, data, len);
    pc_md5_final(&c, out);
}
void md4(const uint8_t *data, size_t len, uint8_t out[16])
{
    MdCtx c;
    pc_md4_init(&c);
    pc_md4_update(&c, data, len);
    pc_md4_final(&c, out);
}

// --- HMAC-MD5 (RFC 2104) ---------------------------------------------------

void pc_hmac_md5(const uint8_t *key, size_t key_len, const uint8_t *msg, size_t msg_len, uint8_t out[16])
{
    uint8_t k[64];
    memset(k, 0, sizeof(k));
    if (key_len > 64)
    {
        md5(key, key_len, k); // keys longer than the block are hashed down (leaves 16 bytes, rest zero)
    }
    else
    {
        memcpy(k, key, key_len);
    }

    uint8_t ipad[64];
    uint8_t opad[64];
    for (int i = 0; i < 64; i++)
    {
        ipad[i] = (uint8_t)(k[i] ^ 0x36);
        opad[i] = (uint8_t)(k[i] ^ 0x5c);
    }

    uint8_t inner[16];
    MdCtx c;
    pc_md5_init(&c);
    pc_md5_update(&c, ipad, 64);
    pc_md5_update(&c, msg, msg_len);
    pc_md5_final(&c, inner);

    pc_md5_init(&c);
    pc_md5_update(&c, opad, 64);
    pc_md5_update(&c, inner, 16);
    pc_md5_final(&c, out);
}
