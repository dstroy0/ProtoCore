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
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_FLOAT_BITS_H
#define PROTOCORE_FLOAT_BITS_H

#include "mmgr/rawmemcpy.h" // proto_raw_u64 - the one spelling of a bit read at a pointer

PROTO_BEGIN_DECLS

#define PROTO_DBL_SIGN_MASK 0x8000000000000000ull ///< bit 63
#define PROTO_DBL_EXP_MASK 0x7FF0000000000000ull  ///< bits 62..52
#define PROTO_DBL_MANT_MASK 0x000FFFFFFFFFFFFFull ///< bits 51..0
#define PROTO_DBL_SIGN_SHIFT 63u
#define PROTO_DBL_MANT_BITS 52u
#define PROTO_DBL_SIGN_ONE 0x1ull  ///< the sign field, shifted down
#define PROTO_DBL_EXP_ALL 0x7FFull ///< the exponent field, shifted down: all ones is an inf or a NaN
#define PROTO_DBL_BIAS 1023        ///< subtracted from the exponent field to get the power of two

/** @brief The sign bit of @p v as 0 or 1. */
PC_INLINE proto_u64 proto_dbl_sign(double v)
{
    return (proto_raw_u64(&v) & PROTO_DBL_SIGN_MASK) >> PROTO_DBL_SIGN_SHIFT;
}

/** @brief The exponent field of @p v, still biased. */
PC_INLINE proto_u64 proto_dbl_exp(double v)
{
    return (proto_raw_u64(&v) & PROTO_DBL_EXP_MASK) >> PROTO_DBL_MANT_BITS;
}

/** @brief The mantissa field of @p v. */
PC_INLINE proto_u64 proto_dbl_mant(double v)
{
    return proto_raw_u64(&v) & PROTO_DBL_MANT_MASK;
}

/** @brief The three fields shifted into place and merged: the bits of the double they describe. */
PC_INLINE proto_u64 proto_dbl_merge(proto_u64 sign, proto_u64 exp, proto_u64 mant)
{
    return ((sign & PROTO_DBL_SIGN_ONE) << PROTO_DBL_SIGN_SHIFT) | ((exp & PROTO_DBL_EXP_ALL) << PROTO_DBL_MANT_BITS) |
           (mant & PROTO_DBL_MANT_MASK);
}

/** @brief The double those bits are. */
PC_INLINE double proto_dbl_from_bits(proto_u64 bits)
{
    double v = 0.0;
    proto_raw_read(&v, &bits, sizeof(v));
    return v;
}

PROTO_END_DECLS

#endif // PROTOCORE_FLOAT_BITS_H
