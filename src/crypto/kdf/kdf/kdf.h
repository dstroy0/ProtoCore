// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_KDF_H
#define PROTOCORE_KDF_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file kdf.h
 * @brief SP800-108 counter-mode key derivation (HMAC-SHA256 PRF).
 *
 * The shared NIST SP800-108 §5.1 counter-mode KDF. SMB 3.x uses it to derive its signing and
 * encryption keys (MS-SMB2 §3.1.4.2); the caller assembles the fixed input, keeping this independent
 * of any protocol's label/context choices. Verified against the NIST CAVP KBKDF (KDFCTR) vectors.
 * Built over the @ref HmacSha256Ns entries, so which arm compresses the PRF is not visible here.
 *
 * K(i) = HMAC-SHA256(Ki, [i]_32be || fixed); the blocks are concatenated for i = 1, 2, ... and the
 * result truncated to @ref KdfCtrArgs::out_len bytes.
 *
 * @c work is PROTOCORE_KDF_BORROW secure bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. The caller releases it, and
 * the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes it. That
 * is what keeps K(i), which is derived from Ki, from outliving the caller.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*ctr_hmac_sha256)(uint8_t *restrict, const uint8_t *, size_t, const uint8_t *, size_t, uint8_t *,
                                  size_t);
} KdfNs;
PROTOCORE_NS_LAYOUT(KdfNs, ctr_hmac_sha256);

/**
 * @brief Derive out_len bytes, counter mode, HMAC-SHA256 PRF.
 * @param work PROTOCORE_KDF_BORROW bytes the caller took. Not held past the call.
 * @param ki the key-derivation key (e.g. the SMB 3.x session key)
 * @param ki_len its length in bytes
 * @param fixed the fixed input, `Label || 0x00 || Context || [L]`
 * @param fixed_len its length in bytes
 * @param out receives out_len derived bytes
 * @param out_len number of output bytes, >= 1; the caller encodes L = out_len * 8 into fixed
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_kdf_ctr_hmac_sha256(uint8_t *restrict work, const uint8_t *ki, size_t ki_len, const uint8_t *fixed,
                                         size_t fixed_len, uint8_t *out, size_t out_len);

/** @brief Module namespace. */
PROTOCORE_NS KdfNs Kdf PROTOCORE_UNUSED = {.ctr_hmac_sha256 = protocore_kdf_ctr_hmac_sha256};

PROTOCORE_END_DECLS

#endif // PROTOCORE_KDF_H
