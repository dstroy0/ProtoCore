// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file chacha20.h
 * @brief ChaCha20 stream cipher (D. J. Bernstein; RFC 8439).
 *
 * The 20-round ARX permutation used by the chacha20-poly1305@openssh.com cipher. Two views of the
 * same core are exposed:
 *
 *   - @ref Chacha20Ns::xor_ : the original ChaCha layout OpenSSH uses - a 64-bit little-endian block
 *     counter (state words 12-13) and a 64-bit nonce/IV (words 14-15). This is what the SSH AEAD
 *     drives; the counter increments per 64-byte block.
 *   - @ref Chacha20Ns::block_ietf : the RFC 8439 layout (32-bit counter in word 12, 96-bit nonce in
 *     words 13-15), exposed so the core can be checked against the published RFC 8439 Section 2.3.2
 *     block test vector.
 *
 * Pure ARX (add-rotate-xor): naturally constant-time, no tables, ~64-byte state. No heap.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_CHACHA20_H
#define PROTOCORE_CHACHA20_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths and PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_CHACHA20

PROTOCORE_BEGIN_DECLS

/** @brief ChaCha20 key length in bytes. */
#define PROTOCORE_CHACHA20_KEY_LEN 32

/** @brief ChaCha20 keystream block length in bytes. */
#define PROTOCORE_CHACHA20_BLOCK_LEN 64

// PROTOCORE_CHACHA20_BORROW - the bytes a keystream runs out of - is stated in protocore_config.h,
// which sums it into the secure arena. A caller takes them once and passes the pointer to every call.

/** @brief The key, nonce, counter and bytes one OpenSSH-layout keystream XOR covers. */
typedef struct
{
    const uint8_t *key; ///< PROTOCORE_CHACHA20_KEY_LEN bytes
    const uint8_t *iv;  ///< 8-byte nonce; OpenSSH uses the packet sequence number, big-endian
    uint64_t counter;   ///< initial 64-bit block counter, stepping once per 64-byte block
    const uint8_t *in;  ///< plaintext, or ciphertext when decrypting; nullptr emits raw keystream
    uint8_t *out;       ///< len bytes; may alias in
    size_t len;         ///< how many
} Chacha20XorArgs;
/** @brief The key, nonce and counter one RFC 8439 keystream block is taken at. */
typedef struct
{
    const uint8_t *key;   ///< PROTOCORE_CHACHA20_KEY_LEN bytes
    uint32_t counter;     ///< 32-bit block counter, state word 12
    const uint8_t *nonce; ///< 12-byte nonce, state words 13-15
    uint8_t *out;         ///< PROTOCORE_CHACHA20_BLOCK_LEN bytes
} Chacha20BlockIetfArgs;
/**
 * @brief ChaCha20 (RFC 8439).
 *
 * A caller sets the members a call takes, invokes it through ::Chacha20 with the bytes it runs out of,
 * and reads the outcome off the same handle. How those bytes are carved is this module's and is never
 * named here.
 *
 *   Chacha20.xor_args.key = key;
 *   Chacha20.xor_args.iv = iv;
 *   Chacha20.xor_args.counter = 1;
 *   Chacha20.xor_args.in = pkt;
 *   Chacha20.xor_args.out = pkt;
 *   Chacha20.xor_args.len = pkt_len;
 *   Chacha20.xor_(work);
 *
 * @var Chacha20Ns::xor_args         the key, nonce, counter and bytes one keystream XOR covers
 * @var Chacha20Ns::block_ietf_args  the key, nonce and counter one RFC 8439 block is taken at
 * @var Chacha20Ns::ok               a call's true/false outcome
 * @var Chacha20Ns::xor_             XOR the OpenSSH-layout keystream over @c in into @c out
 * @var Chacha20Ns::block_ietf       one 64-byte keystream block in the RFC 8439 layout
 *
 * The member is spelled `xor_` because `xor` is an alternative token in C++, and this header reaches
 * the C++ translation units.
 *
 * @c work is PROTOCORE_CHACHA20_BORROW secure bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. The caller releases it, and
 * the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes it. The
 * borrow IS the working state, so two keystreams in flight are two borrows and never collide.
 *
 * No storage member and no context: a caller sets operands and reads @ref Chacha20Ns::ok, and that is
 * all the surface there is.
 */
typedef struct
{
    Chacha20XorArgs xor_args;
    Chacha20BlockIetfArgs block_ietf_args;
    proto_bool ok;
} Chacha20Vars;

/** @brief The operands and the outcome. */
extern Chacha20Vars Chacha20V;

/** @brief The entries. */
typedef struct
{
    void (*const xor_)(uint8_t *restrict work);
    void (*const block_ietf)(uint8_t *restrict work);
} Chacha20Ns;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in Chacha20V or a region of the borrow at a fixed offset.
void protocore_chacha20_xor_(uint8_t *restrict work);
void protocore_chacha20_block_ietf(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Chacha20.xor_(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const Chacha20Ns Chacha20 __attribute__((unused)) = {
    .xor_ = protocore_chacha20_xor_,
    .block_ietf = protocore_chacha20_block_ietf,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_CHACHA20

#endif // PROTOCORE_CHACHA20_H
