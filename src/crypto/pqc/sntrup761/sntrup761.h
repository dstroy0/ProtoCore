// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sntrup761.h
 * @brief Streamlined NTRU Prime sntrup761 KEM - keygen, encapsulation, decapsulation.
 *
 * The second post-quantum KEM OpenSSH ships (alongside ML-KEM-768), used by the
 * sntrup761x25519-sha512@openssh.com hybrid key exchange. Both KEM roles are provided:
 *   - Encapsulation (SSH server / responder): given the peer's public key, produce a ciphertext
 *     and a shared secret.
 *   - KeyGen + Decapsulation (the reverse-SSH client / initiator): generate a keypair, send the
 *     public key, then recover the shared secret from the server's ciphertext.
 *
 * Streamlined NTRU Prime, parameter set sntrup761 (p=761, q=4591, w=286): a lattice KEM over
 * the ring Z_q[x]/(x^761 - x - 1). The algorithm and the byte encodings match OpenSSH's embedded
 * sntrup761 reference (public domain; D. J. Bernstein et al.) so the ciphertext this produces
 * decapsulates byte-for-byte on a real OpenSSH peer. Zero heap; SHA-512 through @ref Sha512Ns,
 * randomness through protocore_rand_fill() (crypto/rng).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SNTRUP761_H
#define PROTOCORE_SNTRUP761_H

#include "protocore_config.h" // the entry point: protocore_types.h for proto_bool and the widths

#if PROTOCORE_ENABLE_SNTRUP761

PROTOCORE_BEGIN_DECLS

#define PROTOCORE_SNTRUP761_PK_BYTES 1158 ///< public key (Rq-encoded h)
#define PROTOCORE_SNTRUP761_SK_BYTES 1763 ///< secret key (f, 1/g, pk, rho, cache)
#define PROTOCORE_SNTRUP761_CT_BYTES 1039 ///< ciphertext (Rounded-encoded c || 32-byte Confirm)
#define PROTOCORE_SNTRUP761_SS_BYTES 32   ///< shared secret (session key)
#define PROTOCORE_SNTRUP761_SK_PK_OFFSET                                                                               \
    382 ///< the public key is embedded in sk at this offset (2*Small_bytes); the KEM initiator reconstructs
        ///< its C_INIT and the exchange hash from sk rather than storing pk twice

// PROTOCORE_SNTRUP761_BORROW - the bytes one KEM operation runs out of - is stated in
// protocore_config.h, which sums it into the secure arena. A caller takes them once and passes the
// pointer to every call.

/** @brief Where a generated keypair lands. */
typedef struct
{
    uint8_t *pk; ///< PROTOCORE_SNTRUP761_PK_BYTES bytes
    uint8_t *sk; ///< PROTOCORE_SNTRUP761_SK_BYTES bytes
} Sntrup761KeypairArgs;

/** @brief The public key an encapsulation runs against, and where its two outputs land. */
typedef struct
{
    const uint8_t *pk; ///< the peer's public key, PROTOCORE_SNTRUP761_PK_BYTES bytes
    uint8_t *ct;       ///< PROTOCORE_SNTRUP761_CT_BYTES bytes
    uint8_t *ss;       ///< PROTOCORE_SNTRUP761_SS_BYTES bytes
} Sntrup761EncArgs;

/** @brief The secret key and ciphertext a decapsulation recovers a shared secret from. */
typedef struct
{
    const uint8_t *sk; ///< this side's secret key, PROTOCORE_SNTRUP761_SK_BYTES bytes
    const uint8_t *ct; ///< the peer's ciphertext, PROTOCORE_SNTRUP761_CT_BYTES bytes
    uint8_t *ss;       ///< PROTOCORE_SNTRUP761_SS_BYTES bytes
} Sntrup761DecArgs;

/**
 * @brief Streamlined NTRU Prime sntrup761 (p=761, q=4591, w=286).
 *
 * A caller sets the members a call takes, invokes it through ::Sntrup761 with the bytes it runs out
 * of, and reads the outcome off the same handle. How those bytes are carved is this module's and is
 * never named here.
 *
 *   Sntrup761.enc_args.pk = peer_pk;
 *   Sntrup761.enc_args.ct = ciphertext;
 *   Sntrup761.enc_args.ss = shared;
 *   Sntrup761.enc(work);
 *
 * @var Sntrup761Ns::keypair_args  where a generated keypair lands
 * @var Sntrup761Ns::enc_args      the public key an encapsulation runs against, and its two outputs
 * @var Sntrup761Ns::dec_args      the secret key and ciphertext a decapsulation recovers a secret from
 * @var Sntrup761Ns::ok            a call's true/false outcome
 * @var Sntrup761Ns::keypair       generate a keypair: send pk, hold sk until the peer's ciphertext arrives
 * @var Sntrup761Ns::enc           draw a short polynomial, encrypt it under pk, derive the session key
 * @var Sntrup761Ns::dec           recover the session key from the peer's ciphertext under sk
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
 * No storage member and no context: a caller sets operands and reads @ref Sntrup761Ns::ok, and that
 * is all the surface there is.
 */
typedef struct
{
    Sntrup761KeypairArgs keypair_args;
    Sntrup761EncArgs enc_args;
    Sntrup761DecArgs dec_args;

    proto_bool ok;

    void (*const keypair)(uint8_t *restrict work);
    void (*const enc)(uint8_t *restrict work);
    void (*const dec)(uint8_t *restrict work);
} Sntrup761Ns;

/** @brief The one symbol this module exports. */
extern Sntrup761Ns Sntrup761;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SNTRUP761

#endif // PROTOCORE_SNTRUP761_H
