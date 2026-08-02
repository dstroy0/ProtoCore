// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ct_eq.h
 * @brief Constant-time comparison for secret-dependent checks.
 *
 * One implementation, so every MAC, tag, digest and signature check in the library compares the same
 * way. A second copy is a second chance to write the early-out version by accident, and an early-out
 * compare is a timing oracle no test catches.
 *
 * Zeroing storage is not here: pc_secure_wipe() is a memory-manager operation and lives in
 * mmgr/secure.h, beside the pool that wipes on release.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CT_EQ_H
#define PROTOCORE_CT_EQ_H

#include "protocore_config.h" // the entry point: types.h for proto_bool / size_t / uint8_t

/**
 * @brief Constant-time equality of two @p n-byte buffers: returns true iff every byte matches, in time
 *        independent of where (or whether) they first differ.
 *
 * Use this for every secret-dependent comparison - AEAD tags, MACs, digests, signature check values - so a
 * timing side channel cannot reveal how many leading bytes matched. Never use memcmp() for those (it returns
 * early on the first mismatch). The XOR-accumulate has no data-dependent branch; only the final all-zero test
 * (the intended result) is a comparison.
 */
static inline proto_bool pc_ct_eq(const void *a, const void *b, size_t n)
{
    const uint8_t *pa = (const uint8_t *)a;
    const uint8_t *pb = (const uint8_t *)b;
    uint8_t diff = 0;
    for (size_t i = 0; i < n; i++)
    {
        diff |= (uint8_t)(pa[i] ^ pb[i]);
    }
    return diff == 0;
}

#endif // PROTOCORE_CT_EQ_H
