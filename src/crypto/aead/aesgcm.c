// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file aesgcm.c
 * @brief AES-256-GCM AEAD - stateless implementation (see aesgcm.h).
 *
 * All working memory (AES key schedule, GHASH table, keystream, accumulator, tag mask, counters) lives in
 * the secure pool, laid out as a GcmWork on the software path or an
 * The working set is a pool borrow, wiped when released. No cipher
 * state ever touches the stack or BSS.
 *
 * The implementation lives in a backend under board_drivers/, chosen by the vendor's
 * PC_HAS_HW_AESGCM: the accelerated AEAD where the silicon has one, the portable software
 * AES + table GHASH where it does not. This file names no vendor and no cipher
 * + GF(2^8) xtime) + software GHASH. The GF(2^128) reduction mirrors aes128gcm.cpp.
 */

#include "crypto/aead/aesgcm.h"
#include "crypto/crypto_opt.h"
#include "crypto/mac/ghash.h"
#include "mmgr/secure.h"
#include <string.h>

PC_CRYPTO_HOT

// Advance the RFC 5647 invocation counter: the low 8 bytes of the 12-byte nonce, big-endian; the 4-byte
// fixed field never changes. Shared by both paths and exposed publicly for the SSH packet layer.
void pc_aesgcm_iv_increment(uint8_t iv[PC_AESGCM_IV_LEN])
{
    for (int j = PC_AESGCM_IV_LEN - 1; j >= 4; j--)
    {
        if (++iv[j])
        {
            break;
        }
    }
}
