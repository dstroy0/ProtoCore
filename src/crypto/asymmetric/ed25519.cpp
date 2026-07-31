// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ed25519.cpp
 * @brief Ed25519 (RFC 8032) sign + verify over edwards25519.
 *
 * Points are held in extended twisted-Edwards coordinates (X, Y, Z, T) as four field
 * elements; scalar multiplication is a constant-time bit-by-bit double-and-add; scalars
 * are reduced mod the group order L. SHA-512 hashes the seed, the nonce input, and R||A||M.
 * Deterministic (no RNG). Validated against the RFC 8032 §7.1 vectors and a reference
 * implementation (test_ed25519).
 *
 * The field elements and point arithmetic have two implementations: the portable radix-2^16
 * `pc_gf` (from pc_curve25519, the native / non-S3 path), and on the ESP32-S3 a canonical
 * `uint32[8]` layer that does each field multiply as one 256-bit modular multiply on the
 * RSA/MPI accelerator (pc_fe25519.h, active as PC_FE25519_MPI_HW) - the same engine that
 * accelerates the X25519 KEX, here driving the Edwards point arithmetic so the host-key
 * signature runs several times faster. Only the point/field layer differs; the SHA-512 hashing
 * and the scalar arithmetic mod L are shared. Both paths are byte-identical by construction.
 */

#include "crypto/asymmetric/ed25519.h"
#include "crypto/asymmetric/curve25519.h" // pc_gf + field ops (native / non-S3 path)
#include "crypto/asymmetric/fe25519.h"    // MODMULT dies: canonical uint32[8] field on the RSA accelerator
#include "crypto/crypto_opt.h"
#include "crypto/crypto_scratch.h" // pc_ct_eq (the canonical constant-time compare)
#include "crypto/hash/sha512.h"
#ifdef PC_FE25519_MPI_HW
#include "crypto/asymmetric/ed25519_comb_table.h" // fixed-base comb ED_COMB[i][j] = (j+1)*256^i*B; drives the MODMULT sign
#endif
PC_CRYPTO_HOT

// --- Shared constants -------------------------------------------------------

// The group order L = 2^252 + 27742317777372353535851937790883648493, little-endian.
static const int64_t ED_L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
                                 0xa2, 0xde, 0xf9, 0xde, 0x14, 0,    0,    0,    0,    0,    0,
                                 0,    0,    0,    0,    0,    0,    0,    0,    0,    0x10};

// --- Shared helpers (representation-independent) ----------------------------

// Constant-time 32-byte compare: 0 if equal, -1 otherwise (Ed25519 verify + point decode, public data).
static int ct_verify32(const uint8_t *x, const uint8_t *y)
{
    return pc_ct_eq(x, y, 32) ? 0 : -1;
}

// --- Scalar reduction mod L -------------------------------------------------

// Reduce the 512-bit little-endian value x[0..63] modulo L into r[0..31].
static void ed_modL(uint8_t r[32], int64_t x[64])
{
    int64_t carry;
    int i;
    int j;
    for (i = 63; i >= 32; --i)
    {
        carry = 0;
        for (j = i - 32; j < i - 12; ++j)
        {
            x[j] += carry - 16 * x[i] * ED_L[j - (i - 32)];
            carry = (x[j] + 128) >> 8;
            x[j] -= carry << 8;
        }
        x[j] += carry;
        x[i] = 0;
    }
    carry = 0;
    for (j = 0; j < 32; ++j)
    {
        x[j] += carry - (x[31] >> 4) * ED_L[j];
        carry = x[j] >> 8;
        x[j] &= 255;
    }
    for (j = 0; j < 32; ++j)
    {
        x[j] -= carry * ED_L[j];
    }
    for (i = 0; i < 32; ++i)
    {
        x[i + 1] += x[i] >> 8;
        r[i] = (uint8_t)(x[i] & 255);
    }
}

// Reduce a 64-byte hash in place: r[0..31] = r mod L.
static void ed_reduce(uint8_t r[64])
{
    int64_t x[64];
    for (int i = 0; i < 64; i++)
    {
        x[i] = (int64_t)(uint64_t)r[i];
    }
    for (int i = 0; i < 64; i++)
    {
        r[i] = 0;
    }
    ed_modL(r, x);
}

// True iff the little-endian 32-byte scalar S is canonical (0 <= S < L). RFC 8032 §5.1.7 requires this:
// S and S+L both satisfy the group equation (L*B is the identity), so without the range check the signature
// is malleable. Verification operates only on public data, so a plain compare from the top byte down is fine.
static bool ed_scalar_canonical(const uint8_t s[32])
{
    for (int i = 31; i >= 0; i--)
    {
        uint8_t li = (uint8_t)ED_L[i];
        if (s[i] < li)
        {
            return true;
        }
        if (s[i] > li)
        {
            return false;
        }
    }
    return false; // S == L is out of range
}

#ifdef PC_FE25519_MPI_HW
// ===================== ESP32-S3 Edwards point arithmetic on the RSA/MPI field ============================
// The curve constants as canonical uint32[8] (each word = pc_gf limb 2i | limb 2i+1 << 16 of the radix-2^16
// constants below; the point arithmetic is byte-identical to the pc_gf path, verified by the RFC 8032 KAT).
static const fe ED_D_FE = {0x135978a3, 0x75eb4dca, 0x4141d8ab, 0x00700a4d,
                           0x7779e898, 0x8cc74079, 0x2b6ffe73, 0x52036cee}; // d = -121665/121666
static const fe ED_D2_FE = {0x26b2f159, 0xebd69b94, 0x8283b156, 0x00e0149a,
                            0xeef3d130, 0x198e80f2, 0x56dffce7, 0x2406d9dc}; // 2d
static const fe ED_X_FE = {0x8f25d51a, 0xc9562d60, 0x9525a7b2, 0x692cc760,
                           0xfdd6dc5c, 0xc0a4e231, 0xcd6e53fe, 0x216936d3}; // base point x
static const fe ED_Y_FE = {0x66666658, 0x66666666, 0x66666666, 0x66666666,
                           0x66666666, 0x66666666, 0x66666666, 0x66666666}; // base point y
static const fe ED_I_FE = {0x4a0ea0b0, 0xc4ee1b27, 0xad2fe478, 0x2f431806,
                           0x3dfbd7a7, 0x2b4d0099, 0x4fc1df0b, 0x2b832480}; // sqrt(-1) mod p

// ED_COMB (the fixed-base comb table) is included at the top of the file with the other headers.

// p += q (twisted-Edwards addition, RFC 8032 §5.1.4). Safe when q aliases p (point doubling): every read of
// q happens before any write of p. Requires pc_fe_hw_enable() (the fe_mul/fe_sq run on the accelerator).
static void edf_add(fe p[4], fe q[4])
{
    fe a;
    fe b;
    fe c;
    fe d;
    fe t;
    fe e;
    fe f;
    fe g;
    fe h;
    fe_sub(a, p[1], p[0]);
    fe_sub(t, q[1], q[0]);
    fe_mul(a, a, t);
    fe_add(b, p[0], p[1]);
    fe_add(t, q[0], q[1]);
    fe_mul(b, b, t);
    fe_mul(c, p[3], q[3]);
    fe_mul(c, c, ED_D2_FE);
    fe_mul(d, p[2], q[2]);
    fe_add(d, d, d);
    fe_sub(e, b, a);
    fe_sub(f, d, c);
    fe_add(g, d, c);
    fe_add(h, b, a);
    fe_mul(p[0], e, f);
    fe_mul(p[1], h, g);
    fe_mul(p[2], g, f);
    fe_mul(p[3], e, h);
}

// Constant-time conditional swap of points p and q when b == 1.
static void edf_cswap(fe p[4], fe q[4], int b)
{
    for (int i = 0; i < 4; i++)
    {
        fe_cswap(p[i], q[i], (uint32_t)b);
    }
}

// Encode a point to 32 bytes: y with x's low bit in the top bit.
static void edf_pack(uint8_t r[32], fe p[4])
{
    fe tx;
    fe ty;
    fe zi;
    fe_invert(zi, p[2]);
    fe_mul(tx, p[0], zi);
    fe_mul(ty, p[1], zi);
    fe_tobytes(r, ty);
    r[31] ^= (uint8_t)(fe_parity(tx) << 7);
}

// p = s * q (variable-base scalar mult), s is 32 bytes little-endian.
static void edf_scalarmult(fe p[4], fe q[4], const uint8_t *s)
{
    fe_0(p[0]);
    fe_1(p[1]);
    fe_1(p[2]);
    fe_0(p[3]);
    for (int i = 255; i >= 0; i--)
    {
        int b = (s[i >> 3] >> (i & 7)) & 1;
        edf_cswap(p, q, b);
        edf_add(q, p);
        edf_add(p, p);
        edf_cswap(p, q, b);
    }
}

// Constant-time select of the comb entry for a signed 4-bit digit into extended point t (Z = 1).
// Scans all 8 entries of group idx (the address depends only on the public idx, not the secret digit),
// selects |digit|, and conditionally negates when digit < 0. digit in [-8, 8]; digit == 0 -> identity.
static void edf_comb_select(fe t[4], int idx, int digit)
{
    int32_t sign = digit >> 31;                        // -1 if digit < 0, else 0
    uint32_t babs = (uint32_t)((digit ^ sign) - sign); // |digit| in 0..8
    fe_0(t[0]);
    fe_1(t[1]);
    fe_1(t[2]); // Z = 1
    fe_0(t[3]);
    for (uint32_t j = 0; j < 8; j++)
    {
        uint32_t x = babs ^ (j + 1);
        uint32_t nz = (x | (0u - x)) >> 31;  // 1 if babs != j+1
        uint32_t mask = (uint32_t)(nz - 1u); // 0xffffffff if babs == j+1, else 0
        for (int k = 0; k < 8; k++)
        {
            t[0][k] = (t[0][k] & ~mask) | (ED_COMB[idx][j][0][k] & mask); // X
            t[1][k] = (t[1][k] & ~mask) | (ED_COMB[idx][j][1][k] & mask); // Y
            t[3][k] = (t[3][k] & ~mask) | (ED_COMB[idx][j][2][k] & mask); // T
        }
    }
    // Conditional negate: -(X, Y, Z, T) = (-X, Y, Z, -T).
    uint32_t neg = (uint32_t)sign;
    fe zero;
    fe negx;
    fe negt;
    fe_0(zero);
    fe_sub(negx, zero, t[0]);
    fe_sub(negt, zero, t[3]);
    for (int k = 0; k < 8; k++)
    {
        t[0][k] = (t[0][k] & ~neg) | (negx[k] & neg);
        t[3][k] = (t[3][k] & ~neg) | (negt[k] & neg);
    }
}

// p = s * B via a constant-time signed 4-bit fixed-base comb (ref10 layout), s is 32 bytes LE.
// The doublings are baked into ED_COMB (each group i holds 256^i * B multiples), so this is ~64
// additions + 4 doublings instead of the 255-add / 255-double variable-base ladder - several times
// cheaper. Both the Ed25519 sign scalar-mults (A = a*B, R = r*B) go through here.
static void edf_scalarbase(fe p[4], const uint8_t *s)
{
    signed char e[64]; // 64 signed 4-bit digits of s (ref10 recoding)
    for (int i = 0; i < 32; i++)
    {
        e[2 * i] = (signed char)(s[i] & 15);
        e[2 * i + 1] = (signed char)((s[i] >> 4) & 15);
    }
    int carry = 0;
    for (int i = 0; i < 63; i++)
    {
        e[i] = (signed char)(e[i] + carry);
        carry = (e[i] + 8) >> 4;
        e[i] = (signed char)(e[i] - (carry << 4));
    }
    e[63] = (signed char)(e[63] + carry);

    fe_0(p[0]);
    fe_1(p[1]);
    fe_1(p[2]);
    fe_0(p[3]);
    fe t[4];
    for (int i = 1; i < 64; i += 2) // odd nibbles (weight 16 * 256^(i/2))
    {
        edf_comb_select(t, i >> 1, e[i]);
        edf_add(p, t);
    }
    edf_add(p, p); // * 16 so the odd sum aligns with the even nibbles
    edf_add(p, p);
    edf_add(p, p);
    edf_add(p, p);
    for (int i = 0; i < 64; i += 2) // even nibbles (weight 256^(i/2))
    {
        edf_comb_select(t, i >> 1, e[i]);
        edf_add(p, t);
    }
}

// Decode a point and negate it (r = -A); returns 0 on success, -1 if the encoding is not a valid point.
static int edf_unpackneg(fe r[4], const uint8_t p[32])
{
    fe t;
    fe chk;
    fe num;
    fe den;
    fe den2;
    fe den4;
    fe den6;
    fe_1(r[2]);
    fe_frombytes(r[1], p); // y (top/sign bit masked off)
    fe_sq(num, r[1]);      // y^2
    fe_mul(den, num, ED_D_FE);
    fe_sub(num, num, r[2]); // u = y^2 - 1
    fe_add(den, r[2], den); // v = d*y^2 + 1

    fe_sq(den2, den);
    fe_sq(den4, den2);
    fe_mul(den6, den4, den2);
    fe_mul(t, den6, num);
    fe_mul(t, t, den);
    fe_pow2523(t, t); // t = (u v^7)^((p-5)/8)
    fe_mul(t, t, num);
    fe_mul(t, t, den);
    fe_mul(t, t, den);
    fe_mul(r[0], t, den); // x candidate = u v^3 (u v^7)^((p-5)/8)

    fe_sq(chk, r[0]);
    fe_mul(chk, chk, den);
    if (fe_neq(chk, num))
    {
        fe_mul(r[0], r[0], ED_I_FE); // multiply by sqrt(-1)
    }
    fe_sq(chk, r[0]);
    fe_mul(chk, chk, den);
    if (fe_neq(chk, num))
    {
        return -1; // no square root: invalid point
    }

    if (fe_parity(r[0]) == (p[31] >> 7))
    {
        fe zero;
        fe_0(zero);
        fe_sub(r[0], zero, r[0]); // pick the correct sign, then negate for -A
    }
    fe_mul(r[3], r[0], r[1]);
    return 0;
}

// out = pack(s * B). Brackets the accelerator for the whole scalar-mult.
static void ed_scalarbase_bytes(uint8_t out[32], const uint8_t s[32])
{
    fe p[4];
    pc_fe_hw_enable();
    edf_scalarbase(p, s);
    edf_pack(out, p);
    pc_fe_hw_disable();
}

// out = pack(S*B - h*A); false if the public key A does not decode to a curve point.
static bool ed_verify_recompute(uint8_t out[32], const uint8_t S[32], const uint8_t h[32], const uint8_t pub[32])
{
    fe p[4];
    fe q[4];
    fe sb[4];
    pc_fe_hw_enable();
    if (edf_unpackneg(q, pub) != 0) // q = -A
    {
        pc_fe_hw_disable();
        return false;
    }
    edf_scalarmult(p, q, h); // p = h * (-A)
    edf_scalarbase(sb, S);   // sb = S * B
    edf_add(p, sb);          // p = S*B - h*A
    edf_pack(out, p);
    pc_fe_hw_disable();
    return true;
}
#else
// --- Curve constants (radix-2^16 field elements, little-endian limbs) --------

static const pc_gf GF0 = {0};
static const pc_gf GF1 = {1};
// d = -121665/121666 (the twisted-Edwards curve constant) and 2d.
static const pc_gf ED_D = {0x78a3, 0x1359, 0x4dca, 0x75eb, 0xd8ab, 0x4141, 0x0a4d, 0x0070,
                           0xe898, 0x7779, 0x4079, 0x8cc7, 0xfe73, 0x2b6f, 0x6cee, 0x5203};
static const pc_gf ED_D2 = {0xf159, 0x26b2, 0x9b94, 0xebd6, 0xb156, 0x8283, 0x149a, 0x00e0,
                            0xd130, 0xeef3, 0x80f2, 0x198e, 0xfce7, 0x56df, 0xd9dc, 0x2406};
// Base point B = (X, Y).
static const pc_gf ED_X = {0xd51a, 0x8f25, 0x2d60, 0xc956, 0xa7b2, 0x9525, 0xc760, 0x692c,
                           0xdc5c, 0xfdd6, 0xe231, 0xc0a4, 0x53fe, 0xcd6e, 0x36d3, 0x2169};
static const pc_gf ED_Y = {0x6658, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666,
                           0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666, 0x6666};
// sqrt(-1) mod p.
static const pc_gf ED_I = {0xa0b0, 0x4a0e, 0x1b27, 0xc4ee, 0xe478, 0xad2f, 0x1806, 0x2f43,
                           0xd7a7, 0x3dfb, 0x0099, 0x2b4d, 0xdf0b, 0x4fc1, 0x2480, 0x2b83};

// Parity (low bit) of the canonical encoding of a field element.
static int gf_parity(const pc_gf a)
{
    uint8_t d[32];
    pc_gf_pack(d, a);
    return d[0] & 1;
}

// 0 if a and b encode the same field element, -1 otherwise.
static int gf_neq(const pc_gf a, const pc_gf b)
{
    uint8_t c[32];
    uint8_t d[32];
    pc_gf_pack(c, a);
    pc_gf_pack(d, b);
    return ct_verify32(c, d);
}

// out = a^(2^252 - 3) - used to take the square root during point decompression.
static void gf_pow2523(pc_gf out, const pc_gf a)
{
    pc_gf c;
    pc_gf_copy(c, a);
    for (int i = 250; i >= 0; i--)
    {
        pc_gf_sq(c, c);
        if (i != 1)
        {
            pc_gf_mul(c, c, a);
        }
    }
    pc_gf_copy(out, c);
}

// p += q (twisted-Edwards addition, RFC 8032 §5.1.4 / the unified add formula).
static void ed_add(pc_gf p[4], pc_gf q[4])
{
    pc_gf a;
    pc_gf b;
    pc_gf c;
    pc_gf d;
    pc_gf t;
    pc_gf e;
    pc_gf f;
    pc_gf g;
    pc_gf h;
    pc_gf_sub(a, p[1], p[0]);
    pc_gf_sub(t, q[1], q[0]);
    pc_gf_mul(a, a, t);
    pc_gf_add(b, p[0], p[1]);
    pc_gf_add(t, q[0], q[1]);
    pc_gf_mul(b, b, t);
    pc_gf_mul(c, p[3], q[3]);
    pc_gf_mul(c, c, ED_D2);
    pc_gf_mul(d, p[2], q[2]);
    pc_gf_add(d, d, d);
    pc_gf_sub(e, b, a);
    pc_gf_sub(f, d, c);
    pc_gf_add(g, d, c);
    pc_gf_add(h, b, a);
    pc_gf_mul(p[0], e, f);
    pc_gf_mul(p[1], h, g);
    pc_gf_mul(p[2], g, f);
    pc_gf_mul(p[3], e, h);
}

// Constant-time conditional swap of points p and q when b == 1.
static void ed_cswap(pc_gf p[4], pc_gf q[4], int b)
{
    for (int i = 0; i < 4; i++)
    {
        pc_gf_cswap(p[i], q[i], b);
    }
}

// Encode a point to 32 bytes: y with x's low bit in the top bit.
static void ed_pack(uint8_t r[32], pc_gf p[4])
{
    pc_gf tx;
    pc_gf ty;
    pc_gf zi;
    pc_gf_inv(zi, p[2]);
    pc_gf_mul(tx, p[0], zi);
    pc_gf_mul(ty, p[1], zi);
    pc_gf_pack(r, ty);
    r[31] ^= (uint8_t)(gf_parity(tx) << 7);
}

// p = s * q (variable-base scalar mult), s is 32 bytes little-endian.
static void ed_scalarmult(pc_gf p[4], pc_gf q[4], const uint8_t *s)
{
    pc_gf_copy(p[0], GF0);
    pc_gf_copy(p[1], GF1);
    pc_gf_copy(p[2], GF1);
    pc_gf_copy(p[3], GF0);
    for (int i = 255; i >= 0; i--)
    {
        int b = (s[i >> 3] >> (i & 7)) & 1;
        ed_cswap(p, q, b);
        ed_add(q, p);
        ed_add(p, p);
        ed_cswap(p, q, b);
    }
}

// p = s * B (base-point scalar mult).
static void ed_scalarbase(pc_gf p[4], const uint8_t *s)
{
    pc_gf q[4];
    pc_gf_copy(q[0], ED_X);
    pc_gf_copy(q[1], ED_Y);
    pc_gf_copy(q[2], GF1);
    pc_gf_mul(q[3], ED_X, ED_Y);
    ed_scalarmult(p, q, s);
}

// Decode a point and negate it (r = -A) for the verification equation; returns 0 on
// success, -1 if the encoding is not a valid curve point.
static int ed_unpackneg(pc_gf r[4], const uint8_t p[32])
{
    pc_gf t;
    pc_gf chk;
    pc_gf num;
    pc_gf den;
    pc_gf den2;
    pc_gf den4;
    pc_gf den6;
    pc_gf_copy(r[2], GF1);
    pc_gf_unpack(r[1], p); // y (top/sign bit masked off)
    pc_gf_sq(num, r[1]);   // y^2
    pc_gf_mul(den, num, ED_D);
    pc_gf_sub(num, num, r[2]); // u = y^2 - 1
    pc_gf_add(den, r[2], den); // v = d*y^2 + 1

    pc_gf_sq(den2, den);
    pc_gf_sq(den4, den2);
    pc_gf_mul(den6, den4, den2);
    pc_gf_mul(t, den6, num);
    pc_gf_mul(t, t, den);
    gf_pow2523(t, t); // t = (u v^7)^((p-5)/8)
    pc_gf_mul(t, t, num);
    pc_gf_mul(t, t, den);
    pc_gf_mul(t, t, den);
    pc_gf_mul(r[0], t, den); // x candidate = u v^3 (u v^7)^((p-5)/8)

    pc_gf_sq(chk, r[0]);
    pc_gf_mul(chk, chk, den);
    if (gf_neq(chk, num))
    {
        pc_gf_mul(r[0], r[0], ED_I); // multiply by sqrt(-1)
    }
    pc_gf_sq(chk, r[0]);
    pc_gf_mul(chk, chk, den);
    if (gf_neq(chk, num))
    {
        return -1; // no square root: invalid point
    }

    if (gf_parity(r[0]) == (p[31] >> 7))
    {
        pc_gf_sub(r[0], GF0, r[0]); // pick the correct sign, then negate for -A
    }
    pc_gf_mul(r[3], r[0], r[1]);
    return 0;
}

// out = pack(s * B).
static void ed_scalarbase_bytes(uint8_t out[32], const uint8_t s[32])
{
    pc_gf p[4];
    ed_scalarbase(p, s);
    ed_pack(out, p);
}

// out = pack(S*B - h*A); false if the public key A does not decode to a curve point.
static bool ed_verify_recompute(uint8_t out[32], const uint8_t S[32], const uint8_t h[32], const uint8_t pub[32])
{
    pc_gf p[4];
    pc_gf q[4];
    pc_gf sb[4];
    if (ed_unpackneg(q, pub) != 0) // q = -A
    {
        return false;
    }
    ed_scalarmult(p, q, h); // p = h * (-A)
    ed_scalarbase(sb, S);   // sb = S * B
    ed_add(p, sb);          // p = S*B - h*A
    ed_pack(out, p);
    return true;
}
#endif // PC_FE25519_MPI_HW

// --- Public API -------------------------------------------------------------

void pc_ed25519_pubkey(uint8_t pub[32], const uint8_t seed[32])
{
    uint8_t d[64];
    pc_sha512(seed, 32, d);
    d[0] &= 248;
    d[31] &= 127;
    d[31] |= 64; // clamp -> secret scalar a = d[0..31]
    ed_scalarbase_bytes(pub, d);
}

void pc_ed25519_sign(uint8_t sig[64], const uint8_t *msg, size_t mlen, const uint8_t seed[32])
{
    uint8_t d[64];
    uint8_t pub[32];
    uint8_t r[64];
    uint8_t h[64];
    pc_sha512(seed, 32, d);
    d[0] &= 248;
    d[31] &= 127;
    d[31] |= 64; // a = d[0..31]; prefix = d[32..63]

    // A = a * B
    ed_scalarbase_bytes(pub, d);

    // r = SHA-512(prefix || M) mod L
    pc_sha512_ctx c;
    pc_sha512_init(&c);
    pc_sha512_update(&c, d + 32, 32);
    pc_sha512_update(&c, msg, mlen);
    pc_sha512_final(&c, r);
    ed_reduce(r);

    // R = r * B
    ed_scalarbase_bytes(sig, r); // sig[0..31] = R

    // h = SHA-512(R || A || M) mod L
    pc_sha512_init(&c);
    pc_sha512_update(&c, sig, 32);
    pc_sha512_update(&c, pub, 32);
    pc_sha512_update(&c, msg, mlen);
    pc_sha512_final(&c, h);
    ed_reduce(h);

    // S = (r + h*a) mod L
    int64_t x[64];
    for (int i = 0; i < 64; i++)
    {
        x[i] = 0;
    }
    for (int i = 0; i < 32; i++)
    {
        x[i] = (int64_t)(uint64_t)r[i];
    }
    for (int i = 0; i < 32; i++)
    {
        for (int j = 0; j < 32; j++)
        {
            x[i + j] += (int64_t)(uint64_t)h[i] * (int64_t)(uint64_t)d[j];
        }
    }
    ed_modL(sig + 32, x); // sig[32..63] = S
}

bool pc_ed25519_verify(const uint8_t pub[32], const uint8_t *msg, size_t mlen, const uint8_t sig[64])
{
    if (!ed_scalar_canonical(sig + 32))
    {
        return false; // non-canonical S (RFC 8032 §5.1.7): reject to prevent malleability
    }

    // h = SHA-512(R || A || M) mod L
    uint8_t h[64];
    pc_sha512_ctx c;
    pc_sha512_init(&c);
    pc_sha512_update(&c, sig, 32); // R
    pc_sha512_update(&c, pub, 32); // A
    pc_sha512_update(&c, msg, mlen);
    pc_sha512_final(&c, h);
    ed_reduce(h);

    uint8_t t[32];
    if (!ed_verify_recompute(t, sig + 32, h, pub))
    {
        return false; // invalid A
    }
    return ct_verify32(sig, t) == 0; // R == S*B - h*A ?
}
