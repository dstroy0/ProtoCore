// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_SNTRUP761_H
#define PROTOCORE_SNTRUP761_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file sntrup761.h
 * @brief Streamlined NTRU Prime sntrup761 KEM - keygen, encapsulation, decapsulation.
 *
 * The second post-quantum KEM OpenSSH ships (alongside ML-KEM-768), used by the
 * sntrup761x25519-sha512@openssh.com hybrid key exchange. Both KEM roles are provided:
 * - Encapsulation (SSH server / responder): given the peer's public key, produce a ciphertext
 * and a shared secret.
 * - KeyGen + Decapsulation (the reverse-SSH client / initiator): generate a keypair, send the
 * public key, then recover the shared secret from the server's ciphertext.
 *
 * Streamlined NTRU Prime, parameter set sntrup761 (p=761, q=4591, w=286): a lattice KEM over
 * the ring Z_q[x]/(x^761 - x - 1). The algorithm and the byte encodings match OpenSSH's embedded
 * sntrup761 reference (public domain; D. J. Bernstein et al.) so the ciphertext this produces
 * decapsulates byte-for-byte on a real OpenSSH peer. Zero heap; SHA-512 through @ref Sha512Ns,
 * randomness through protocore_rand_fill() (crypto/rng).
 *
 * @ref Sntrup761Ns::dec is implicit-rejection (FO): a ciphertext that fails the re-encrypt check
 * yields a deterministic pseudo-random secret rather than an error, so @ref Sntrup761Ns::ok is true
 * for any well-formed call.
 *
 * @c work is PROTOCORE_SNTRUP761_BORROW secure bytes the CALLER took, at an address it knows. It
 * arrives @c restrict and is not held past the call, so nothing here aliases it. The caller releases
 * it, and the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes
 * it. That is what keeps the hashed key material in it from outliving the caller.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#define PROTOCORE_SNTRUP761_PK_BYTES 1158 ///< public key (Rq-encoded h)
#define PROTOCORE_SNTRUP761_SK_BYTES 1763 ///< secret key (f, 1/g, pk, rho, cache)
#define PROTOCORE_SNTRUP761_CT_BYTES 1039 ///< ciphertext (Rounded-encoded c || 32-byte Confirm)
#define PROTOCORE_SNTRUP761_SS_BYTES 32   ///< shared secret (session key)
#define PROTOCORE_SNTRUP761_SK_PK_OFFSET                                                                               \
    382 ///< the public key is embedded in sk at this offset (2*Small_bytes); the KEM initiator reconstructs

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*keypair)(uint8_t *restrict, uint8_t *, uint8_t *);
    proto_bool (*enc)(uint8_t *restrict, const uint8_t *, uint8_t *, uint8_t *);
    proto_bool (*dec)(uint8_t *restrict, const uint8_t *, const uint8_t *, uint8_t *);
} Sntrup761Ns;
PROTOCORE_NS_LAYOUT(Sntrup761Ns, keypair, enc, dec);

/**
 * @brief Generate a keypair: send pk, hold sk until the peer's ciphertext arrives.
 * @param work PROTOCORE_SNTRUP761_BORROW bytes the caller took. Not held past the call.
 * @param pk PROTOCORE_SNTRUP761_PK_BYTES bytes
 * @param sk PROTOCORE_SNTRUP761_SK_BYTES bytes
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_sntrup761_keypair(uint8_t *restrict work, uint8_t *pk, uint8_t *sk);
/**
 * @brief Draw a short polynomial, encrypt it under pk, derive the session key.
 * @param work PROTOCORE_SNTRUP761_BORROW bytes the caller took. Not held past the call.
 * @param pk the peer's public key, PROTOCORE_SNTRUP761_PK_BYTES bytes
 * @param ct PROTOCORE_SNTRUP761_CT_BYTES bytes
 * @param ss PROTOCORE_SNTRUP761_SS_BYTES bytes
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_sntrup761_enc(uint8_t *restrict work, const uint8_t *pk, uint8_t *ct, uint8_t *ss);
/**
 * @brief Recover the session key from the peer's ciphertext under sk.
 * @param work PROTOCORE_SNTRUP761_BORROW bytes the caller took. Not held past the call.
 * @param sk this side's secret key, PROTOCORE_SNTRUP761_SK_BYTES bytes
 * @param ct the peer's ciphertext, PROTOCORE_SNTRUP761_CT_BYTES bytes
 * @param ss PROTOCORE_SNTRUP761_SS_BYTES bytes
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_sntrup761_dec(uint8_t *restrict work, const uint8_t *sk, const uint8_t *ct, uint8_t *ss);

/** @brief Module namespace. */
PROTOCORE_NS Sntrup761Ns Sntrup761 PROTOCORE_UNUSED = {
    .keypair = protocore_sntrup761_keypair, .enc = protocore_sntrup761_enc, .dec = protocore_sntrup761_dec};

PROTOCORE_END_DECLS

#endif // PROTOCORE_SNTRUP761_H
