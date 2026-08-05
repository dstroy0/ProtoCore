// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "crypto/pqc/mlkem.h"

#if PC_ENABLE_PQC_KEX

#include "crypto/hash/sha3.h"

// ML-KEM-768 parameters (FIPS 203).
#define MK_N 256
#define MK_Q 3329
#define MK_K 3
#define MK_ETA 2 // eta1 == eta2 == 2 for ML-KEM-768
#define MK_DU 10
#define MK_DV 4
#define MK_POLYBYTES 384 // 12 bits * 256 / 8
#define MK_QINV (-3327)  // q^-1 mod 2^16, signed

// Twiddle factors zeta^BitRev7(i) in Montgomery form, reduced to (-q/2, q/2] (FIPS 203 / pq-crystals).
static const int16_t mk_zetas[128] = {
    -1044, -758,  -359,  -1517, 1493,  1422,  287,   202,   -171,  622,  1577,  182,   962,   -1202, -1474, 1468,
    573,   -1325, 264,   383,   -829,  1458,  -1602, -130,  -681,  1017, 732,   608,   -1542, 411,   -205,  -1571,
    1223,  652,   -552,  1015,  -1293, 1491,  -282,  -1544, 516,   -8,   -320,  -666,  -1618, -1162, 126,   1469,
    -853,  -90,   -271,  830,   107,   -1421, -247,  -951,  -398,  961,  -1508, -725,  448,   -1065, 677,   -1275,
    -1103, 430,   555,   843,   -1251, 871,   1550,  105,   422,   587,  177,   -235,  -291,  -460,  1574,  1653,
    -246,  778,   1159,  -147,  -777,  1483,  -602,  1119,  -1590, 644,  -872,  349,   418,   329,   -156,  -75,
    817,   1097,  603,   610,   1322,  -1285, -1465, 384,   -1215, -136, 1218,  -1335, -874,  220,   -1187, -1659,
    -1185, -1530, -1278, 794,   -1510, -854,  -870,  478,   -108,  -308, 996,   991,   958,   -1460, 1522,  1628};

// Montgomery reduction: given a = m*R, return m mod q in (-q, q). R = 2^16.
static int16_t mont_reduce(int32_t a)
{
    int16_t t = (int16_t)((int16_t)a * (int16_t)MK_QINV);
    t = (int16_t)((a - (int32_t)t * MK_Q) >> 16);
    return t;
}

static inline int16_t fqmul(int16_t a, int16_t b)
{
    return mont_reduce((int32_t)a * b);
}

// Barrett reduction: a mod q in (-q/2, q/2].
static int16_t barrett_reduce(int16_t a)
{
    const int16_t v = 20159; // round(2^26 / q)
    int16_t t = (int16_t)(((int32_t)v * a + (1 << 25)) >> 26);
    t = (int16_t)(t * MK_Q);
    return (int16_t)(a - t);
}

// Forward NTT (in place). Coefficients enter in normal order, leave in bit-reversed NTT order.
static void ntt(int16_t r[MK_N])
{
    unsigned k = 1;
    for (unsigned len = 128; len >= 2; len >>= 1)
    {
        for (unsigned start = 0; start < MK_N; start += (len << 1))
        {
            int16_t zeta = mk_zetas[k++];
            for (unsigned j = start; j < start + len; j++)
            {
                int16_t t = fqmul(zeta, r[j + len]);
                r[j + len] = (int16_t)(r[j] - t);
                r[j] = (int16_t)(r[j] + t);
            }
        }
    }
}

// Inverse NTT (in place), with the 1/128 * Montgomery scaling folded into the final multiply.
static void invntt(int16_t r[MK_N])
{
    const int16_t f = 1441; // mont^2 / 128
    unsigned k = 127;
    for (unsigned len = 2; len <= 128; len <<= 1)
    {
        for (unsigned start = 0; start < MK_N; start += (len << 1))
        {
            int16_t zeta = mk_zetas[k--];
            for (unsigned j = start; j < start + len; j++)
            {
                int16_t t = r[j];
                r[j] = barrett_reduce((int16_t)(t + r[j + len]));
                r[j + len] = (int16_t)(r[j + len] - t);
                r[j + len] = fqmul(zeta, r[j + len]);
            }
        }
    }
    for (unsigned j = 0; j < MK_N; j++)
    {
        r[j] = fqmul(r[j], f);
    }
}

// Multiply two degree-1 residues mod (X^2 - zeta) in the NTT domain.
static void basemul(int16_t r[2], const int16_t a[2], const int16_t b[2], int16_t zeta)
{
    r[0] = fqmul(a[1], b[1]);
    r[0] = fqmul(r[0], zeta);
    r[0] = (int16_t)(r[0] + fqmul(a[0], b[0]));
    r[1] = fqmul(a[0], b[1]);
    r[1] = (int16_t)(r[1] + fqmul(a[1], b[0]));
}

static void poly_basemul(int16_t r[MK_N], const int16_t a[MK_N], const int16_t b[MK_N])
{
    for (unsigned i = 0; i < MK_N / 4; i++)
    {
        basemul(&r[4 * i], &a[4 * i], &b[4 * i], mk_zetas[64 + i]);
        basemul(&r[4 * i + 2], &a[4 * i + 2], &b[4 * i + 2], (int16_t)(-mk_zetas[64 + i]));
    }
}

// r = sum_i a[i] o b[i]  (pointwise in the NTT domain), then Barrett-reduced.
static void polyvec_basemul_acc(int16_t r[MK_N], const int16_t a[MK_K][MK_N], const int16_t b[MK_K][MK_N])
{
    int16_t t[MK_N];
    poly_basemul(r, a[0], b[0]);
    for (unsigned i = 1; i < MK_K; i++)
    {
        poly_basemul(t, a[i], b[i]);
        for (unsigned j = 0; j < MK_N; j++)
        {
            r[j] = (int16_t)(r[j] + t[j]);
        }
    }
    for (unsigned j = 0; j < MK_N; j++)
    {
        r[j] = barrett_reduce(r[j]);
    }
}

// ByteDecode_12: 384 octets -> 256 coefficients in [0, 2^12).
static void poly_frombytes(int16_t r[MK_N], const uint8_t a[MK_POLYBYTES])
{
    for (unsigned i = 0; i < MK_N / 2; i++)
    {
        r[2 * i] = (int16_t)(((a[3 * i + 0] >> 0) | ((uint16_t)a[3 * i + 1] << 8)) & 0xFFF);
        r[2 * i + 1] = (int16_t)(((a[3 * i + 1] >> 4) | ((uint16_t)a[3 * i + 2] << 4)) & 0xFFF);
    }
}

// Decompress_1: each message bit b -> b ? (q+1)/2 : 0.
static void poly_frommsg(int16_t r[MK_N], const uint8_t msg[32])
{
    for (unsigned i = 0; i < 32; i++)
    {
        for (unsigned j = 0; j < 8; j++)
        {
            int16_t mask = (int16_t)(-(int16_t)((msg[i] >> j) & 1));
            r[8 * i + j] = (int16_t)(mask & ((MK_Q + 1) / 2));
        }
    }
}

static inline uint32_t load32_le(const uint8_t *x)
{
    return (uint32_t)x[0] | ((uint32_t)x[1] << 8) | ((uint32_t)x[2] << 16) | ((uint32_t)x[3] << 24);
}

// Centered binomial distribution, eta = 2: 128 octets of PRF output -> 256 coefficients in [-2, 2].
static void cbd2(int16_t r[MK_N], const uint8_t buf[128])
{
    for (unsigned i = 0; i < MK_N / 8; i++)
    {
        uint32_t t = load32_le(buf + 4 * i);
        uint32_t d = t & 0x55555555u;
        d += (t >> 1) & 0x55555555u;
        for (unsigned j = 0; j < 8; j++)
        {
            int16_t a = (int16_t)((d >> (4 * j)) & 0x3);
            int16_t b = (int16_t)((d >> (4 * j + 2)) & 0x3);
            r[8 * i + j] = (int16_t)(a - b);
        }
    }
}

// PRF_eta(seed, nonce) = SHAKE256(seed || nonce), then sample CBD_eta (eta = 2).
static void poly_getnoise(int16_t r[MK_N], const uint8_t seed[32], uint8_t nonce)
{
    uint8_t extseed[33];
    memcpy(extseed, seed, 32);
    extseed[32] = nonce;
    uint8_t buf[MK_ETA * MK_N / 4]; // 128
    shake256(buf, sizeof(buf), extseed, sizeof(extseed));
    cbd2(r, buf);
}

// One transposed matrix entry: A^T[i][j] = SampleNTT(XOF(rho || i || j)) (FIPS 203 gen with (i,j)).
static void gen_matrix_entry(int16_t out[MK_N], const uint8_t rho[32], uint8_t i, uint8_t j)
{
    uint8_t seed[34];
    memcpy(seed, rho, 32);
    seed[32] = i;
    seed[33] = j;
    KeccakCtx ctx;
    pc_shake128_absorb(&ctx, seed, sizeof(seed));

    unsigned count = 0;
    while (count < MK_N)
    {
        uint8_t buf[KECCAK_RATE_SHAKE128]; // 168 = 56*3, no 3-octet group straddles a block
        pc_keccak_squeeze(&ctx, buf, sizeof(buf));
        for (unsigned p = 0; p + 3 <= sizeof(buf) && count < MK_N; p += 3)
        {
            uint16_t d1 = (uint16_t)(buf[p] | ((uint16_t)(buf[p + 1] & 0xF) << 8));
            uint16_t d2 = (uint16_t)((buf[p + 1] >> 4) | ((uint16_t)buf[p + 2] << 4));
            if (d1 < MK_Q)
            {
                out[count++] = (int16_t)d1;
            }
            if (count < MK_N && d2 < MK_Q)
            {
                out[count++] = (int16_t)d2;
            }
        }
    }
}

// Compress_10 + ByteEncode_10 for one polynomial: 256 coefficients -> 320 octets.
static void poly_compress10(uint8_t r[320], const int16_t a[MK_N])
{
    unsigned k = 0;
    for (unsigned i = 0; i < MK_N / 4; i++)
    {
        uint16_t t[4];
        for (unsigned j = 0; j < 4; j++)
        {
            int16_t u = a[4 * i + j];
            u = (int16_t)(u + ((u >> 15) & MK_Q)); // to [0, q)
            t[j] = (uint16_t)(((((uint32_t)u << 10) + MK_Q / 2) / MK_Q) & 0x3FF);
        }
        r[k + 0] = (uint8_t)(t[0]);
        r[k + 1] = (uint8_t)((t[0] >> 8) | (t[1] << 2));
        r[k + 2] = (uint8_t)((t[1] >> 6) | (t[2] << 4));
        r[k + 3] = (uint8_t)((t[2] >> 4) | (t[3] << 6));
        r[k + 4] = (uint8_t)(t[3] >> 2);
        k += 5;
    }
}

// Compress_4 + ByteEncode_4 for one polynomial: 256 coefficients -> 128 octets.
static void poly_compress4(uint8_t r[128], const int16_t a[MK_N])
{
    for (unsigned i = 0; i < MK_N / 8; i++)
    {
        uint8_t t[8];
        for (unsigned j = 0; j < 8; j++)
        {
            int16_t u = a[8 * i + j];
            u = (int16_t)(u + ((u >> 15) & MK_Q));
            t[j] = (uint8_t)(((((uint16_t)u << 4) + MK_Q / 2) / MK_Q) & 15);
        }
        r[4 * i + 0] = (uint8_t)(t[0] | (t[1] << 4));
        r[4 * i + 1] = (uint8_t)(t[2] | (t[3] << 4));
        r[4 * i + 2] = (uint8_t)(t[4] | (t[5] << 4));
        r[4 * i + 3] = (uint8_t)(t[6] | (t[7] << 4));
    }
}

// FIPS 203 modulus check: ek's decoded coefficients must all be < q.
static proto_bool check_ek(const uint8_t ek[MLKEM768_EK_BYTES])
{
    for (unsigned i = 0; i < MK_K; i++)
    {
        int16_t p[MK_N];
        poly_frombytes(p, ek + i * MK_POLYBYTES);
        for (unsigned j = 0; j < MK_N; j++)
        {
            if ((uint16_t)p[j] >= MK_Q)
            {
                return PROTO_FALSE;
            }
        }
    }
    return PROTO_TRUE;
}

// K-PKE.Encrypt(ek, m, r) -> ct. u is streamed and compressed one row at a time to bound stack.
static void k_pke_encrypt(uint8_t ct[MLKEM768_CT_BYTES], const uint8_t ek[MLKEM768_EK_BYTES], const uint8_t m[32],
                          const uint8_t coins[32])
{
    int16_t that[MK_K][MK_N];
    for (unsigned i = 0; i < MK_K; i++)
    {
        poly_frombytes(that[i], ek + i * MK_POLYBYTES);
    }
    const uint8_t *rho = ek + MK_K * MK_POLYBYTES;

    int16_t sp[MK_K][MK_N];
    for (unsigned i = 0; i < MK_K; i++)
    {
        poly_getnoise(sp[i], coins, (uint8_t)i); // y, nonce 0..k-1
        ntt(sp[i]);
    }

    // u = NTT^-1(A^T o y) + e1, compressed with du = 10 (320 octets/row).
    for (unsigned i = 0; i < MK_K; i++)
    {
        int16_t at_row[MK_K][MK_N];
        for (unsigned j = 0; j < MK_K; j++)
        {
            gen_matrix_entry(at_row[j], rho, (uint8_t)i, (uint8_t)j);
        }
        int16_t u_row[MK_N];
        polyvec_basemul_acc(u_row, at_row, sp);
        invntt(u_row);
        int16_t e1[MK_N];
        poly_getnoise(e1, coins, (uint8_t)(MK_K + i)); // e1, nonce k..2k-1
        for (unsigned x = 0; x < MK_N; x++)
        {
            u_row[x] = barrett_reduce((int16_t)(u_row[x] + e1[x]));
        }
        poly_compress10(ct + i * 320, u_row);
    }

    // v = NTT^-1(t^T o y) + e2 + Decompress_1(m), compressed with dv = 4 (128 octets).
    int16_t v[MK_N];
    polyvec_basemul_acc(v, that, sp);
    invntt(v);
    int16_t e2[MK_N];
    poly_getnoise(e2, coins, (uint8_t)(2 * MK_K)); // e2, nonce 2k
    int16_t mu[MK_N];
    poly_frommsg(mu, m);
    for (unsigned x = 0; x < MK_N; x++)
    {
        v[x] = barrett_reduce((int16_t)(v[x] + e2[x] + mu[x]));
    }
    poly_compress4(ct + MK_K * 320, v);
}

proto_bool pc_mlkem768_encaps(const uint8_t ek[MLKEM768_EK_BYTES], const uint8_t m[MLKEM768_MSG_BYTES],
                              uint8_t ct[MLKEM768_CT_BYTES], uint8_t ss[MLKEM768_SS_BYTES])
{
    if (!check_ek(ek))
    {
        return PROTO_FALSE;
    }

    // (K, r) = G(m || H(ek)); ss = K.
    uint8_t g_in[64];
    memcpy(g_in, m, 32);
    sha3_256(g_in + 32, ek, MLKEM768_EK_BYTES); // H(ek)
    uint8_t g_out[64];
    sha3_512(g_out, g_in, sizeof(g_in));
    memcpy(ss, g_out, 32);

    k_pke_encrypt(ct, ek, m, g_out + 32);
    return PROTO_TRUE;
}

// ── Initiator side: KeyGen + Decaps (the FO transform) ──────────────────────────────────────────
// These reuse the same NTT / sampling / K-PKE.Encrypt core as Encaps; only the inverse codecs
// (ByteEncode_12, Decompress+ByteDecode for du/dv, Compress_1) and the two FIPS 203 top-level
// algorithms are new.

// ByteEncode_12: 256 coefficients -> 384 octets (inverse of poly_frombytes). Coefficients are frozen
// to [0, q) first (a negative representative maps to +q).
static void poly_tobytes(uint8_t r[MK_POLYBYTES], const int16_t a[MK_N])
{
    for (unsigned i = 0; i < MK_N / 2; i++)
    {
        uint16_t t0 = (uint16_t)(a[2 * i] + ((a[2 * i] >> 15) & MK_Q));
        uint16_t t1 = (uint16_t)(a[2 * i + 1] + ((a[2 * i + 1] >> 15) & MK_Q));
        r[3 * i + 0] = (uint8_t)(t0);
        r[3 * i + 1] = (uint8_t)((t0 >> 8) | (t1 << 4));
        r[3 * i + 2] = (uint8_t)(t1 >> 4);
    }
}

// ByteDecode_10 + Decompress_10: 320 octets -> 256 coefficients (inverse of poly_compress10).
static void poly_decompress10(int16_t r[MK_N], const uint8_t a[320])
{
    unsigned k = 0;
    for (unsigned i = 0; i < MK_N / 4; i++)
    {
        uint16_t t[4];
        t[0] = (uint16_t)((a[k + 0] | ((uint16_t)a[k + 1] << 8)) & 0x3FF);
        t[1] = (uint16_t)(((a[k + 1] >> 2) | ((uint16_t)a[k + 2] << 6)) & 0x3FF);
        t[2] = (uint16_t)(((a[k + 2] >> 4) | ((uint16_t)a[k + 3] << 4)) & 0x3FF);
        t[3] = (uint16_t)(((a[k + 3] >> 6) | ((uint16_t)a[k + 4] << 2)) & 0x3FF);
        k += 5;
        for (unsigned j = 0; j < 4; j++)
        {
            r[4 * i + j] = (int16_t)(((uint32_t)t[j] * MK_Q + 512) >> 10);
        }
    }
}

// ByteDecode_4 + Decompress_4: 128 octets -> 256 coefficients (inverse of poly_compress4).
static void poly_decompress4(int16_t r[MK_N], const uint8_t a[128])
{
    for (unsigned i = 0; i < MK_N / 2; i++)
    {
        uint8_t byte = a[i];
        r[2 * i + 0] = (int16_t)(((uint32_t)(byte & 0x0F) * MK_Q + 8) >> 4);
        r[2 * i + 1] = (int16_t)(((uint32_t)(byte >> 4) * MK_Q + 8) >> 4);
    }
}

// Compress_1 + ByteEncode_1: 256 coefficients -> 32 octets (inverse of poly_frommsg). Each bit is
// round(2*coeff/q) mod 2 - whether the coefficient sits closer to q/2 than to 0.
static void poly_tomsg(uint8_t msg[32], const int16_t a[MK_N])
{
    for (unsigned i = 0; i < 32; i++)
    {
        msg[i] = 0;
        for (unsigned j = 0; j < 8; j++)
        {
            int16_t t = a[8 * i + j];
            t = (int16_t)(t + ((t >> 15) & MK_Q)); // to [0, q)
            uint16_t bit = (uint16_t)(((((uint32_t)t << 1) + MK_Q / 2) / MK_Q) & 1);
            msg[i] |= (uint8_t)(bit << j);
        }
    }
}

// Convert a polynomial into Montgomery form (multiply each coefficient by R = 2^16 mod q via one
// Montgomery reduction of coeff * (2^32 mod q)). polyvec_basemul_acc leaves an extra R^-1 factor; on
// the Encaps/Decrypt paths a following invntt absorbs it, but KeyGen encodes t in the NTT domain
// directly, so it must undo that factor here.
static void poly_tomont(int16_t r[MK_N])
{
    const int16_t f = 1353; // 2^32 mod q
    for (unsigned i = 0; i < MK_N; i++)
    {
        r[i] = mont_reduce((int32_t)r[i] * f);
    }
}

// Constant-time inequality mask over n octets: 0x00 if a == b, 0xFF if they differ. No data-dependent
// branch or early exit - the FO transform must not leak whether the re-encryption matched.
static uint8_t ct_diff_mask(const uint8_t *a, const uint8_t *b, size_t n)
{
    uint32_t acc = 0;
    for (size_t i = 0; i < n; i++)
    {
        acc |= (uint32_t)((uint8_t)(a[i] ^ b[i]));
    }
    uint8_t equal = (uint8_t)(((acc - 1) >> 31) & 1); // 1 iff acc == 0 (all octets equal)
    return (uint8_t)(equal - 1);                      // 0x00 if equal, 0xFF if any differ
}

// K-PKE.KeyGen(d) -> ek_PKE (ByteEncode_12(t) || rho) and dk_PKE (ByteEncode_12(s)), both in the NTT
// domain. t[i] = sum_j A[i][j] o s[j]. K-PKE.Encrypt multiplies by the SAME matrix transposed - its
// u[i] = sum_j XOF(rho, i, j) o y[j] - so for the KEM to invert, KeyGen's A[i][j] = XOF(rho, j, i).
static void k_pke_keygen(uint8_t ek[MLKEM768_EK_BYTES], uint8_t dk_pke[MK_K * MK_POLYBYTES], const uint8_t d[32])
{
    // (rho, sigma) = G(d || k). The trailing k byte is the FIPS 203 domain separation on module rank.
    uint8_t g_in[33];
    memcpy(g_in, d, 32);
    g_in[32] = MK_K;
    uint8_t g_out[64];
    sha3_512(g_out, g_in, sizeof(g_in));
    const uint8_t *rho = g_out;
    const uint8_t *sigma = g_out + 32;

    int16_t s[MK_K][MK_N];
    int16_t e[MK_K][MK_N];
    uint8_t nonce = 0;
    for (unsigned i = 0; i < MK_K; i++)
    {
        poly_getnoise(s[i], sigma, nonce++); // s, nonce 0..k-1
    }
    for (unsigned i = 0; i < MK_K; i++)
    {
        poly_getnoise(e[i], sigma, nonce++); // e, nonce k..2k-1
    }
    for (unsigned i = 0; i < MK_K; i++)
    {
        ntt(s[i]);
        ntt(e[i]);
        for (unsigned x = 0; x < MK_N; x++) // ntt() leaves coeffs unreduced; canonicalize both
        {
            s[i][x] = barrett_reduce(s[i][x]);
            e[i][x] = barrett_reduce(e[i][x]);
        }
    }

    for (unsigned i = 0; i < MK_K; i++)
    {
        int16_t a_row[MK_K][MK_N];
        for (unsigned j = 0; j < MK_K; j++)
        {
            gen_matrix_entry(a_row[j], rho, (uint8_t)j, (uint8_t)i); // A[i][j] = XOF(rho, j, i)
        }
        int16_t t_row[MK_N];
        polyvec_basemul_acc(t_row, a_row, s);
        poly_tomont(t_row); // undo the R^-1 from basemul (no invntt here to absorb it)
        for (unsigned x = 0; x < MK_N; x++)
        {
            t_row[x] = barrett_reduce((int16_t)(t_row[x] + e[i][x]));
        }
        poly_tobytes(ek + i * MK_POLYBYTES, t_row);
    }
    memcpy(ek + MK_K * MK_POLYBYTES, rho, 32);

    for (unsigned i = 0; i < MK_K; i++)
    {
        poly_tobytes(dk_pke + i * MK_POLYBYTES, s[i]);
    }
}

// K-PKE.Decrypt(dk_PKE, ct) -> m: w = v - NTT^-1(s^T o NTT(u)), then Compress_1.
static void k_pke_decrypt(uint8_t m[32], const uint8_t dk_pke[MK_K * MK_POLYBYTES], const uint8_t ct[MLKEM768_CT_BYTES])
{
    int16_t u[MK_K][MK_N];
    for (unsigned i = 0; i < MK_K; i++)
    {
        poly_decompress10(u[i], ct + i * 320);
        ntt(u[i]);
    }
    int16_t shat[MK_K][MK_N];
    for (unsigned i = 0; i < MK_K; i++)
    {
        poly_frombytes(shat[i], dk_pke + i * MK_POLYBYTES);
    }

    int16_t w[MK_N];
    polyvec_basemul_acc(w, shat, u); // s^T o u  (dot product in the NTT domain)
    invntt(w);

    int16_t v[MK_N];
    poly_decompress4(v, ct + MK_K * 320);
    for (unsigned x = 0; x < MK_N; x++)
    {
        w[x] = barrett_reduce((int16_t)(v[x] - w[x]));
    }
    poly_tomsg(m, w);
}

void pc_mlkem768_keygen(const uint8_t d[MLKEM768_D_BYTES], const uint8_t z[MLKEM768_Z_BYTES],
                        uint8_t ek[MLKEM768_EK_BYTES], uint8_t dk[MLKEM768_DK_BYTES])
{
    // dk = dk_PKE || ek || H(ek) || z  (FIPS 203 Algorithm 16).
    k_pke_keygen(ek, dk, d);
    memcpy(dk + MK_K * MK_POLYBYTES, ek, MLKEM768_EK_BYTES);
    sha3_256(dk + MK_K * MK_POLYBYTES + MLKEM768_EK_BYTES, ek, MLKEM768_EK_BYTES);
    memcpy(dk + MK_K * MK_POLYBYTES + MLKEM768_EK_BYTES + 32, z, 32);
}

void pc_mlkem768_decaps(const uint8_t dk[MLKEM768_DK_BYTES], const uint8_t ct[MLKEM768_CT_BYTES],
                        uint8_t ss[MLKEM768_SS_BYTES])
{
    const uint8_t *dk_pke = dk;
    const uint8_t *ek_pke = dk + MK_K * MK_POLYBYTES;
    const uint8_t *h = ek_pke + MLKEM768_EK_BYTES; // H(ek), 32 octets
    const uint8_t *z = h + 32;                     // implicit-reject seed, 32 octets

    // m' = K-PKE.Decrypt(dk_PKE, ct); (K', r') = G(m' || H(ek)).
    uint8_t mprime[32];
    k_pke_decrypt(mprime, dk_pke, ct);
    uint8_t g_in[64];
    memcpy(g_in, mprime, 32);
    memcpy(g_in + 32, h, 32);
    uint8_t g_out[64];
    sha3_512(g_out, g_in, sizeof(g_in));

    // Implicit-reject key K_bar = J(z || ct) = SHAKE256(z || ct, 32).
    uint8_t jbuf[32 + MLKEM768_CT_BYTES];
    memcpy(jbuf, z, 32);
    memcpy(jbuf + 32, ct, MLKEM768_CT_BYTES);
    uint8_t kbar[32];
    shake256(kbar, sizeof(kbar), jbuf, sizeof(jbuf));

    // Re-encrypt under the embedded ek with the derived coins r'; select in constant time.
    uint8_t ctprime[MLKEM768_CT_BYTES];
    k_pke_encrypt(ctprime, ek_pke, mprime, g_out + 32);
    uint8_t diff = ct_diff_mask(ct, ctprime, MLKEM768_CT_BYTES);
    for (unsigned i = 0; i < 32; i++)
    {
        ss[i] = (uint8_t)((g_out[i] & (uint8_t)~diff) | (kbar[i] & diff)); // K' if match, K_bar if not
    }
}

#endif // PC_ENABLE_PQC_KEX
