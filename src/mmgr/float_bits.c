// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file float_bits.c
 * @brief The binary64 field reads - see float_bits.h.
 *
 * Every read takes the eight bytes at the double's address as one integer and cuts the wanted field
 * out of it with a mask and a shift. The merge runs the same arithmetic backwards, and from_bits
 * moves the bytes into a double.
 *
 * The one symbol this file exports is @ref dbl.
 */

#include "mmgr/float_bits.h"
#include "mmgr/rawmemcpy.h" // proto_raw_u64 / proto_raw_read - the bit read and the byte move

proto_u64 proto_dbl_sign(double v)
{
    return (proto_raw_u64(&v) & PROTO_DBL_SIGN_MASK) >> PROTO_DBL_SIGN_SHIFT;
}

proto_u64 proto_dbl_exp(double v)
{
    return (proto_raw_u64(&v) & PROTO_DBL_EXP_MASK) >> PROTO_DBL_MANT_BITS;
}

proto_u64 proto_dbl_mant(double v)
{
    return proto_raw_u64(&v) & PROTO_DBL_MANT_MASK;
}

// Each field is masked to its own width before it is shifted, so a bit above a field stays out of
// the one above it.
proto_u64 proto_dbl_merge(proto_u64 sign, proto_u64 exp, proto_u64 mant)
{
    return ((sign & PROTO_DBL_SIGN_ONE) << PROTO_DBL_SIGN_SHIFT) | ((exp & PROTO_DBL_EXP_ALL) << PROTO_DBL_MANT_BITS) |
           (mant & PROTO_DBL_MANT_MASK);
}

double proto_dbl_from_bits(proto_u64 bits)
{
    double v = 0.0;
    proto_raw_read(&v, &bits, sizeof(v));
    return v;
}
