// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mlkem.h
 * @brief ML-KEM-768 (FIPS 203): KeyGen, Encaps (responder) and Decaps (initiator).
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
 * randomness (KeyGen's (d, z), Encaps's message m), drawn from the platform RNG in production and
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_MLKEM

PROTOCORE_BEGIN_DECLS

#define MLKEM768_EK_BYTES 1184 ///< encapsulation key (public key): 384*k + 32
#define MLKEM768_DK_BYTES 2400 ///< decapsulation key (private): 768*k + 96
#define MLKEM768_CT_BYTES 1088 ///< ciphertext: 32*(du*k + dv) = 32*(30+4)
#define MLKEM768_SS_BYTES 32   ///< shared secret
#define MLKEM768_MSG_BYTES 32  ///< the random message m fed to Encaps
#define MLKEM768_D_BYTES 32    ///< KeyGen seed d (K-PKE key material)
#define MLKEM768_Z_BYTES 32    ///< KeyGen seed z (implicit-reject value)

// PROTOCORE_MLKEM_BORROW - the bytes a KEM operation runs out of - is stated in protocore_config.h,
// which sums it into the secure arena. A caller takes them once and passes the pointer to every call.

/** @brief The two seeds a KeyGen runs from, and where the key pair lands. */
typedef struct
{
    const uint8_t *d; ///< MLKEM768_D_BYTES key-material seed
    const uint8_t *z; ///< MLKEM768_Z_BYTES implicit-reject seed
    uint8_t *ek;      ///< MLKEM768_EK_BYTES encapsulation key
    uint8_t *dk;      ///< MLKEM768_DK_BYTES decapsulation key, embedding ek, H(ek) and z
} MlKemKeygenArgs;

/** @brief The peer key and message an Encaps runs on, and where its outputs land. */
typedef struct
{
    const uint8_t *ek; ///< MLKEM768_EK_BYTES peer encapsulation key
    const uint8_t *m;  ///< MLKEM768_MSG_BYTES encapsulation randomness
    uint8_t *ct;       ///< MLKEM768_CT_BYTES ciphertext
    uint8_t *ss;       ///< MLKEM768_SS_BYTES shared secret
} MlKemEncapsArgs;

/** @brief The key and ciphertext a Decaps runs on, and where the secret lands. */
typedef struct
{
    const uint8_t *dk; ///< MLKEM768_DK_BYTES decapsulation key from a KeyGen
    const uint8_t *ct; ///< MLKEM768_CT_BYTES ciphertext from the peer's Encaps
    uint8_t *ss;       ///< MLKEM768_SS_BYTES shared secret
} MlKemDecapsArgs;

/**
 * @brief ML-KEM-768 (FIPS 203).
 *
 * A caller sets the members a call takes, invokes it through ::MlKem with the bytes it runs out of,
 * and reads the outcome off the same handle. How those bytes are carved is this module's and is never
 * named here.
 *
 *   MlKem.encaps_args.ek = peer_ek;
 *   MlKem.encaps_args.m = m;
 *   MlKem.encaps_args.ct = ct;
 *   MlKem.encaps_args.ss = ss;
 *   MlKem.encaps(work);
 *
 * @var MlKemNs::keygen_args  the two seeds a KeyGen runs from, and where the key pair lands
 * @var MlKemNs::encaps_args  the peer key and message an Encaps runs on, and where its outputs land
 * @var MlKemNs::decaps_args  the key and ciphertext a Decaps runs on, and where the secret lands
 * @var MlKemNs::ok           a call's true/false outcome
 * @var MlKemNs::keygen       (ek, dk) from the two seeds, deterministic given them
 * @var MlKemNs::encaps       (ct, ss) from a peer key and a message
 * @var MlKemNs::decaps       the shared secret from a ciphertext, through the FO transform
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
 * No storage member and no context: a caller sets operands and reads @ref MlKemNs::ok, and that is
 * all the surface there is.
 */
typedef struct
{
    MlKemKeygenArgs keygen_args;
    MlKemEncapsArgs encaps_args;
    MlKemDecapsArgs decaps_args;
    proto_bool ok;
} MlKemVars;

/** @brief The operands and the outcome. */
extern MlKemVars MlKemV;

/** @brief The entries. */
typedef struct
{
    void (*const keygen)(uint8_t *restrict work);
    void (*const encaps)(uint8_t *restrict work);
    void (*const decaps)(uint8_t *restrict work);
} MlKemNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in MlKemV or a region of the borrow at a fixed offset.
void protocore_ml_kem_keygen(uint8_t *restrict work);
void protocore_ml_kem_encaps(uint8_t *restrict work);
void protocore_ml_kem_decaps(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `MlKem.keygen(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const MlKemNs MlKem __attribute__((unused)) = {
    .keygen = protocore_ml_kem_keygen,
    .encaps = protocore_ml_kem_encaps,
    .decaps = protocore_ml_kem_decaps,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_MLKEM

#endif // PROTOCORE_MLKEM_H
