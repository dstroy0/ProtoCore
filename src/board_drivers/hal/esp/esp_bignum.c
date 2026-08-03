// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file esp_bignum.c
 * @brief DH-2048 modexp on the Espressif RSA accelerator, via mbedtls.
 *
 * The bignum backend for a vendor profile that sets PC_HAS_HW_BIGNUM 1. Vendor headers are fine
 * here - this is board_drivers/, the partition vendor code is segregated to. The core names none of
 * them and simply calls bn_expmod_group14().
 */

#include "board_drivers/board_profiles/pc_platform.h" // PC_HAS_HW_BIGNUM
#include "crypto/asymmetric/bignum.h"
#include "crypto/crypto_opt.h"
#include "mmgr/secure.h"
#include <string.h>

#if PC_HAS_HW_BIGNUM

#include <mbedtls/bignum.h> // HW bignum acceleration for the DH-2048 modexp

PC_CRYPTO_HOT

// On ESP32 delegate to mbedtls which uses HW bignum acceleration.

// ESP32 path: the big-endian buffers handed to mbedtls (HW bignum acceleration).
typedef struct
{
    uint8_t base_be[256];
    uint8_t exp_be[256];
    uint8_t p_be[256];
    uint8_t res_be[256];
} BnExpmodBytes;

// Worst-case bytes this backend borrows in one modexp. PC_SECURE_ARENA_SIZE is derived
// from declarations like this one; the static_assert below is what proves it.
static_assert(sizeof(BnExpmodBytes) <= PC_WORK_BIGNUM_HW,
              "BnExpmodBytes outgrew PC_WORK_BIGNUM_HW - raise it; PC_SECURE_ARENA_SIZE derives from it");

void bn_expmod_group14(pc_bignum *out, const pc_bignum *base, const pc_bignum *exp)
{
    // Big-endian temporaries live in the shared crypto scratch, not on the stack: exp holds the DH private
    // exponent and res the shared secret, so the whole region is wiped on exit (mmgr/secure.h).
    size_t mark = pc_secure_mark();
    pc_span ws = pc_secure_span(sizeof(BnExpmodBytes), _Alignof(BnExpmodBytes));
    if (!pc_span_ok(ws))
    {
        pc_secure_release(mark);
        memset(out, 0, sizeof(*out)); // pool exhausted: a zero result fails every downstream check
        return;
    }
    BnExpmodBytes *w = (BnExpmodBytes *)(ws.buf);
    uint8_t *base_be = w->base_be;
    uint8_t *exp_be = w->exp_be;
    uint8_t *p_be = w->p_be;
    uint8_t *res_be = w->res_be;
    bn_to_bytes(base_be, base);
    bn_to_bytes(exp_be, exp);
    bn_to_bytes(p_be, &group14_p);

    mbedtls_mpi B;
    mbedtls_mpi E;
    mbedtls_mpi P;
    mbedtls_mpi R;
    mbedtls_mpi_init(&B);
    mbedtls_mpi_init(&E);
    mbedtls_mpi_init(&P);
    mbedtls_mpi_init(&R);

    mbedtls_mpi_read_binary(&B, base_be, 256);
    mbedtls_mpi_read_binary(&E, exp_be, 256);
    mbedtls_mpi_read_binary(&P, p_be, 256);

    mbedtls_mpi_exp_mod(&R, &B, &E, &P, NULL);
    mbedtls_mpi_write_binary(&R, res_be, 256);

    mbedtls_mpi_free(&B);
    mbedtls_mpi_free(&E);
    mbedtls_mpi_free(&P);
    mbedtls_mpi_free(&R);

    bn_from_bytes(out, res_be, 256);
    pc_secure_release(mark);
}

#endif // PC_HAS_HW_BIGNUM
