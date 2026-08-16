// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file curve25519.c
 * @brief Curve25519 field arithmetic (GF(2^255-19), radix-2^16) + X25519 (RFC 7748).
 *
 * The field element is sixteen int64 limbs of ~16 bits each; a full 16x16 schoolbook
 * product fits in int64 (no 128-bit type, so it builds on 32-bit xtensa). Reduction
 * folds anything at or above 2^256 back as *38 (2^256 = 2*2^255 = 2*19 mod p). The
 * X25519 scalar multiplication is the RFC 7748 §5 Montgomery ladder with constant-time
 * conditional swaps. Validated against the RFC 7748 §5.2 vectors (test_ed25519).
 *
 * On the ESP32-S3 (Arduino) X25519 has a second, byte-identical implementation that runs the
 * ladder in canonical uint32[8] and does each field multiply as one 256-bit modular multiply on
 * the RSA/MPI accelerator (~4.3x the software/PIE ladder); the field layer is in protocore_fe25519.h (shared with
 * Ed25519, active as PROTOCORE_FE25519_MPI_HW). A scalar-mult takes the accelerator lock for its whole
 * run, bracketed by protocore_fe_hw_enable()/protocore_fe_hw_disable().
 *
 * The context is this file's. The module's own borrow holds one scalar multiplication's whole working
 * set: the clamped scalar, the base point a base multiplication runs against, and the ladder's field
 * elements, which are the radix-2^16 seven on the software arm and the canonical sixteen on the
 * accelerated one. The field ops carry nothing across a call and run on the caller's stack; Ed25519
 * links against them directly.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_CURVE25519

#if PROTOCORE_HAS_HW_ECC
#include "sdkconfig.h" // CONFIG_IDF_TARGET_ESP32S3 - selects the vector (PIE) field multiply
#endif
#include "crypto/asymmetric/curve25519.h"
// On the S3, X25519 runs its whole Montgomery ladder in canonical uint32[8] and does each field multiply as
// one 256-bit modular multiply on the RSA/MPI accelerator (~4.3x the software/PIE ladder). That field layer is
// shared with Ed25519 (protocore_ed25519.cpp) and defines PROTOCORE_FE25519_MPI_HW when active (Arduino + S3).
#include "crypto/asymmetric/fe25519.h"
#include "crypto/crypto_opt.h"
#include "mmgr/protomem.h"

PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

// The one definition, both arms, private to this TU. It sits at CURVE25519_OFF_CTX in the caller's
// borrow, so its size never leaves this file and no consumer can name it.
//
// Only what is not derivable: the region lives at a fixed offset in the borrow, so a macro computes it
// from the pointer rather than anything storing it.
#if PROTOCORE_FE25519_MPI_HW
typedef struct
{
    uint8_t e[32];    ///< the clamped scalar
    uint8_t base[32]; ///< the standard base point u = 9
    fe x1;            ///< the input u coordinate
    fe x2;            ///< ladder: (x2:z2) and (x3:z3), the two running points
    fe z2;
    fe x3;
    fe z3;
    fe A; ///< ladder: the RFC 7748 §5 per-bit intermediates
    fe AA;
    fe B;
    fe BB;
    fe E;
    fe C;
    fe D;
    fe DA;
    fe CB;
    fe t0;
    fe t1;
} Curve25519Ctx;
#endif
#if !PROTOCORE_FE25519_MPI_HW
typedef struct
{
    uint8_t z[32];    ///< the clamped scalar
    uint8_t base[32]; ///< the standard base point u = 9
    protocore_gf x;   ///< the input u coordinate
    protocore_gf a;   ///< ladder: the two running points and the per-bit intermediates
    protocore_gf b;
    protocore_gf c;
    protocore_gf d;
    protocore_gf e;
    protocore_gf f;
} Curve25519Ctx;
#endif

// The caller's borrow, split: one scalar multiplication's working set at the base. Nothing else is
// carried, and nothing is carried across a call.
#define CURVE25519_OFF_CTX 0u
#define CURVE25519_OFF_END (CURVE25519_OFF_CTX + sizeof(Curve25519Ctx))
static_assert(CURVE25519_OFF_END <= PROTOCORE_CURVE25519_BORROW,
              "PROTOCORE_CURVE25519_BORROW is short of the ladder's working set - raise it in "
              "protocore_config.h, which derives PROTOCORE_SECURE_ARENA_SIZE from it");

// The region, at its offset in the caller's borrow.
#define CURVE25519_CTX(w) ((Curve25519Ctx *)(void *)((w) + CURVE25519_OFF_CTX))

// ---------------------------------------------------------------------------
// Field arithmetic (GF(2^255-19), radix-2^16). Shared with Ed25519.
// ---------------------------------------------------------------------------

// Small field constant (radix-2^16). Used only by the software X25519 ladder (the S3 MODMULT path carries its
// own canonical a24), so it would be unused there.
#if !PROTOCORE_FE25519_MPI_HW
static const protocore_gf GF_121665 = {0xDB41, 1}; // 121665 = 0x1DB41 (Montgomery a24)
#endif

// Normalize each limb toward 16 bits, folding the carry above 2^256 back in as *38.
// Two passes fully reduce a product's limbs; the +2^16 / -1 dance keeps it branch-free.
static void gf_carry(protocore_gf o)
{
    for (int i = 0; i < 16; i++)
    {
        o[i] += (int64_t)1 << 16;
        int64_t c = o[i] >> 16;
        o[(i + 1) * (i < 15)] += c - 1 + 37 * (c - 1) * (i == 15); // wrap: limb 15's carry *38 into limb 0
        o[i] -= c << 16;
    }
}

void protocore_gf_copy(protocore_gf out, const protocore_gf in)
{
    for (int i = 0; i < 16; i++)
    {
        out[i] = in[i];
    }
}

void protocore_gf_add(protocore_gf out, const protocore_gf a, const protocore_gf b)
{
    for (int i = 0; i < 16; i++)
    {
        out[i] = a[i] + b[i];
    }
}

void protocore_gf_sub(protocore_gf out, const protocore_gf a, const protocore_gf b)
{
    for (int i = 0; i < 16; i++)
    {
        out[i] = a[i] - b[i];
    }
}

#if defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3
// ---- ESP32-S3 vector (PIE) field multiply --------------------------------------------------------
// The S3 vector unit multiply-accumulates signed-16-bit lanes into a 40-bit accumulator
// (ee.vmulas.s16.accx), but radix-2^16 limbs run to ~2^18 and go negative after a subtraction, so they
// do not fit s16. Fix: balance each limb into signed-16-bit (a value-preserving carry redistribution
// mod p), after which a*b is a pure s16xs16 convolution - exactly the vector MAC. Device-validated
// byte-exact vs the scalar path (rig test, and test_gf_mul_s16_model_matches_scalar for the model);
// ~1.55x the scalar multiply on hardware.

// Balance a[16] into signed-16-bit limbs of the same value mod p (round-to-nearest carry; limb-15
// overflow wraps *38 into limb 0 since 2^256 == 38; three passes settle the wrap).
static void gf_balance_s16(int16_t o[16], const protocore_gf a)
{
    // int32 throughout: limbs stay ~+-2^18 and carries ~+-2, so no value exceeds int32 - which avoids the
    // emulated 64-bit carry-propagation math (48 steps per operand) that dominated the field multiply.
    int32_t c[16];
    for (int i = 0; i < 16; i++)
    {
        c[i] = (int32_t)a[i];
    }
    for (int pass = 0; pass < 3; pass++)
    {
        int32_t carry = 0;
        for (int i = 0; i < 16; i++)
        {
            int32_t v = c[i] + carry;
            carry = (v + 0x8000) >> 16;
            c[i] = v - (carry << 16);
        }
        c[0] += 38 * carry;
    }
    for (int i = 0; i < 16; i++)
    {
        o[i] = (int16_t)c[i];
    }
}

// One output limb t[k] = as[0..15] . window[0..15] on the ACCX. as is 16-byte aligned; the reversed-b
// window w may be unaligned (loaded via ee.ld.128.usar + ee.src.q). ACCX is 40-bit: sign-extend bit 39.
static inline int64_t gf_accx_dot_win(const int16_t *as, const int16_t *w)
{
    uint32_t lo, hi;
    const int16_t *pa = as, *pw = w;
    asm volatile("ee.zero.accx\n"
                 "ee.vld.128.ip q3, %[a], 16\n"
                 "ee.ld.128.usar.ip q0, %[w], 16\n"
                 "ee.ld.128.usar.ip q1, %[w], 16\n"
                 "ee.src.q q2, q0, q1\n"
                 "ee.vmulas.s16.accx q3, q2\n"
                 "ee.vld.128.ip q3, %[a], 16\n"
                 "ee.ld.128.usar.ip q0, %[w], 16\n"
                 "ee.src.q q2, q1, q0\n"
                 "ee.vmulas.s16.accx q3, q2\n"
                 "rur.accx_0 %[lo]\n"
                 "rur.accx_1 %[hi]\n"
                 : [lo] "=&r"(lo), [hi] "=&r"(hi), [a] "+r"(pa), [w] "+r"(pw)
                 :
                 : "memory");
    uint64_t raw = (uint64_t)lo | ((uint64_t)(uint8_t)hi << 32);
    return ((int64_t)(raw << 24)) >> 24;
}

// Shared tail: build the reversed-bs window array, run the ACCX convolution, fold + carry. as points at
// a 16-byte-aligned int16[16]; bs is read scalar-only (no alignment needed).
static void gf_conv_finish(protocore_gf out, const int16_t *as, const int16_t *bs)
{
    // bp = [15 zeros][bs reversed: bs15..bs0][zeros]; output k's window starts at bp[30-k].
    __attribute__((aligned(16))) int16_t bp[64];
    for (int i = 0; i < 64; i++)
    {
        bp[i] = 0;
    }
    for (int m = 0; m < 16; m++)
    {
        bp[15 + m] = bs[15 - m];
    }
    int64_t t[31];
    for (int k = 0; k < 31; k++)
    {
        t[k] = gf_accx_dot_win(as, bp + (30 - k));
    }
    for (int i = 0; i < 15; i++)
    {
        t[i] += 38 * t[i + 16];
    }
    for (int i = 0; i < 16; i++)
    {
        out[i] = t[i];
    }
    gf_carry(out);
    gf_carry(out);
}

void protocore_gf_mul(protocore_gf out, const protocore_gf a, const protocore_gf b)
{
    __attribute__((aligned(16))) int16_t as[16];
    int16_t bs[16];
    gf_balance_s16(as, a);
    gf_balance_s16(bs, b);
    gf_conv_finish(out, as, bs);
}

// Squaring balances the operand ONCE (a == b, and gf_balance_s16 is deterministic, so the second balance
// in mul(a,a) is pure waste). ~2/3 of the Montgomery-ladder field ops are squarings, so this matters.
// Byte-exact with protocore_gf_mul(out, a, a) by construction.
void protocore_gf_sq(protocore_gf out, const protocore_gf a)
{
    __attribute__((aligned(16))) int16_t as[16];
    gf_balance_s16(as, a);
    gf_conv_finish(out, as, as);
}
#endif // CONFIG_IDF_TARGET_ESP32S3 (vector field multiply)

#if !(defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3)
void protocore_gf_mul(protocore_gf out, const protocore_gf a, const protocore_gf b)
{
    int64_t t[31];
    for (int i = 0; i < 31; i++)
    {
        t[i] = 0;
    }
    // Every limb fits in int32 (~16-18 bits after carry / add / sub), but a[i] and b[j] are int64, so a
    // plain a[i]*b[j] compiles to a full emulated 64x64 multiply on the 32-bit xtensa core. Casting both
    // operands to int32 makes gcc emit a single widening 32x32->64 multiply (mull+mulsh) instead - the
    // products (~34 bits) and their sums (~38 bits) still accumulate in the int64 t[].
    for (int i = 0; i < 16; i++)
    {
        int32_t ai = (int32_t)a[i];
        for (int j = 0; j < 16; j++)
        {
            t[i + j] += (int64_t)ai * (int32_t)b[j];
        }
    }
    for (int i = 0; i < 15; i++)
    {
        t[i] += 38 * t[i + 16]; // fold the upper half (weight >= 2^256) down
    }
    for (int i = 0; i < 16; i++)
    {
        out[i] = t[i];
    }
    gf_carry(out);
    gf_carry(out);
}
#endif

#if !(defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3)
void protocore_gf_sq(protocore_gf out, const protocore_gf a)
{
    protocore_gf_mul(out, a, a);
}
#endif

// Software field inversion out = a^-1 = a^(p-2). Fixed addition chain: square 255 times,
// multiplying in a at every bit except positions 2 and 4 (which are 0 in p-2 = 2^255 - 21).
// The reference path (native builds) and the fallback if the hardware modexp ever fails.
static void gf_inv_sw(protocore_gf out, const protocore_gf a)
{
    protocore_gf c;
    protocore_gf_copy(c, a);
    for (int i = 253; i >= 0; i--)
    {
        protocore_gf_sq(c, c);
        if (i != 2 && i != 4)
        {
            protocore_gf_mul(c, c, a);
        }
    }
    protocore_gf_copy(out, c);
}

#if PROTOCORE_FE25519_MPI_HW
// out = a^-1 mod p through the canonical field layer, whose multiply is one 256-bit MODMULT on the
// RSA/MPI accelerator. Only the inversion is offloaded; the 255-round Montgomery ladder multiply stays
// in the software radix-2^16 core, where per-multiply marshalling to the peripheral would cost more
// than it saves. The base is packed to its canonical residue first.
void protocore_gf_inv(protocore_gf out, const protocore_gf a)
{
    uint8_t le[32];
    fe x;
    fe r;
    fe zero;
    protocore_gf_pack(le, a); // canonical little-endian residue in [0, p)
    fe_frombytes(x, le);
    protocore_fe_hw_enable();
    fe_invert(r, x);
    protocore_fe_hw_disable();
    fe_0(zero);
    if (fe_neq(r, zero) == 0)
    {
        gf_inv_sw(out, a); // the modmul zeroes its result on a peripheral timeout; keep correctness
        return;
    }
    fe_tobytes(le, r);
    protocore_gf_unpack(out, le);
}
#endif

#if !PROTOCORE_FE25519_MPI_HW
void protocore_gf_inv(protocore_gf out, const protocore_gf a)
{
    gf_inv_sw(out, a);
}
#endif

// Constant-time conditional swap of p and q when b == 1 (b must be 0 or 1).
void protocore_gf_cswap(protocore_gf p, protocore_gf q, int b)
{
    int64_t mask = ~((int64_t)b - 1); // all ones when b==1, zero when b==0
    for (int i = 0; i < 16; i++)
    {
        int64_t t = mask & (p[i] ^ q[i]);
        p[i] ^= t;
        q[i] ^= t;
    }
}

// Canonical little-endian encoding: fully reduce mod p (conditional subtract twice),
// then emit 16-bit limbs low byte first.
void protocore_gf_pack(uint8_t out[32], const protocore_gf a)
{
    protocore_gf t;
    protocore_gf m;
    protocore_gf_copy(t, a);
    gf_carry(t);
    gf_carry(t);
    gf_carry(t);
    for (int j = 0; j < 2; j++)
    {
        m[0] = t[0] - 0xffed;
        for (int i = 1; i < 15; i++)
        {
            m[i] = t[i] - 0xffff - ((m[i - 1] >> 16) & 1);
            m[i - 1] &= 0xffff;
        }
        m[15] = t[15] - 0x7fff - ((m[14] >> 16) & 1);
        int b = (int)((m[15] >> 16) & 1);
        m[14] &= 0xffff;
        protocore_gf_cswap(t, m, 1 - b); // keep the subtracted value only if it did not borrow
    }
    for (int i = 0; i < 16; i++)
    {
        out[2 * i] = (uint8_t)(t[i] & 0xff);
        out[2 * i + 1] = (uint8_t)(t[i] >> 8);
    }
}

// Decode 32 little-endian bytes into a field element; the top bit is masked off (255-bit).
void protocore_gf_unpack(protocore_gf out, const uint8_t in[32])
{
    for (int i = 0; i < 16; i++)
    {
        out[i] = (int64_t)in[2 * i] + ((int64_t)in[2 * i + 1] << 8);
    }
    out[15] &= 0x7fff;
}

// ---------------------------------------------------------------------------
// X25519 (RFC 7748 §5). The ladder runs entirely in the caller's borrow.
// ---------------------------------------------------------------------------

#if PROTOCORE_FE25519_MPI_HW
// ============================= ESP32-S3 X25519 on the RSA/MPI accelerator =================================
// The canonical uint32[8] field layer (fe, fe_add/sub/mul/sq/..., the MODMULT, and the lock+power bring-up)
// lives in protocore_fe25519.h - shared with Ed25519. Here is only the X25519-specific a24 and the RFC 7748 ladder.
static const uint32_t FE_A24[8] = {121665u, 0, 0, 0, 0, 0, 0, 0}; // X25519 a24 = (486662-2)/4 (RFC 7748 §5)

// out = scalar * point. Every field element is a named member of the borrow's context, bound here so the
// ladder below reads as the RFC does.
static void x25519_mult(uint8_t *restrict work, uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32])
{
    Curve25519Ctx *ctx = CURVE25519_CTX(work);
    uint8_t *e = ctx->e;
    for (int i = 0; i < 32; i++)
    {
        e[i] = scalar[i];
    }
    e[0] &= 248; // clamp the scalar (RFC 7748 §5)
    e[31] &= 127;
    e[31] |= 64;

    protocore_fe_hw_enable(); // lock + power the accelerator for the whole ladder

    uint32_t *x1 = ctx->x1;
    uint32_t *x2 = ctx->x2;
    uint32_t *z2 = ctx->z2;
    uint32_t *x3 = ctx->x3;
    uint32_t *z3 = ctx->z3;
    uint32_t *A = ctx->A;
    uint32_t *AA = ctx->AA;
    uint32_t *B = ctx->B;
    uint32_t *BB = ctx->BB;
    uint32_t *E = ctx->E;
    uint32_t *C = ctx->C;
    uint32_t *D = ctx->D;
    uint32_t *DA = ctx->DA;
    uint32_t *CB = ctx->CB;
    uint32_t *t0 = ctx->t0;
    uint32_t *t1 = ctx->t1;
    fe_frombytes(x1, point);
    fe_1(x2);
    fe_0(z2);
    fe_copy(x3, x1);
    fe_1(z3);
    uint32_t swap = 0;

    // Montgomery ladder over the 255 scalar bits, high to low (RFC 7748 §5).
    for (int t = 254; t >= 0; t--)
    {
        uint32_t k_t = (e[t >> 3] >> (t & 7)) & 1;
        swap ^= k_t;
        fe_cswap(x2, x3, swap);
        fe_cswap(z2, z3, swap);
        swap = k_t;
        fe_add(A, x2, z2);
        fe_sq(AA, A);
        fe_sub(B, x2, z2);
        fe_sq(BB, B);
        fe_sub(E, AA, BB);
        fe_add(C, x3, z3);
        fe_sub(D, x3, z3);
        fe_mul(DA, D, A);
        fe_mul(CB, C, B);
        fe_add(t0, DA, CB);
        fe_sq(x3, t0);
        fe_sub(t1, DA, CB);
        fe_sq(t1, t1);
        fe_mul(z3, x1, t1);
        fe_mul(x2, AA, BB);
        fe_mul(t0, FE_A24, E);
        fe_add(t0, AA, t0);
        fe_mul(z2, E, t0);
    }
    fe_cswap(x2, x3, swap);
    fe_cswap(z2, z3, swap);

    // Result = X / Z = x2 * z2^-1.
    fe_invert(z2, z2);
    fe_mul(x2, x2, z2);
    fe_tobytes(out, x2);
    protocore_fe_hw_disable(); // release the lock + power down
}
#endif

#if !PROTOCORE_FE25519_MPI_HW
// out = scalar * point. Every field element is a named member of the borrow's context, bound here so the
// ladder below reads as the RFC does.
static void x25519_mult(uint8_t *restrict work, uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32])
{
    Curve25519Ctx *ctx = CURVE25519_CTX(work);
    uint8_t *z = ctx->z;
    for (int i = 0; i < 31; i++)
    {
        z[i] = scalar[i];
    }
    z[31] = (uint8_t)((scalar[31] & 127) | 64); // clamp the scalar (RFC 7748 §5)
    z[0] &= 248;

    int64_t *x = ctx->x;
    int64_t *a = ctx->a;
    int64_t *b = ctx->b;
    int64_t *c = ctx->c;
    int64_t *d = ctx->d;
    int64_t *e = ctx->e;
    int64_t *f = ctx->f;
    protocore_gf_unpack(x, point);
    for (int i = 0; i < 16; i++)
    {
        b[i] = x[i];
        a[i] = c[i] = d[i] = 0;
    }
    a[0] = d[0] = 1;

    // Montgomery ladder over the 255 scalar bits, high to low.
    for (int i = 254; i >= 0; i--)
    {
        int r = (z[i >> 3] >> (i & 7)) & 1;
        protocore_gf_cswap(a, b, r);
        protocore_gf_cswap(c, d, r);
        protocore_gf_add(e, a, c);
        protocore_gf_sub(a, a, c);
        protocore_gf_add(c, b, d);
        protocore_gf_sub(b, b, d);
        protocore_gf_sq(d, e);
        protocore_gf_sq(f, a);
        protocore_gf_mul(a, c, a);
        protocore_gf_mul(c, b, e);
        protocore_gf_add(e, a, c);
        protocore_gf_sub(a, a, c);
        protocore_gf_sq(b, a);
        protocore_gf_sub(c, d, f);
        protocore_gf_mul(a, c, GF_121665);
        protocore_gf_add(a, a, d);
        protocore_gf_mul(c, c, a);
        protocore_gf_mul(a, d, f);
        protocore_gf_mul(d, b, x);
        protocore_gf_sq(b, e);
        protocore_gf_cswap(a, b, r);
        protocore_gf_cswap(c, d, r);
    }

    // Result = X / Z = a * c^-1.
    protocore_gf_inv(c, c);
    protocore_gf_mul(a, a, c);
    protocore_gf_pack(out, a);
}
#endif // !PROTOCORE_FE25519_MPI_HW (SW path)

// --- the entries -----------------------------------------------------------

static void curve25519_x25519(uint8_t *restrict work)
{
    Curve25519.ok = PROTO_FALSE;
    if (!work || !Curve25519.x25519_args.out || !Curve25519.x25519_args.scalar || !Curve25519.x25519_args.point)
    {
        return;
    }
    x25519_mult(work, Curve25519.x25519_args.out, Curve25519.x25519_args.scalar, Curve25519.x25519_args.point);
    Curve25519.ok = PROTO_TRUE;
}

// The standard base point is written into the borrow and multiplied by the same body: u = 9, the rest
// zero (RFC 7748 §5).
static void curve25519_x25519_base(uint8_t *restrict work)
{
    Curve25519.ok = PROTO_FALSE;
    if (!work || !Curve25519.x25519_base_args.out || !Curve25519.x25519_base_args.scalar)
    {
        return;
    }
    Curve25519Ctx *ctx = CURVE25519_CTX(work);
    mem.zero(ctx->base, 32);
    ctx->base[0] = 9;
    x25519_mult(work, Curve25519.x25519_base_args.out, Curve25519.x25519_base_args.scalar, ctx->base);
    Curve25519.ok = PROTO_TRUE;
}

Curve25519Ns Curve25519 = {.x25519 = curve25519_x25519, .x25519_base = curve25519_x25519_base};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CURVE25519
