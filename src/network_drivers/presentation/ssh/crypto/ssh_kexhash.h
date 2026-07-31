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
#include <stddef.h>
#include <stdint.h>

#define SSH_KEXHASH_MAX_LEN 64 ///< longest exchange-hash / session_id (SHA-512)

/** @brief A key-exchange digest bound to one of the SSH KEX hashes (SHA-256 or SHA-512). */
typedef struct
{
    bool is512;
    pc_sha256_ctx c256;
    pc_sha512_ctx c512;
} SshKexHash;

static inline void ssh_kexhash_init(SshKexHash *h, bool is512)
{
    h->is512 = is512;
    if (is512)
    {
        pc_sha512_init(&h->c512);
    }
    else
    {
        pc_sha256_init(&h->c256);
    }
}

static inline void ssh_kexhash_update(SshKexHash *h, const uint8_t *data, size_t len)
{
    if (h->is512)
    {
        pc_sha512_update(&h->c512, data, len);
    }
    else
    {
        pc_sha256_update(&h->c256, data, len);
    }
}

/** @brief Finalize into @p out (must hold SSH_KEXHASH_MAX_LEN); returns the digest length (32 or 64). */
static inline size_t ssh_kexhash_final(SshKexHash *h, uint8_t out[SSH_KEXHASH_MAX_LEN])
{
    if (h->is512)
    {
        pc_sha512_final(&h->c512, out);
        return PC_SHA512_DIGEST_LEN;
    }
    pc_sha256_final(&h->c256, out);
    return PC_SHA256_DIGEST_LEN;
}

/** @brief The digest length for a given hash selection (32 or 64). */
static inline size_t ssh_kexhash_len(bool is512)
{
    return is512 ? PC_SHA512_DIGEST_LEN : PC_SHA256_DIGEST_LEN;
}

#endif // PROTOCORE_SSH_KEXHASH_H
