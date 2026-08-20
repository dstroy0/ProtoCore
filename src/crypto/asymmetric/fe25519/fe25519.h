// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file fe25519.h
 * @brief Per-variant GF(2^255-19) field layer on the RSA/MPI hardware accelerator (X25519 + Ed25519).
 *
 * Field elements are canonical `uint32[8]` (< p = 2^255-19) so every field multiply is a single
 * 256-bit modular multiply on the RSA accelerator (S3: ~1,386 cycles vs 7,955 for the software SIMD
 * `protocore_gf_mul`; P4: ~2,118 cycles / 5.9 us). add/sub are native 32-bit (carry + one conditional subtract
 * of p); bytes<->fe is a per-scalar-mult conversion, not per multiply. This is the shared engine behind
 * both the X25519 KEX (`protocore_curve25519.cpp`) and the Ed25519 host-key signature (`protocore_ed25519.cpp`) on
 * every die with a single-shot hardware MODMULT (S3 hw_ver1, P4 and newer hw_ver3 - see the gate below);
 * the radix-2^16 `protocore_gf` path is the native / classic-ESP32 fallback in both.
 *
 * The accelerator (and its lock) are shared with mbedTLS RSA/DH, so a scalar-mult brackets itself with
 * `protocore_fe_hw_enable()` / `protocore_fe_hw_disable()` (mbedTLS's own `esp_mpi_{enable,disable}_hardware_hw_op`,
 * i.e. acquire the MPI lock + clock/power the peripheral) and holds the lock for its whole run.
 *
 * `static inline` on purpose: the cheap ops (add/sub/cswap) inline into the ladder in each translation
 * unit with no cross-TU call overhead, and the whole layer stays one source of truth.
 *
 * Two surfaces, and they are not the same kind of thing: the field ops below are shared internals that
 * X25519 and Ed25519 are written in and call directly, and ::Fe25519 is the namespace this module
 * exports over those same ops.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_FE25519_H
#define PROTOCORE_FE25519_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_FE25519

#include "crypto/ct_eq.h" // protocore_ct_eq

PROTOCORE_BEGIN_DECLS

// 25519 has no dedicated ECC accelerator on any ESP32 die, so the RSA MODMULT is the field-layer win wherever
// it exists - track the HAL's PROTOCORE_RSA_MODMUL_HW (S3, P4, ...). Classic ESP32 / native keep the software ladder.
#if PROTOCORE_RSA_MODMUL_HW
#define PROTOCORE_FE25519_MPI_HW 1
#endif

#if PROTOCORE_FE25519_MPI_HW

/** @brief A field element of GF(2^255-19): canonical, eight little-endian 32-bit limbs (< p). */
typedef uint32_t fe[8];

// Constants for the 256-bit modular multiply mod p = 2^255-19 (scratchpad/montconst.py): Montgomery m'
// and R^2 mod p (= 38^2 = 1444 = 0x5a4). Preloading R^2 into the result block makes the accelerator
// return a plain residue X*Y mod p rather than a Montgomery form (the esp_mpi_mul_mpi_mod convention).
static const uint32_t FE_MOD_MPRIME = 0x286bca1bu;
static const uint32_t FE_MOD_P[8] = {0xffffffedu, 0xffffffffu, 0xffffffffu, 0xffffffffu,
                                     0xffffffffu, 0xffffffffu, 0xffffffffu, 0x7fffffffu};
static const uint32_t FE_MOD_R2[8] = {0x000005a4u, 0, 0, 0, 0, 0, 0, 0};

// Acquire the accelerator (lock + power) for a scalar-mult, and drop it after. Bracket every run. Thin names
// over the HAL so the X25519 ladder / Ed25519 point arithmetic read as before.
static inline void protocore_fe_hw_enable(void)
{
    protocore_rsa_hw_acquire();
}
static inline void protocore_fe_hw_disable(void)
{
    protocore_rsa_hw_release();
}

// z = x*y mod p (8 words / 256-bit) on the RSA MODMULT. Requires protocore_fe_hw_enable() first. Canonical (< p),
// safe if z aliases x/y. Delegates to the HAL modmul with this domain's constants; the crypto TUs that pull
// this in build at -O2 (PROTOCORE_CRYPTO_HOT), where the always_inline HAL folds FE_MOD_P / the mostly-zero
// FE_MOD_R2 into immediate stores - the hand-tuned ~1,380-cyc path.
static inline void fe_mul(fe z, const fe x, const fe y)
{
    protocore_rsa_modmul(z, x, y, FE_MOD_P, FE_MOD_MPRIME, FE_MOD_R2, 8);
}
static inline void fe_sq(fe o, const fe x)
{
    fe_mul(o, x, x);
}

static inline void fe_copy(fe o, const fe a)
{
    for (int i = 0; i < 8; i++)
    {
        o[i] = a[i];
    }
}
static inline void fe_0(fe o)
{
    for (int i = 0; i < 8; i++)
    {
        o[i] = 0;
    }
}
static inline void fe_1(fe o)
{
    o[0] = 1;
    for (int i = 1; i < 8; i++)
    {
        o[i] = 0;
    }
}
// If o >= p (o is in [p, 2p)), subtract p. Constant-time: the borrow out of o-p selects o or o-p.
static inline void fe_reduce_once(fe o)
{
    uint32_t t[8];
    int64_t b = 0;
    for (int i = 0; i < 8; i++)
    {
        b += (int64_t)o[i] - (int64_t)FE_MOD_P[i];
        t[i] = (uint32_t)b;
        b >>= 32;
    }
    uint32_t keep = (uint32_t)b; // 0 if o>=p (take t=o-p), 0xffffffff if o<p (keep o)
    for (int i = 0; i < 8; i++)
    {
        o[i] = (o[i] & keep) | (t[i] & ~keep);
    }
}
static inline void fe_add(fe o, const fe x, const fe y) // x,y < p -> o = x+y mod p
{
    uint64_t c = 0;
    for (int i = 0; i < 8; i++)
    {
        c += (uint64_t)x[i] + y[i];
        o[i] = (uint32_t)c;
        c >>= 32;
    }
    fe_reduce_once(o); // x+y < 2p, one conditional subtract
}
static inline void fe_sub(fe o, const fe x, const fe y) // x,y < p -> o = x-y mod p
{
    int64_t b = 0;
    uint32_t t[8];
    for (int i = 0; i < 8; i++)
    {
        b += (int64_t)x[i] - (int64_t)y[i];
        t[i] = (uint32_t)b;
        b >>= 32;
    }
    uint32_t borrow = (uint32_t)b; // 0xffffffff if x<y -> add p back
    uint64_t c = 0;
    for (int i = 0; i < 8; i++)
    {
        c += (uint64_t)t[i] + (FE_MOD_P[i] & borrow);
        o[i] = (uint32_t)c;
        c >>= 32;
    }
}
static inline void fe_cswap(fe x, fe y, uint32_t swap) // constant-time swap of x,y when swap==1
{
    uint32_t mask = (uint32_t)(-(int32_t)swap);
    for (int i = 0; i < 8; i++)
    {
        uint32_t t = mask & (x[i] ^ y[i]);
        x[i] ^= t;
        y[i] ^= t;
    }
}
static inline void fe_frombytes(fe o, const uint8_t b[32])
{
    for (int i = 0; i < 8; i++)
    {
        o[i] = (uint32_t)b[4 * i] | ((uint32_t)b[4 * i + 1] << 8) | ((uint32_t)b[4 * i + 2] << 16) |
               ((uint32_t)b[4 * i + 3] << 24);
    }
    o[7] &= 0x7fffffffu; // Ed25519/X25519 both ignore bit 255 of the y/u coordinate
    fe_reduce_once(o);   // the masked value can still be in [p, 2^255) -> canonicalize
}
static inline void fe_tobytes(uint8_t b[32], const fe a)
{
    fe t;
    fe_copy(t, a);
    fe_reduce_once(t); // freeze to the canonical residue
    for (int i = 0; i < 8; i++)
    {
        b[4 * i] = (uint8_t)t[i];
        b[4 * i + 1] = (uint8_t)(t[i] >> 8);
        b[4 * i + 2] = (uint8_t)(t[i] >> 16);
        b[4 * i + 3] = (uint8_t)(t[i] >> 24);
    }
}
// o = a^(p-2) = a^-1 mod p (tweetnacl square-and-multiply chain for the exponent 2^255-21).
static inline void fe_invert(fe o, const fe a)
{
    fe c;
    fe_copy(c, a);
    for (int i = 253; i >= 0; i--)
    {
        fe_sq(c, c);
        if (i != 2 && i != 4)
        {
            fe_mul(c, c, a);
        }
    }
    fe_copy(o, c);
}
// o = a^((p-5)/8) = a^(2^252-3) - the square-root exponent for Ed25519 point decompression.
static inline void fe_pow2523(fe o, const fe a)
{
    fe c;
    fe_copy(c, a);
    for (int i = 250; i >= 0; i--)
    {
        fe_sq(c, c);
        if (i != 1)
        {
            fe_mul(c, c, a);
        }
    }
    fe_copy(o, c);
}
// Low bit of the canonical encoding (Ed25519 x-coordinate sign).
static inline int fe_parity(const fe a)
{
    uint8_t d[32];
    fe_tobytes(d, a);
    return d[0] & 1;
}
// 0 if a and b encode the same field element, -1 otherwise (constant-time over the 32 bytes).
static inline int fe_neq(const fe a, const fe b)
{
    uint8_t c[32];
    uint8_t d[32];
    fe_tobytes(c, a);
    fe_tobytes(d, b);
    return protocore_ct_eq(c, d, 32) ? 0 : -1;
}

// --- the namespace over the field layer ------------------------------------
//
// Every field element an entry reads or writes is the caller's own, and no entry carries anything to
// the next one, so none of them needs a borrow and this module states no BORROW constant.

/** @brief The product a multiply writes and the two factors it reads. */
typedef struct
{
    uint32_t *z;       ///< the destination fe, eight limbs; may alias @c x or @c y
    const uint32_t *x; ///< the first factor, canonical
    const uint32_t *y; ///< the second factor, canonical
} Fe25519MulArgs;
/** @brief The square a squaring writes and the element it reads. */
typedef struct
{
    uint32_t *o;       ///< the destination fe, eight limbs
    const uint32_t *x; ///< the element squared
} Fe25519SqArgs;
/** @brief The destination and source of a field-element copy. */
typedef struct
{
    uint32_t *o;       ///< the destination fe, eight limbs
    const uint32_t *a; ///< the source fe
} Fe25519CopyArgs;
/** @brief The element set to zero. */
typedef struct
{
    uint32_t *o; ///< the destination fe, eight limbs
} Fe25519ZeroArgs;
/** @brief The element set to one. */
typedef struct
{
    uint32_t *o; ///< the destination fe, eight limbs
} Fe25519OneArgs;
/** @brief The element canonicalized in place. */
typedef struct
{
    uint32_t *o; ///< the fe reduced in place, in [p, 2p) on entry
} Fe25519ReduceArgs;
/** @brief The sum an addition writes and the two terms it reads. */
typedef struct
{
    uint32_t *o;       ///< the destination fe, eight limbs
    const uint32_t *x; ///< the first term, < p
    const uint32_t *y; ///< the second term, < p
} Fe25519AddArgs;
/** @brief The difference a subtraction writes and the two terms it reads. */
typedef struct
{
    uint32_t *o;       ///< the destination fe, eight limbs
    const uint32_t *x; ///< the minuend, < p
    const uint32_t *y; ///< the subtrahend, < p
} Fe25519SubArgs;
/** @brief The two elements a conditional swap exchanges, and the bit selecting it. */
typedef struct
{
    uint32_t *x;   ///< the first fe, exchanged in place
    uint32_t *y;   ///< the second fe, exchanged in place
    uint32_t swap; ///< 1 swaps, 0 leaves both alone
} Fe25519CswapArgs;
/** @brief The 32 bytes a decode reads and the element it writes. */
typedef struct
{
    uint32_t *o;      ///< the destination fe, eight limbs
    const uint8_t *b; ///< 32 little-endian bytes; bit 255 is ignored
} Fe25519FromBytesArgs;
/** @brief The element an encode reads and the 32 bytes it writes. */
typedef struct
{
    uint8_t *b;        ///< 32 little-endian bytes of the canonical residue
    const uint32_t *a; ///< the source fe
} Fe25519ToBytesArgs;
/** @brief The inverse an inversion writes and the element it reads. */
typedef struct
{
    uint32_t *o;       ///< the destination fe, eight limbs
    const uint32_t *a; ///< the element inverted
} Fe25519InvertArgs;
/** @brief The power a^((p-5)/8) writes and the element it reads. */
typedef struct
{
    uint32_t *o;       ///< the destination fe, eight limbs
    const uint32_t *a; ///< the base
} Fe25519Pow2523Args;
/** @brief The element a parity read is taken over. */
typedef struct
{
    const uint32_t *a; ///< the fe whose canonical encoding supplies the low bit
} Fe25519ParityArgs;
/** @brief The two elements an equality test compares. */
typedef struct
{
    const uint32_t *a; ///< the first fe
    const uint32_t *b; ///< the second fe
} Fe25519NeqArgs;
/**
 * @brief GF(2^255-19) on the RSA/MPI accelerator.
 *
 * A caller sets the members a call takes, invokes it through ::Fe25519, and reads the outcome off the
 * same handle.
 *
 *   Fe25519.hw_enable(work);
 *   Fe25519.mul_args.z = z;
 *   Fe25519.mul_args.x = x;
 *   Fe25519.mul_args.y = y;
 *   Fe25519.mul(work);
 *   Fe25519.hw_disable(work);
 *
 * @var Fe25519Ns::mul_args        the product a multiply writes and the two factors it reads
 * @var Fe25519Ns::sq_args         the square a squaring writes and the element it reads
 * @var Fe25519Ns::copy_args       the destination and source of a field-element copy
 * @var Fe25519Ns::zero_args       the element set to zero
 * @var Fe25519Ns::one_args        the element set to one
 * @var Fe25519Ns::reduce_args     the element canonicalized in place
 * @var Fe25519Ns::add_args        the sum an addition writes and the two terms it reads
 * @var Fe25519Ns::sub_args        the difference a subtraction writes and the two terms it reads
 * @var Fe25519Ns::cswap_args      the two elements a conditional swap exchanges, and the bit selecting it
 * @var Fe25519Ns::frombytes_args  the 32 bytes a decode reads and the element it writes
 * @var Fe25519Ns::tobytes_args    the element an encode reads and the 32 bytes it writes
 * @var Fe25519Ns::invert_args     the inverse an inversion writes and the element it reads
 * @var Fe25519Ns::pow2523_args    the power a^((p-5)/8) writes and the element it reads
 * @var Fe25519Ns::parity_args     the element a parity read is taken over
 * @var Fe25519Ns::neq_args        the two elements an equality test compares
 * @var Fe25519Ns::ok              a call's true/false outcome; false on a null operand
 * @var Fe25519Ns::parity          the low bit of the canonical encoding the last parity read recovered
 * @var Fe25519Ns::neq             0 when the last compare found the same element, -1 otherwise
 * @var Fe25519Ns::hw_enable       take the accelerator lock and power it for a run
 * @var Fe25519Ns::hw_disable      drop the lock and power the accelerator down
 * @var Fe25519Ns::mul             z = x*y mod p, one 256-bit MODMULT
 * @var Fe25519Ns::sq              o = x^2 mod p
 * @var Fe25519Ns::copy            o = a
 * @var Fe25519Ns::zero            o = 0
 * @var Fe25519Ns::one             o = 1
 * @var Fe25519Ns::reduce_once     subtract p once when o >= p, in constant time
 * @var Fe25519Ns::add             o = x+y mod p
 * @var Fe25519Ns::sub             o = x-y mod p
 * @var Fe25519Ns::cswap           exchange x and y when @c swap is 1, in constant time
 * @var Fe25519Ns::frombytes       decode 32 little-endian bytes to a canonical element
 * @var Fe25519Ns::tobytes         encode a canonical element to 32 little-endian bytes
 * @var Fe25519Ns::invert          o = a^(p-2) = a^-1 mod p
 * @var Fe25519Ns::pow2523         o = a^((p-5)/8), the Ed25519 decompression square-root exponent
 * @var Fe25519Ns::get_parity      read the low bit of a's canonical encoding into @ref Fe25519Ns::parity
 * @var Fe25519Ns::get_neq         compare a and b in constant time into @ref Fe25519Ns::neq
 *
 * @ref Fe25519Ns::mul, @ref Fe25519Ns::sq, @ref Fe25519Ns::invert and @ref Fe25519Ns::pow2523 run on the
 * accelerator, so @ref Fe25519Ns::hw_enable brackets them and @ref Fe25519Ns::hw_disable closes the run.
 *
 * @c work goes unread by every entry: each one works on the caller's own field elements and carries
 * nothing to the next call, so this module needs no borrow, holds no context, and names no BORROW
 * constant. The ops themselves stay @c static @c inline above, where the X25519 ladder and the Ed25519
 * point arithmetic call them directly with no cross-TU call in the loop.
 *
 * No storage member and no context: a caller sets operands and reads @ref Fe25519Ns::ok, and that is
 * all the surface there is.
 */
typedef struct
{
    Fe25519MulArgs mul_args;
    Fe25519SqArgs sq_args;
    Fe25519CopyArgs copy_args;
    Fe25519ZeroArgs zero_args;
    Fe25519OneArgs one_args;
    Fe25519ReduceArgs reduce_args;
    Fe25519AddArgs add_args;
    Fe25519SubArgs sub_args;
    Fe25519CswapArgs cswap_args;
    Fe25519FromBytesArgs frombytes_args;
    Fe25519ToBytesArgs tobytes_args;
    Fe25519InvertArgs invert_args;
    Fe25519Pow2523Args pow2523_args;
    Fe25519ParityArgs parity_args;
    Fe25519NeqArgs neq_args;
    proto_bool ok;
    int parity;
    int neq;
} Fe25519Vars;

/** @brief The operands and the outcome. */
extern Fe25519Vars Fe25519V;

/** @brief The entries. */
typedef struct
{
    void (*const hw_enable)(uint8_t *restrict work);
    void (*const hw_disable)(uint8_t *restrict work);
    void (*const mul)(uint8_t *restrict work);
    void (*const sq)(uint8_t *restrict work);
    void (*const copy)(uint8_t *restrict work);
    void (*const zero)(uint8_t *restrict work);
    void (*const one)(uint8_t *restrict work);
    void (*const reduce_once)(uint8_t *restrict work);
    void (*const add)(uint8_t *restrict work);
    void (*const sub)(uint8_t *restrict work);
    void (*const cswap)(uint8_t *restrict work);
    void (*const frombytes)(uint8_t *restrict work);
    void (*const tobytes)(uint8_t *restrict work);
    void (*const invert)(uint8_t *restrict work);
    void (*const pow2523)(uint8_t *restrict work);
    void (*const get_parity)(uint8_t *restrict work);
    void (*const get_neq)(uint8_t *restrict work);
} Fe25519Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Fe25519V or a region of the borrow at a fixed offset.
void protocore_fe25519_hw_enable(uint8_t *restrict work);
void protocore_fe25519_hw_disable(uint8_t *restrict work);
void protocore_fe25519_mul(uint8_t *restrict work);
void protocore_fe25519_sq(uint8_t *restrict work);
void protocore_fe25519_copy(uint8_t *restrict work);
void protocore_fe25519_zero(uint8_t *restrict work);
void protocore_fe25519_one(uint8_t *restrict work);
void protocore_fe25519_reduce_once(uint8_t *restrict work);
void protocore_fe25519_add(uint8_t *restrict work);
void protocore_fe25519_sub(uint8_t *restrict work);
void protocore_fe25519_cswap(uint8_t *restrict work);
void protocore_fe25519_frombytes(uint8_t *restrict work);
void protocore_fe25519_tobytes(uint8_t *restrict work);
void protocore_fe25519_invert(uint8_t *restrict work);
void protocore_fe25519_pow2523(uint8_t *restrict work);
void protocore_fe25519_get_parity(uint8_t *restrict work);
void protocore_fe25519_get_neq(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Fe25519.hw_enable(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Fe25519Ns Fe25519 __attribute__((unused)) = {
    .hw_enable = protocore_fe25519_hw_enable,
    .hw_disable = protocore_fe25519_hw_disable,
    .mul = protocore_fe25519_mul,
    .sq = protocore_fe25519_sq,
    .copy = protocore_fe25519_copy,
    .zero = protocore_fe25519_zero,
    .one = protocore_fe25519_one,
    .reduce_once = protocore_fe25519_reduce_once,
    .add = protocore_fe25519_add,
    .sub = protocore_fe25519_sub,
    .cswap = protocore_fe25519_cswap,
    .frombytes = protocore_fe25519_frombytes,
    .tobytes = protocore_fe25519_tobytes,
    .invert = protocore_fe25519_invert,
    .pow2523 = protocore_fe25519_pow2523,
    .get_parity = protocore_fe25519_get_parity,
    .get_neq = protocore_fe25519_get_neq,
};

#endif // PROTOCORE_FE25519_MPI_HW

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_FE25519

#endif // PROTOCORE_FE25519_H
