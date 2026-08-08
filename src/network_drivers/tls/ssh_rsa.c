// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_rsa.c
 * @brief SSH RSA host-key layer: NVS/fixture host key, host-key signing, "ssh-rsa" blob (see ssh_rsa.h).
 *
 * The RSASSA-PKCS1-v1.5 math lives in crypto/rsa; this file owns the SSH host key and calls into it.
 */

#include "network_drivers/tls/ssh_rsa.h"
#include "crypto/asymmetric/rsa.h"
#include "crypto/hash/sha256.h"
#include "crypto/hash/sha512.h"
#include "mmgr/secure.h"
#include "network_drivers/presentation/ssh/transport/ssh_keymat.h"

// Public host key (BSS - no secret material).
#if PC_HAS_HW_BIGNUM
#include "core_setup/hal/nvs.h" // the host key is read from non-volatile storage
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#endif
SshRsaPubKey ssh_host_pubkey;

#if PC_HAS_HW_BIGNUM

// ---------------------------------------------------------------------------
// Accelerated - cached mbedtls host-key signer over the vendor's modexp (NVS-backed)
// ---------------------------------------------------------------------------

// RNG callback for mbedtls private-key operations (mbedtls v3 requires a real f_rng for RSA blinding).
static int ssh_mbedtls_rng(void *ctx, unsigned char *buf, size_t len)
{
    (void)ctx;
    pc_platform_rand_fill(buf, len);
    return 0;
}

// Cached RSA host-key signer. Re-parsing the PKCS#8 key per handshake also re-ran mbedtls's first-use
// blinding setup (~170 ms wasted per sign); the parsed context caches the blinding state, so keeping it
// resident means each sign pays only the CRT modexp. The private key stays in RAM for the server
// lifetime (as an SSH host key normally does); the mutex serializes signs because mbedtls mutates the
// blinding values per operation. Loaded once at startup by pc_ssh_rsa_load_pubkey().
typedef struct
{
    mbedtls_pk_context pk;             ///< parsed host key + cached blinding state
    pc_platform_mutex lock;            ///< serializes signs on the shared context
    pc_platform_mutex_ctrl lock_store; ///< the mutex object itself, in BSS
    proto_bool ready;                  ///< pk holds a valid parsed key
} SshRsaCtx;
static SshRsaCtx s_rsa;

int pc_ssh_rsa_load_pubkey(void)
{
    if (!s_rsa.lock)
    {
        s_rsa.lock = pc_platform_mutex_create(&s_rsa.lock_store);
    }

    uint8_t der[SSH_RSA_KEY_DER_MAX];
    size_t der_len = pc_nvs_get_blob(PC_SSH_HOST_KEY_NS, PC_SSH_HOST_KEY_ITEM, der, sizeof(der));
    if (der_len == 0)
    {
        return -1;
    }

    // (Re)parse into the persistent context. Free any prior key first.
    if (s_rsa.ready)
    {
        mbedtls_pk_free(&s_rsa.pk);
        s_rsa.ready = PROTO_FALSE;
    }
    mbedtls_pk_init(&s_rsa.pk);
    int rc = mbedtls_pk_parse_key(&s_rsa.pk, der, der_len, NULL, 0
#if MBEDTLS_VERSION_MAJOR >= 3
                                  ,
                                  ssh_mbedtls_rng, NULL
#endif
    );
    pc_secure_wipe(der, der_len);

    if (rc != 0)
    {
        mbedtls_pk_free(&s_rsa.pk);
        return -1;
    }

    mbedtls_rsa_context *rsa = mbedtls_pk_rsa(s_rsa.pk);
    if (mbedtls_rsa_get_len(rsa) != PC_RSA_KEY_BYTES)
    {
        mbedtls_pk_free(&s_rsa.pk);
        return -1;
    }

    // Write n and e into the public-only BSS struct.
    mbedtls_mpi n_mpi;
    mbedtls_mpi e_mpi;
    mbedtls_mpi_init(&n_mpi);
    mbedtls_mpi_init(&e_mpi);
    mbedtls_rsa_export(rsa, &n_mpi, NULL, NULL, NULL, &e_mpi);
    mbedtls_mpi_write_binary(&n_mpi, ssh_host_pubkey.n, PC_RSA_KEY_BYTES);
    mbedtls_mpi_write_binary(&e_mpi, ssh_host_pubkey.e_bytes + 4 - sizeof(ssh_host_pubkey.e_bytes),
                             sizeof(ssh_host_pubkey.e_bytes));
    mbedtls_mpi_free(&n_mpi);
    mbedtls_mpi_free(&e_mpi);

    s_rsa.ready = PROTO_TRUE;
    ssh_host_pubkey.loaded = PROTO_TRUE;
    return 0;
}

int ssh_rsa_sign(const uint8_t *msg, size_t msg_len, pc_rsa_hash hash, uint8_t sig[PC_RSA_SIG_BYTES])
{
    // Reuse the key parsed once at startup; lazy-load as a fallback if the sketch never did.
    if (!s_rsa.ready && pc_ssh_rsa_load_pubkey() != 0)
    {
        return -1;
    }

    // mbedtls_pk_sign() PKCS#1-pads the supplied digest (it does NOT hash), so for rsa-sha2-256/512 we
    // pass SHA-256(msg) / SHA-512(msg).
    const proto_bool sha512 = (hash == PC_RSA_HASH_SHA512);
    const mbedtls_md_type_t md = sha512 ? MBEDTLS_MD_SHA512 : MBEDTLS_MD_SHA256;
    const size_t dlen = sha512 ? PC_SHA512_DIGEST_LEN : PC_SHA256_DIGEST_LEN;
    uint8_t digest[PC_SHA512_DIGEST_LEN];
    if (sha512)
    {
        pc_sha512(msg, msg_len, digest);
    }
    else
    {
        pc_sha256(msg, msg_len, digest);
    }

    // Serialize: mbedtls mutates the context's blinding state on each private op.
    if (s_rsa.lock)
    {
        pc_platform_mutex_take(s_rsa.lock, PC_PLATFORM_WAIT_FOREVER);
    }
    size_t sig_len = 0;
#if MBEDTLS_VERSION_MAJOR >= 3
    int rc = mbedtls_pk_sign(&s_rsa.pk, md, digest, dlen, sig, PC_RSA_SIG_BYTES, &sig_len, ssh_mbedtls_rng, NULL);
#else
    int rc = mbedtls_pk_sign(&s_rsa.pk, md, digest, dlen, sig, &sig_len, ssh_mbedtls_rng, NULL);
#endif
    if (s_rsa.lock)
    {
        pc_platform_mutex_give(s_rsa.lock);
    }
    pc_secure_wipe(digest, sizeof(digest));

    return (rc == 0 && sig_len == PC_RSA_SIG_BYTES) ? 0 : -1;
}

#else

// ---------------------------------------------------------------------------
// Native - test fixture host key; signing delegates to the crypto/rsa software path.
// ---------------------------------------------------------------------------

// The native test fixture sets these before calling ssh_rsa_sign(); plain arrays, test-owned.
uint8_t _test_rsa_n[PC_RSA_KEY_BYTES];
uint8_t _test_rsa_d[PC_RSA_KEY_BYTES];
uint8_t _test_rsa_e[4];

int pc_ssh_rsa_load_pubkey(void)
{
    memcpy(ssh_host_pubkey.n, _test_rsa_n, PC_RSA_KEY_BYTES);
    memcpy(ssh_host_pubkey.e_bytes, _test_rsa_e, 4);
    ssh_host_pubkey.loaded = PROTO_TRUE;
    return 0;
}

int ssh_rsa_sign(const uint8_t *msg, size_t msg_len, pc_rsa_hash hash, uint8_t sig[PC_RSA_SIG_BYTES])
{
    return pc_rsa_sign_sw(_test_rsa_n, _test_rsa_d, msg, msg_len, hash, sig);
}

#endif // PC_HAS_HW_BIGNUM

// ---------------------------------------------------------------------------
// "ssh-rsa" public-key blob serialization (both backends)
// ---------------------------------------------------------------------------

// Write a 4-byte big-endian uint32 to p and advance p by 4.
static uint8_t *put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
    return p + 4;
}

// Write an SSH mpint (4-byte length + optional 0x00 prefix + data). data is big-endian, data_len bytes.
static uint8_t *put_mpint(uint8_t *p, const uint8_t *data, size_t data_len)
{
    size_t off = 0;
    while (off < data_len && data[off] == 0)
    {
        off++;
    }
    const uint8_t *src = data + off;
    size_t src_len = data_len - off;
    proto_bool need_pad = (src_len > 0) && (src[0] & 0x80u);
    uint32_t mpint_len = (uint32_t)src_len + (need_pad ? 1u : 0u);
    p = put_u32(p, mpint_len);
    if (need_pad)
    {
        *p++ = 0x00;
    }
    memcpy(p, src, src_len);
    return p + src_len;
}

int ssh_rsa_encode_pubkey(uint8_t *out, size_t *out_len, size_t out_cap)
{
    if (!ssh_host_pubkey.loaded)
    {
        return -1;
    }
    if (out_cap < SSH_RSA_PUBKEY_BLOB_MAX)
    {
        return -1;
    }

    const char *alg = SSH_RSA_PUBKEY_ALG; // "ssh-rsa" (RFC 8332 §3)
    size_t alg_len = SSH_RSA_PUBKEY_ALG_LEN;

    uint8_t *p = out;
    p = put_u32(p, (uint32_t)alg_len);
    memcpy(p, alg, alg_len);
    p += alg_len;
    p = put_mpint(p, ssh_host_pubkey.e_bytes, sizeof(ssh_host_pubkey.e_bytes));
    p = put_mpint(p, ssh_host_pubkey.n, PC_RSA_KEY_BYTES);

    *out_len = (size_t)(p - out);
    return 0;
}
