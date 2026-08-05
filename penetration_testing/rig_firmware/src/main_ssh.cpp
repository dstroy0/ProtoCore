// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Slim SSH server rig (builds main_ssh.cpp only): an Ed25519 host key set directly from an embedded
// throwaway seed (no NVS provisioning), password auth admin/s3cret, and a channel that echoes bytes.
// Exists to exercise the SSH transport/KEX on the stock espressif32@6.13.0 (arduino 2.x, mailbox lwIP)
// toolchain, alongside the arduino-cli / IDF-5.5 (core-locking) path. WiFi creds come from the env.
// Connect:  ssh -p 22 admin@<ip>   (password s3cret); the server echoes each line back.
#include <Arduino.h>

#include "network_drivers/physical/physical.h"
#include "network_drivers/presentation/ssh/auth/ssh_auth.h"
#include "network_drivers/presentation/ssh/connection/ssh_channel.h"
#include "network_drivers/presentation/ssh/connection/ssh_conn.h"
#include "network_drivers/presentation/ssh/transport/ssh_transport.h"
#include "protocore.h"
#include <WiFi.h>

#ifdef PC_SSH_BENCH
#include "crypto/asymmetric/curve25519.h"
#include "crypto/asymmetric/ed25519.h"
#include "driver/periph_ctrl.h" // periph_module_enable(PERIPH_RSA_MODULE) - proper clk/reset for the accelerator
#include "soc/hwcrypto_reg.h"   // S3 RSA/MPI accelerator register map (MODMULT experiment)
#include "soc/periph_defs.h"
#include "soc/soc.h"
#include "soc/system_reg.h"

// Balance radix-2^16 limbs into signed-16-bit (value-preserving mod p), so the product is a pure
// s16xs16 convolution (the validated C model, test_gf_mul_s16_model_matches_scalar).
static void balance_s16(int16_t o[16], const pc_gf a)
{
    int64_t c[16];
    for (int i = 0; i < 16; i++)
    {
        c[i] = a[i];
    }
    for (int pass = 0; pass < 3; pass++)
    {
        int64_t carry = 0;
        for (int i = 0; i < 16; i++)
        {
            int64_t v = c[i] + carry;
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

// One output limb t[k] = as[0..15] . window[0..15] via ACCX; window (in bp) may be unaligned. Reuses the
// device-validated MAC (40-bit ACCX, sign-extend bit39) + usar/src.q window load.
static inline int64_t accx_dot_win(const int16_t *as, const int16_t *w)
{
    uint32_t lo, hi;
    const int16_t *pa = as, *pw = w;
    asm volatile("ee.zero.accx\n"
                 "ee.vld.128.ip q3, %[a], 16\n"     // as[0..7]
                 "ee.ld.128.usar.ip q0, %[w], 16\n" // block@w  (SAR := w&15)
                 "ee.ld.128.usar.ip q1, %[w], 16\n" // block@w+16
                 "ee.src.q q2, q0, q1\n"            // window[0..7]
                 "ee.vmulas.s16.accx q3, q2\n"
                 "ee.vld.128.ip q3, %[a], 16\n"     // as[8..15]
                 "ee.ld.128.usar.ip q0, %[w], 16\n" // block@w+32
                 "ee.src.q q2, q1, q0\n"            // window[8..15]
                 "ee.vmulas.s16.accx q3, q2\n"
                 "rur.accx_0 %[lo]\n"
                 "rur.accx_1 %[hi]\n"
                 : [lo] "=&r"(lo), [hi] "=&r"(hi), [a] "+r"(pa), [w] "+r"(pw)
                 :
                 : "memory");
    uint64_t raw = (uint64_t)lo | ((uint64_t)(uint8_t)hi << 32);
    return ((int64_t)(raw << 24)) >> 24;
}

// OPTIMIZED: the whole 31-output convolution in ONE asm block - as[] hoisted into q6/q7 once (not
// reloaded per output), the loop stores int64 t[k] directly (no per-output C glue / asm setup).
static void gf_conv_1block(const int16_t *as, const int16_t *w0, int64_t *t)
{
    const int16_t *as_p = as;
    asm volatile("ee.vld.128.ip q6, %[as], 16\n" // as[0..7]  (persist across the loop)
                 "ee.vld.128.ip q7, %[as], 16\n" // as[8..15]
                 "movi.n a8, 31\n"               // output counter
                 "mov a9, %[w]\n"                // a9 = window start &bp[30]; slides -1 int16 per output
                 "1:\n"
                 "mov a10, a9\n"
                 "ee.ld.128.usar.ip q0, a10, 16\n"
                 "ee.ld.128.usar.ip q1, a10, 16\n"
                 "ee.src.q q2, q0, q1\n" // window[0..7]
                 "ee.ld.128.usar.ip q0, a10, 16\n"
                 "ee.src.q q3, q1, q0\n" // window[8..15]
                 "ee.zero.accx\n"
                 "ee.vmulas.s16.accx q6, q2\n"
                 "ee.vmulas.s16.accx q7, q3\n"
                 "rur.accx_0 a11\n"
                 "rur.accx_1 a12\n"
                 "slli a12, a12, 24\n"
                 "srai a12, a12, 24\n"   // sign-extend hi byte (bit39) -> high 32 bits
                 "s32i.n a11, %[t], 0\n" // store int64 t[k] (little-endian)
                 "s32i.n a12, %[t], 4\n"
                 "addi %[t], %[t], 8\n"
                 "addi a9, a9, -2\n"
                 "addi.n a8, a8, -1\n"
                 "bnez a8, 1b\n"
                 : [as] "+r"(as_p), [t] "+r"(t)
                 : [w] "r"(w0)
                 : "a8", "a9", "a10", "a11", "a12", "memory");
}

// IN-REGISTER sliding window: consecutive outputs' windows overlap 15/16, and the aligned 16-byte blocks
// change only every 8 outputs, so load 3 blocks per group of 8 into q0/q1/q2 and slide the SAR (no
// per-output ee.ld.128.usar). 4 groups (7+8+8+8 = 31 outputs). Same inner loop each group.
static void gf_conv_inreg(const int16_t *as, const int16_t *bp, int64_t *t)
{
    const int16_t *asp = as;
    const int16_t *grp = bp + 24; // group0 base = &bp[24]; -16 bytes (8 int16) per group
    int64_t *tp = t;
    asm volatile("ee.vld.128.ip q6, %[as], 16\n" // as[0..7]  (persist)
                 "ee.vld.128.ip q7, %[as], 16\n" // as[8..15]
                 "movi.n a3, 12\n"               // SAR start: group0 window k=0 is at bp[30] = +12 bytes in block
                 "movi.n a4, 7\n"                // group0 has 7 outputs (k=0..6)
                 "movi.n a6, 4\n"                // 4 groups
                 "2:\n"
                 "mov a7, %[grp]\n"
                 "ee.vld.128.ip q0, a7, 16\n" // 3 aligned blocks for this group
                 "ee.vld.128.ip q1, a7, 16\n"
                 "ee.vld.128.ip q2, a7, 16\n"
                 "mov a8, a3\n" // SAR for the first output of the group
                 "1:\n"
                 "wur.sar_byte a8\n"
                 "ee.src.q q3, q0, q1\n" // window[0..7]  (bp[grp + SAR/2 ..])
                 "ee.zero.accx\n"
                 "ee.vmulas.s16.accx q6, q3\n"
                 "ee.src.q q3, q1, q2\n" // window[8..15]
                 "ee.vmulas.s16.accx q7, q3\n"
                 "rur.accx_0 a9\n"
                 "rur.accx_1 a10\n"
                 "slli a10, a10, 24\n"
                 "srai a10, a10, 24\n" // sign-extend 40-bit
                 "s32i.n a9, %[t], 0\n"
                 "s32i.n a10, %[t], 4\n"
                 "addi %[t], %[t], 8\n"
                 "addi a8, a8, -2\n" // slide window 1 int16
                 "addi.n a4, a4, -1\n"
                 "bnez a4, 1b\n"
                 "addi %[grp], %[grp], -16\n" // next group's blocks start 8 int16 earlier
                 "movi.n a3, 14\n"            // groups 1..3 start at SAR 14, 8 outputs each
                 "movi.n a4, 8\n"
                 "addi.n a6, a6, -1\n"
                 "bnez a6, 2b\n"
                 : [as] "+r"(asp), [grp] "+r"(grp), [t] "+r"(tp)
                 :
                 : "a3", "a4", "a6", "a7", "a8", "a9", "a10", "memory");
}

static void pc_gf_mul_s3(pc_gf out, const pc_gf a, const pc_gf b)
{
    __attribute__((aligned(16))) int16_t as[16];
    int16_t bs[16];
    balance_s16(as, a);
    balance_s16(bs, b);
    // bp = [15 zeros][bs reversed: bs15..bs0][zeros]; window for output k starts at bp[30-k].
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
    gf_conv_inreg(as, bp, t);
    for (int i = 0; i < 15; i++)
    {
        t[i] += 38 * t[i + 16];
    }
    for (int i = 0; i < 16; i++)
    {
        out[i] = t[i];
    }
    (void)accx_dot_win;
    (void)gf_conv_1block;
}

// ---- RSA/MPI-accelerator MODMULT feasibility probe (esp32.com t=23830) ----
// Register-direct 256-bit modular multiply on the S3 RSA peripheral (zero-heap, static bufs only). Open
// question: does ONE 8-word HW MODMULT beat the ~8000-cyc SIMD gf_mul? Load r=R^2 mod p into the Z block
// (esp_mpi convention) so MODMULT yields X*Y mod p directly. Constants: scratchpad/montconst.py, p=2^255-19.
static const uint32_t MOD_MPRIME = 0x286bca1bu;
static const uint32_t MOD_P[8] = {0xffffffedu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
                                  0xffffffffu, 0xffffffffu, 0xffffffffu, 0x7fffffffu};
static const uint32_t MOD_R2[8] = {0x000005a4u, 0, 0, 0, 0, 0, 0, 0};
static const uint32_t MOD_X[8] = {0xaabbccddu, 0x77889900u, 0x33445566u, 0x99001122u,
                                  0x55667788u, 0x11223344u, 0x90abcdefu, 0x12345678u};
static const uint32_t MOD_Y[8] = {0x33445579u, 0xee001122u, 0xc2d1e0ffu, 0x8695a4b3u,
                                  0x4a596877u, 0x0e1d2c3bu, 0x7654321fu, 0x7edcba98u};
static const uint32_t MOD_ZPLAIN[8] = {0x0a2e79dbu, 0xb6069683u, 0x291c89f2u, 0xfc854282u,
                                       0xcfdbd054u, 0xb802cf41u, 0x0ddbbaf4u, 0x64a88352u};
static const uint32_t MOD_ZMONT[8] = {0x72cb5405u, 0x8b86e903u, 0x954adb35u, 0xb5cd9cb2u,
                                      0xbb5d5d0fu, 0x1250ea81u, 0xe56ad5c3u, 0x352cdb08u};

#define RSA_REG(a) (*(volatile uint32_t *)(a))

static void mpi_hw_enable(void)
{
    periph_module_enable(PERIPH_RSA_MODULE);       // clk on + reset released (refcounted, DPORT-safe)
    RSA_REG(SYSTEM_RSA_PD_CTRL_REG) &= ~(1u << 0); // clear SYSTEM_RSA_MEM_PD (BIT0, default 1) - power up the RAM
    RSA_REG(RSA_INTERRUPT_REG) = 0;                // poll-only, no CPU IRQ
    while (RSA_REG(RSA_QUERY_CLEAN_REG) == 0)
        ;
}

// Z = X*Y mod m (8 words / 256-bit), r2=R^2 mod m preloaded into the Z block.
static void mpi_modmul256(uint32_t z[8], const uint32_t x[8], const uint32_t y[8], const uint32_t m[8],
                          const uint32_t r2[8], uint32_t mprime)
{
    volatile uint32_t *M = (volatile uint32_t *)RSA_MEM_M_BLOCK_BASE;
    volatile uint32_t *X = (volatile uint32_t *)RSA_MEM_X_BLOCK_BASE;
    volatile uint32_t *Y = (volatile uint32_t *)RSA_MEM_Y_BLOCK_BASE;
    volatile uint32_t *Z = (volatile uint32_t *)RSA_MEM_Z_BLOCK_BASE;
    RSA_REG(RSA_LENGTH_REG) = 8 - 1;
    RSA_REG(RSA_M_DASH_REG) = mprime;
    for (int i = 0; i < 8; i++)
    {
        M[i] = m[i];
        X[i] = x[i];
        Y[i] = y[i];
        Z[i] = r2[i];
    }
    RSA_REG(RSA_CLEAR_INTERRUPT_REG) = 1; // clear any stale done flag BEFORE start (mbedtls left it set)
    RSA_REG(RSA_MOD_MULT_START_REG) = 1;
    while (RSA_REG(RSA_QUERY_INTERRUPT_REG) == 0)
        ;
    RSA_REG(RSA_CLEAR_INTERRUPT_REG) = 1;
    for (int i = 0; i < 8; i++)
    {
        z[i] = Z[i];
    }
}

// THE CRYPTO REWRITE PROTOTYPE: field multiply via the HW MODMULT. Pack both operands to canonical uint32[8]
// (< p) with the library's own pc_gf_pack, one MODMULT (Z = X*Y mod p), unpack the result back to pc_gf.
// Byte-exact with the scalar pc_gf_mul by construction (pack/unpack are the validated canonical conversions;
// MODMULT is exact). This is the drop-in that replaces pc_gf_mul_s3 on the S3. Assumes mpi_hw_enable() done.
static void pc_gf_mul_mpi(pc_gf out, const pc_gf a, const pc_gf b)
{
    uint32_t xw[8], yw[8], zw[8];
    pc_gf_pack((uint8_t *)xw, a); // canonical little-endian 32 bytes == 8 LE words, each < p
    pc_gf_pack((uint8_t *)yw, b);
    mpi_modmul256(zw, xw, yw, MOD_P, MOD_R2, MOD_MPRIME);
    pc_gf_unpack(out, (const uint8_t *)zw);
}

// ============================================================================================
// fe25519: field elements as canonical uint32[8] (< p = 2^255-19), THROUGHOUT the ladder - so mul/sq are the
// raw HW MODMULT (1381 cyc, no per-op pack/unpack) and add/sub are native 32-bit. bytes<->fe conversion is
// once per scalar-mult. This is the real X25519 rewrite; validated byte-exact vs the library pc_x25519_base.
// ============================================================================================
typedef uint32_t fe[8];
static const uint32_t FE_A24[8] = {121665u, 0, 0, 0, 0, 0, 0, 0}; // X25519 a24 = (486662-2)/4 (RFC 7748 section 5)

static void fe_copy(fe o, const fe a)
{
    for (int i = 0; i < 8; i++)
    {
        o[i] = a[i];
    }
}
static void fe_0(fe o)
{
    for (int i = 0; i < 8; i++)
    {
        o[i] = 0;
    }
}
static void fe_1(fe o)
{
    o[0] = 1;
    for (int i = 1; i < 8; i++)
    {
        o[i] = 0;
    }
}
// If o >= p (o < 2p), o -= p. Constant-time (mask select).
static void fe_reduce_once(fe o)
{
    uint32_t t[8];
    int64_t b = 0;
    for (int i = 0; i < 8; i++)
    {
        b += (int64_t)o[i] - (int64_t)MOD_P[i];
        t[i] = (uint32_t)b;
        b >>= 32;
    }
    uint32_t keep = (uint32_t)b; // 0 if o>=p (use t=o-p), 0xffffffff if o<p (keep o)
    for (int i = 0; i < 8; i++)
    {
        o[i] = (o[i] & keep) | (t[i] & ~keep);
    }
}
static void fe_add(fe o, const fe x, const fe y) // x,y < p -> o = x+y mod p
{
    uint64_t c = 0;
    for (int i = 0; i < 8; i++)
    {
        c += (uint64_t)x[i] + y[i];
        o[i] = (uint32_t)c;
        c >>= 32;
    }
    fe_reduce_once(o); // x+y < 2p, one conditional subtract
}
static void fe_sub(fe o, const fe x, const fe y) // x,y < p -> o = x-y mod p
{
    int64_t b = 0;
    uint32_t t[8];
    for (int i = 0; i < 8; i++)
    {
        b += (int64_t)x[i] - (int64_t)y[i];
        t[i] = (uint32_t)b;
        b >>= 32;
    }
    uint32_t borrow = (uint32_t)b; // 0xffffffff if x<y -> add p back
    uint64_t c = 0;
    for (int i = 0; i < 8; i++)
    {
        c += (uint64_t)t[i] + (MOD_P[i] & borrow);
        o[i] = (uint32_t)c;
        c >>= 32;
    }
}
static void fe_mul(fe o, const fe x, const fe y) // o = x*y mod p (HW MODMULT; safe if o aliases x/y)
{
    mpi_modmul256(o, x, y, MOD_P, MOD_R2, MOD_MPRIME);
}
static void fe_sq(fe o, const fe x)
{
    mpi_modmul256(o, x, x, MOD_P, MOD_R2, MOD_MPRIME);
}
static int fe_is_canonical(const fe z) // 1 if z < p, 0 if z in [p, 2p)
{
    int64_t b = 0;
    for (int i = 0; i < 8; i++)
    {
        b += (int64_t)z[i] - (int64_t)MOD_P[i];
        b >>= 32;
    }
    return (int)(b & 1); // b = -1 (borrow) when z<p -> 1; b = 0 when z>=p -> 0
}
static void fe_cswap(fe x, fe y, uint32_t swap)
{
    uint32_t mask = (uint32_t)(-(int32_t)swap);
    for (int i = 0; i < 8; i++)
    {
        uint32_t t = mask & (x[i] ^ y[i]);
        x[i] ^= t;
        y[i] ^= t;
    }
}
static void fe_frombytes(fe o, const uint8_t b[32])
{
    for (int i = 0; i < 8; i++)
    {
        o[i] = (uint32_t)b[4 * i] | ((uint32_t)b[4 * i + 1] << 8) | ((uint32_t)b[4 * i + 2] << 16) |
               ((uint32_t)b[4 * i + 3] << 24);
    }
    o[7] &= 0x7fffffffu; // RFC 7748: mask bit 255 of u
    fe_reduce_once(o);   // canonicalize (< 2^255 could be in [p, 2^255))
}
static void fe_tobytes(uint8_t b[32], const fe a)
{
    fe t;
    fe_copy(t, a);
    fe_reduce_once(t); // freeze
    for (int i = 0; i < 8; i++)
    {
        b[4 * i] = (uint8_t)t[i];
        b[4 * i + 1] = (uint8_t)(t[i] >> 8);
        b[4 * i + 2] = (uint8_t)(t[i] >> 16);
        b[4 * i + 3] = (uint8_t)(t[i] >> 24);
    }
}
// o = a^(p-2) = a^-1 mod p (tweetnacl sq-and-multiply chain for the exponent 2^255-21).
static void fe_invert(fe o, const fe a)
{
    fe c;
    fe_copy(c, a);
    for (int i = 253; i >= 0; i--)
    {
        fe_sq(c, c);
        if (i != 2 && i != 4)
        {
            fe_mul(c, c, a);
        }
    }
    fe_copy(o, c);
}
// X25519 scalar mult via the fe25519 field (RFC 7748 Montgomery ladder).
static void fe_x25519(uint8_t out[32], const uint8_t scalar[32], const uint8_t point[32])
{
    mpi_hw_enable(); // own the accelerator for the whole ladder (once, not per mul)
    uint8_t e[32];
    memcpy(e, scalar, 32);
    e[0] &= 248;
    e[31] &= 127;
    e[31] |= 64;
    fe x1, x2, z2, x3, z3, A, AA, B, BB, E, C, D, DA, CB, t0, t1;
    fe_frombytes(x1, point);
    fe_1(x2);
    fe_0(z2);
    fe_copy(x3, x1);
    fe_1(z3);
    uint32_t swap = 0;
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
    fe_invert(z2, z2);
    fe_mul(x2, x2, z2);
    fe_tobytes(out, x2);
}
static void fe_x25519_base(uint8_t out[32], const uint8_t scalar[32])
{
    uint8_t nine[32] = {9};
    fe_x25519(out, scalar, nine);
}

static void ssh_bench_task(void *)
{
    pc_gf a, b, r1, r2;
    uint8_t p1[32], p2[32];
    uint32_t s = 0x1234567u;
    int fails = 0;
    for (int t = 0; t < 3000; t++)
    {
        for (int i = 0; i < 16; i++)
        {
            s = s * 1664525u + 1013904223u;
            a[i] = (int64_t)(s & 0x3FFFF) - 0x10000;
            s = s * 1664525u + 1013904223u;
            b[i] = (int64_t)(s & 0x3FFFF) - 0x10000;
        }
        pc_gf_mul(r1, a, b);
        pc_gf_mul_s3(r2, a, b);
        pc_gf_pack(p1, r1);
        pc_gf_pack(p2, r2);
        if (memcmp(p1, p2, 32) != 0)
        {
            if (fails < 3)
            {
                Serial.printf("SSHBENCH s3 MISMATCH t=%d\n", t);
            }
            fails++;
        }
    }
    pc_gf a2, b2, o;
    for (int i = 0; i < 16; i++)
    {
        a2[i] = 0x5a5a + i;
        b2[i] = 0x1234 - i;
    }
    pc_gf_mul_s3(o, a2, b2);
    uint32_t c0 = ESP.getCycleCount();
    for (int i = 0; i < 2000; i++)
    {
        pc_gf_mul_s3(o, a2, b2);
    }
    uint32_t cyc = (ESP.getCycleCount() - c0) / 2000;
    Serial.printf("SSHBENCH gf_mul_s3 %s  %d/3000  cyc=%u (scalar=13308)\n", fails == 0 ? "PASS" : "FAIL", 3000 - fails,
                  cyc);

    // End-to-end cost with the shipped vector pc_gf_mul in the ladder (was: X25519 150.8ms, ed25519 547.9ms).
    volatile uint32_t sink = 0;
    uint8_t sk[32], pk[32], peer[32], shared[32];
    memset(sk, 0x11, 32);
    memset(peer, 0x22, 32);
    pc_x25519_base(pk, sk); // warm
    uint32_t x0 = ESP.getCycleCount();
    for (int i = 0; i < 8; i++)
    {
        pc_x25519_base(pk, sk);
    }
    uint32_t xc = (ESP.getCycleCount() - x0) / 8;
    sink += pk[0];
    (void)pc_x25519;
    uint8_t seed[32], sig[64], h[32];
    memset(seed, 0x33, 32);
    memset(h, 0x44, 32);
    pc_ed25519_sign(sig, h, 32, seed); // warm
    uint32_t e0 = ESP.getCycleCount();
    for (int i = 0; i < 4; i++)
    {
        pc_ed25519_sign(sig, h, 32, seed);
    }
    uint32_t ec = (ESP.getCycleCount() - e0) / 4;
    sink += sig[0];
    (void)shared;
    Serial.printf("SSHBENCH x25519 us=%.1f  ed25519_sign us=%.1f  (scalar 150845 / 547885)\n", xc / 240.0, ec / 240.0);

    // --- RSA/MPI MODMULT feasibility probe: cost of ONE 256-bit HW modmul vs the SIMD gf_mul ---
    mpi_hw_enable();
    // First: is the accelerator memory actually writable after our enable? (isolates enable vs op)
    volatile uint32_t *Xb = (volatile uint32_t *)RSA_MEM_X_BLOCK_BASE;
    Xb[0] = 0xdeadbeefu;
    Xb[1] = 0x12345678u;
    Serial.printf("SSHBENCH mpi_memtest X0=%08x X1=%08x clean=%u (want deadbeef 12345678 1)\n", (unsigned)Xb[0],
                  (unsigned)Xb[1], (unsigned)RSA_REG(RSA_QUERY_CLEAN_REG));
    uint32_t mz[8];
    mpi_modmul256(mz, MOD_X, MOD_Y, MOD_P, MOD_R2, MOD_MPRIME); // warm
    const char *which = (memcmp(mz, MOD_ZPLAIN, 32) == 0)  ? "PLAIN(XY mod p)"
                        : (memcmp(mz, MOD_ZMONT, 32) == 0) ? "MONT(XY*Rinv)"
                                                           : "NEITHER";
    uint32_t m0 = ESP.getCycleCount();
    for (int i = 0; i < 2000; i++)
    {
        mpi_modmul256(mz, MOD_X, MOD_Y, MOD_P, MOD_R2, MOD_MPRIME);
    }
    uint32_t mcyc = (ESP.getCycleCount() - m0) / 2000;
    Serial.printf("SSHBENCH mpi_modmul256 %s  cyc=%u  (simd gf_mul=7955, scalar=13308)  Z=%08x %08x %08x\n", which,
                  mcyc, mz[0], mz[1], mz[7]);

    // THE REWRITE: validate pc_gf_mul_mpi (pack -> MODMULT -> unpack) byte-exact vs scalar pc_gf_mul across
    // 3000 random operands, then time it (pack + modmul + unpack all included) - the real per-mul cost that
    // will land in the ladder.
    int mpifails = 0;
    uint32_t ss = 0x99aabbccu;
    pc_gf ma, mb, mr1, mr3;
    uint8_t mp1[32], mp3[32];
    for (int t = 0; t < 3000; t++)
    {
        for (int i = 0; i < 16; i++)
        {
            ss = ss * 1664525u + 1013904223u;
            ma[i] = (int64_t)(ss & 0x3FFFF) - 0x10000;
            ss = ss * 1664525u + 1013904223u;
            mb[i] = (int64_t)(ss & 0x3FFFF) - 0x10000;
        }
        pc_gf_mul(mr1, ma, mb);
        pc_gf_mul_mpi(mr3, ma, mb);
        pc_gf_pack(mp1, mr1);
        pc_gf_pack(mp3, mr3);
        if (memcmp(mp1, mp3, 32) != 0)
        {
            if (mpifails < 3)
            {
                Serial.printf("SSHBENCH mpi_mul MISMATCH t=%d\n", t);
            }
            mpifails++;
        }
    }
    pc_gf_mul_mpi(mr3, ma, mb); // warm
    uint32_t mm0 = ESP.getCycleCount();
    for (int i = 0; i < 2000; i++)
    {
        pc_gf_mul_mpi(mr3, ma, mb);
    }
    uint32_t mmcyc = (ESP.getCycleCount() - mm0) / 2000;
    Serial.printf("SSHBENCH gf_mul_mpi %s  %d/3000  cyc=%u (pack+modmul+unpack; simd=7955 scalar=13308)\n",
                  mpifails == 0 ? "PASS" : "FAIL", 3000 - mpifails, mmcyc);

    // fe25519 self-tests to isolate any ladder bug: add/sub inverse, mul-by-1 identity, x*x^-1==1.
    {
        mpi_hw_enable();
        fe fa, fb, fsum, fdif, fo, finv, fchk, fONE;
        fe_1(fONE);
        uint32_t rs = 0x2468ace0u;
        int e_addsub = 0, e_mulid = 0, e_inv = 0;
        for (int t = 0; t < 500; t++)
        {
            for (int i = 0; i < 8; i++)
            {
                rs = rs * 1664525u + 1013904223u;
                fa[i] = rs;
                rs = rs * 1664525u + 1013904223u;
                fb[i] = rs;
            }
            fa[7] &= 0x7fffffffu;
            fb[7] &= 0x7fffffffu;
            fe_reduce_once(fa);
            fe_reduce_once(fb);
            fe_add(fsum, fa, fb);
            fe_sub(fdif, fsum, fb);
            if (memcmp(fdif, fa, 32) != 0)
            {
                e_addsub++;
            }
            fe_mul(fo, fa, fONE);
            if (memcmp(fo, fa, 32) != 0)
            {
                e_mulid++;
            }
            fe_invert(finv, fa);
            fe_mul(fchk, fa, finv);
            if (memcmp(fchk, fONE, 32) != 0)
            {
                e_inv++;
            }
        }
        Serial.printf("SSHBENCH fe_selftest addsub_fail=%d mulid_fail=%d inv_fail=%d /500\n", e_addsub, e_mulid, e_inv);
    }

    // #2 canonicality test: does the HW MODMULT ever return [p, 2p)? If noncanonical==0, the defensive
    // fe_reduce_once in fe_mul/fe_sq is provably unnecessary (the single biggest remaining speedup).
    {
        mpi_hw_enable();
        int noncanon = 0;
        uint32_t cs = 0x0f1e2d3cu;
        fe cx, cy, cz;
        for (int t = 0; t < 5000; t++)
        {
            for (int i = 0; i < 8; i++)
            {
                cs = cs * 1664525u + 1013904223u;
                cx[i] = cs;
                cs = cs * 1664525u + 1013904223u;
                cy[i] = cs;
            }
            cx[7] &= 0x7fffffffu;
            cy[7] &= 0x7fffffffu;
            fe_reduce_once(cx);
            fe_reduce_once(cy);
            mpi_modmul256(cz, cx, cy, MOD_P, MOD_R2, MOD_MPRIME);
            if (!fe_is_canonical(cz))
            {
                noncanon++;
            }
        }
        Serial.printf("SSHBENCH modmul_canon noncanonical=%d /5000 (0 => MODMULT always <p, reduce_once unneeded)\n",
                      noncanon);
    }

    // Isolate frombytes/tobytes: 2*3 must be 6, and a roundtrip must be stable.
    {
        uint8_t two[32] = {2}, three[32] = {3}, ro[32], rt[32];
        fe fa2, fb3, fc6, frt;
        fe_frombytes(fa2, two);
        fe_frombytes(fb3, three);
        fe_mul(fc6, fa2, fb3);
        fe_tobytes(ro, fc6);
        fe_frombytes(frt, three);
        fe_tobytes(rt, frt);
        Serial.printf("SSHBENCH fe_smoke 2*3=%02x%02x(want0600) roundtrip3=%02x%02x(want0300)\n", ro[0], ro[1], rt[0],
                      rt[1]);
    }

    // RFC 7748 section 5.2 known-answer vector (ground truth for both fe and the library).
    {
        static const uint8_t ksc[32] = {0xa5, 0x46, 0xe3, 0x6b, 0xf0, 0x52, 0x7c, 0x9d, 0x3b, 0x16, 0x15,
                                        0x4b, 0x82, 0x46, 0x5e, 0xdd, 0x62, 0x14, 0x4c, 0x0a, 0xc1, 0xfc,
                                        0x5a, 0x18, 0x50, 0x6a, 0x22, 0x44, 0xba, 0x44, 0x9a, 0xc4};
        static const uint8_t kus[32] = {0xe6, 0xdb, 0x68, 0x67, 0x58, 0x30, 0x30, 0xdb, 0x35, 0x94, 0xc1,
                                        0xa4, 0x24, 0xb1, 0x5f, 0x7c, 0x72, 0x66, 0x24, 0xec, 0x26, 0xb3,
                                        0x35, 0x3b, 0x10, 0xa9, 0x03, 0xa6, 0xd0, 0xab, 0x1c, 0x4c};
        static const uint8_t kexp[32] = {0xc3, 0xda, 0x55, 0x37, 0x9d, 0xe9, 0xc6, 0x90, 0x8e, 0x94, 0xea,
                                         0x4d, 0xf2, 0x8d, 0x08, 0x4f, 0x32, 0xec, 0xcf, 0x03, 0x49, 0x1c,
                                         0x71, 0xf7, 0x54, 0xb4, 0x07, 0x55, 0x77, 0xa2, 0x85, 0x52};
        uint8_t kfe[32], klib[32];
        fe_x25519(kfe, ksc, kus);
        pc_x25519(klib, ksc, kus);
        Serial.printf("SSHBENCH fe_kat fe=%s lib=%s  fe=%02x%02x%02x exp=%02x%02x%02x\n",
                      memcmp(kfe, kexp, 32) == 0 ? "MATCH" : "WRONG", memcmp(klib, kexp, 32) == 0 ? "MATCH" : "WRONG",
                      kfe[0], kfe[1], kfe[31], kexp[0], kexp[1], kexp[31]);
    }

    // THE FULL REWRITE: fe25519 X25519 ladder (uint32[8] throughout) vs the library pc_x25519_base, byte-exact
    // across 200 random scalars, then timed end-to-end (was 97646 us with the SIMD ladder).
    uint8_t fsk[32], fpk_lib[32], fpk_fe[32];
    int fefails = 0;
    uint32_t fs = 0x13572468u;
    for (int t = 0; t < 200; t++)
    {
        for (int i = 0; i < 32; i++)
        {
            fs = fs * 1664525u + 1013904223u;
            fsk[i] = (uint8_t)(fs >> 16);
        }
        pc_x25519_base(fpk_lib, fsk);
        fe_x25519_base(fpk_fe, fsk);
        if (memcmp(fpk_lib, fpk_fe, 32) != 0)
        {
            if (fefails < 3)
            {
                Serial.printf("SSHBENCH fe MISMATCH t=%d\n", t);
            }
            fefails++;
        }
    }
    fe_x25519_base(fpk_fe, fsk); // warm
    uint32_t f0 = ESP.getCycleCount();
    for (int i = 0; i < 8; i++)
    {
        fe_x25519_base(fpk_fe, fsk);
    }
    uint32_t fc = (ESP.getCycleCount() - f0) / 8;
    Serial.printf("SSHBENCH fe_x25519 %s  %d/200  us=%.1f  (SIMD ladder was 97646 us)\n",
                  fefails == 0 ? "PASS" : "FAIL", 200 - fefails, fc / 240.0);
    vTaskDelete(nullptr);
}
#endif

static const char *SSID = WIFI_SSID;
static const char *PASSWORD = WIFI_PASS;

// Throwaway Ed25519 host-key seed (32 bytes). DEMO ONLY - never a real key.
static const uint8_t SSH_HOST_SEED[32] = {
    0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd, 0xee, 0xff, 0x00,
    0x0f, 0x1e, 0x2d, 0x3c, 0x4b, 0x5a, 0x69, 0x78, 0x87, 0x96, 0xa5, 0xb4, 0xc3, 0xd2, 0xe1, 0xf0,
};

PC server;

static bool ssh_password_auth(const char *user, const char *pass)
{
    return strcmp(user, "admin") == 0 && strcmp(pass, "s3cret") == 0;
}

static void ssh_on_data(uint8_t slot, uint32_t channel, const uint8_t *data, size_t len)
{
    pc_ssh_conn_send(slot, channel, data, len); // echo back on the same channel
}

void setup()
{
    Serial.begin(115200);

    init_wifi_physical(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!wifi_ready())
    {
        delay(250);
        Serial.print('.');
    }
    Serial.print("\nIP: ");
    Serial.println(WiFi.localIP());
    WiFi.setSleep(false);

    pc_ssh_hostkey_ed25519_set(SSH_HOST_SEED);
    ssh_kex_set_prefer_rsa(false); // only an ed25519 host key is installed - do not advertise/pick RSA
    pc_ssh_auth_set_password_cb(ssh_password_auth);
    pc_ssh_channel_set_data_cb(ssh_on_data);

    server.listen(22, ConnProto::PROTO_SSH);
    int32_t rc = server.begin();
    Serial.printf("SSH BEGIN=%ld  (port 22, admin/s3cret)\n", (long)rc);
#ifdef PC_SSH_BENCH
    xTaskCreatePinnedToCore(ssh_bench_task, "sshbench", 16384, nullptr, 24, nullptr, 1);
#endif
}

void loop()
{
    server.handle();
#ifdef PC_SSH_KEX_BENCH
    // Print each completed KEX's device-side compute spans (performance_benching/FEATURE_PERFORMANCE wall-clock item).
    // ssh_transport records the numbers; the firmware owns the (USB-CDC) serial, so it prints them here.
    static unsigned seen_kex = 0;
    if (pc_ssh_kex_bench.kex_count != seen_kex)
    {
        seen_kex = pc_ssh_kex_bench.kex_count;
        Serial.printf("KEXBENCH #%u  gen_us=%lld  reply_us=%lld  device_us=%lld\n", seen_kex,
                      pc_ssh_kex_bench.last_kexgen_us, pc_ssh_kex_bench.last_kexreply_us,
                      pc_ssh_kex_bench.last_kexgen_us + pc_ssh_kex_bench.last_kexreply_us);
    }
#endif
}
