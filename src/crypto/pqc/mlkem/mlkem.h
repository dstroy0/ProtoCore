// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_MLKEM_H
#define PROTOCORE_MLKEM_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file mlkem.h
 * @brief ML-KEM-768 (FIPS 203): KeyGen, Encaps (responder) and Decaps (initiator).
 *
 * The post-quantum half of the mlkem768x25519-sha256 (SSH) and X25519MLKEM768 (TLS 1.3) hybrid key
 * exchanges. Both KEM roles are present:
 * - responder (server terminating an inbound handshake): Encaps takes the peer's encapsulation key
 * and produces (ciphertext, shared secret);
 * - initiator (the device dialling out as an SSH/TLS *client*): KeyGen produces (ek, dk), the peer
 * Encaps against ek, and Decaps recovers the shared secret from the returned ciphertext.
 *
 * Decaps carries the full constant-time Fujisaki-Okamoto transform (re-encrypt m' under the embedded
 * ek and select the real key vs the implicit-reject key J(z || ct) under a constant-time ciphertext
 * compare), so a malformed or tampered ciphertext yields a pseudorandom secret rather than leaking a
 * decryption failure - FIPS 203 §6.3.
 *
 * KeyGen, Encaps and Decaps are the FIPS 203 "internal" (derandomized) forms: the caller supplies the
 * randomness (KeyGen's (d, z), Encaps's message m), drawn from the platform RNG in production and
 * fixed in known-answer tests. Deterministic given their inputs, which is exactly what the ACVP
 * keyGen / encapDecap vectors pin.
 *
 * Arithmetic is a software NTT over q=3329 with Montgomery reduction (the twiddle factors are fixed
 * constants premultiplied into Montgomery form, so each butterfly is two int16 multiplies and a
 * shift - no division, and the hardware MPI, which targets RSA/DH-sized operands, would only add
 * marshaling overhead). Zero heap; peak stack ~9 KB (Decaps, which re-encrypts).
 *
 * @ref MlKemNs::encaps runs the FIPS 203 modulus check on the peer key first: on a key whose decoded
 * coefficients are not all < q it writes nothing and leaves @ref MlKemNs::ok false.
 *
 * @ref MlKemNs::decaps has no failure of its own: a malformed or tampered ciphertext selects
 * J(z || ct) in constant time and the call still reports true.
 *
 * @c work is PROTOCORE_MLKEM_BORROW secure bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. The caller releases it, and
 * the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes it. That
 * is what keeps the seeds, the noise and the decrypted message from outliving the caller.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#define MLKEM768_EK_BYTES 1184 ///< encapsulation key (public key): 384*k + 32
#define MLKEM768_DK_BYTES 2400 ///< decapsulation key (private): 768*k + 96
#define MLKEM768_CT_BYTES 1088 ///< ciphertext: 32*(du*k + dv) = 32*(30+4)
#define MLKEM768_SS_BYTES 32   ///< shared secret
#define MLKEM768_MSG_BYTES 32  ///< the random message m fed to Encaps
#define MLKEM768_D_BYTES 32    ///< KeyGen seed d (K-PKE key material)
#define MLKEM768_Z_BYTES 32    ///< KeyGen seed z (implicit-reject value)

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*keygen)(uint8_t *restrict, const uint8_t *, const uint8_t *, uint8_t *, uint8_t *);
    proto_bool (*encaps)(uint8_t *restrict, const uint8_t *, const uint8_t *, uint8_t *, uint8_t *);
    proto_bool (*decaps)(uint8_t *restrict, const uint8_t *, const uint8_t *, uint8_t *);
} MlKemNs;
PROTOCORE_NS_LAYOUT(MlKemNs, keygen, encaps, decaps);

/**
 * @brief (ek, dk) from the two seeds, deterministic given them.
 * @param work PROTOCORE_ML_KEM_BORROW bytes the caller took. Not held past the call.
 * @param d MLKEM768_D_BYTES key-material seed
 * @param z MLKEM768_Z_BYTES implicit-reject seed
 * @param ek MLKEM768_EK_BYTES encapsulation key
 * @param dk MLKEM768_DK_BYTES decapsulation key, embedding ek, H(ek) and z
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_ml_kem_keygen(uint8_t *restrict work, const uint8_t *d, const uint8_t *z, uint8_t *ek,
                                   uint8_t *dk);
/**
 * @brief (ct, ss) from a peer key and a message.
 * @param work PROTOCORE_ML_KEM_BORROW bytes the caller took. Not held past the call.
 * @param ek MLKEM768_EK_BYTES peer encapsulation key
 * @param m MLKEM768_MSG_BYTES encapsulation randomness
 * @param ct MLKEM768_CT_BYTES ciphertext
 * @param ss MLKEM768_SS_BYTES shared secret
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_ml_kem_encaps(uint8_t *restrict work, const uint8_t *ek, const uint8_t *m, uint8_t *ct,
                                   uint8_t *ss);
/**
 * @brief The shared secret from a ciphertext, through the FO transform.
 * @param work PROTOCORE_ML_KEM_BORROW bytes the caller took. Not held past the call.
 * @param dk MLKEM768_DK_BYTES decapsulation key from a KeyGen
 * @param ct MLKEM768_CT_BYTES ciphertext from the peer's Encaps
 * @param ss MLKEM768_SS_BYTES shared secret
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_ml_kem_decaps(uint8_t *restrict work, const uint8_t *dk, const uint8_t *ct, uint8_t *ss);

/** @brief Module namespace. */
PROTOCORE_NS MlKemNs MlKem PROTOCORE_UNUSED = {
    .keygen = protocore_ml_kem_keygen, .encaps = protocore_ml_kem_encaps, .decaps = protocore_ml_kem_decaps};

PROTOCORE_END_DECLS

#endif // PROTOCORE_MLKEM_H
