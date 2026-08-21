// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_HMAC_SHA512_H
#define PROTOCORE_HMAC_SHA512_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file hmac_sha512.h
 * @brief HMAC-SHA2-512 (RFC 2104 + FIPS 198-1) - streaming context and one-shot API.
 *
 * The shared HMAC-SHA512 primitive. Backs the SSH hmac-sha2-512 / hmac-sha2-512-etm@openssh.com
 * integrity algorithms. Built over the @ref Sha512Ns entries, so which arm compresses the inner hash
 * is not visible here. SSH-derived MAC keys are 64 bytes (<= the 128-byte block), so the key is
 * zero-padded, not pre-hashed.
 *
 * RFC 2104 construction: HMAC(K, m) = H((K XOR opad) || H((K XOR ipad) || m)), H = SHA-512.
 *
 * SECURITY NOTE - a MAC must be verified before the covered plaintext is acted upon; that ordering
 * guarantee lives in each protocol's packet layer, not here. These functions are pure crypto.
 *
 * For a MAC assembled from separate pieces - the SSH packet MAC over
 * (uint32_be(seq_num) || plaintext_packet):
 *
 * @c work is PROTOCORE_HMAC_SHA512_BORROW secure bytes the CALLER took, at an address it knows. It
 * arrives @c restrict and is not held past the call, so nothing here aliases it. The caller releases
 * it, and the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes
 * it. A connection takes those bytes once for its slot and passes them on every packet.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

/** @brief HMAC-SHA2-512 output length in bytes. */
#define PROTOCORE_HMAC_SHA512_LEN 64

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*init)(uint8_t *restrict, const uint8_t *, size_t);
    proto_bool (*update)(uint8_t *restrict, const uint8_t *, size_t);
    proto_bool (*final)(uint8_t *restrict, uint8_t *);
    proto_bool (*mac)(uint8_t *restrict, const uint8_t *, size_t, const uint8_t *, size_t, uint8_t *);
} HmacSha512Ns;
PROTOCORE_NS_LAYOUT(HmacSha512Ns, init, update, final, mac);

/**
 * @brief Start a MAC under HmacSha512Ns::key_args.
 * @param work PROTOCORE_HMAC_SHA512_BORROW bytes the caller took. Not held past the call.
 * @param key MAC key bytes
 * @param key_len key length; > 128 is pre-hashed (RFC 2104), shorter is zero-padded to the block
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_hmac_sha512_init(uint8_t *restrict work, const uint8_t *key, size_t key_len);
/**
 * @brief Feed the running MAC a chunk.
 * @param work PROTOCORE_HMAC_SHA512_BORROW bytes the caller took. Not held past the call.
 * @param data the bytes
 * @param len how many
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_hmac_sha512_update(uint8_t *restrict work, const uint8_t *data, size_t len);
/**
 * @brief Finish, writing the 64 bytes out.
 * @param work PROTOCORE_HMAC_SHA512_BORROW bytes the caller took. Not held past the call.
 * @param out PROTOCORE_HMAC_SHA512_LEN bytes
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_hmac_sha512_final(uint8_t *restrict work, uint8_t *out);
/**
 * @brief Init, update and final in one call, for a message already whole.
 * @param work PROTOCORE_HMAC_SHA512_BORROW bytes the caller took. Not held past the call.
 * @param key MAC key bytes
 * @param key_len key length
 * @param data the message
 * @param len its length
 * @param out PROTOCORE_HMAC_SHA512_LEN bytes
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_hmac_sha512_mac(uint8_t *restrict work, const uint8_t *key, size_t key_len, const uint8_t *data,
                                     size_t len, uint8_t *out);

/** @brief Module namespace. */
PROTOCORE_NS HmacSha512Ns HmacSha512 PROTOCORE_UNUSED = {.init = protocore_hmac_sha512_init,
                                                         .update = protocore_hmac_sha512_update,
                                                         .final = protocore_hmac_sha512_final,
                                                         .mac = protocore_hmac_sha512_mac};

PROTOCORE_END_DECLS

#endif // PROTOCORE_HMAC_SHA512_H
