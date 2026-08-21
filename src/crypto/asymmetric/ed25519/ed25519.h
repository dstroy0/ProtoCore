// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_ED25519_H
#define PROTOCORE_ED25519_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file ed25519.h
 * @brief Ed25519 signatures (RFC 8032) for ssh-ed25519 host keys + client auth.
 *
 * PureEdDSA over edwards25519. Deterministic signing (RFC 8032 §5.1.6) - no RNG - and
 * verification, built on the shared Curve25519 field arithmetic (protocore_curve25519) and the
 * @ref Sha512Ns entries, so which arm hashes is not visible here. Correctness is pinned to the
 * RFC 8032 §7.1 vectors and to a reference implementation (test_ed25519).
 *
 * The server signs the KEX exchange hash with its ssh-ed25519 host key, and verifies a
 * client's ed25519 public-key authentication signature.
 *
 * @c work is PROTOCORE_ED25519_BORROW secure bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. The caller releases it, and
 * the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes it. That
 * is what keeps the expanded seed, the nonce and the challenge from outliving the caller.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

/** @brief Ed25519 seed (private key) length. */
#define PROTOCORE_ED25519_SEED_LEN 32

/** @brief Ed25519 public key length. */
#define PROTOCORE_ED25519_PUBKEY_LEN 32

/** @brief Ed25519 signature length (R || S). */
#define PROTOCORE_ED25519_SIG_LEN 64

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*pubkey)(uint8_t *restrict, const uint8_t *, uint8_t *);
    proto_bool (*sign)(uint8_t *restrict, const uint8_t *, const uint8_t *, size_t, uint8_t *);
    proto_bool (*verify)(uint8_t *restrict, const uint8_t *, const uint8_t *, size_t, const uint8_t *);
} Ed25519Ns;
PROTOCORE_NS_LAYOUT(Ed25519Ns, pubkey, sign, verify);

/**
 * @brief Derive the 32-byte public key A from the seed.
 * @param work PROTOCORE_ED25519_BORROW bytes the caller took. Not held past the call.
 * @param seed PROTOCORE_ED25519_SEED_LEN bytes
 * @param pub PROTOCORE_ED25519_PUBKEY_LEN bytes
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_ed25519_pubkey(uint8_t *restrict work, const uint8_t *seed, uint8_t *pub);
/**
 * @brief Sign deterministically (RFC 8032 §5.1.6), writing R || S.
 * @param work PROTOCORE_ED25519_BORROW bytes the caller took. Not held past the call.
 * @param seed PROTOCORE_ED25519_SEED_LEN bytes
 * @param msg the message
 * @param msg_len its length
 * @param sig PROTOCORE_ED25519_SIG_LEN bytes, R || S
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_ed25519_sign(uint8_t *restrict work, const uint8_t *seed, const uint8_t *msg, size_t msg_len,
                                  uint8_t *sig);
/**
 * @brief Check a signature (RFC 8032 §5.1.7); the answer is Ed25519Ns::ok.
 * @param work PROTOCORE_ED25519_BORROW bytes the caller took. Not held past the call.
 * @param pub PROTOCORE_ED25519_PUBKEY_LEN bytes
 * @param msg the message
 * @param msg_len its length
 * @param sig PROTOCORE_ED25519_SIG_LEN bytes, R || S
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_ed25519_verify(uint8_t *restrict work, const uint8_t *pub, const uint8_t *msg, size_t msg_len,
                                    const uint8_t *sig);

/** @brief Module namespace. */
PROTOCORE_NS Ed25519Ns Ed25519 PROTOCORE_UNUSED = {
    .pubkey = protocore_ed25519_pubkey, .sign = protocore_ed25519_sign, .verify = protocore_ed25519_verify};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ED25519_H
