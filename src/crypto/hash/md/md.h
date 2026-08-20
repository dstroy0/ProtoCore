// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file md.h
 * @brief MD4 (RFC 1320), MD5 (RFC 1321), and HMAC-MD5 (RFC 2104) - the legacy digests NTLM needs.
 *
 * The shared library home for the MD-family digests. The only consumer is the SMB2 client's NTLMv2
 * (MS-NLMP): the NT hash is MD4(UTF-16LE(password)); the NTLMv2 response and session key are HMAC-MD5
 * chains. MD4/MD5 are cryptographically broken and are included ONLY because SMB/NTLM requires them on
 * the wire - do not use them for anything security-new. Zero heap, streaming; verified against the RFC
 * test vectors (see test_smb_crypto).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_MD_H
#define PROTOCORE_MD_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_MD

PROTOCORE_BEGIN_DECLS

/** @brief MD digest length in bytes. MD4 and MD5 are both 128-bit. */
#define PROTOCORE_MD_DIGEST_LEN 16

// PROTOCORE_MD_BORROW - the bytes a digest runs out of - is stated in protocore_config.h, which sums
// it into the secure arena. A caller takes them once and passes the pointer to every call.

/** @brief One chunk fed to a running digest. */
typedef struct
{
    const uint8_t *data; ///< the bytes
    size_t len;          ///< how many
} MdUpdateArgs;
/** @brief Where a finished digest lands. */
typedef struct
{
    uint8_t *out; ///< PROTOCORE_MD_DIGEST_LEN bytes
} MdFinalArgs;
/** @brief The key and message an HMAC-MD5 is taken over. */
typedef struct
{
    const uint8_t *key; ///< MAC key bytes
    size_t key_len;     ///< key length
    const uint8_t *msg; ///< the message
    size_t msg_len;     ///< its length
    uint8_t *out;       ///< PROTOCORE_MD_DIGEST_LEN bytes
} MdHmacArgs;
/**
 * @brief MD4 / MD5 / HMAC-MD5.
 *
 * A caller sets the members a call takes, invokes it through ::Md, and reads the outcome off the same
 * handle, with the bytes it runs out of. How those bytes are carved is this module's and is never
 * named here.
 *
 *   Md.md4_init(work);
 *   Md.update_args.data = pw;
 *   Md.update_args.len = pw_len;
 *   Md.update(work);
 *   Md.final_args.out = nt_hash;
 *   Md.final(work);
 *
 * @var MdNs::update_args  one chunk fed to a running digest
 * @var MdNs::final_args   where a finished digest lands
 * @var MdNs::hmac_args    the key and message an HMAC-MD5 is taken over
 * @var MdNs::ok           a call's true/false outcome
 * @var MdNs::md5_init     start an MD5
 * @var MdNs::md4_init     start an MD4
 * @var MdNs::update       feed the running digest a chunk
 * @var MdNs::final        pad, compress the last block, write the 16 bytes out
 * @var MdNs::md5          init, update and final in one call
 * @var MdNs::md4          the same for MD4, the NT-hash primitive
 * @var MdNs::hmac_md5     HMAC-MD5 (RFC 2104), the NTLMv2 MAC primitive
 *
 * @c work is PROTOCORE_MD_BORROW secure bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. The caller releases it, and
 * the pool wipes on release; this module neither takes it, holds it, releases it, nor wipes it. That
 * is what keeps the NTLM password and session-key material in it from outliving the caller. The borrow
 * IS the digest, so two running digests are two borrows and never collide.
 *
 * No storage member and no context: a caller sets operands and reads @ref MdNs::ok, and that is all
 * the surface there is.
 */
typedef struct
{
    MdUpdateArgs update_args;
    MdFinalArgs final_args;
    MdHmacArgs hmac_args;
    proto_bool ok;
} MdVars;

/** @brief The operands and the outcome. */
extern MdVars MdV;

/** @brief The entries. */
typedef struct
{
    void (*const md5_init)(uint8_t *restrict work);
    void (*const md4_init)(uint8_t *restrict work);
    void (*const update)(uint8_t *restrict work);
    void (*const final)(uint8_t *restrict work);
    void (*const md5)(uint8_t *restrict work);
    void (*const md4)(uint8_t *restrict work);
    void (*const hmac_md5)(uint8_t *restrict work);
} MdNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in MdV or a region of the borrow at a fixed offset.
void protocore_md_md5_init(uint8_t *restrict work);
void protocore_md_md4_init(uint8_t *restrict work);
void protocore_md_update(uint8_t *restrict work);
void protocore_md_final(uint8_t *restrict work);
void protocore_md_md5(uint8_t *restrict work);
void protocore_md_md4(uint8_t *restrict work);
void protocore_md_hmac_md5(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Md.md5_init(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const MdNs Md __attribute__((unused)) = {
    .md5_init = protocore_md_md5_init,
    .md4_init = protocore_md_md4_init,
    .update = protocore_md_update,
    .final = protocore_md_final,
    .md5 = protocore_md_md5,
    .md4 = protocore_md_md4,
    .hmac_md5 = protocore_md_hmac_md5,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_MD

#endif // PROTOCORE_MD_H
