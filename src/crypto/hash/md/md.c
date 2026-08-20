// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file md.c
 * @brief MD4 / MD5 / HMAC-MD5 implementation (see md.h). Little-endian word order.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_MD

#include "crypto/crypto_opt.h"
#include "crypto/hash/md/md.h"
#include "mmgr/endian/endian.h"
#include "mmgr/protomem/protomem.h"
#include "mmgr/secure/secure.h" // the secure pool: digest state, wiped on release

PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

// The one definition of MdCtx - private to this TU. It sits at MD_OFF_CTX in the caller's borrow, so
// its size never leaves this file and no consumer can name it.
typedef struct MdCtx
{
    uint32_t state[4];
    uint64_t bits;    ///< total message length in bits
    uint8_t buf[64];  ///< partial block
    uint32_t buf_len; ///< bytes currently in buf
    uint8_t md4;      ///< 1 when seeded for MD4, 0 for MD5
} MdCtx;

// The caller's borrow, split by offset: the running digest state, then the regions HMAC-MD5 needs -
// the key block, its two pads, the inner digest, and the state a key longer than the block is hashed
// down in. Everything here is NTLM password and session-key material, so none of it touches the stack.
#define MD_OFF_CTX 0u
#define MD_OFF_KEY (MD_OFF_CTX + sizeof(struct MdCtx))
#define MD_OFF_IPAD (MD_OFF_KEY + 64u)
#define MD_OFF_OPAD (MD_OFF_IPAD + 64u)
#define MD_OFF_INNER (MD_OFF_OPAD + 64u)
#define MD_OFF_KCTX (MD_OFF_INNER + PROTOCORE_MD_DIGEST_LEN)
static_assert(MD_OFF_KCTX + sizeof(struct MdCtx) <= PROTOCORE_MD_BORROW,
              "PROTOCORE_MD_BORROW is short of the digest state and the HMAC regions - raise it in "
              "protocore_config.h, which derives PROTOCORE_SECURE_ARENA_SIZE from it");

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

static void protocore_md5_compress(uint32_t s[4], const uint8_t block[64])
{
    uint32_t m[16];
    for (int i = 0; i < 16; i++)
    {
        m[i] = protocore_rd32le(block + i * 4);
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

static void md5_state_init(struct MdCtx *c)
{
    c->state[0] = 0x67452301;
    c->state[1] = 0xefcdab89;
    c->state[2] = 0x98badcfe;
    c->state[3] = 0x10325476;
    c->bits = 0;
    c->buf_len = 0;
}

// --- MD4 (RFC 1320) --------------------------------------------------------

static void protocore_md4_compress(uint32_t s[4], const uint8_t block[64])
{
    uint32_t x[16];
    for (int i = 0; i < 16; i++)
    {
        x[i] = protocore_rd32le(block + i * 4);
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

static void md4_state_init(struct MdCtx *c)
{
    c->state[0] = 0x67452301;
    c->state[1] = 0xefcdab89;
    c->state[2] = 0x98badcfe;
    c->state[3] = 0x10325476;
    c->bits = 0;
    c->buf_len = 0;
}

// --- shared absorb / finish (MD4 and MD5 share the framing) ----------------

typedef void (*md_compress_fn)(uint32_t[4], const uint8_t[64]);

static void md_absorb(struct MdCtx *c, const uint8_t *data, size_t len, md_compress_fn compress)
{
    c->bits += (uint64_t)len * 8;
    while (len > 0)
    {
        uint32_t take = 64 - c->buf_len;
        if ((size_t)take > len)
        {
            take = (uint32_t)len;
        }
        mem.cpy(c->buf + c->buf_len, data, take);
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

static void md_finish(struct MdCtx *c, uint8_t out[16], md_compress_fn compress)
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
        protocore_wr32le(out + i * 4, c->state[i]);
    }
}

// The regions, at their offsets in the caller's borrow.
#define MD_STATE(w) ((struct MdCtx *)(void *)((w) + MD_OFF_CTX))
#define MD_KEY(w) ((w) + MD_OFF_KEY)
#define MD_IPAD(w) ((w) + MD_OFF_IPAD)
#define MD_OPAD(w) ((w) + MD_OFF_OPAD)
#define MD_INNER(w) ((w) + MD_OFF_INNER)
#define MD_KCTX(w) ((struct MdCtx *)(void *)((w) + MD_OFF_KCTX))

// Which compression the state was seeded for. It rides in the borrow with the state, so a running
// digest carries it and nothing survives the call here.
static md_compress_fn md_bound(const uint8_t *restrict work)
{
    return MD_STATE(work)->md4 ? protocore_md4_compress : protocore_md5_compress;
}

// Seed the state. Shared by both inits, which differ only in the IV and the compression.
static void md_begin(uint8_t *restrict work, md_compress_fn compress)
{
    if (compress == protocore_md5_compress)
    {
        md5_state_init(MD_STATE(work));
        MD_STATE(work)->md4 = 0;
    }
    else
    {
        md4_state_init(MD_STATE(work));
        MD_STATE(work)->md4 = 1;
    }
    Md.ok = PROTO_TRUE;
}

static void md_md5_init(uint8_t *restrict work)
{
    md_begin(work, protocore_md5_compress);
}

static void md_md4_init(uint8_t *restrict work)
{
    md_begin(work, protocore_md4_compress);
}

static void md_update(uint8_t *restrict work)
{
    md_absorb(MD_STATE(work), Md.update_args.data, Md.update_args.len, md_bound(work));
}

static void md_final(uint8_t *restrict work)
{
    if (!Md.final_args.out)
    {
        Md.ok = PROTO_FALSE;
        return;
    }
    md_finish(MD_STATE(work), Md.final_args.out, md_bound(work));
    Md.ok = PROTO_TRUE;
}

// One-shot over the members already set: init, absorb, finish.
static void md_one(uint8_t *restrict work, md_compress_fn compress)
{
    if (!Md.final_args.out)
    {
        Md.ok = PROTO_FALSE;
        return;
    }
    md_begin(work, compress);
    md_absorb(MD_STATE(work), Md.update_args.data, Md.update_args.len, compress);
    md_finish(MD_STATE(work), Md.final_args.out, compress);
}

static void md_md5(uint8_t *restrict work)
{
    md_one(work, protocore_md5_compress);
}

static void md_md4(uint8_t *restrict work)
{
    md_one(work, protocore_md4_compress);
}

// --- HMAC-MD5 (RFC 2104) ---------------------------------------------------

static void md_hmac_md5(uint8_t *restrict work)
{
    Md.ok = PROTO_FALSE;
    if (!Md.hmac_args.out)
    {
        return;
    }
    const uint8_t *key = Md.hmac_args.key;
    const size_t key_len = Md.hmac_args.key_len;

    // Every region is a named offset in the caller's borrow: the key block, its two pads, the inner
    // digest, the running state, and the state a long key is hashed down in.
    uint8_t *k = MD_KEY(work);
    uint8_t *ipad = MD_IPAD(work);
    uint8_t *opad = MD_OPAD(work);
    uint8_t *inner = MD_INNER(work);
    struct MdCtx *c = MD_STATE(work);

    mem.set(k, 0, 64);
    if (key_len > 64)
    {
        // Keys longer than the block are hashed down, leaving 16 bytes and the rest zero. Its state is
        // its own region so hashing the key does not disturb the one the MAC runs in.
        struct MdCtx *kc = MD_KCTX(work);
        md5_state_init(kc);
        md_absorb(kc, key, key_len, protocore_md5_compress);
        md_finish(kc, k, protocore_md5_compress);
    }
    else
    {
        mem.cpy(k, key, key_len);
    }

    for (int i = 0; i < 64; i++)
    {
        ipad[i] = (uint8_t)(k[i] ^ 0x36);
        opad[i] = (uint8_t)(k[i] ^ 0x5c);
    }

    md5_state_init(c);
    md_absorb(c, ipad, 64, protocore_md5_compress);
    md_absorb(c, Md.hmac_args.msg, Md.hmac_args.msg_len, protocore_md5_compress);
    md_finish(c, inner, protocore_md5_compress);

    md5_state_init(c);
    md_absorb(c, opad, 64, protocore_md5_compress);
    md_absorb(c, inner, PROTOCORE_MD_DIGEST_LEN, protocore_md5_compress);
    md_finish(c, Md.hmac_args.out, protocore_md5_compress);

    Md.ok = PROTO_TRUE;
}

MdNs Md = {.md5_init = md_md5_init,
           .md4_init = md_md4_init,
           .update = md_update,
           .final = md_final,
           .md5 = md_md5,
           .md4 = md_md4,
           .hmac_md5 = md_hmac_md5};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_MD
