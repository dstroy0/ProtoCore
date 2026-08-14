// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file kdf.c
 * @brief SP800-108 counter-mode KDF with HMAC-SHA256 PRF (see kdf.h).
 *
 * K(i) = HMAC-SHA256(Ki, [i]_32be || fixed) for i = 1, 2, ..., concatenated and truncated to the
 * requested length. The PRF is protocore_hmac_sha256, which carries the hardware acceleration underneath it.
 */

#include "crypto/kdf/kdf.h"
#include "crypto/crypto_opt.h"
#include "crypto/mac/hmac_sha256.h"
#include "mmgr/endian.h"
#include "mmgr/protomem.h"
#include "mmgr/secure.h" // the secure pool: the PRF's working set, wiped on release

PROTOCORE_CRYPTO_HOT

// Transient KDF working set, borrowed from the secure pool per call and wiped on release. K(i) is
// derived from Ki, so it is key material and never lands on the stack.
typedef struct
{
    uint8_t block[PROTOCORE_HMAC_SHA256_LEN]; ///< K(i)
    uint8_t ctr[4];                           ///< the counter, big-endian
    protocore_hmac_sha256_ctx h;              ///< the PRF
} KdfWork;

// The borrow is the struct then the PRF's own bytes, so the PRF takes its storage from this caller
// the way every other caller hands it over.
#define KDF_WORK_SPAN (sizeof(KdfWork) + PROTOCORE_HMAC_SHA256_BORROW)
static_assert(KDF_WORK_SPAN <= PROTOCORE_WORK_KDF,
              "KdfWork outgrew PROTOCORE_WORK_KDF - raise it in protocore_config.h, which derives "
              "PROTOCORE_SECURE_ARENA_SIZE from it");

proto_bool protocore_kdf_ctr_hmac_sha256(const uint8_t *ki, size_t ki_len, const uint8_t *fixed, size_t fixed_len,
                                         uint8_t *out, size_t out_len)
{
    if (!ki || !fixed || !out || out_len == 0)
    {
        return PROTO_FALSE;
    }
    size_t mark = protocore_secure_mark();
    protocore_span ws = protocore_secure_span(KDF_WORK_SPAN, _Alignof(KdfWork));
    if (!protocore_span_ok(ws))
    {
        protocore_secure_release(mark);
        return PROTO_FALSE; // pool exhausted: the empty borrow is the supported failure signal
    }
    KdfWork *w = (KdfWork *)ws.buf;
    uint8_t *hw = ws.buf + sizeof(KdfWork);

    size_t done = 0;
    for (uint32_t counter = 1; done < out_len; counter++)
    {
        protocore_wr32be(w->ctr, counter);
        protocore_hmac_sha256_init(&w->h, hw, ki, ki_len);
        protocore_hmac_sha256_update(&w->h, w->ctr, sizeof(w->ctr));
        protocore_hmac_sha256_update(&w->h, fixed, fixed_len);
        protocore_hmac_sha256_final(&w->h, w->block);
        size_t take = PROTOCORE_HMAC_SHA256_LEN;
        if (out_len - done < PROTOCORE_HMAC_SHA256_LEN)
        {
            take = out_len - done;
        }
        mem.cpy(out + done, w->block, take);
        done += take;
    }
    protocore_secure_release(mark);
    return PROTO_TRUE;
}
