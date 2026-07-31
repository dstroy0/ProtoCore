// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// Test-only ML-KEM-768 Decaps reference (header-only so any test can include it - the SSH and HTTP/3
// hybrid tests both use it as the client side). The library ships Encaps only (the device is always
// the KEM responder), so to verify a hybrid handshake the way a conforming client would - decapsulating
// the server's ciphertext to recover the shared secret - the test needs its own independent Decaps.
// This is a FIPS 203 K-PKE.Decrypt + K = G(m' || H(ek)); it omits the implicit-reject re-encryption
// because the test only ever feeds it a genuine ciphertext. Validated against the pinned KAT before use.
// It deliberately duplicates the NTT/poly machinery rather than reaching into the src static internals
// so the test is a genuine second implementation.

#ifndef PC_TEST_MLKEM_REF_H
#define PC_TEST_MLKEM_REF_H

#include "crypto/hash/sha3.h"
#include <stdint.h>
#include <string.h>

namespace mlkem_ref_detail
{
static const int Q = 3329;
static const int NREF = 256;
static const int KREF = 3;
static const int QINV = -3327;

static const int16_t zetas[128] = {
    -1044, -758,  -359,  -1517, 1493,  1422,  287,   202,   -171,  622,  1577,  182,   962,   -1202, -1474, 1468,
    573,   -1325, 264,   383,   -829,  1458,  -1602, -130,  -681,  1017, 732,   608,   -1542, 411,   -205,  -1571,
    1223,  652,   -552,  1015,  -1293, 1491,  -282,  -1544, 516,   -8,   -320,  -666,  -1618, -1162, 126,   1469,
    -853,  -90,   -271,  830,   107,   -1421, -247,  -951,  -398,  961,  -1508, -725,  448,   -1065, 677,   -1275,
    -1103, 430,   555,   843,   -1251, 871,   1550,  105,   422,   587,  177,   -235,  -291,  -460,  1574,  1653,
    -246,  778,   1159,  -147,  -777,  1483,  -602,  1119,  -1590, 644,  -872,  349,   418,   329,   -156,  -75,
    817,   1097,  603,   610,   1322,  -1285, -1465, 384,   -1215, -136, 1218,  -1335, -874,  220,   -1187, -1659,
    -1185, -1530, -1278, 794,   -1510, -854,  -870,  478,   -108,  -308, 996,   991,   958,   -1460, 1522,  1628};

static inline int16_t montred(int32_t a)
{
    int16_t t = (int16_t)((int16_t)a * (int16_t)QINV);
    return (int16_t)((a - (int32_t)t * Q) >> 16);
}
static inline int16_t fqmul(int16_t a, int16_t b)
{
    return montred((int32_t)a * b);
}
static inline int16_t barrett(int16_t a)
{
    const int16_t v = 20159;
    int16_t t = (int16_t)(((int32_t)v * a + (1 << 25)) >> 26);
    return (int16_t)(a - (int16_t)(t * Q));
}
static inline void ntt(int16_t r[256])
{
    unsigned k = 1;
    for (unsigned len = 128; len >= 2; len >>= 1)
    {
        for (unsigned start = 0; start < 256; start += (len << 1))
        {
            int16_t z = zetas[k++];
            for (unsigned j = start; j < start + len; j++)
            {
                int16_t t = fqmul(z, r[j + len]);
                r[j + len] = (int16_t)(r[j] - t);
                r[j] = (int16_t)(r[j] + t);
            }
        }
    }
}
static inline void invntt(int16_t r[256])
{
    const int16_t f = 1441;
    unsigned k = 127;
    for (unsigned len = 2; len <= 128; len <<= 1)
    {
        for (unsigned start = 0; start < 256; start += (len << 1))
        {
            int16_t z = zetas[k--];
            for (unsigned j = start; j < start + len; j++)
            {
                int16_t t = r[j];
                r[j] = barrett((int16_t)(t + r[j + len]));
                r[j + len] = (int16_t)(r[j + len] - t);
                r[j + len] = fqmul(z, r[j + len]);
            }
        }
    }
    for (unsigned j = 0; j < 256; j++)
    {
        r[j] = fqmul(r[j], f);
    }
}
static inline void basemul(int16_t r[2], const int16_t a[2], const int16_t b[2], int16_t z)
{
    r[0] = fqmul(a[1], b[1]);
    r[0] = fqmul(r[0], z);
    r[0] = (int16_t)(r[0] + fqmul(a[0], b[0]));
    r[1] = fqmul(a[0], b[1]);
    r[1] = (int16_t)(r[1] + fqmul(a[1], b[0]));
}
static inline void polyvec_basemul_acc(int16_t r[256], const int16_t a[3][256], const int16_t b[3][256])
{
    int16_t t[256];
    for (unsigned p = 0; p < 64; p++)
    {
        basemul(&r[4 * p], &a[0][4 * p], &b[0][4 * p], zetas[64 + p]);
        basemul(&r[4 * p + 2], &a[0][4 * p + 2], &b[0][4 * p + 2], (int16_t)(-zetas[64 + p]));
    }
    for (unsigned i = 1; i < 3; i++)
    {
        for (unsigned p = 0; p < 64; p++)
        {
            basemul(&t[4 * p], &a[i][4 * p], &b[i][4 * p], zetas[64 + p]);
            basemul(&t[4 * p + 2], &a[i][4 * p + 2], &b[i][4 * p + 2], (int16_t)(-zetas[64 + p]));
        }
        for (unsigned j = 0; j < 256; j++)
        {
            r[j] = (int16_t)(r[j] + t[j]);
        }
    }
    for (unsigned j = 0; j < 256; j++)
    {
        r[j] = barrett(r[j]);
    }
}
static inline void poly_frombytes(int16_t r[256], const uint8_t a[384])
{
    for (unsigned i = 0; i < 128; i++)
    {
        r[2 * i] = (int16_t)(((a[3 * i] >> 0) | ((uint16_t)a[3 * i + 1] << 8)) & 0xFFF);
        r[2 * i + 1] = (int16_t)(((a[3 * i + 1] >> 4) | ((uint16_t)a[3 * i + 2] << 4)) & 0xFFF);
    }
}
static inline void decompress10(int16_t r[256], const uint8_t a[320])
{
    for (unsigned i = 0; i < 64; i++)
    {
        uint16_t t[4];
        t[0] = (uint16_t)((a[0] >> 0) | ((uint16_t)a[1] << 8));
        t[1] = (uint16_t)((a[1] >> 2) | ((uint16_t)a[2] << 6));
        t[2] = (uint16_t)((a[2] >> 4) | ((uint16_t)a[3] << 4));
        t[3] = (uint16_t)((a[3] >> 6) | ((uint16_t)a[4] << 2));
        a += 5;
        for (unsigned j = 0; j < 4; j++)
        {
            r[4 * i + j] = (int16_t)((((uint32_t)(t[j] & 0x3FF) * Q) + 512) >> 10);
        }
    }
}
static inline void decompress4(int16_t r[256], const uint8_t a[128])
{
    for (unsigned i = 0; i < 128; i++)
    {
        r[2 * i] = (int16_t)(((uint16_t)(a[i] & 15) * Q + 8) >> 4);
        r[2 * i + 1] = (int16_t)(((uint16_t)(a[i] >> 4) * Q + 8) >> 4);
    }
}
static inline void poly_tomsg(uint8_t msg[32], const int16_t a[256])
{
    for (unsigned i = 0; i < 32; i++)
    {
        msg[i] = 0;
        for (unsigned j = 0; j < 8; j++)
        {
            int16_t t = a[8 * i + j];
            t = (int16_t)(t + ((t >> 15) & Q));
            t = (int16_t)((((t << 1) + Q / 2) / Q) & 1);
            msg[i] = (uint8_t)(msg[i] | (t << j));
        }
    }
}
} // namespace mlkem_ref_detail

// Recover the 32-octet shared secret from a decapsulation key (2400 B) and a genuine ciphertext.
static inline void pc_mlkem768_decaps_ref(const uint8_t dk[2400], const uint8_t ct[1088], uint8_t ss[32])
{
    using namespace mlkem_ref_detail;
    // dk = dk_pke(1152) || ek(1184) || H(ek)(32) || z(32); s_hat is stored in NTT domain.
    int16_t shat[3][256];
    for (unsigned i = 0; i < 3; i++)
    {
        poly_frombytes(shat[i], dk + i * 384);
    }

    int16_t u[3][256];
    for (unsigned i = 0; i < 3; i++)
    {
        decompress10(u[i], ct + i * 320);
        ntt(u[i]);
    }
    int16_t v[256];
    decompress4(v, ct + 3 * 320);

    int16_t w[256];
    polyvec_basemul_acc(w, shat, u);
    invntt(w);
    for (unsigned j = 0; j < 256; j++)
    {
        w[j] = (int16_t)(v[j] - w[j]); // m-carrying polynomial = v - s^T u
    }

    uint8_t m2[32];
    poly_tomsg(m2, w);

    const uint8_t *h = dk + 1152 + 1184; // H(ek)
    uint8_t gin[64];
    memcpy(gin, m2, 32);
    memcpy(gin + 32, h, 32);
    uint8_t gout[64];
    sha3_512(gout, gin, sizeof(gin)); // (K, r) = G(m' || H(ek)); shared secret = K
    memcpy(ss, gout, 32);
}

#endif
