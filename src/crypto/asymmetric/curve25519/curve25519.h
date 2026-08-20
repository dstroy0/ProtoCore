// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file curve25519.h
 * @brief Curve25519 field arithmetic + X25519 (RFC 7748) for the curve25519-sha256 KEX.
 *
 * Field elements are GF(2^255 - 19) in a portable radix-2^16 representation (sixteen
 * int64 limbs), so no 128-bit integer type is needed - important because 32-bit xtensa
 * (ESP32) gcc has no __int128. The same field arithmetic backs Ed25519 (protocore_ed25519),
 * so the field ops are exported here. Correctness is pinned to the RFC 7748 §5.2 test
 * vectors (test_ed25519).
 *
 * Two surfaces, and they are not the same kind of thing: the field ops below are shared internals that
 * Ed25519 links against directly, and X25519 is the namespace this module exports.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CURVE25519_H
#define PROTOCORE_CURVE25519_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths and PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_CURVE25519

PROTOCORE_BEGIN_DECLS

// --- Field arithmetic (shared with protocore_ed25519) ----------------------------
//
// Not the namespace: these are the field layer X25519 below and Ed25519 are both written in. Each one
// carries nothing across a call and takes its operands directly, so none of them needs a borrow.

/** @brief A field element of GF(2^255 - 19): 16 limbs, radix 2^16 (limb i weighs 2^(16i)). */
typedef int64_t protocore_gf[16];

void protocore_gf_copy(protocore_gf out, const protocore_gf in);                     ///< out = in
void protocore_gf_add(protocore_gf out, const protocore_gf a, const protocore_gf b); ///< out = a + b (unreduced)
void protocore_gf_sub(protocore_gf out, const protocore_gf a, const protocore_gf b); ///< out = a - b (unreduced)
void protocore_gf_mul(protocore_gf out, const protocore_gf a, const protocore_gf b); ///< out = a * b mod p
void protocore_gf_sq(protocore_gf out, const protocore_gf a);                        ///< out = a^2 mod p
void protocore_gf_inv(protocore_gf out, const protocore_gf a);                       ///< out = a^-1 mod p (= a^(p-2))
void protocore_gf_pack(uint8_t out[32], const protocore_gf a);    ///< canonical little-endian 32-byte encoding
void protocore_gf_unpack(protocore_gf out, const uint8_t in[32]); ///< decode 32 bytes (high bit ignored)
void protocore_gf_cswap(protocore_gf p, protocore_gf q, int b);   ///< constant-time conditional swap of p,q when b==1

// --- X25519 (RFC 7748) -----------------------------------------------------

// PROTOCORE_CURVE25519_BORROW - the bytes a scalar multiplication runs out of - is stated in
// protocore_config.h, which sums it into the secure arena. A caller takes them once and passes the
// pointer to every call.

/** @brief The scalar and the point one X25519 runs over. */
typedef struct
{
    const uint8_t *scalar; ///< 32 bytes little-endian, clamped internally
    const uint8_t *point;  ///< 32 bytes little-endian u coordinate
    uint8_t *out;          ///< 32 bytes; aliases neither input
} Curve25519X25519Args;

/** @brief The scalar one X25519 against the standard base point u=9 runs over. */
typedef struct
{
    const uint8_t *scalar; ///< 32 bytes little-endian, clamped internally
    uint8_t *out;          ///< 32 bytes
} Curve25519X25519BaseArgs;

/**
 * @brief X25519 scalar multiplication (RFC 7748 §5).
 *
 * A caller sets the members a call takes, invokes it through ::Curve25519 with the bytes it runs out of,
 * and reads the outcome off the same handle. How those bytes are carved is this module's and is never
 * named here.
 *
 *   Curve25519.x25519_base_args.scalar = ephemeral_priv;
 *   Curve25519.x25519_base_args.out = our_share;
 *   Curve25519.x25519_base(work);
 *   Curve25519.x25519_args.scalar = ephemeral_priv;
 *   Curve25519.x25519_args.point = peer_share;
 *   Curve25519.x25519_args.out = shared_secret;
 *   Curve25519.x25519(work);
 *
 * @var Curve25519Ns::x25519_args       the scalar and the point one X25519 runs over
 * @var Curve25519Ns::x25519_base_args  the scalar one X25519 against the standard base point runs over
 * @var Curve25519Ns::ok                a call's true/false outcome
 * @var Curve25519Ns::x25519            out = scalar * point, the Montgomery ladder of RFC 7748 §5
 * @var Curve25519Ns::x25519_base       out = scalar * G, the same ladder against u = 9
 *
 * Both entries clamp the scalar per RFC 7748 §5 and are constant-time in it: the ladder swaps
 * conditionally rather than branching on a scalar bit.
 *
 * @c work is PROTOCORE_CURVE25519_BORROW secure bytes the CALLER took, at an address it knows. It
 * arrives @c restrict and is not held past the call, so nothing here aliases it. The caller releases it,
 * and the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes it. The
 * clamped private scalar and every ladder intermediate live in those bytes and nowhere else, so none of
 * them reaches BSS or the stack. Two key exchanges running at once are two borrows and never collide.
 *
 * No storage member and no context: a caller sets operands and reads @ref Curve25519Ns::ok, and that is
 * all the surface there is.
 */
typedef struct
{
    Curve25519X25519Args x25519_args;
    Curve25519X25519BaseArgs x25519_base_args;
    proto_bool ok;
} Curve25519Vars;

/** @brief The operands and the outcome. */
extern Curve25519Vars Curve25519V;

/** @brief The entries. */
typedef struct
{
    void (*const x25519)(uint8_t *restrict work);
    void (*const x25519_base)(uint8_t *restrict work);
} Curve25519Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Curve25519V or a region of the borrow at a fixed offset.
void protocore_curve25519_x25519(uint8_t *restrict work);
void protocore_curve25519_x25519_base(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Curve25519.x25519(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Curve25519Ns Curve25519 __attribute__((unused)) = {
    .x25519 = protocore_curve25519_x25519,
    .x25519_base = protocore_curve25519_x25519_base,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CURVE25519

#endif // PROTOCORE_CURVE25519_H
