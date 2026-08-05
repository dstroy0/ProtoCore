// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file kdf.h
 * @brief SP800-108 counter-mode key derivation (HMAC-SHA256 PRF).
 *
 * The shared NIST SP800-108 §5.1 counter-mode KDF. SMB 3.x uses it to derive its signing and
 * encryption keys (MS-SMB2 §3.1.4.2); the caller assembles the fixed input, keeping this independent
 * of any protocol's label/context choices. Verified against the NIST CAVP KBKDF (KDFCTR) vectors.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_KDF_H
#define PROTOCORE_KDF_H

#include "protocore_config.h" // the entry point: types.h for proto_bool and the widths

PROTO_BEGIN_DECLS

/**
 * @brief SP800-108 KDF in counter mode with HMAC-SHA256 as the PRF (NIST SP800-108 §5.1; r = 32-bit
 *        counter placed before the fixed input).
 *
 * K(i) = HMAC-SHA256(Ki, [i]_32be || fixed); the blocks are concatenated for i = 1, 2, ... and the
 * result truncated to @p out_len bytes. The caller assembles @p fixed as `Label || 0x00 || Context ||
 * [L]` (L = the output length in bits, 32-bit big-endian) and passes it whole.
 *
 * @param ki        the key-derivation key (e.g. the SMB 3.x session key).
 * @param ki_len    length of @p ki in bytes.
 * @param fixed     the fixed input (`Label || 0x00 || Context || [L]`).
 * @param fixed_len length of @p fixed in bytes.
 * @param out       receives @p out_len derived bytes.
 * @param out_len   number of output bytes (>= 1); the caller must encode L = out_len * 8 into @p fixed.
 * @return true on success; false on a null pointer or @p out_len == 0.
 */
proto_bool pc_kdf_ctr_hmac_sha256(const uint8_t *ki, size_t ki_len, const uint8_t *fixed, size_t fixed_len,
                                  uint8_t *out, size_t out_len);

PROTO_END_DECLS

#endif // PROTOCORE_KDF_H
