// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Opaque streaming digest context (MD4 / MD5). Forward-declared only: the definition is private to
 * md.cpp, so other translation units know the symbol but never its members - they hold it via `MdCtx *`,
 * getting their storage from pc_md_wants() below.
 */
struct MdCtx;

/**
 * @brief Storage this module wants for one MD4/MD5 context.
 *
 * The type is opaque, so a consumer cannot size it - this module owns the definition and therefore
 * owns the allocation. Call inside a SecureScope: the scope states how long the caller needs the
 * resource, and the pool wipes the digest state when that scope ends. MD4/MD5 here carry NTLM
 * password and session-key material, so the storage comes from the secure pool.
 *
 * @return a context to pass to pc_md4_init() / pc_md5_init(), or nullptr if the pool could not
 *         satisfy it.
 */
MdCtx *pc_md_wants(void);

void pc_md5_init(MdCtx *c);
void pc_md5_update(MdCtx *c, const uint8_t *data, size_t len);
void pc_md5_final(MdCtx *c, uint8_t out[16]);
/** @brief One-shot MD5. */
void md5(const uint8_t *data, size_t len, uint8_t out[16]);

void pc_md4_init(MdCtx *c);
void pc_md4_update(MdCtx *c, const uint8_t *data, size_t len);
void pc_md4_final(MdCtx *c, uint8_t out[16]);
/** @brief One-shot MD4 (the NT-hash primitive). */
void md4(const uint8_t *data, size_t len, uint8_t out[16]);

/** @brief HMAC-MD5 (RFC 2104): the NTLMv2 MAC primitive. */
void pc_hmac_md5(const uint8_t *key, size_t key_len, const uint8_t *msg, size_t msg_len, uint8_t out[16]);

#endif // PROTOCORE_MD_H
