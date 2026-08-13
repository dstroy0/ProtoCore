// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file float_bits.h
 * @brief A double read as the three fields it is: sign, exponent, mantissa.
 *
 *     63 62        52 51                                                  0
 *     +--+-----------+----------------------------------------------------+
 *     | s|  exponent |                      mantissa                      |
 *     +--+-----------+----------------------------------------------------+
 *
 * The module exports one symbol, @ref dbl. The field reads live in float_bits.c.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_FLOAT_BITS_H
#define PROTOCORE_FLOAT_BITS_H

#include "protocore_config.h" // proto_u64 - the integer one double's bits fill

PROTOCORE_BEGIN_DECLS

#define PROTO_DBL_SIGN_MASK 0x8000000000000000ull ///< bit 63
#define PROTO_DBL_EXP_MASK 0x7FF0000000000000ull  ///< bits 62..52
#define PROTO_DBL_MANT_MASK 0x000FFFFFFFFFFFFFull ///< bits 51..0
#define PROTO_DBL_SIGN_SHIFT 63u
#define PROTO_DBL_MANT_BITS 52u
#define PROTO_DBL_SIGN_ONE 0x1ull  ///< the sign field, shifted down
#define PROTO_DBL_EXP_ALL 0x7FFull ///< the exponent field, shifted down: all ones is an inf or a NaN
#define PROTO_DBL_BIAS 1023        ///< subtracted from the exponent field to get the power of two

/**
 * @brief The binary64 field module. Each read is one mask and one shift over the bits at the
 *        double's own address; each write is the mirror.
 *
 * @var DblNs::sign
 * The sign bit of @c v as 0 or 1.
 *
 * @var DblNs::exp
 * The exponent field of @c v, still biased. ::PROTO_DBL_BIAS comes off it for the power of two.
 *
 * @var DblNs::mant
 * The mantissa field of @c v, without the implicit leading bit a normal value carries.
 *
 * @var DblNs::merge
 * The three fields, each masked to its own width and shifted into place, ORed: the bits of the
 * double they describe. Bits above a field's width are dropped rather than carried into the next.
 *
 * @var DblNs::from_bits
 * The double those bits are.
 *
 * No storage member: every entry point works on the caller's value and holds nothing of its own.
 */
typedef struct
{
    proto_u64 (*sign)(double v);
    proto_u64 (*exp)(double v);
    proto_u64 (*mant)(double v);
    proto_u64 (*merge)(proto_u64 sign, proto_u64 exp, proto_u64 mant);
    double (*from_bits)(proto_u64 bits);
} DblNs;

// The field reads, in float_bits.c. Named here because the table below has to name them, and
// prefixed because that puts them in the linker's namespace.
proto_u64 proto_dbl_sign(double v);
proto_u64 proto_dbl_exp(double v);
proto_u64 proto_dbl_mant(double v);
proto_u64 proto_dbl_merge(proto_u64 sign, proto_u64 exp, proto_u64 mant);
double proto_dbl_from_bits(proto_u64 bits);

/**
 * @brief The names, aliased.
 *
 * `static const` and initialized here, not declared `extern` against a definition in the .c: a
 * translation unit that can see this initializer knows which function each member holds, so a member
 * read folds away and the call to the field read in float_bits.c is direct, leaving the table
 * referenced by nothing for the linker to drop.
 *
 * `unused` because this header is included by files that take none of it.
 */
static const DblNs dbl __attribute__((unused)) = {proto_dbl_sign, proto_dbl_exp, proto_dbl_mant, proto_dbl_merge,
                                                  proto_dbl_from_bits};

PROTOCORE_END_DECLS

#endif // PROTOCORE_FLOAT_BITS_H
