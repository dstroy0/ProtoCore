// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file crypto_scratch.h
 * @brief Constant-time comparison for secret-dependent checks.
 *
 * Was also the home of the shared `crypto_work` scratch buffer and the secure wipe. Both have moved:
 * crypto operations now borrow their working sets from the secure pool (server/mmgr/secure.h), which
 * wipes on release, so no fixed buffer and no hand-assigned offsets remain. pc_secure_wipe() lives
 * there too - zeroing storage is a memory-manager operation. What is left here is pc_ct_eq, which is
 * genuinely a crypto concern. The file name now overstates its contents; renaming it is a follow-up.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CRYPTO_SCRATCH_H
#define PROTOCORE_CRYPTO_SCRATCH_H

#include "protocore_config.h"
#include "server/mmgr/span.h"
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Constant-time equality of two @p n-byte buffers: returns true iff every byte matches, in time
 *        independent of where (or whether) they first differ.
 *
 * Use this for every secret-dependent comparison - AEAD tags, MACs, digests, signature check values - so a
 * timing side channel cannot reveal how many leading bytes matched. Never use memcmp() for those (it returns
 * early on the first mismatch). The XOR-accumulate has no data-dependent branch; only the final all-zero test
 * (the intended result) is a comparison.
 */
static inline bool pc_ct_eq(const void *a, const void *b, size_t n)
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

#endif // PROTOCORE_CRYPTO_SCRATCH_H
