// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_kexhash.h
 * @brief One key-exchange digest that dispatches SHA-256 or SHA-512 by the negotiated method.
 *
 * RFC 4253 §8 ties the exchange hash H (and the §7.2 key derivation) to the KEX method's hash: the
 * `-sha256` methods (curve25519-sha256, ecdh-sha2-nistp256, dh-group14-sha256, mlkem768x25519-sha256)
 * use SHA-256; `-sha512` methods (sntrup761x25519-sha512@openssh.com) use SHA-512. Rather than fork
 * every hash site, the exchange hash and the KDF run over this small wrapper, so adding a KEX with a
 * different hash is a one-line change (SshKexHash + ssh_kex_hash_is512()), not a new code path.
 *
 * The digest length is 32 (SHA-256) or 64 (SHA-512); H and the session_id are sized to the max (64).
 */

#ifndef PROTOCORE_SSH_KEXHASH_H
#define PROTOCORE_SSH_KEXHASH_H

#include "crypto/hash/sha256.h"
#include "crypto/hash/sha512.h"
#include "protocore_config.h" // the entry point: protocore_types.h for the widths and PROTOCORE_INLINE

#define SSH_KEXHASH_MAX_LEN 64 ///< longest exchange-hash / session_id (SHA-512)

/** @brief A key-exchange digest bound to one of the SSH KEX hashes (SHA-256 or SHA-512). */
typedef struct
{
    proto_bool is512;
    protocore_sha256_ctx c256;
    protocore_sha512_ctx c512;
} SshKexHash;

/**
 * @brief Bind the digest to @p work and start it.
 * @param work PROTOCORE_SHA256_BORROW bytes of caller storage; the SHA-512 arm carries its own.
 */
static inline void ssh_kexhash_init(SshKexHash *h, uint8_t *work, proto_bool is512)
{
    h->is512 = is512;
    if (is512)
    {
        protocore_sha512_init(&h->c512, work);
    }
    else
    {
        protocore_sha256_init(&h->c256, work);
    }
}

static inline void ssh_kexhash_update(SshKexHash *h, const uint8_t *data, size_t len)
{
    if (h->is512)
    {
        protocore_sha512_update(&h->c512, data, len);
    }
    else
    {
        protocore_sha256_update(&h->c256, data, len);
    }
}

/** @brief Finalize into @p out (must hold SSH_KEXHASH_MAX_LEN); returns the digest length (32 or 64). */
static inline size_t ssh_kexhash_final(SshKexHash *h, uint8_t out[SSH_KEXHASH_MAX_LEN])
{
    if (h->is512)
    {
        protocore_sha512_final(&h->c512, out);
        return PROTOCORE_SHA512_DIGEST_LEN;
    }
    protocore_sha256_final(&h->c256, out);
    return PROTOCORE_SHA256_DIGEST_LEN;
}

/** @brief The digest length for a given hash selection (32 or 64). */
static inline size_t ssh_kexhash_len(proto_bool is512)
{
    return is512 ? PROTOCORE_SHA512_DIGEST_LEN : PROTOCORE_SHA256_DIGEST_LEN;
}

#endif // PROTOCORE_SSH_KEXHASH_H
