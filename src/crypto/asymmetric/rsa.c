// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file rsa.c
 * @brief RSA-2048 PKCS#1 v1.5 verify and sign (RFC 8017 sec 8.2, see rsa.h).
 *
 * One context and one set of entries; only the modular multiply has two arms - the accelerator's
 * single-shot MODMULT where the part carries one, a schoolbook product and a bit-serial reduction
 * below where it does not. The square-and-multiply ladder, the PKCS#1 v1.5 encoding, the digest and
 * the constant-time compare sit above the seam and are the same either way.
 *
 * The context is this file's. The module's own borrow is split by offset into the region the SHA-256
 * runs in, the region the SHA-512 runs in, the region the bignum conversions run in, and the working
 * set the ladder runs on, so the private exponent and the encoded block never touch the stack.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_RSA

#include "crypto/asymmetric/bignum.h" // protocore_bignum, the byte conversions and the limb compare
#include "crypto/asymmetric/rsa.h"
#include "crypto/crypto_opt.h"
#include "crypto/ct_eq.h" // protocore_ct_eq
#include "crypto/hash/sha256.h"
#include "crypto/hash/sha512.h"
#include "mmgr/protomem.h"

PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

// The one definition of RsaCtx - private to this TU, and the same members on both arms. The
// accelerator multiplies two residues; it does not raise one to a power, encode a block, or hash a
// message. Those are this file's, so the state an operation carries is the same either way and only
// the multiply below has two arms.
//
// An entry stages a multiply's operands here and the seam reads them, so the seam carries one
// parameter. Only what is not derivable: the context lives at a fixed offset in the caller's borrow,
// so a macro computes it from the pointer rather than anything storing it.
typedef struct
{
    const uint8_t *msg;                          ///< the message the digest covers
    size_t msg_len;                              ///< its length
    const uint32_t *x;                           ///< left operand of the staged multiply
    const uint32_t *y;                           ///< right operand of the staged multiply
    uint32_t *z;                                 ///< where the staged multiply lands
    uint32_t mprime;                             ///< -n^-1 mod 2^32, the accelerator's Montgomery constant
    protocore_rsa_hash hash;                     ///< which digest, and which DigestInfo goes in front of it
    protocore_bignum n;                          ///< the modulus every multiply reduces against
    protocore_bignum base;                       ///< the value the ladder raises
    protocore_bignum exp;                        ///< the exponent the ladder walks
    protocore_bignum acc;                        ///< the accumulator, and the result
    protocore_bignum rr;                         ///< R^2 mod n, the accelerator's other Montgomery constant
    uint32_t prod[2 * PROTOCORE_BN_LIMBS];       ///< the double-width product a software multiply builds
    uint8_t digest[PROTOCORE_SHA512_DIGEST_LEN]; ///< the message digest, the selected algorithm's length
    uint8_t em[PROTOCORE_RSA_KEY_BYTES];         ///< the PKCS#1 v1.5 block the digest is encoded into
    uint8_t recovered[PROTOCORE_RSA_KEY_BYTES];  ///< the block a verify raises the signature to
} RsaCtx;

// The caller's borrow, split: a region for each digest this drives through its own namespace, a
// region the bignum conversions run in, then the working set the ladder runs on. One split, both arms.
#define RSA_OFF_SHA256 0u
#define RSA_OFF_SHA512 (RSA_OFF_SHA256 + PROTOCORE_SHA256_BORROW)
#define RSA_OFF_BIGNUM (RSA_OFF_SHA512 + PROTOCORE_SHA512_BORROW)
#define RSA_OFF_CTX (RSA_OFF_BIGNUM + PROTOCORE_BIGNUM_BORROW)
static_assert(RSA_OFF_CTX + sizeof(RsaCtx) <= PROTOCORE_RSA_BORROW,
              "PROTOCORE_RSA_BORROW is short of the two digest regions, the bignum region and the ladder's "
              "working set - raise it in protocore_config.h, which derives PROTOCORE_SECURE_ARENA_SIZE from it");

// The regions, at their offsets in the caller's borrow.
#define RSA_SHA256(w) ((w) + RSA_OFF_SHA256)
#define RSA_SHA512(w) ((w) + RSA_OFF_SHA512)
#define RSA_BIGNUM(w) ((w) + RSA_OFF_BIGNUM)
#define RSA_CTX(w) ((RsaCtx *)(void *)((w) + RSA_OFF_CTX))

// ---------------------------------------------------------------------------
// DigestInfo for SHA-256 / SHA-512 (PKCS#1 v1.5, RFC 8017 sec 9.2, RFC 5754)
// ---------------------------------------------------------------------------

const uint8_t protocore_pkcs1_sha256_digestinfo[PROTOCORE_PKCS1_DIGESTINFO_LEN] = {
    0x30, 0x31,                                           // SEQUENCE, length 49
    0x30, 0x0d,                                           // SEQUENCE, length 13 (AlgorithmIdentifier)
    0x06, 0x09,                                           // OID, length 9
    0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01, // OID 2.16.840.1.101.3.4.2.1
    0x05, 0x00,                                           // NULL parameters
    0x04, 0x20                                            // OCTET STRING, length 32 (digest follows)
};

const uint8_t protocore_pkcs1_sha512_digestinfo[PROTOCORE_PKCS1_SHA512_DIGESTINFO_LEN] = {
    0x30, 0x51,                                           // SEQUENCE, length 81
    0x30, 0x0d,                                           // SEQUENCE, length 13 (AlgorithmIdentifier)
    0x06, 0x09,                                           // OID, length 9
    0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03, // OID 2.16.840.1.101.3.4.2.3
    0x05, 0x00,                                           // NULL parameters
    0x04, 0x40                                            // OCTET STRING, length 64 (digest follows)
};

// ---------------------------------------------------------------------------
// z = x*y mod n over the staged operands.
//
// The RSA MODMULT is modulus-generic and is what every 2048-bit exponentiation costs, so the arm is
// drawn here and not around the ladder. PROTOCORE_RSA_MODMUL_HW is what the crypto HAL states per die,
// exactly as crypto/asymmetric/ecdsa.c and crypto/asymmetric/fe25519.h do; a host build reaches the
// same function through the HAL's host arm, so the accelerated path is compiled and run off target.
// ---------------------------------------------------------------------------

#if PROTOCORE_RSA_MODMUL_HW
// m' = -n^-1 mod 2^32 by Newton, and R^2 mod n by 4096 conditional doublings from 1. Both are
// functions of the modulus alone, so a ladder derives them once and every multiply under it reuses
// them. Newton doubles the correct low bits each round, and an odd seed starts with three, so five
// rounds reach ninety-six - past the thirty-two the word holds.
static void rsa_mont(uint8_t *restrict work)
{
    RsaCtx *ctx = RSA_CTX(work);
    const uint32_t n0 = ctx->n.d[0];

    uint32_t inv = n0;
    for (int i = 0; i < 5; i++)
    {
        inv *= 2u - n0 * inv;
    }
    ctx->mprime = 0u - inv;

    uint32_t *t = ctx->rr.d;
    mem.zero(t, sizeof(ctx->rr.d));
    t[0] = 1u;
    for (int i = 0; i < 2 * PROTOCORE_BN_LIMBS * 32; i++)
    {
        uint32_t carry = 0;
        for (int k = 0; k < PROTOCORE_BN_LIMBS; k++)
        {
            const uint32_t nc = t[k] >> 31;
            t[k] = (t[k] << 1) | carry;
            carry = nc;
        }
        // t was below n, so the double is below 2n and one subtract canonicalizes it. The bit that
        // left the top is the borrow that leaves it.
        if (carry || bn_cmp_raw(t, ctx->n.d, PROTOCORE_BN_LIMBS) >= 0)
        {
            uint64_t borrow = 0;
            for (int k = 0; k < PROTOCORE_BN_LIMBS; k++)
            {
                const uint64_t v = (uint64_t)t[k] - ctx->n.d[k] - borrow;
                t[k] = (uint32_t)v;
                borrow = (v >> 32) & 1u;
            }
        }
    }
}

static void rsa_modmul_begin(uint8_t *restrict work)
{
    protocore_rsa_hw_acquire();
    rsa_mont(work);
}

static void rsa_modmul(uint8_t *restrict work)
{
    RsaCtx *ctx = RSA_CTX(work);
    protocore_rsa_modmul(ctx->z, ctx->x, ctx->y, ctx->n.d, ctx->mprime, ctx->rr.d, PROTOCORE_BN_LIMBS);
}

static void rsa_modmul_end(uint8_t *restrict work)
{
    (void)work;
    protocore_rsa_hw_release();
}
#endif

#if !PROTOCORE_RSA_MODMUL_HW
// Full 2*PROTOCORE_BN_LIMBS-limb product of two PROTOCORE_BN_LIMBS-limb little-endian integers.
static void bn_mul_full(const uint32_t *a, const uint32_t *b, uint32_t *p)
{
    for (int k = 0; k < 2 * PROTOCORE_BN_LIMBS; k++)
    {
        p[k] = 0;
    }
    for (int i = 0; i < PROTOCORE_BN_LIMBS; i++)
    {
        uint64_t carry = 0;
        for (int j = 0; j < PROTOCORE_BN_LIMBS; j++)
        {
            const uint64_t cur = (uint64_t)p[i + j] + (uint64_t)a[i] * b[j] + carry;
            p[i + j] = (uint32_t)cur;
            carry = cur >> 32;
        }
        int k = i + PROTOCORE_BN_LIMBS;
        // a and b are both PROTOCORE_BN_LIMBS limbs, so their full product is bounded by
        // 2*PROTOCORE_BN_LIMBS limbs; carry propagation out of the top half can never still be pending
        // when k reaches 2*PROTOCORE_BN_LIMBS. The "k < 2*PROTOCORE_BN_LIMBS" half of this guard is
        // defensive and provably unreachable.
        while (carry && k < 2 * PROTOCORE_BN_LIMBS)
        {
            const uint64_t cur = (uint64_t)p[k] + carry;
            p[k] = (uint32_t)cur;
            carry = cur >> 32;
            k++;
        }
    }
}

// out = p mod m, bit-serial from the top of the double-width product.
static void bn_reduce_full(const uint32_t *p, const uint32_t *m, uint32_t *out)
{
    uint32_t r[PROTOCORE_BN_LIMBS + 1];
    for (int k = 0; k <= PROTOCORE_BN_LIMBS; k++)
    {
        r[k] = 0;
    }

    for (int bit = 2 * PROTOCORE_BN_LIMBS * 32 - 1; bit >= 0; bit--)
    {
        uint32_t carry = 0;
        for (int k = 0; k <= PROTOCORE_BN_LIMBS; k++)
        {
            const uint32_t nc = r[k] >> 31;
            r[k] = (r[k] << 1) | carry;
            carry = nc;
        }
        r[0] |= (p[bit >> 5] >> (bit & 31)) & 1u;

        proto_bool ge = r[PROTOCORE_BN_LIMBS] != 0;
        if (!ge)
        {
            ge = bn_cmp_raw(r, m, PROTOCORE_BN_LIMBS) >= 0;
        }
        if (ge)
        {
            uint64_t borrow = 0;
            for (int k = 0; k < PROTOCORE_BN_LIMBS; k++)
            {
                const uint64_t v = (uint64_t)r[k] - m[k] - borrow;
                r[k] = (uint32_t)v;
                borrow = (v >> 32) & 1u;
            }
            r[PROTOCORE_BN_LIMBS] -= (uint32_t)borrow;
        }
    }
    for (int k = 0; k < PROTOCORE_BN_LIMBS; k++)
    {
        out[k] = r[k];
    }
}

static void rsa_modmul_begin(uint8_t *restrict work)
{
    (void)work;
}

static void rsa_modmul(uint8_t *restrict work)
{
    RsaCtx *ctx = RSA_CTX(work);
    bn_mul_full(ctx->x, ctx->y, ctx->prod);
    bn_reduce_full(ctx->prod, ctx->n.d, ctx->z);
}

static void rsa_modmul_end(uint8_t *restrict work)
{
    (void)work;
}
#endif // !PROTOCORE_RSA_MODMUL_HW (software multiply)

// --- the ladder and the encoding (one arm, both multiplies) ----------------

// acc = base^exp mod n, square-and-multiply from the exponent's top set bit. Both operands are the
// context's and so is the accumulator, so a round stages three pointers and nothing moves.
static void rsa_modexp(uint8_t *restrict work)
{
    RsaCtx *ctx = RSA_CTX(work);

    mem.zero(ctx->acc.d, sizeof(ctx->acc.d));
    ctx->acc.d[0] = 1u;

    int top_limb = PROTOCORE_BN_LIMBS - 1;
    while (top_limb >= 0 && ctx->exp.d[top_limb] == 0u)
    {
        top_limb--;
    }
    if (top_limb < 0)
    {
        return; // exp == 0 -> the accumulator is already 1
    }
    int top_bit = 31;
    // ctx->exp.d[top_limb] != 0 by the scan above, so this finds a set bit before top_bit passes 0;
    // the "top_bit >= 0" half of the guard is defensive, not reachable.
    while (top_bit >= 0 && !((ctx->exp.d[top_limb] >> top_bit) & 1u))
    {
        top_bit--;
    }

    rsa_modmul_begin(work);
    for (int limb = top_limb; limb >= 0; limb--)
    {
        const int start = (limb == top_limb) ? top_bit : 31;
        for (int bit = start; bit >= 0; bit--)
        {
            ctx->x = ctx->acc.d;
            ctx->y = ctx->acc.d;
            ctx->z = ctx->acc.d;
            rsa_modmul(work);
            if ((ctx->exp.d[limb] >> bit) & 1u)
            {
                ctx->x = ctx->acc.d;
                ctx->y = ctx->base.d;
                ctx->z = ctx->acc.d;
                rsa_modmul(work);
            }
        }
    }
    rsa_modmul_end(work);
}

// Digest the staged message and lay it out as the PKCS#1 v1.5 block em (RFC 8017 sec 9.2):
//   0x00 0x01 [0xFF x pad] 0x00 [DigestInfo] [digest]
static void rsa_encode(uint8_t *restrict work)
{
    RsaCtx *ctx = RSA_CTX(work);
    const uint8_t *di;
    size_t di_len;
    size_t digest_len;

    if (ctx->hash == PROTOCORE_RSA_HASH_SHA512)
    {
        Sha512.hash_args.data = ctx->msg;
        Sha512.hash_args.len = ctx->msg_len;
        Sha512.hash_args.out = ctx->digest;
        Sha512.hash(RSA_SHA512(work));
        digest_len = PROTOCORE_SHA512_DIGEST_LEN;
        di = protocore_pkcs1_sha512_digestinfo;
        di_len = PROTOCORE_PKCS1_SHA512_DIGESTINFO_LEN;
    }
    else
    {
        Sha256.hash_args.data = ctx->msg;
        Sha256.hash_args.len = ctx->msg_len;
        Sha256.hash_args.out = ctx->digest;
        Sha256.hash(RSA_SHA256(work));
        digest_len = PROTOCORE_SHA256_DIGEST_LEN;
        di = protocore_pkcs1_sha256_digestinfo;
        di_len = PROTOCORE_PKCS1_DIGESTINFO_LEN;
    }

    const size_t pad_len = PROTOCORE_RSA_KEY_BYTES - 3u - di_len - digest_len;
    ctx->em[0] = 0x00;
    ctx->em[1] = 0x01;
    mem.set(ctx->em + 2, 0xFF, pad_len);
    ctx->em[2 + pad_len] = 0x00;
    mem.cpy(ctx->em + 3 + pad_len, di, di_len);
    mem.cpy(ctx->em + 3 + pad_len + di_len, ctx->digest, digest_len);
}

// --- the entries -----------------------------------------------------------

static void rsa_verify(uint8_t *restrict work)
{
    Rsa.ok = PROTO_FALSE;
    if (!work || !Rsa.verify_args.n || !Rsa.verify_args.e || !Rsa.verify_args.sig ||
        Rsa.verify_args.sig_len != PROTOCORE_RSA_KEY_BYTES)
    {
        return;
    }
    RsaCtx *ctx = RSA_CTX(work);

    Bignum.from_bytes_args.out = &ctx->n;
    Bignum.from_bytes_args.bytes = Rsa.verify_args.n;
    Bignum.from_bytes_args.len = PROTOCORE_RSA_KEY_BYTES;
    Bignum.from_bytes(RSA_BIGNUM(work));

    Bignum.from_bytes_args.out = &ctx->base;
    Bignum.from_bytes_args.bytes = Rsa.verify_args.sig;
    Bignum.from_bytes_args.len = PROTOCORE_RSA_KEY_BYTES;
    Bignum.from_bytes(RSA_BIGNUM(work));

    Bignum.from_bytes_args.out = &ctx->exp;
    Bignum.from_bytes_args.bytes = Rsa.verify_args.e;
    Bignum.from_bytes_args.len = 4u;
    Bignum.from_bytes(RSA_BIGNUM(work));

    // RFC 8017 sec 5.2.2: the signature representative is an integer between 0 and n-1.
    Bignum.cmp_args.a = &ctx->base;
    Bignum.cmp_args.b = &ctx->n;
    Bignum.cmp(RSA_BIGNUM(work));
    if (Bignum.sign >= 0)
    {
        return;
    }

    rsa_modexp(work);

    Bignum.to_bytes_args.bytes = ctx->recovered;
    Bignum.to_bytes_args.in = &ctx->acc;
    Bignum.to_bytes(RSA_BIGNUM(work));

    ctx->msg = Rsa.verify_args.msg;
    ctx->msg_len = Rsa.verify_args.msg_len;
    ctx->hash = Rsa.verify_args.hash;
    rsa_encode(work);

    if (protocore_ct_eq(ctx->recovered, ctx->em, PROTOCORE_RSA_KEY_BYTES))
    {
        Rsa.ok = PROTO_TRUE;
    }
}

static void rsa_sign(uint8_t *restrict work)
{
    Rsa.ok = PROTO_FALSE;
    if (!work || !Rsa.sign_args.n || !Rsa.sign_args.d || !Rsa.sign_args.sig)
    {
        return;
    }
    RsaCtx *ctx = RSA_CTX(work);

    ctx->msg = Rsa.sign_args.msg;
    ctx->msg_len = Rsa.sign_args.msg_len;
    ctx->hash = Rsa.sign_args.hash;
    rsa_encode(work);

    Bignum.from_bytes_args.out = &ctx->n;
    Bignum.from_bytes_args.bytes = Rsa.sign_args.n;
    Bignum.from_bytes_args.len = PROTOCORE_RSA_KEY_BYTES;
    Bignum.from_bytes(RSA_BIGNUM(work));

    Bignum.from_bytes_args.out = &ctx->exp;
    Bignum.from_bytes_args.bytes = Rsa.sign_args.d;
    Bignum.from_bytes_args.len = PROTOCORE_RSA_KEY_BYTES;
    Bignum.from_bytes(RSA_BIGNUM(work));

    Bignum.from_bytes_args.out = &ctx->base;
    Bignum.from_bytes_args.bytes = ctx->em;
    Bignum.from_bytes_args.len = PROTOCORE_RSA_KEY_BYTES;
    Bignum.from_bytes(RSA_BIGNUM(work));

    // RFC 8017 sec 5.2.1: the message representative is an integer between 0 and n-1.
    Bignum.cmp_args.a = &ctx->base;
    Bignum.cmp_args.b = &ctx->n;
    Bignum.cmp(RSA_BIGNUM(work));
    if (Bignum.sign >= 0)
    {
        return;
    }

    rsa_modexp(work);

    Bignum.to_bytes_args.bytes = Rsa.sign_args.sig;
    Bignum.to_bytes_args.in = &ctx->acc;
    Bignum.to_bytes(RSA_BIGNUM(work));
    Rsa.ok = PROTO_TRUE;
}

RsaNs Rsa = {.verify = rsa_verify, .sign = rsa_sign};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_RSA
