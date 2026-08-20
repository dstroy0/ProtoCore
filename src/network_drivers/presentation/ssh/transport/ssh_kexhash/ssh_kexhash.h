// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_kexhash.h
 * @brief One key-exchange digest that dispatches SHA-256 or SHA-512 by the negotiated method.
 *
 * RFC 4253 sec 8 ties the exchange hash H (and the sec 7.2 key derivation) to the KEX method's hash:
 * the `-sha256` methods (curve25519-sha256, ecdh-sha2-nistp256, dh-group14-sha256,
 * mlkem768x25519-sha256) use SHA-256; `-sha512` methods (sntrup761x25519-sha512@openssh.com) use
 * SHA-512. Rather than fork every hash site, the exchange hash and the KDF run through this one
 * namespace, so adding a KEX with a different hash is a one-line change and not a new code path.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SSH_KEXHASH_H
#define PROTOCORE_SSH_KEXHASH_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/** @brief Longest exchange hash / session_id the two KEX hashes produce (SHA-512). */
#define SSH_KEXHASH_MAX_LEN 64

// PROTOCORE_SSH_KEXHASH_BORROW - the bytes one digest runs out of - is stated in protocore_config.h,
// which sums it into the secure arena. A caller takes them once and passes the pointer to every call.

/** @brief Which of the two KEX hashes the negotiated method binds a digest to. */
typedef struct
{
    proto_bool is512; ///< true for a -sha512 method, false for a -sha256 one
} SshKexHashInitArgs;

/** @brief The bytes absorbed. */
typedef struct
{
    const uint8_t *data; ///< the bytes
    size_t len;          ///< how many
} SshKexHashUpdateArgs;

/** @brief Where the digest lands. */
typedef struct
{
    uint8_t *out; ///< SSH_KEXHASH_MAX_LEN bytes; the bound hash writes its own length
} SshKexHashFinalArgs;

/**
 * @brief The SSH key-exchange digest (RFC 4253 sec 8), bound to the KEX method's hash.
 *
 * A caller sets the members a call takes, invokes it through ::SshKexHash with the bytes it runs out
 * of, and reads the outcome off the same handle. How those bytes are carved is this module's and is
 * never named here.
 *
 *   SshKexHash.init_args.is512 = is512;
 *   SshKexHash.init(work);
 *   SshKexHash.update_args.data = v_c;
 *   SshKexHash.update_args.len = v_c_len;
 *   SshKexHash.update(work);
 *   SshKexHash.final_args.out = H;
 *   SshKexHash.final(work);
 *   // SshKexHash.len is 32 or 64, the length written
 *
 * @var SshKexHashNs::init_args    which of the two KEX hashes the negotiated method binds this to
 * @var SshKexHashNs::update_args  the bytes absorbed
 * @var SshKexHashNs::final_args   where the digest lands
 * @var SshKexHashNs::ok           a call's true/false outcome; false on a null pointer
 * @var SshKexHashNs::len          the bound hash's digest length, 32 or 64, from @ref SshKexHashNs::init
 * @var SshKexHashNs::init         bind the digest to the negotiated method's hash and start it
 * @var SshKexHashNs::update       absorb the staged bytes
 * @var SshKexHashNs::final        write @ref SshKexHashNs::len octets of digest
 *
 * @c work is PROTOCORE_SSH_KEXHASH_BORROW secure bytes the CALLER took, at an address it knows. It
 * arrives @c restrict and is not held past the call, so nothing here aliases it. The caller releases
 * it, and the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes
 * it. The exchange hash and every key the sec 7.2 chain derives pass through those bytes, so they die
 * with the release rather than on the stack. Two digests are two borrows and never collide.
 *
 * No storage member and no context: a caller sets operands and reads @ref SshKexHashNs::ok, and that
 * is all the surface there is.
 */
typedef struct
{
    SshKexHashInitArgs init_args;
    SshKexHashUpdateArgs update_args;
    SshKexHashFinalArgs final_args;
    proto_bool ok;
    size_t len;
} SshKexHashVars;

/** @brief The operands and the outcome. */
extern SshKexHashVars SshKexHashV;

/** @brief The entries. */
typedef struct
{
    void (*const init)(uint8_t *restrict work);
    void (*const update)(uint8_t *restrict work);
    void (*const final)(uint8_t *restrict work);
} SshKexHashNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in SshKexHashV or a region of the borrow at a fixed offset.
void protocore_ssh_kex_hash_init(uint8_t *restrict work);
void protocore_ssh_kex_hash_update(uint8_t *restrict work);
void protocore_ssh_kex_hash_final(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `SshKexHash.init(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const SshKexHashNs SshKexHash __attribute__((unused)) = {
    .init = protocore_ssh_kex_hash_init,
    .update = protocore_ssh_kex_hash_update,
    .final = protocore_ssh_kex_hash_final,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_SSH_KEXHASH_H
