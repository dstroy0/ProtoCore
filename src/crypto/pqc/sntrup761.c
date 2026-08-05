// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sntrup761.c
 * @brief Streamlined NTRU Prime sntrup761 - full KEM: KeyGen, Encaps, Decaps (see sntrup761.h).
 *
 * Ported from OpenSSH's embedded sntrup761 reference (public domain; D. J. Bernstein,
 * C. Chuengsatiansup, T. Lange, C. van Vredendaal). We target a known set of platforms (the ESP32
 * variants + native), so the constant-time integer helpers are written directly with our int widths
 * instead of the reference's portable crypto_int layer. The generic sntrup761_encode/sntrup761_decode take a scratch
 * arena rather than the reference's variable-length recursion arrays, so the stack stays bounded.
 * SHA-512 and the RNG come from the SSH crypto seams; byte encodings and the hashing (prefix bytes
 * 1/2/3/4) match OpenSSH exactly, so a ciphertext produced here decapsulates on a real peer and a
 * public key generated here encapsulates on one - verified byte-exact against the reference both ways.
 *
 * Everything NOT called out above tracks upstream line for line on purpose - crypto_sort_int32 is
 * upstream's own vendored djbsort (supercop crypto_sort/int32/portable4), and R3_recip / Rq_recip3
 * are unchanged. The way this file gets re-audited when a new sntrup revision lands is to diff it
 * against upstream, so inherited lines keep upstream's shape even where our style rules differ
 * (multi-declarator locals, the sort's goto, its nesting depth). sonar-project.properties carries
 * the matching rule carve-outs, scoped to this file; the project-written wrappers below follow
 * house style normally.
 */

#include "crypto/pqc/sntrup761.h"

#if PC_ENABLE_SSH_SNTRUP761

#include "crypto/hash/sha512.h"

// The CSPRNG seam (defined in ssh_dh.cpp) - forward-declared so this PQC primitive does not pull in
// the SSH key-exchange machinery (ServerConfig / bignum / keymat) that ssh_dh.h transitively includes.
void ssh_rng_fill(uint8_t *buf, size_t len);

// --- parameters (sntrup761) ---
// The spec names these p, q, w. Spelled out here because a bare `#define P`
// rewrites the token P in every header this TU includes.
#define PC_SNTRUP_P 761                // spec: p
#define PC_SNTRUP_Q 4591               // spec: q
#define PC_SNTRUP_W 286                // spec: w
#define PC_Q12 ((PC_SNTRUP_Q - 1) / 2) // 2295
#define PC_HASH_BYTES 32
#define PC_SMALL_BYTES ((PC_SNTRUP_P + 3) / 4) // 191
#define PC_CONFIRM_BYTES 32
#define PC_CT_BYTES PC_SNTRUP761_CT_BYTES // 1039
#define PC_PK_BYTES PC_SNTRUP761_PK_BYTES // 1158

typedef int8_t small_t;
typedef int16_t Fq;

// Scratch arena for the sntrup761_encode/sntrup761_decode recursion (sum over levels of (len_i+1)/2 = 764;
// sntrup761_decode carves 3 uint16 arrays + 1 uint32 array per level, sntrup761_encode 2 uint16 arrays - both fit
// these).
#define PC_SCR16 2304
#define PC_SCR32 768

static small_t F3_freeze(int16_t x)
{
    return (small_t)(x - 3 * ((10923 * x + 16384) >> 15));
}

static Fq Fq_freeze(int32_t x)
{
    const int32_t q16 = (0x10000 + PC_SNTRUP_Q / 2) / PC_SNTRUP_Q;
    const int32_t q20 = (0x100000 + PC_SNTRUP_Q / 2) / PC_SNTRUP_Q;
    const int32_t q28 = (0x10000000 + PC_SNTRUP_Q / 2) / PC_SNTRUP_Q;
    x -= PC_SNTRUP_Q * ((q16 * x) >> 16);
    x -= PC_SNTRUP_Q * ((q20 * x) >> 20);
    return (Fq)(x - PC_SNTRUP_Q * ((q28 * x + 0x8000000) >> 28));
}

// sign bit of x broadcast to all 32 bits (portable, constant-time).
static inline int32_t negative_mask(int32_t x)
{
    return -(int32_t)((uint32_t)x >> 31);
}

static void uint32_divmod_uint14(uint32_t *Qout, uint16_t *rout, uint32_t x, uint16_t m)
{
    uint32_t qpart, mask, v = 0x80000000u / m;
    qpart = (uint32_t)((x * (uint64_t)v) >> 31);
    x -= qpart * m;
    *Qout = qpart;
    qpart = (uint32_t)((x * (uint64_t)v) >> 31);
    x -= qpart * m;
    *Qout += qpart;
    x -= m;
    *Qout += 1;
    mask = (uint32_t)negative_mask((int32_t)x);
    x += mask & (uint32_t)m;
    *Qout += mask;
    *rout = (uint16_t)x;
}

static uint16_t uint32_mod_uint14(uint32_t x, uint16_t m)
{
    uint32_t Qq;
    uint16_t r;
    uint32_divmod_uint14(&Qq, &r, x, m);
    return r;
}

// Generic sntrup761_encode: appends bytes at *out, halving (R,M) each level via a scratch arena.
static uint8_t *sntrup761_encode(uint8_t *out, const uint16_t *R, const uint16_t *M, int len, uint16_t *scr)
{
    if (len == 1)
    {
        uint16_t r = R[0], m = M[0];
        while (m > 1)
        {
            *out++ = (uint8_t)r;
            r >>= 8;
            m = (uint16_t)((m + 255) >> 8);
        }
        return out;
    }
    int half = (len + 1) / 2;
    uint16_t *R2 = scr, *M2 = scr + half;
    int i;
    for (i = 0; i + 1 < len; i += 2)
    {
        uint32_t m0 = M[i];
        uint32_t r = (uint32_t)R[i] + (uint32_t)R[i + 1] * m0;
        uint32_t m = (uint32_t)M[i + 1] * m0;
        while (m >= 16384)
        {
            *out++ = (uint8_t)r;
            r >>= 8;
            m = (m + 255) >> 8;
        }
        R2[i / 2] = (uint16_t)r;
        M2[i / 2] = (uint16_t)m;
    }
    if (i < len)
    {
        R2[i / 2] = R[i];
        M2[i / 2] = M[i];
    }
    return sntrup761_encode(out, R2, M2, half, scr + 2 * half);
}

// Generic sntrup761_decode: fills out[len] from S using moduli M, via a scratch arena.
static void sntrup761_decode(uint16_t *out, const uint8_t *S, const uint16_t *M, int len, uint16_t *scr,
                             uint32_t *scr32)
{
    if (len == 1)
    {
        // sntrup761_decode's only two callers (Rq_decode: M[i] = Q = 4591; Rounded_decode: M[i] = (Q+2)/3 = 1531,
        // both constant across the array) recurse this generic halving purely on P = 761 and the fixed
        // modulus - the base-case M[0] is therefore deterministic, not data-dependent, and works out to
        // 1608 and 3475 respectively for those two configurations: never <= 256. The M[0]==1 and
        // M[0]<=256 arms exist for other parameter sets this generic routine was written to support
        // upstream; unreachable from any host input given this file's two fixed call sites.
        if (M[0] == 1)
        {
            out[0] = 0;
        }
        else if (M[0] <= 256)
        {
            out[0] = uint32_mod_uint14(S[0], M[0]);
        }
        else
        {
            out[0] = uint32_mod_uint14((uint32_t)S[0] + ((uint32_t)(uint16_t)S[1] << 8), M[0]);
        }
        return;
    }
    int half = (len + 1) / 2;
    uint16_t *R2 = scr, *M2 = scr + half, *bottomr = scr + 2 * half;
    uint32_t *bottomt = scr32;
    int i;
    for (i = 0; i + 1 < len; i += 2)
    {
        uint32_t m = (uint32_t)M[i] * (uint32_t)M[i + 1];
        if (m > 256 * 16383)
        {
            bottomt[i / 2] = 256 * 256;
            bottomr[i / 2] = (uint16_t)(S[0] + 256 * S[1]);
            S += 2;
            M2[i / 2] = (uint16_t)((((m + 255) >> 8) + 255) >> 8);
        }
        else if (m >= 16384)
        // caller moduli (Q or (Q+2)/3) and P = 761 alone, never of decoded data;
        // for both configurations m never drops below 16384 at any recursion
        // level, so the else below is unreachable from any host input.
        {
            bottomt[i / 2] = 256;
            bottomr[i / 2] = S[0];
            S += 1;
            M2[i / 2] = (uint16_t)((m + 255) >> 8);
        }
        else
        {
            bottomt[i / 2] = 1;
            bottomr[i / 2] = 0;
            M2[i / 2] = (uint16_t)m;
        }
    }
    if (i < len)
    {
        M2[i / 2] = M[i];
    }
    sntrup761_decode(R2, S, M2, half, scr + 3 * half, scr32 + half);
    for (i = 0; i + 1 < len; i += 2)
    {
        uint32_t r1, r = bottomr[i / 2];
        uint16_t r0;
        r += bottomt[i / 2] * R2[i / 2];
        uint32_divmod_uint14(&r1, &r0, r, M[i]);
        r1 = uint32_mod_uint14(r1, M[i + 1]);
        *out++ = r0;
        *out++ = (uint16_t)r1;
    }
    if (i < len)
    {
        *out++ = R2[i / 2];
    }
}

// h = f * g in Rq (g small), mod x^p - x - 1.
static void Rq_mult_small(Fq *h, const Fq *f, const small_t *g)
{
    int32_t fg[PC_SNTRUP_P + PC_SNTRUP_P - 1];
    int i, j;
    for (i = 0; i < PC_SNTRUP_P + PC_SNTRUP_P - 1; ++i)
    {
        fg[i] = 0;
    }
    for (i = 0; i < PC_SNTRUP_P; ++i)
    {
        for (j = 0; j < PC_SNTRUP_P; ++j)
        {
            fg[i + j] += f[i] * (int32_t)g[j];
        }
    }
    for (i = PC_SNTRUP_P; i < PC_SNTRUP_P + PC_SNTRUP_P - 1; ++i)
    {
        fg[i - PC_SNTRUP_P] += fg[i];
    }
    for (i = PC_SNTRUP_P; i < PC_SNTRUP_P + PC_SNTRUP_P - 1; ++i)
    {
        fg[i - PC_SNTRUP_P + 1] += fg[i];
    }
    for (i = 0; i < PC_SNTRUP_P; ++i)
    {
        h[i] = Fq_freeze(fg[i]);
    }
}

static void Round(Fq *out, const Fq *a)
{
    for (int i = 0; i < PC_SNTRUP_P; ++i)
    {
        out[i] = (Fq)(a[i] - F3_freeze(a[i]));
    }
}

// --- constant-time uint32 sort (djbsort network), for Short_fromlist ---
static inline void int32_minmax(int32_t *pp, int32_t *pq)
{
    int32_t x = *pp, y = *pq;
    int64_t d = (int64_t)y - (int64_t)x; // 64-bit to avoid overflow
    // (d>>63) is the sign bit (0 or 1); negate it as a signed 0/1 to get a 0 / all-ones mask - the branchless
    // constant-time select. (Negating the *unsigned* value was an equivalent idiom but is a Sonar bug flag.)
    int32_t swap = -(int32_t)((uint64_t)d >> 63) & (x ^ y); // all-ones when y < x
    *pp = x ^ swap;
    *pq = y ^ swap;
}

static void crypto_sort_int32(int32_t *x, long long n)
{
    long long top, p, q, r, i, j;
    // The sole caller (crypto_sort_uint32, itself only called from Short_fromlist) always passes
    // n = P = 761, a fixed compile-time constant, so n < 2 can never hold for any host input.
    if (n < 2)
    {
        return;
    }
    top = 1;
    while (top < n - top)
    {
        top += top;
    }
    for (p = top; p >= 1; p >>= 1)
    {
        i = 0;
        while (i + 2 * p <= n)
        {
            for (j = i; j < i + p; ++j)
            {
                int32_minmax(&x[j], &x[j + p]);
            }
            i += 2 * p;
        }
        for (j = i; j < n - p; ++j)
        {
            int32_minmax(&x[j], &x[j + p]);
        }
        i = 0;
        j = 0;
        for (q = top; q > p; q >>= 1)
        {
            if (j != i)
            {
                for (;;)
                {
                    // The (i,j,p,q) control values driving this network depend only on n, which is
                    // always P = 761 (see crypto_sort_int32's n < 2 note above) - never on the data
                    // being sorted. For n = 761 this inner loop always exits via j == i + p below;
                    // confirmed by exhaustively tracing the (i,j,p,q) sequence for n = 761, where this
                    // arm is never taken. Verbatim upstream djbsort shape kept for other n (see file
                    // header); unreachable from any host input at this file's fixed P.
                    if (j == n - q)
                    {
                        goto done;
                    }
                    int32_t a = x[j + p];
                    for (r = q; r > p; r >>= 1)
                    {
                        int32_minmax(&a, &x[j + r]);
                    }
                    x[j + p] = a;
                    ++j;
                    if (j == i + p)
                    {
                        i += 2 * p;
                        break;
                    }
                }
            }
            while (i + p <= n - q)
            {
                for (j = i; j < i + p; ++j)
                {
                    int32_t a = x[j + p];
                    for (r = q; r > p; r >>= 1)
                    {
                        int32_minmax(&a, &x[j + r]);
                    }
                    x[j + p] = a;
                }
                i += 2 * p;
            }
            j = i;
            while (j < n - q)
            {
                int32_t a = x[j + p];
                for (r = q; r > p; r >>= 1)
                {
                    int32_minmax(&a, &x[j + r]);
                }
                x[j + p] = a;
                ++j;
            }
        done:;
        }
    }
}

static void crypto_sort_uint32(uint32_t *x, long long n)
{
    for (long long j = 0; j < n; ++j)
    {
        x[j] ^= 0x80000000u;
    }
    crypto_sort_int32((int32_t *)x, n);
    for (long long j = 0; j < n; ++j)
    {
        x[j] ^= 0x80000000u;
    }
}

static void Short_fromlist(small_t *out, const uint32_t *in)
{
    uint32_t L[PC_SNTRUP_P];
    int i;
    for (i = 0; i < PC_SNTRUP_W; ++i)
    {
        L[i] = in[i] & (uint32_t)-2;
    }
    for (i = PC_SNTRUP_W; i < PC_SNTRUP_P; ++i)
    {
        L[i] = (in[i] & (uint32_t)-3) | 1;
    }
    crypto_sort_uint32(L, PC_SNTRUP_P);
    for (i = 0; i < PC_SNTRUP_P; ++i)
    {
        out[i] = (small_t)((L[i] & 3) - 1);
    }
}

static void Short_random(small_t *out)
{
    uint32_t L[PC_SNTRUP_P];
    uint8_t rb[4];
    for (int i = 0; i < PC_SNTRUP_P; ++i)
    {
        ssh_rng_fill(rb, 4);
        L[i] = (uint32_t)rb[0] | ((uint32_t)rb[1] << 8) | ((uint32_t)rb[2] << 16) | ((uint32_t)rb[3] << 24);
    }
    Short_fromlist(out, L);
}

// out = SHA512(b || in)[0:32].
static void Hash_prefix(uint8_t *out, int b, const uint8_t *in, size_t inlen)
{
    pc_sha512_ctx ctx;
    uint8_t h[PC_SHA512_DIGEST_LEN];
    uint8_t bb = (uint8_t)b;
    pc_sha512_init(&ctx);
    pc_sha512_update(&ctx, &bb, 1);
    pc_sha512_update(&ctx, in, inlen);
    pc_sha512_final(&ctx, h);
    memcpy(out, h, PC_HASH_BYTES);
}

static void Small_encode(uint8_t *s, const small_t *f)
{
    for (int i = 0; i < PC_SNTRUP_P / 4; ++i)
    {
        small_t x = 0;
        for (int j = 0; j < 4; ++j)
        {
            x = (small_t)(x + ((*f++ + 1) << (2 * j)));
        }
        *s++ = (uint8_t)x;
    }
    *s = (uint8_t)(*f + 1);
}

static void Rq_decode(Fq *r, const uint8_t *s, uint16_t *scr, uint32_t *scr32)
{
    uint16_t Rr[PC_SNTRUP_P], M[PC_SNTRUP_P];
    for (int i = 0; i < PC_SNTRUP_P; ++i)
    {
        M[i] = PC_SNTRUP_Q;
    }
    sntrup761_decode(Rr, s, M, PC_SNTRUP_P, scr, scr32);
    for (int i = 0; i < PC_SNTRUP_P; ++i)
    {
        r[i] = (Fq)(((Fq)Rr[i]) - PC_Q12);
    }
}

static void Rounded_encode(uint8_t *s, const Fq *r, uint16_t *scr)
{
    uint16_t Rr[PC_SNTRUP_P], M[PC_SNTRUP_P];
    for (int i = 0; i < PC_SNTRUP_P; ++i)
    {
        Rr[i] = (uint16_t)(((r[i] + PC_Q12) * 10923) >> 15);
    }
    for (int i = 0; i < PC_SNTRUP_P; ++i)
    {
        M[i] = (PC_SNTRUP_Q + 2) / 3;
    }
    sntrup761_encode(s, Rr, M, PC_SNTRUP_P, scr);
}

static void HashConfirm(uint8_t *h, const uint8_t *r_enc, const uint8_t *cache)
{
    uint8_t x[PC_HASH_BYTES * 2];
    Hash_prefix(x, 3, r_enc, PC_SMALL_BYTES);
    memcpy(x + PC_HASH_BYTES, cache, PC_HASH_BYTES);
    Hash_prefix(h, 2, x, sizeof x);
}

static void HashSession(uint8_t *k, int b, const uint8_t *r_enc, const uint8_t *c)
{
    uint8_t x[PC_HASH_BYTES + PC_CT_BYTES];
    Hash_prefix(x, 3, r_enc, PC_SMALL_BYTES);
    memcpy(x + PC_HASH_BYTES, c, PC_CT_BYTES);
    Hash_prefix(k, b, x, sizeof x);
}

// Encapsulation reused for the Decapsulation FO re-encrypt check.
static void Hide(uint8_t *c, uint8_t *r_enc, const small_t *r, const uint8_t *pk, const uint8_t *cache, uint16_t *scr,
                 uint32_t *scr32)
{
    Small_encode(r_enc, r);
    Fq h[PC_SNTRUP_P], cp[PC_SNTRUP_P];
    Rq_decode(h, pk, scr, scr32);
    Rq_mult_small(cp, h, r); // c = Round(h * r)
    Round(cp, cp);
    Rounded_encode(c, cp, scr);
    HashConfirm(c + PC_CT_BYTES - PC_CONFIRM_BYTES, r_enc, cache);
}

// ===========================================================================
// KeyGen + Decapsulation (the reverse-SSH client / initiator side)
// ===========================================================================

// -1 (all ones) when the argument is nonzero / negative; 0 otherwise (our int widths are known).
static inline int nonzero_mask16(int16_t x)
{
    uint32_t u = (uint16_t)x;
    return -(int)((u | (0u - u)) >> 31);
}
static inline int negative_mask16(int16_t x)
{
    return -((uint16_t)x >> 15);
}

static void R3_fromRq(small_t *out, const Fq *r)
{
    for (int i = 0; i < PC_SNTRUP_P; ++i)
    {
        out[i] = F3_freeze(r[i]);
    }
}

static void R3_mult(small_t *h, const small_t *f, const small_t *g)
{
    int16_t fg[PC_SNTRUP_P + PC_SNTRUP_P - 1];
    int i, j;
    for (i = 0; i < PC_SNTRUP_P + PC_SNTRUP_P - 1; ++i)
    {
        fg[i] = 0;
    }
    for (i = 0; i < PC_SNTRUP_P; ++i)
    {
        for (j = 0; j < PC_SNTRUP_P; ++j)
        {
            fg[i + j] = (int16_t)(fg[i + j] + f[i] * (int16_t)g[j]);
        }
    }
    for (i = PC_SNTRUP_P; i < PC_SNTRUP_P + PC_SNTRUP_P - 1; ++i)
    {
        fg[i - PC_SNTRUP_P] = (int16_t)(fg[i - PC_SNTRUP_P] + fg[i]);
    }
    for (i = PC_SNTRUP_P; i < PC_SNTRUP_P + PC_SNTRUP_P - 1; ++i)
    {
        fg[i - PC_SNTRUP_P + 1] = (int16_t)(fg[i - PC_SNTRUP_P + 1] + fg[i]);
    }
    for (i = 0; i < PC_SNTRUP_P; ++i)
    {
        h[i] = F3_freeze(fg[i]);
    }
}

// 1/in in R3 (mod 3); returns 0 on success (in invertible), -1 otherwise. Constant-time GCD.
static int R3_recip(small_t *out, const small_t *in)
{
    small_t f[PC_SNTRUP_P + 1], g[PC_SNTRUP_P + 1], v[PC_SNTRUP_P + 1], r[PC_SNTRUP_P + 1];
    int sign, swap, t, i, loop, delta = 1;
    for (i = 0; i < PC_SNTRUP_P + 1; ++i)
    {
        v[i] = 0;
    }
    for (i = 0; i < PC_SNTRUP_P + 1; ++i)
    {
        r[i] = 0;
    }
    r[0] = 1;
    for (i = 0; i < PC_SNTRUP_P; ++i)
    {
        f[i] = 0;
    }
    f[0] = 1;
    f[PC_SNTRUP_P - 1] = f[PC_SNTRUP_P] = -1;
    for (i = 0; i < PC_SNTRUP_P; ++i)
    {
        g[PC_SNTRUP_P - 1 - i] = in[i];
    }
    g[PC_SNTRUP_P] = 0;
    for (loop = 0; loop < 2 * PC_SNTRUP_P - 1; ++loop)
    {
        for (i = PC_SNTRUP_P; i > 0; --i)
        {
            v[i] = v[i - 1];
        }
        v[0] = 0;
        sign = -g[0] * f[0];
        swap = negative_mask16((int16_t)-delta) & nonzero_mask16(g[0]);
        delta ^= swap & (delta ^ -delta);
        delta += 1;
        for (i = 0; i < PC_SNTRUP_P + 1; ++i)
        {
            t = swap & (f[i] ^ g[i]);
            f[i] = (small_t)(f[i] ^ t);
            g[i] = (small_t)(g[i] ^ t);
            t = swap & (v[i] ^ r[i]);
            v[i] = (small_t)(v[i] ^ t);
            r[i] = (small_t)(r[i] ^ t);
        }
        for (i = 0; i < PC_SNTRUP_P + 1; ++i)
        {
            g[i] = F3_freeze((int16_t)(g[i] + sign * f[i]));
        }
        for (i = 0; i < PC_SNTRUP_P + 1; ++i)
        {
            r[i] = F3_freeze((int16_t)(r[i] + sign * v[i]));
        }
        for (i = 0; i < PC_SNTRUP_P; ++i)
        {
            g[i] = g[i + 1];
        }
        g[PC_SNTRUP_P] = 0;
    }
    sign = f[0];
    for (i = 0; i < PC_SNTRUP_P; ++i)
    {
        out[i] = (small_t)(sign * v[PC_SNTRUP_P - 1 - i]);
    }
    return nonzero_mask16((int16_t)delta);
}

static void Rq_mult3(Fq *h, const Fq *f)
{
    for (int i = 0; i < PC_SNTRUP_P; ++i)
    {
        h[i] = Fq_freeze(3 * f[i]);
    }
}

static Fq Fq_recip(Fq a1)
{
    int i = 1;
    Fq ai = a1;
    while (i < PC_SNTRUP_Q - 2)
    {
        ai = Fq_freeze(a1 * (int32_t)ai);
        i += 1;
    }
    return ai;
}

// out = 1/(3*in) in Rq (used by KeyGen). Constant-time GCD over Fq.
static int Rq_recip3(Fq *out, const small_t *in)
{
    Fq f[PC_SNTRUP_P + 1], g[PC_SNTRUP_P + 1], v[PC_SNTRUP_P + 1], r[PC_SNTRUP_P + 1], scale;
    int swap, i, loop, delta = 1;
    int32_t f0, g0;
    for (i = 0; i < PC_SNTRUP_P + 1; ++i)
    {
        v[i] = 0;
    }
    for (i = 0; i < PC_SNTRUP_P + 1; ++i)
    {
        r[i] = 0;
    }
    r[0] = Fq_recip(3);
    for (i = 0; i < PC_SNTRUP_P; ++i)
    {
        f[i] = 0;
    }
    f[0] = 1;
    f[PC_SNTRUP_P - 1] = f[PC_SNTRUP_P] = -1;
    for (i = 0; i < PC_SNTRUP_P; ++i)
    {
        g[PC_SNTRUP_P - 1 - i] = in[i];
    }
    g[PC_SNTRUP_P] = 0;
    for (loop = 0; loop < 2 * PC_SNTRUP_P - 1; ++loop)
    {
        for (i = PC_SNTRUP_P; i > 0; --i)
        {
            v[i] = v[i - 1];
        }
        v[0] = 0;
        swap = negative_mask16((int16_t)-delta) & nonzero_mask16(g[0]);
        delta ^= swap & (delta ^ -delta);
        delta += 1;
        Fq tmp;
        for (i = 0; i < PC_SNTRUP_P + 1; ++i)
        {
            tmp = (Fq)(swap & (f[i] ^ g[i]));
            f[i] ^= tmp;
            g[i] ^= tmp;
            tmp = (Fq)(swap & (v[i] ^ r[i]));
            v[i] ^= tmp;
            r[i] ^= tmp;
        }
        f0 = f[0];
        g0 = g[0];
        for (i = 0; i < PC_SNTRUP_P + 1; ++i)
        {
            g[i] = Fq_freeze(f0 * g[i] - g0 * f[i]);
        }
        for (i = 0; i < PC_SNTRUP_P + 1; ++i)
        {
            r[i] = Fq_freeze(f0 * r[i] - g0 * v[i]);
        }
        for (i = 0; i < PC_SNTRUP_P; ++i)
        {
            g[i] = g[i + 1];
        }
        g[PC_SNTRUP_P] = 0;
    }
    scale = Fq_recip(f[0]);
    for (i = 0; i < PC_SNTRUP_P; ++i)
    {
        out[i] = Fq_freeze(scale * (int32_t)v[PC_SNTRUP_P - 1 - i]);
    }
    return nonzero_mask16((int16_t)delta);
}

static int Weightw_mask(const small_t *r)
{
    int weight = 0;
    for (int i = 0; i < PC_SNTRUP_P; ++i)
    {
        weight += (r[i] & 1);
    }
    return nonzero_mask16((int16_t)(weight - PC_SNTRUP_W));
}

static void Small_random(small_t *out)
{
    uint8_t rb[4];
    for (int i = 0; i < PC_SNTRUP_P; ++i)
    {
        ssh_rng_fill(rb, 4);
        uint32_t u = (uint32_t)rb[0] | ((uint32_t)rb[1] << 8) | ((uint32_t)rb[2] << 16) | ((uint32_t)rb[3] << 24);
        out[i] = (small_t)((((u & 0x3fffffff) * 3) >> 30) - 1);
    }
}

static void KeyGen(Fq *h, small_t *f, small_t *ginv)
{
    small_t g[PC_SNTRUP_P];
    Fq finv[PC_SNTRUP_P];
    for (;;)
    {
        Small_random(g);
        if (R3_recip(ginv, g) == 0)
        {
            break;
        }
    }
    Short_random(f);
    Rq_recip3(finv, f);
    Rq_mult_small(h, finv, g);
}

static void Small_decode(small_t *f, const uint8_t *s)
{
    for (int i = 0; i < PC_SNTRUP_P / 4; ++i)
    {
        uint8_t x = *s++;
        for (int j = 0; j < 4; ++j)
        {
            *f++ = (small_t)(((x >> (2 * j)) & 3) - 1);
        }
    }
    *f = (small_t)((*s & 3) - 1);
}

static void Rounded_decode(Fq *r, const uint8_t *s, uint16_t *scr, uint32_t *scr32)
{
    uint16_t Rr[PC_SNTRUP_P], M[PC_SNTRUP_P];
    for (int i = 0; i < PC_SNTRUP_P; ++i)
    {
        M[i] = (PC_SNTRUP_Q + 2) / 3;
    }
    sntrup761_decode(Rr, s, M, PC_SNTRUP_P, scr, scr32);
    for (int i = 0; i < PC_SNTRUP_P; ++i)
    {
        r[i] = (Fq)(Rr[i] * 3 - PC_Q12);
    }
}

static void Rq_encode(uint8_t *s, const Fq *r, uint16_t *scr)
{
    uint16_t Rr[PC_SNTRUP_P], M[PC_SNTRUP_P];
    for (int i = 0; i < PC_SNTRUP_P; ++i)
    {
        Rr[i] = (uint16_t)(r[i] + PC_Q12);
    }
    for (int i = 0; i < PC_SNTRUP_P; ++i)
    {
        M[i] = PC_SNTRUP_Q;
    }
    sntrup761_encode(s, Rr, M, PC_SNTRUP_P, scr);
}

static void Decrypt(small_t *r, const Fq *c, const small_t *f, const small_t *ginv)
{
    Fq cf[PC_SNTRUP_P], cf3[PC_SNTRUP_P];
    small_t e[PC_SNTRUP_P], ev[PC_SNTRUP_P];
    int mask, i;
    Rq_mult_small(cf, c, f);
    Rq_mult3(cf3, cf);
    R3_fromRq(e, cf3);
    R3_mult(ev, e, ginv);
    mask = Weightw_mask(ev);
    for (i = 0; i < PC_SNTRUP_W; ++i)
    {
        r[i] = (small_t)(((ev[i] ^ 1) & ~mask) ^ 1);
    }
    for (i = PC_SNTRUP_W; i < PC_SNTRUP_P; ++i)
    {
        r[i] = (small_t)(ev[i] & ~mask);
    }
}

// 0 when the two ciphertexts are equal, -1 otherwise.
static int Ciphertexts_diff_mask(const uint8_t *c, const uint8_t *c2)
{
    uint16_t differentbits = 0;
    for (int i = 0; i < PC_CT_BYTES; ++i)
    {
        differentbits |= (uint16_t)(c[i] ^ c2[i]);
    }
    // keep the uint16_t cast: it makes the differentbits==0 case wrap to 0xffff explicitly rather
    // than leaning on an implementation-defined arithmetic shift of the promoted -1.
    return ((((uint16_t)(differentbits - 1)) >> 8) & 1) - 1;
}

void pc_sntrup761_enc(const uint8_t pk[PC_SNTRUP761_PK_BYTES], uint8_t ct[PC_SNTRUP761_CT_BYTES],
                      uint8_t ss[PC_SNTRUP761_SS_BYTES])
{
    uint16_t scr16[PC_SCR16];
    uint32_t scr32[PC_SCR32];
    small_t r[PC_SNTRUP_P];
    uint8_t r_enc[PC_SMALL_BYTES];
    uint8_t cache[PC_HASH_BYTES];

    Hash_prefix(cache, 4, pk, PC_PK_BYTES);
    Short_random(r);
    Hide(ct, r_enc, r, pk, cache, scr16, scr32);
    HashSession(ss, 1, r_enc, ct);
}

void pc_sntrup761_keypair(uint8_t pk[PC_SNTRUP761_PK_BYTES], uint8_t sk[PC_SNTRUP761_SK_BYTES])
{
    uint16_t scr16[PC_SCR16];
    Fq h[PC_SNTRUP_P];
    small_t f[PC_SNTRUP_P];
    small_t ginv[PC_SNTRUP_P];

    KeyGen(h, f, ginv);
    Rq_encode(pk, h, scr16);
    Small_encode(sk, f);
    Small_encode(sk + PC_SMALL_BYTES, ginv);
    // ...then the pk copy, a random rho for implicit reject, and the cached H(4||pk).
    uint8_t *tail = sk + 2 * PC_SMALL_BYTES; // SecretKeys_bytes = 2 * Small_bytes
    memcpy(tail, pk, PC_PK_BYTES);
    ssh_rng_fill(tail + PC_PK_BYTES, PC_SMALL_BYTES);
    Hash_prefix(tail + PC_PK_BYTES + PC_SMALL_BYTES, 4, pk, PC_PK_BYTES);
}

void pc_sntrup761_dec(const uint8_t sk[PC_SNTRUP761_SK_BYTES], const uint8_t ct[PC_SNTRUP761_CT_BYTES],
                      uint8_t ss[PC_SNTRUP761_SS_BYTES])
{
    uint16_t scr16[PC_SCR16];
    uint32_t scr32[PC_SCR32];
    const uint8_t *pk = sk + 2 * PC_SMALL_BYTES;
    const uint8_t *rho = pk + PC_PK_BYTES;
    const uint8_t *cache = rho + PC_SMALL_BYTES;
    small_t f[PC_SNTRUP_P];
    small_t ginv[PC_SNTRUP_P];
    small_t r[PC_SNTRUP_P];
    Fq cp[PC_SNTRUP_P];
    uint8_t r_enc[PC_SMALL_BYTES];
    uint8_t cnew[PC_CT_BYTES];

    Small_decode(f, sk);
    Small_decode(ginv, sk + PC_SMALL_BYTES);
    Rounded_decode(cp, ct, scr16, scr32);
    Decrypt(r, cp, f, ginv);
    Hide(cnew, r_enc, r, pk, cache, scr16, scr32); // re-encrypt: FO check
    int mask = Ciphertexts_diff_mask(ct, cnew);
    for (int i = 0; i < PC_SMALL_BYTES; ++i)
    {
        r_enc[i] = (uint8_t)(r_enc[i] ^ (mask & (r_enc[i] ^ rho[i]))); // implicit reject -> rho
    }
    HashSession(ss, 1 + mask, r_enc, ct);
}

#endif // PC_ENABLE_SSH_SNTRUP761
