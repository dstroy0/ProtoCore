// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file fe25519.c
 * @brief The Fe25519 namespace over the GF(2^255-19) field layer (see fe25519.h).
 *
 * One arm: the field ops exist where the die carries a single-shot MODMULT, so the entries do too and
 * this translation unit is empty everywhere else. Each entry reads its operands off its args member and
 * runs the one op; the ops stay static inline in the header for the X25519 ladder and the Ed25519 point
 * arithmetic, which call them in their loops.
 *
 * No context and no offsets: nothing crosses a call, every element an entry touches is the caller's own,
 * and the borrow goes unread.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_FE25519

#include "crypto/asymmetric/fe25519/fe25519.h"
#include "crypto/crypto_opt.h"

PROTOCORE_CRYPTO_HOT
PROTOCORE_BEGIN_DECLS

#if PROTOCORE_FE25519_MPI_HW

// --- the entries -----------------------------------------------------------

// The lock and the power are the HAL's, so these two touch neither operands nor the borrow.
void protocore_fe25519_hw_enable(uint8_t *restrict work)
{
    (void)work;
    protocore_fe_hw_enable();
    Fe25519V.ok = PROTO_TRUE;
}

void protocore_fe25519_hw_disable(uint8_t *restrict work)
{
    (void)work;
    protocore_fe_hw_disable();
    Fe25519V.ok = PROTO_TRUE;
}

void protocore_fe25519_mul(uint8_t *restrict work)
{
    (void)work;
    Fe25519V.ok = PROTO_FALSE;
    if (!Fe25519V.mul_args.z || !Fe25519V.mul_args.x || !Fe25519V.mul_args.y)
    {
        return;
    }
    fe_mul(Fe25519V.mul_args.z, Fe25519V.mul_args.x, Fe25519V.mul_args.y);
    Fe25519V.ok = PROTO_TRUE;
}

void protocore_fe25519_sq(uint8_t *restrict work)
{
    (void)work;
    Fe25519V.ok = PROTO_FALSE;
    if (!Fe25519V.sq_args.o || !Fe25519V.sq_args.x)
    {
        return;
    }
    fe_sq(Fe25519V.sq_args.o, Fe25519V.sq_args.x);
    Fe25519V.ok = PROTO_TRUE;
}

void protocore_fe25519_copy(uint8_t *restrict work)
{
    (void)work;
    Fe25519V.ok = PROTO_FALSE;
    if (!Fe25519V.copy_args.o || !Fe25519V.copy_args.a)
    {
        return;
    }
    fe_copy(Fe25519V.copy_args.o, Fe25519V.copy_args.a);
    Fe25519V.ok = PROTO_TRUE;
}

void protocore_fe25519_zero(uint8_t *restrict work)
{
    (void)work;
    Fe25519V.ok = PROTO_FALSE;
    if (!Fe25519V.zero_args.o)
    {
        return;
    }
    fe_0(Fe25519V.zero_args.o);
    Fe25519V.ok = PROTO_TRUE;
}

void protocore_fe25519_one(uint8_t *restrict work)
{
    (void)work;
    Fe25519V.ok = PROTO_FALSE;
    if (!Fe25519V.one_args.o)
    {
        return;
    }
    fe_1(Fe25519V.one_args.o);
    Fe25519V.ok = PROTO_TRUE;
}

void protocore_fe25519_reduce_once(uint8_t *restrict work)
{
    (void)work;
    Fe25519V.ok = PROTO_FALSE;
    if (!Fe25519V.reduce_args.o)
    {
        return;
    }
    fe_reduce_once(Fe25519V.reduce_args.o);
    Fe25519V.ok = PROTO_TRUE;
}

void protocore_fe25519_add(uint8_t *restrict work)
{
    (void)work;
    Fe25519V.ok = PROTO_FALSE;
    if (!Fe25519V.add_args.o || !Fe25519V.add_args.x || !Fe25519V.add_args.y)
    {
        return;
    }
    fe_add(Fe25519V.add_args.o, Fe25519V.add_args.x, Fe25519V.add_args.y);
    Fe25519V.ok = PROTO_TRUE;
}

void protocore_fe25519_sub(uint8_t *restrict work)
{
    (void)work;
    Fe25519V.ok = PROTO_FALSE;
    if (!Fe25519V.sub_args.o || !Fe25519V.sub_args.x || !Fe25519V.sub_args.y)
    {
        return;
    }
    fe_sub(Fe25519V.sub_args.o, Fe25519V.sub_args.x, Fe25519V.sub_args.y);
    Fe25519V.ok = PROTO_TRUE;
}

void protocore_fe25519_cswap(uint8_t *restrict work)
{
    (void)work;
    Fe25519V.ok = PROTO_FALSE;
    if (!Fe25519V.cswap_args.x || !Fe25519V.cswap_args.y)
    {
        return;
    }
    fe_cswap(Fe25519V.cswap_args.x, Fe25519V.cswap_args.y, Fe25519V.cswap_args.swap);
    Fe25519V.ok = PROTO_TRUE;
}

void protocore_fe25519_frombytes(uint8_t *restrict work)
{
    (void)work;
    Fe25519V.ok = PROTO_FALSE;
    if (!Fe25519V.frombytes_args.o || !Fe25519V.frombytes_args.b)
    {
        return;
    }
    fe_frombytes(Fe25519V.frombytes_args.o, Fe25519V.frombytes_args.b);
    Fe25519V.ok = PROTO_TRUE;
}

void protocore_fe25519_tobytes(uint8_t *restrict work)
{
    (void)work;
    Fe25519V.ok = PROTO_FALSE;
    if (!Fe25519V.tobytes_args.b || !Fe25519V.tobytes_args.a)
    {
        return;
    }
    fe_tobytes(Fe25519V.tobytes_args.b, Fe25519V.tobytes_args.a);
    Fe25519V.ok = PROTO_TRUE;
}

void protocore_fe25519_invert(uint8_t *restrict work)
{
    (void)work;
    Fe25519V.ok = PROTO_FALSE;
    if (!Fe25519V.invert_args.o || !Fe25519V.invert_args.a)
    {
        return;
    }
    fe_invert(Fe25519V.invert_args.o, Fe25519V.invert_args.a);
    Fe25519V.ok = PROTO_TRUE;
}

void protocore_fe25519_pow2523(uint8_t *restrict work)
{
    (void)work;
    Fe25519V.ok = PROTO_FALSE;
    if (!Fe25519V.pow2523_args.o || !Fe25519V.pow2523_args.a)
    {
        return;
    }
    fe_pow2523(Fe25519V.pow2523_args.o, Fe25519V.pow2523_args.a);
    Fe25519V.ok = PROTO_TRUE;
}

// The low bit of the canonical encoding, onto the handle beside ok.
void protocore_fe25519_get_parity(uint8_t *restrict work)
{
    (void)work;
    Fe25519V.ok = PROTO_FALSE;
    Fe25519V.parity = 0;
    if (!Fe25519V.parity_args.a)
    {
        return;
    }
    Fe25519V.parity = fe_parity(Fe25519V.parity_args.a);
    Fe25519V.ok = PROTO_TRUE;
}

// 0 when the two encode the same element, -1 otherwise, onto the handle beside ok.
void protocore_fe25519_get_neq(uint8_t *restrict work)
{
    (void)work;
    Fe25519V.ok = PROTO_FALSE;
    Fe25519V.neq = -1;
    if (!Fe25519V.neq_args.a || !Fe25519V.neq_args.b)
    {
        return;
    }
    Fe25519V.neq = fe_neq(Fe25519V.neq_args.a, Fe25519V.neq_args.b);
    Fe25519V.ok = PROTO_TRUE;
}

/** @brief The operands and the outcome. */
Fe25519Vars Fe25519V;

#endif // PROTOCORE_FE25519_MPI_HW

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_FE25519
