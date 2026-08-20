// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file sha3.h
 * @brief Keccak-f[1600] sponge: SHA3-256, SHA3-512, SHAKE128, SHAKE256 (FIPS 202).
 *
 * The symmetric primitives ML-KEM (FIPS 203) is built on: G = SHA3-512, H = SHA3-256, the matrix
 * XOF = SHAKE128, and the noise PRF = SHAKE256. Zero-heap, endian-independent (the sponge state is
 * addressed as a little-endian byte string regardless of host byte order), no external dependency.
 *
 * One-shot entries cover fixed-length digests and arbitrary SHAKE output. For an incremental XOF
 * (ML-KEM samples the public matrix by squeezing three bytes at a time) absorb once with
 * @ref Sha3Ns::shake128_absorb then pull with @ref Sha3Ns::squeeze as many times as needed.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SHA3_H
#define PROTOCORE_SHA3_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_SHA3

PROTOCORE_BEGIN_DECLS

/// Sponge rates (block size in octets = 1600/8 - 2*capacity/8) for the modes we use.
#define KECCAK_RATE_SHA3_256 136
#define KECCAK_RATE_SHA3_512 72
#define KECCAK_RATE_SHAKE128 168
#define KECCAK_RATE_SHAKE256 136

/** @brief The message a raw sponge absorbs, at a stated rate and domain. */
typedef struct
{
    uint32_t rate;     ///< sponge rate in octets
    const uint8_t *in; ///< the message
    size_t inlen;      ///< its length
    uint8_t domain;    ///< domain-separation byte (0x06 SHA3, 0x1F SHAKE)
} Sha3AbsorbArgs;
/** @brief Where squeezed octets land. */
typedef struct
{
    uint8_t *out;  ///< the output buffer
    size_t outlen; ///< how many octets to pull
} Sha3SqueezeArgs;
/** @brief The message a fixed-length digest is taken over. */
typedef struct
{
    uint8_t *out;      ///< 32 octets for SHA3-256, 64 for SHA3-512
    const uint8_t *in; ///< the message
    size_t inlen;      ///< its length
} Sha3DigestArgs;
/** @brief The message a one-shot XOF is taken over, and how much output it yields. */
typedef struct
{
    uint8_t *out;      ///< the output buffer
    size_t outlen;     ///< how many octets to produce
    const uint8_t *in; ///< the message
    size_t inlen;      ///< its length
} Sha3XofArgs;
/** @brief The message an incremental SHAKE128 XOF absorbs. */
typedef struct
{
    const uint8_t *in; ///< the message
    size_t inlen;      ///< its length
} Sha3Shake128AbsorbArgs;
// PROTOCORE_SHA3_BORROW - the bytes a sponge runs out of - is stated in protocore_config.h, which sums
// it into the secure arena. A caller takes them once and passes the pointer to every call.
/**
 * @brief SHA3-256 / SHA3-512 / SHAKE128 / SHAKE256 (FIPS 202).
 *
 * A caller sets the members a call takes, invokes it through ::Sha3 with the bytes it runs out of, and
 * reads the outcome off the same handle. How those bytes are carved is this module's and is never
 * named here.
 *
 * The incremental XOF ML-KEM samples its matrix with:
 *
 *   Sha3.shake128_absorb_args.in = seed;
 *   Sha3.shake128_absorb_args.inlen = sizeof(seed);
 *   Sha3.shake128_absorb(work);
 *   Sha3.squeeze_args.out = buf;
 *   Sha3.squeeze_args.outlen = sizeof(buf);
 *   Sha3.squeeze(work);
 *
 * @var Sha3Ns::absorb_args           the message a raw sponge absorbs, at a stated rate and domain
 * @var Sha3Ns::squeeze_args          where squeezed octets land
 * @var Sha3Ns::digest_args           the message a fixed-length digest is taken over
 * @var Sha3Ns::xof_args              the message a one-shot XOF is taken over
 * @var Sha3Ns::shake128_absorb_args  the message an incremental SHAKE128 XOF absorbs
 * @var Sha3Ns::ok                    a call's true/false outcome
 * @var Sha3Ns::absorb                absorb the whole message, pad, leave the sponge ready to squeeze
 * @var Sha3Ns::squeeze               pull octets, permuting between blocks; repeatable for XOF use
 * @var Sha3Ns::sha3_256              SHA3-256 one-shot, 32 octets out
 * @var Sha3Ns::sha3_512              SHA3-512 one-shot, 64 octets out
 * @var Sha3Ns::shake128              SHAKE128 one-shot
 * @var Sha3Ns::shake256              SHAKE256 one-shot
 * @var Sha3Ns::shake128_absorb       begin an incremental SHAKE128 XOF
 *
 * @c work is PROTOCORE_SHA3_BORROW secure bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. The caller releases it, and
 * the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes it.
 *
 * The borrow IS the sponge, and everything carried call to call lives in it. A digest taken in its own
 * borrow therefore leaves an incremental XOF running in another exactly where it was.
 *
 * No storage member and no context: a caller sets operands and reads @ref Sha3Ns::ok, and that is
 * all the surface there is.
 */
typedef struct
{
    Sha3AbsorbArgs absorb_args;
    Sha3SqueezeArgs squeeze_args;
    Sha3DigestArgs digest_args;
    Sha3XofArgs xof_args;
    Sha3Shake128AbsorbArgs shake128_absorb_args;
    proto_bool ok;
} Sha3Vars;

/** @brief The operands and the outcome. */
extern Sha3Vars Sha3V;

/** @brief The entries. */
typedef struct
{
    void (*const absorb)(uint8_t *restrict work);
    void (*const squeeze)(uint8_t *restrict work);
    void (*const sha3_256)(uint8_t *restrict work);
    void (*const sha3_512)(uint8_t *restrict work);
    void (*const shake128)(uint8_t *restrict work);
    void (*const shake256)(uint8_t *restrict work);
    void (*const shake128_absorb)(uint8_t *restrict work);
} Sha3Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Sha3V or a region of the borrow at a fixed offset.
void protocore_sha3_absorb(uint8_t *restrict work);
void protocore_sha3_squeeze(uint8_t *restrict work);
void protocore_sha3_sha3_256(uint8_t *restrict work);
void protocore_sha3_sha3_512(uint8_t *restrict work);
void protocore_sha3_shake128(uint8_t *restrict work);
void protocore_sha3_shake256(uint8_t *restrict work);
void protocore_sha3_shake128_absorb(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Sha3.absorb(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Sha3Ns Sha3 __attribute__((unused)) = {
    .absorb = protocore_sha3_absorb,
    .squeeze = protocore_sha3_squeeze,
    .sha3_256 = protocore_sha3_sha3_256,
    .sha3_512 = protocore_sha3_sha3_512,
    .shake128 = protocore_sha3_shake128,
    .shake256 = protocore_sha3_shake256,
    .shake128_absorb = protocore_sha3_shake128_absorb,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SHA3

#endif // PROTOCORE_SHA3_H
