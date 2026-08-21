// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_HMAC_SHA384_H
#define PROTOCORE_HMAC_SHA384_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file hmac_sha384.h
 * @brief HMAC-SHA2-384 (RFC 2104 + FIPS 198-1) - streaming context and one-shot API.
 *
 * The shared HMAC-SHA384 primitive. Backs the HKDF the TLS 1.3 SHA-384 cipher suites run their key
 * schedule on, and the Finished MAC under them. Built over the @ref Sha384Ns entries, so which arm
 * compresses the inner hash is not visible here. TLS 1.3 keys a Finished MAC with a 48-byte secret
 * (<= the 128-byte block), so the key is zero-padded, not pre-hashed.
 *
 * RFC 2104 construction: HMAC(K, m) = H((K XOR opad) || H((K XOR ipad) || m)), H = SHA-384. The block
 * is SHA-512's 128 octets, not the 48-octet digest, so the pads are 128 wide (RFC 4231 sec 2 tabulates
 * the published HMAC-SHA-384 vectors on that block).
 *
 * SECURITY NOTE - a MAC must be verified before the covered plaintext is acted upon; that ordering
 * guarantee lives in each protocol's packet layer, not here. These functions are pure crypto.
 *
 * For a MAC assembled from separate pieces - an HKDF-Expand block over (T(i-1) || info || i):
 *
 * @c work is PROTOCORE_HMAC_SHA384_BORROW secure bytes the CALLER took, at an address it knows. It
 * arrives @c restrict and is not held past the call, so nothing here aliases it. The caller releases
 * it, and the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes
 * it. A connection takes those bytes once for its slot and passes them on every record.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

/** @brief HMAC-SHA2-384 output length in bytes. */
#define PROTOCORE_HMAC_SHA384_LEN 48

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*init)(uint8_t *restrict, const uint8_t *, size_t);
    proto_bool (*update)(uint8_t *restrict, const uint8_t *, size_t);
    proto_bool (*final)(uint8_t *restrict, uint8_t *);
    proto_bool (*mac)(uint8_t *restrict, const uint8_t *, size_t, const uint8_t *, size_t, uint8_t *);
} HmacSha384Ns;
PROTOCORE_NS_LAYOUT(HmacSha384Ns, init, update, final, mac);

/**
 * @brief Start a MAC under HmacSha384Ns::key_args.
 * @param work PROTOCORE_HMAC_SHA384_BORROW bytes the caller took. Not held past the call.
 * @param key MAC key bytes
 * @param key_len key length; > 128 is pre-hashed (RFC 2104), shorter is zero-padded to the block
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_hmac_sha384_init(uint8_t *restrict work, const uint8_t *key, size_t key_len);
/**
 * @brief Feed the running MAC a chunk.
 * @param work PROTOCORE_HMAC_SHA384_BORROW bytes the caller took. Not held past the call.
 * @param data the bytes
 * @param len how many
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_hmac_sha384_update(uint8_t *restrict work, const uint8_t *data, size_t len);
/**
 * @brief Finish, writing the 48 bytes out.
 * @param work PROTOCORE_HMAC_SHA384_BORROW bytes the caller took. Not held past the call.
 * @param out PROTOCORE_HMAC_SHA384_LEN bytes
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_hmac_sha384_final(uint8_t *restrict work, uint8_t *out);
/**
 * @brief Init, update and final in one call, for a message already whole.
 * @param work PROTOCORE_HMAC_SHA384_BORROW bytes the caller took. Not held past the call.
 * @param key MAC key bytes
 * @param key_len key length
 * @param data the message
 * @param len its length
 * @param out PROTOCORE_HMAC_SHA384_LEN bytes
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_hmac_sha384_mac(uint8_t *restrict work, const uint8_t *key, size_t key_len, const uint8_t *data,
                                     size_t len, uint8_t *out);

/** @brief Module namespace. */
PROTOCORE_NS HmacSha384Ns HmacSha384 PROTOCORE_UNUSED = {.init = protocore_hmac_sha384_init,
                                                         .update = protocore_hmac_sha384_update,
                                                         .final = protocore_hmac_sha384_final,
                                                         .mac = protocore_hmac_sha384_mac};

PROTOCORE_END_DECLS

#endif // PROTOCORE_HMAC_SHA384_H
