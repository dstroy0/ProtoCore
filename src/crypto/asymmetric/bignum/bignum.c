// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file bignum.c
 * @brief 2048-bit big-integer arithmetic implementation (see bignum.h).
 *
 * The group-14 prime and generator, the big-endian conversions, the limb compare the backends share,
 * the RFC 4253 §8 DH check, and the entry that hands a modular exponentiation to the linked backend.
 *
 * The context is this file's. The module's own borrow is split by offset into the operands an entry
 * stages for its helpers and the p-1 the DH check compares against.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_BIGNUM

#include "crypto/asymmetric/bignum/bignum.h"
#include "crypto/crypto_opt.h"
#include "mmgr/protomem/protomem.h"

PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

// The one definition of BignumCtx - private to this TU. It sits at BIGNUM_OFF_CTX in the caller's
// borrow, so its size never leaves this file and no consumer can name it. An entry stages its operands
// here and the helpers below read them, so a helper carries one parameter.
//
// Only what is not derivable: p-1 lives at a fixed offset in the caller's borrow, so a macro computes
// it from the pointer rather than the context storing it.
typedef struct BignumCtx
{
    const uint32_t *a;            ///< left magnitude a compare reads
    const uint32_t *b;            ///< right magnitude a compare reads
    int n;                        ///< limbs the compare spans
    int sign;                     ///< sign of a - b the compare left: -1, 0 or 1
    protocore_bignum *out;        ///< where a modexp result lands
    const protocore_bignum *base; ///< the modexp base
    const protocore_bignum *exp;  ///< the modexp exponent
} BignumCtx;

// The caller's borrow, split: the staged operands, then the p-1 the DH check builds and compares
// against. p-1 is 256 bytes and is built per call, so it lives here rather than on the stack.
#define BIGNUM_OFF_CTX 0u
#define BIGNUM_OFF_PM1 (BIGNUM_OFF_CTX + sizeof(struct BignumCtx))
static_assert(BIGNUM_OFF_PM1 + sizeof(protocore_bignum) <= PROTOCORE_BIGNUM_BORROW,
              "PROTOCORE_BIGNUM_BORROW is short of the staged operands and p-1 - raise it in "
              "protocore_config.h, which sums it into the secure arena");

// A region reached through a cast is only aligned if its OFFSET is: the arena aligns the base up to
// PROTOCORE_ARENA_MAX_ALIGN, so a borrow is met by aligning its offset alone. Both sides are
// compile-time constants, so this is a compile-time claim rather than a runtime branch. The size
// assert above bounds the far end of the chain and says nothing about where a region begins.
static_assert(BIGNUM_OFF_PM1 % _Alignof(protocore_bignum) == 0,
              "BIGNUM_OFF_PM1 is not a multiple of alignof(protocore_bignum) - BIGNUM_PM1() would return a misaligned "
              "pointer; pad the region ahead of it");

// The regions, at their offsets in the caller's borrow.
#define BIGNUM_CTX(w) ((struct BignumCtx *)(void *)((w) + BIGNUM_OFF_CTX))
#define BIGNUM_PM1(w) ((protocore_bignum *)(void *)((w) + BIGNUM_OFF_PM1))

// ---------------------------------------------------------------------------
// Group-14 prime and generator (RFC 3526, §3)
// Little-endian 32-bit limbs: d[0] = least significant.
// ---------------------------------------------------------------------------

const protocore_bignum group14_p = {{
    // 2048-bit MODP group-14 prime
    0xFFFFFFFFu, 0xFFFFFFFFu, 0x8AACaa68u, 0x15728E5Au, 0x98FA0510u, 0x15D22618u, 0xEA956AE5u, 0x3995497Cu,
    0x95581718u, 0xDE2BCBF6u, 0x6F4C52C9u, 0xB5C55DF0u, 0xEC07A28Fu, 0x9B2783A2u, 0x180E8603u, 0xE39E772Cu,
    0x2E36CE3Bu, 0x32905E46u, 0xCA18217Cu, 0xF1746C08u, 0x4ABC9804u, 0x670C354Eu, 0x7096966Du, 0x9ED52907u,
    0x208552BBu, 0x1C62F356u, 0xDCA3AD96u, 0x83655D23u, 0xFD24CF5Fu, 0x69163FA8u, 0x1C55D39Au, 0x98DA4836u,
    0xA163BF05u, 0xC2007CB8u, 0xECE45B3Du, 0x49286651u, 0x7C4B1FE6u, 0xAE9F2411u, 0x5A899FA5u, 0xEE386BFBu,
    0xF406B7EDu, 0x0BFF5CB6u, 0xA637ED6Bu, 0xF44C42E9u, 0x625E7EC6u, 0xE485B576u, 0x6D51C245u, 0x4FE1356Du,
    0xF25F1437u, 0x302B0A6Du, 0xCD3A431Bu, 0xEF9519B3u, 0x8E3404DDu, 0x514A0879u, 0x3B139B22u, 0x020BBEa6u,
    0x8A67CC74u, 0x29024E08u, 0x80DC1CD1u, 0xC4C6628Bu, 0x2168C234u, 0xC90FDAA2u, 0xFFFFFFFFu, 0xFFFFFFFFu,
}};

const protocore_bignum group14_g = {{
    2u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
    0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u, 0u,
}};

// ---------------------------------------------------------------------------
// Shared with the backends
// ---------------------------------------------------------------------------

int bn_cmp_raw(const uint32_t *a, const uint32_t *b, int n)
{
    for (int i = n - 1; i >= 0; i--)
    {
        if (a[i] < b[i])
        {
            return -1;
        }
        if (a[i] > b[i])
        {
            return 1;
        }
    }
    return 0;
}

// --- the helpers, over the staged operands ---------------------------------

// Order the staged magnitudes, leaving the sign of a - b in the context.
static void bignum_order(uint8_t *restrict work)
{
    struct BignumCtx *ctx = BIGNUM_CTX(work);
    ctx->sign = bn_cmp_raw(ctx->a, ctx->b, ctx->n);
}

// Hand the staged operands to the backend the vendor linked.
static void bignum_expmod(uint8_t *restrict work)
{
    struct BignumCtx *ctx = BIGNUM_CTX(work);
    bn_expmod_group14(ctx->out, ctx->base, ctx->exp);
}

// --- the entries -----------------------------------------------------------

static void bignum_from_bytes(uint8_t *restrict work)
{
    Bignum.ok = PROTO_FALSE;
    if (!Bignum.from_bytes_args.out || !Bignum.from_bytes_args.bytes)
    {
        return;
    }
    protocore_bignum *out = Bignum.from_bytes_args.out;
    const uint8_t *bytes = Bignum.from_bytes_args.bytes;
    const size_t len = Bignum.from_bytes_args.len;

    mem.set(out->d, 0, sizeof(protocore_bignum));
    // bytes are big-endian; map to little-endian limbs.
    size_t blen = len < 256 ? len : 256;
    for (size_t i = 0; i < blen; i++)
    {
        size_t byte_pos = i; // byte i from LSB (bytes[len-1-i] is the i-th byte from the end)
        out->d[byte_pos / 4] |= (uint32_t)bytes[len - 1 - i] << ((byte_pos % 4) * 8);
    }
    Bignum.ok = PROTO_TRUE;
}

static void bignum_to_bytes(uint8_t *restrict work)
{
    Bignum.ok = PROTO_FALSE;
    if (!Bignum.to_bytes_args.bytes || !Bignum.to_bytes_args.in)
    {
        return;
    }
    uint8_t *bytes = Bignum.to_bytes_args.bytes;
    const protocore_bignum *in = Bignum.to_bytes_args.in;

    for (int i = 0; i < PROTOCORE_BN_LIMBS; i++)
    {
        uint32_t v = in->d[PROTOCORE_BN_LIMBS - 1 - i];
        bytes[i * 4 + 0] = (uint8_t)(v >> 24);
        bytes[i * 4 + 1] = (uint8_t)(v >> 16);
        bytes[i * 4 + 2] = (uint8_t)(v >> 8);
        bytes[i * 4 + 3] = (uint8_t)(v);
    }
    Bignum.ok = PROTO_TRUE;
}

static void bignum_cmp(uint8_t *restrict work)
{
    Bignum.ok = PROTO_FALSE;
    if (!Bignum.cmp_args.a || !Bignum.cmp_args.b)
    {
        return;
    }
    struct BignumCtx *ctx = BIGNUM_CTX(work);
    ctx->a = Bignum.cmp_args.a->d;
    ctx->b = Bignum.cmp_args.b->d;
    ctx->n = PROTOCORE_BN_LIMBS;
    bignum_order(work);
    Bignum.sign = ctx->sign;
    Bignum.ok = PROTO_TRUE;
}

static void bignum_cmp_raw(uint8_t *restrict work)
{
    Bignum.ok = PROTO_FALSE;
    if (!Bignum.cmp_raw_args.a || !Bignum.cmp_raw_args.b)
    {
        return;
    }
    struct BignumCtx *ctx = BIGNUM_CTX(work);
    ctx->a = Bignum.cmp_raw_args.a;
    ctx->b = Bignum.cmp_raw_args.b;
    ctx->n = Bignum.cmp_raw_args.n;
    bignum_order(work);
    Bignum.sign = ctx->sign;
    Bignum.ok = PROTO_TRUE;
}

static void bignum_is_zero(uint8_t *restrict work)
{
    Bignum.ok = PROTO_FALSE;
    Bignum.zero = PROTO_FALSE;
    if (!Bignum.is_zero_args.a)
    {
        return;
    }
    const protocore_bignum *a = Bignum.is_zero_args.a;

    for (int i = 0; i < PROTOCORE_BN_LIMBS; i++)
    {
        if (a->d[i])
        {
            Bignum.ok = PROTO_TRUE;
            return;
        }
    }
    Bignum.zero = PROTO_TRUE;
    Bignum.ok = PROTO_TRUE;
}

static void bignum_expmod_group14(uint8_t *restrict work)
{
    Bignum.ok = PROTO_FALSE;
    if (!Bignum.expmod_args.out || !Bignum.expmod_args.base || !Bignum.expmod_args.exp)
    {
        return;
    }
    struct BignumCtx *ctx = BIGNUM_CTX(work);
    ctx->out = Bignum.expmod_args.out;
    ctx->base = Bignum.expmod_args.base;
    ctx->exp = Bignum.expmod_args.exp;
    bignum_expmod(work);
    Bignum.ok = PROTO_TRUE;
}

// RFC 4253 §8: the received value e (or f) must satisfy 1 < e < p-1.
static void bignum_dh_validate(uint8_t *restrict work)
{
    Bignum.ok = PROTO_FALSE;
    if (!Bignum.validate_args.v)
    {
        return;
    }
    const protocore_bignum *v = Bignum.validate_args.v;
    struct BignumCtx *ctx = BIGNUM_CTX(work);
    protocore_bignum *pm1 = BIGNUM_PM1(work);

    // Must be > 1
    int ok = 0;
    for (int i = 1; i < PROTOCORE_BN_LIMBS; i++)
    {
        if (v->d[i])
        {
            ok = 1;
            break;
        }
    }
    if (!ok && v->d[0] <= 1u)
    {
        return;
    }
    // Must be < p-1
    // p-1: subtract 1 from p
    *pm1 = group14_p;
    pm1->d[0]--;
    ctx->a = v->d;
    ctx->b = pm1->d;
    ctx->n = PROTOCORE_BN_LIMBS;
    bignum_order(work);
    if (ctx->sign >= 0)
    {
        return;
    }
    Bignum.ok = PROTO_TRUE;
}

BignumNs Bignum = {.from_bytes = bignum_from_bytes,
                   .to_bytes = bignum_to_bytes,
                   .cmp = bignum_cmp,
                   .cmp_raw = bignum_cmp_raw,
                   .is_zero = bignum_is_zero,
                   .expmod_group14 = bignum_expmod_group14,
                   .dh_validate = bignum_dh_validate};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_BIGNUM
