// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mlkem.h
 * @brief ML-KEM-768 (FIPS 203): Encaps (responder) + KeyGen and Decaps (initiator).
 *
 * The post-quantum half of the mlkem768x25519-sha256 (SSH) and X25519MLKEM768 (TLS 1.3) hybrid key
 * exchanges. Both KEM roles are present:
 *   - responder (server terminating an inbound handshake): Encaps takes the peer's encapsulation key
 *     and produces (ciphertext, shared secret);
 *   - initiator (the device dialling out as an SSH/TLS *client*): KeyGen produces (ek, dk), the peer
 *     Encaps against ek, and Decaps recovers the shared secret from the returned ciphertext.
 *
 * Decaps carries the full constant-time Fujisaki-Okamoto transform (re-encrypt m' under the embedded
 * ek and select the real key vs the implicit-reject key J(z || ct) under a constant-time ciphertext
 * compare), so a malformed or tampered ciphertext yields a pseudorandom secret rather than leaking a
 * decryption failure - FIPS 203 §6.3.
 *
 * KeyGen, Encaps and Decaps are the FIPS 203 "internal" (derandomized) forms: the caller supplies the
 * randomness (KeyGen's (d, z), Encaps's message @p m), drawn from the platform RNG in production and
 * fixed in known-answer tests. Deterministic given their inputs, which is exactly what the ACVP
 * keyGen / encapDecap vectors pin.
 *
 * Arithmetic is a software NTT over q=3329 with Montgomery reduction (the twiddle factors are fixed
 * constants premultiplied into Montgomery form, so each butterfly is two int16 multiplies and a
 * shift - no division, and the hardware MPI, which targets RSA/DH-sized operands, would only add
 * marshaling overhead). Zero heap; peak stack ~9 KB (Decaps, which re-encrypts).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MLKEM_H
#define PROTOCORE_MLKEM_H

#include "protocore_config.h"

#if PC_ENABLE_PQC_KEX

#define MLKEM768_EK_BYTES 1184 ///< encapsulation key (public key): 384*k + 32
#define MLKEM768_DK_BYTES 2400 ///< decapsulation key (private): 768*k + 96
#define MLKEM768_CT_BYTES 1088 ///< ciphertext: 32*(du*k + dv) = 32*(30+4)
#define MLKEM768_SS_BYTES 32   ///< shared secret
#define MLKEM768_MSG_BYTES 32  ///< the random message m fed to Encaps
#define MLKEM768_D_BYTES 32    ///< KeyGen seed d (K-PKE key material)
#define MLKEM768_Z_BYTES 32    ///< KeyGen seed z (implicit-reject value)

/**
 * @brief ML-KEM-768 KeyGen (FIPS 203, derandomized): (ek, dk) from the two 32-octet seeds.
 *
 * The initiator's step: publish @p ek (the peer Encaps against it) and keep @p dk for Decaps. @p dk
 * embeds @p ek, H(ek) and @p z, so Decaps needs no other state. Deterministic given (@p d, @p z).
 *
 * @param[in]  d   32-octet key-material seed.
 * @param[in]  z   32-octet implicit-reject seed.
 * @param[out] ek  encapsulation key (MLKEM768_EK_BYTES).
 * @param[out] dk  decapsulation key (MLKEM768_DK_BYTES).
 */
void pc_mlkem768_keygen(const uint8_t d[MLKEM768_D_BYTES], const uint8_t z[MLKEM768_Z_BYTES],
                        uint8_t ek[MLKEM768_EK_BYTES], uint8_t dk[MLKEM768_DK_BYTES]);

/**
 * @brief ML-KEM-768 Encaps (FIPS 203, derandomized): (ct, ss) from an encapsulation key and message.
 *
 * Validates @p ek (FIPS 203 modulus check: every decoded coefficient must be < q); on a malformed key
 * it writes nothing and returns false. Otherwise derives K = ss and encrypts @p m under @p ek.
 *
 * @param[in]  ek  peer encapsulation key (MLKEM768_EK_BYTES).
 * @param[in]  m   32-octet message (the encapsulation randomness).
 * @param[out] ct  ciphertext (MLKEM768_CT_BYTES).
 * @param[out] ss  32-octet shared secret.
 * @return true on success, false if @p ek fails the modulus check.
 */
proto_bool pc_mlkem768_encaps(const uint8_t ek[MLKEM768_EK_BYTES], const uint8_t m[MLKEM768_MSG_BYTES],
                              uint8_t ct[MLKEM768_CT_BYTES], uint8_t ss[MLKEM768_SS_BYTES]);

/**
 * @brief ML-KEM-768 Decaps (FIPS 203, §6.3): recover the shared secret from a ciphertext.
 *
 * Runs the full constant-time Fujisaki-Okamoto transform: decrypt @p ct to m', re-derive (K', r') and
 * re-encrypt under the ek embedded in @p dk, then in constant time return K' if the recomputed
 * ciphertext matches @p ct or the implicit-reject key J(z || ct) if it does not. Never fails - a bad
 * ciphertext yields a pseudorandom secret, not an error - so there is no boolean return.
 *
 * @param[in]  dk  decapsulation key (MLKEM768_DK_BYTES) from pc_mlkem768_keygen().
 * @param[in]  ct  ciphertext (MLKEM768_CT_BYTES) from the peer's Encaps.
 * @param[out] ss  32-octet shared secret.
 */
void pc_mlkem768_decaps(const uint8_t dk[MLKEM768_DK_BYTES], const uint8_t ct[MLKEM768_CT_BYTES],
                        uint8_t ss[MLKEM768_SS_BYTES]);

#endif // PC_ENABLE_PQC_KEX

#endif // PROTOCORE_MLKEM_H
