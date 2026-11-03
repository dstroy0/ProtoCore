// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_HMAC_SHA256_H
#define PROTOCORE_HMAC_SHA256_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file hmac_sha256.h
 * @brief HMAC-SHA2-256 (RFC 2104 + FIPS 198-1) - streaming context and one-shot API.
 *
 * The shared keyed-MAC primitive for the whole library: SSH binary-packet MAC (RFC 4253 §6.4), the
 * TLS 1.3 / QUIC / DTLS HKDF PRF, SNMPv3 usmHMACSHAAuthProtocol, JWT HS256, CSRF tokens, and SMB 2.x
 * message signing / the SP800-108 KDF. Built over the @ref Sha256Ns entries, so which arm compresses
 * the inner hash is not visible here.
 *
 * RFC 2104 construction: HMAC(K, m) = H((K XOR opad) || H((K XOR ipad) || m)), H = SHA-256.
 *
 * SECURITY NOTE - a MAC must be verified before the covered plaintext is acted upon; that ordering
 * guarantee lives in each protocol's packet layer, not here. These functions are pure crypto.
 *
 * For a MAC assembled from separate pieces - the SSH packet MAC over
 * (uint32_be(seq_num) || plaintext_packet):
 *
 * @c work is PROTOCORE_HMAC_SHA256_BORROW secure bytes the CALLER took, at an address it knows. It
 * is not held past the call, so nothing here aliases it. The caller releases
 * it, and the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes
 * it. A connection takes those bytes once for its slot and passes them on every packet.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

/** @brief HMAC-SHA2-256 output length in bytes. */
#define PROTOCORE_HMAC_SHA256_LEN 32

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*init)(uint8_t *, const uint8_t *, size_t);
    proto_bool (*update)(uint8_t *, const uint8_t *, size_t);
    proto_bool (*final)(uint8_t *, uint8_t *);
    proto_bool (*mac)(uint8_t *, const uint8_t *, size_t, const uint8_t *, size_t, uint8_t *);
} HmacSha256Ns;
PROTOCORE_NS_LAYOUT(HmacSha256Ns, init, update, final, mac);

/**
 * @brief Start a MAC under HmacSha256Ns::key_args.
 * @param work PROTOCORE_HMAC_SHA256_BORROW bytes the caller took. Not held past the call.
 * @param key MAC key bytes
 * @param key_len key length; > 64 is pre-hashed (RFC 2104), shorter is zero-padded to the block
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_hmac_sha256_init(uint8_t *work, const uint8_t *key, size_t key_len);
/**
 * @brief Feed the running MAC a chunk.
 * @param work PROTOCORE_HMAC_SHA256_BORROW bytes the caller took. Not held past the call.
 * @param data the bytes
 * @param len how many
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_hmac_sha256_update(uint8_t *work, const uint8_t *data, size_t len);
/**
 * @brief Finish, writing the 32 bytes out.
 * @param work PROTOCORE_HMAC_SHA256_BORROW bytes the caller took. Not held past the call.
 * @param out PROTOCORE_HMAC_SHA256_LEN bytes
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_hmac_sha256_final(uint8_t *work, uint8_t *out);
/**
 * @brief Init, update and final in one call, for a message already whole.
 * @param work PROTOCORE_HMAC_SHA256_BORROW bytes the caller took. Not held past the call.
 * @param key MAC key bytes
 * @param key_len key length
 * @param data the message
 * @param len its length
 * @param out PROTOCORE_HMAC_SHA256_LEN bytes
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_hmac_sha256_mac(uint8_t *work, const uint8_t *key, size_t key_len, const uint8_t *data, size_t len,
                                     uint8_t *out);

/** @brief Module namespace. */
PROTOCORE_NS HmacSha256Ns HmacSha256 PROTOCORE_UNUSED = {.init = protocore_hmac_sha256_init,
                                                         .update = protocore_hmac_sha256_update,
                                                         .final = protocore_hmac_sha256_final,
                                                         .mac = protocore_hmac_sha256_mac};

PROTOCORE_END_DECLS

#endif // PROTOCORE_HMAC_SHA256_H
