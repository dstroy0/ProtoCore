// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file bignum.h
 * @brief 2048-bit big-integer arithmetic for DH-group14 and RSA-2048.
 *
 * A protocore_bignum is a fixed-width 2048-bit unsigned integer held as 64 little-endian 32-bit limbs,
 * so its size is the compile-time constant 256 and a DH scalar and an RSA key fragment are the same
 * type. The entries below read big-endian bytes in and write them back out, order two values, test one
 * for zero, check a received DH public value against RFC 4253 §8, and run the group-14 modular
 * exponentiation on whichever backend the vendor linked.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_BIGNUM_H
#define PROTOCORE_BIGNUM_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_BIGNUM

PROTOCORE_BEGIN_DECLS

// ---------------------------------------------------------------------------
// Fixed-width 2048-bit integer
// ---------------------------------------------------------------------------

/** @brief Number of 32-bit limbs in a 2048-bit integer. */
#define PROTOCORE_BN_LIMBS 64

/**
 * @brief A 2048-bit unsigned integer stored as 64 little-endian 32-bit limbs.
 *
 * d[0] = least significant 32 bits.
 * d[63] = most significant 32 bits.
 */
typedef struct
{
    uint32_t d[PROTOCORE_BN_LIMBS]; ///< 256 bytes of magnitude, little-endian limbs.
} protocore_bignum;

// ---------------------------------------------------------------------------
// Group-14 prime constant (exposed for key-derivation and validation)
// ---------------------------------------------------------------------------

/** @brief The RFC 3526 MODP group-14 prime (2048-bit). */
extern const protocore_bignum group14_p;

/** @brief Generator for group-14: g = 2. */
extern const protocore_bignum group14_g;

// PROTOCORE_BIGNUM_BORROW - the bytes a bignum call runs out of - is stated in protocore_config.h,
// which sums it into the secure arena. A caller takes them once and passes the pointer to every call.

// ---------------------------------------------------------------------------
// Backend-facing
// ---------------------------------------------------------------------------
//
// bn_expmod_group14() is DECLARED here and DEFINED by exactly one backend under test/core_setup/,
// chosen by the vendor's PROTOCORE_HAS_HW_BIGNUM. There is no weak default: link no backend and this is an
// undefined reference; link two and it is a duplicate definition. Software crypto is a legitimate
// choice - on some parts the only one - but it is always a stated one, never a fallback.

/** @brief Compare two @p n-limb magnitudes: -1, 0 or 1. Shared with the backends. */
int bn_cmp_raw(const uint32_t *a, const uint32_t *b, int n);

void bn_expmod_group14(protocore_bignum *out, const protocore_bignum *base, const protocore_bignum *exp);

/** @brief The big-endian bytes read into a bignum. */
typedef struct
{
    protocore_bignum *out; ///< destination
    const uint8_t *bytes;  ///< big-endian source bytes
    size_t len;            ///< how many; a shorter source zeroes the top limbs, a longer one keeps its low 256
} BignumFromBytesArgs;

/** @brief Where a bignum lands as big-endian bytes. */
typedef struct
{
    uint8_t *bytes;             ///< exactly 256 bytes
    const protocore_bignum *in; ///< the value written
} BignumToBytesArgs;

/** @brief The two values a compare orders. */
typedef struct
{
    const protocore_bignum *a; ///< left
    const protocore_bignum *b; ///< right
} BignumCmpArgs;

/** @brief The two magnitudes a raw compare orders. */
typedef struct
{
    const uint32_t *a; ///< left limbs, little-endian
    const uint32_t *b; ///< right limbs, little-endian
    int n;             ///< limbs spanned
} BignumCmpRawArgs;

/** @brief The value tested for zero. */
typedef struct
{
    const protocore_bignum *a; ///< the value
} BignumIsZeroArgs;

/** @brief The operands of a group-14 modular exponentiation. */
typedef struct
{
    protocore_bignum *out;        ///< base^exp mod group14_p
    const protocore_bignum *base; ///< the base, 1 < base < p-1
    const protocore_bignum *exp;  ///< the exponent, e.g. the 2048-bit private DH scalar y
} BignumExpmodArgs;

/** @brief The received DH public value a validation checks. */
typedef struct
{
    const protocore_bignum *v; ///< the received e or f
} BignumValidateArgs;

/**
 * @brief 2048-bit big-integer arithmetic (RFC 3526 group-14).
 *
 * A caller sets the members a call takes, invokes it through ::Bignum with the bytes it runs out of,
 * and reads the outcome off the same handle. How those bytes are carved is this module's and is never
 * named here.
 *
 *   Bignum.from_bytes_args.out = &e;
 *   Bignum.from_bytes_args.bytes = e_be;
 *   Bignum.from_bytes_args.len = 256;
 *   Bignum.from_bytes(work);
 *   Bignum.validate_args.v = &e;
 *   Bignum.dh_validate(work);
 *   Bignum.expmod_args.out = &K;
 *   Bignum.expmod_args.base = &e;
 *   Bignum.expmod_args.exp = &y;
 *   Bignum.expmod_group14(work);
 *
 * @var BignumNs::from_bytes_args  the big-endian bytes read into a bignum
 * @var BignumNs::to_bytes_args    where a bignum lands as big-endian bytes
 * @var BignumNs::cmp_args         the two values a compare orders
 * @var BignumNs::cmp_raw_args     the two magnitudes a raw compare orders
 * @var BignumNs::is_zero_args     the value tested for zero
 * @var BignumNs::expmod_args      the operands of a group-14 modular exponentiation
 * @var BignumNs::validate_args    the received DH public value a validation checks
 * @var BignumNs::ok               a call's true/false outcome
 * @var BignumNs::sign             the sign of a - b the last @ref BignumNs::cmp or @ref BignumNs::cmp_raw
 *                                 left: -1, 0 or 1
 * @var BignumNs::zero             whether the last @ref BignumNs::is_zero found every limb zero
 * @var BignumNs::from_bytes       read a big-endian byte array into a bignum
 * @var BignumNs::to_bytes         write a bignum as 256 big-endian bytes
 * @var BignumNs::cmp              order two bignums over all 64 limbs
 * @var BignumNs::cmp_raw          order two magnitudes over the stated limb count
 * @var BignumNs::is_zero          test every limb for zero
 * @var BignumNs::expmod_group14   out = base^exp mod group14_p, on the linked backend
 * @var BignumNs::dh_validate      RFC 4253 §8: @ref BignumNs::ok is true when 1 < v < p-1
 *
 * @c work is PROTOCORE_BIGNUM_BORROW secure bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. The caller releases it, and
 * the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes it. The DH
 * exponent and the shared secret pass through those bytes, so they die with the release rather than on
 * the stack. Two callers are two borrows and never collide.
 *
 * No storage member and no context: a caller sets operands and reads @ref BignumNs::ok, and that is
 * all the surface there is.
 */
typedef struct
{
    BignumFromBytesArgs from_bytes_args;
    BignumToBytesArgs to_bytes_args;
    BignumCmpArgs cmp_args;
    BignumCmpRawArgs cmp_raw_args;
    BignumIsZeroArgs is_zero_args;
    BignumExpmodArgs expmod_args;
    BignumValidateArgs validate_args;
    proto_bool ok;
    int sign;
    proto_bool zero;
} BignumVars;

/** @brief The operands and the outcome. */
extern BignumVars BignumV;

/** @brief The entries. */
typedef struct
{
    void (*const from_bytes)(uint8_t *restrict work);
    void (*const to_bytes)(uint8_t *restrict work);
    void (*const cmp)(uint8_t *restrict work);
    void (*const cmp_raw)(uint8_t *restrict work);
    void (*const is_zero)(uint8_t *restrict work);
    void (*const expmod_group14)(uint8_t *restrict work);
    void (*const dh_validate)(uint8_t *restrict work);
} BignumNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in BignumV or a region of the borrow at a fixed offset.
void protocore_bignum_from_bytes(uint8_t *restrict work);
void protocore_bignum_to_bytes(uint8_t *restrict work);
void protocore_bignum_cmp(uint8_t *restrict work);
void protocore_bignum_cmp_raw(uint8_t *restrict work);
void protocore_bignum_is_zero(uint8_t *restrict work);
void protocore_bignum_expmod_group14(uint8_t *restrict work);
void protocore_bignum_dh_validate(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Bignum.from_bytes(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const BignumNs Bignum __attribute__((unused)) = {
    .from_bytes = protocore_bignum_from_bytes,
    .to_bytes = protocore_bignum_to_bytes,
    .cmp = protocore_bignum_cmp,
    .cmp_raw = protocore_bignum_cmp_raw,
    .is_zero = protocore_bignum_is_zero,
    .expmod_group14 = protocore_bignum_expmod_group14,
    .dh_validate = protocore_bignum_dh_validate,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_BIGNUM

#endif // PROTOCORE_BIGNUM_H
