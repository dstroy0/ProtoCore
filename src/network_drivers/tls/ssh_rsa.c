// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_rsa.c
 * @brief SSH RSA host-key layer: NVS/fixture host key, host-key signing, "ssh-rsa" blob (see ssh_rsa.h).
 *
 * The RSASSA-PKCS1-v1.5 math lives in crypto/rsa; this file owns the SSH host key and calls into it.
 */

#include "network_drivers/tls/ssh_rsa.h"
#include "core_setup/hal/nvs.h" // the host key is read from non-volatile storage
#include "crypto/asymmetric/rsa.h"
#include "crypto/hash/sha256.h"
#include "crypto/hash/sha512.h"
#include "mmgr/protomem.h"
#include "mmgr/secure.h"
#include "network_drivers/presentation/ssh/transport/ssh_keymat.h"

#if PC_HAS_HW_BIGNUM
#include <mbedtls/md.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#endif

// Public host key (BSS - no secret material).
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
// Software - the same NVS host key, walked here; signing runs crypto/rsa's software path.
// ---------------------------------------------------------------------------

// The private exponent, borrowed from the secure pool. n and e are public and live in
// ssh_host_pubkey; only d is secret, so only d comes from here. The persistent end is walked by no
// mark and no release, so the key stays for the life of the program - the lifetime the accelerated
// arm gives its parsed mbedtls context.
typedef struct
{
    pc_span d;        ///< private exponent, PC_RSA_KEY_BYTES big-endian
    proto_bool ready; ///< d holds a parsed key
} SshRsaCtx;
static SshRsaCtx s_rsa;

_Static_assert(PC_RSA_KEY_BYTES + SSH_RSA_KEY_DER_MAX <= PC_WORK_SSH_HOST_KEY,
               "PC_WORK_SSH_HOST_KEY must cover the private exponent and the DER walked over it");

// Step over the DER element at *off. Writes the value's offset and length, and advances *off past
// the element. False if the header or the value runs past len.
static proto_bool der_step(const uint8_t *der, size_t len, size_t *off, size_t *val, size_t *val_len)
{
    size_t p = *off;
    if (p + 2 > len)
    {
        return PROTO_FALSE;
    }
    p += 1; // tag
    size_t n = der[p];
    p += 1;
    if ((n & 0x80u) != 0)
    {
        // Long form: the low 7 bits count the length bytes that follow, big-endian.
        size_t k = n & 0x7Fu;
        if (k == 0 || k > 4 || p + k > len)
        {
            return PROTO_FALSE;
        }
        n = 0;
        while (k != 0)
        {
            n = (n << 8) | der[p];
            p += 1;
            k -= 1;
        }
    }
    // Against the bytes left rather than p + n, which wraps where size_t is 32 bits wide and a
    // 4-byte length can reach the top of the range.
    if (n > len - p)
    {
        return PROTO_FALSE;
    }
    *val = p;
    *val_len = n;
    *off = p + n;
    return PROTO_TRUE;
}

// Enter the DER element at *off: the value becomes the region, the header is skipped.
static proto_bool der_enter(const uint8_t *der, size_t len, size_t *off, size_t *end)
{
    size_t val = 0;
    size_t val_len = 0;
    if (!der_step(der, len, off, &val, &val_len))
    {
        return PROTO_FALSE;
    }
    *off = val;
    *end = val + val_len;
    return PROTO_TRUE;
}

// Copy the DER INTEGER at *off into a width-byte big-endian field, left-padded with zeros. A
// leading sign byte is dropped; a magnitude wider than the field fails.
static proto_bool der_int(const uint8_t *der, size_t len, size_t *off, uint8_t *out, size_t width)
{
    size_t val = 0;
    size_t val_len = 0;
    if (!der_step(der, len, off, &val, &val_len))
    {
        return PROTO_FALSE;
    }
    while (val_len != 0 && der[val] == 0)
    {
        val += 1;
        val_len -= 1;
    }
    if (val_len > width)
    {
        return PROTO_FALSE;
    }
    mem.zero(out, width - val_len);
    mem.cpy(out + width - val_len, der + val, val_len);
    return PROTO_TRUE;
}

/** @brief DER tag for a constructed SEQUENCE. */
#define SSH_RSA_DER_SEQUENCE 0x30

// Read n, e and d out of an RSA private key, in either shape mbedtls accepts.
//
// PKCS#1 RSAPrivateKey is SEQUENCE { INTEGER version, n, e, d, p, q, dp, dq, qinv }. PKCS#8 wraps
// it: SEQUENCE { INTEGER version, SEQUENCE algorithm, OCTET STRING privateKey }, the privateKey
// holding that same RSAPrivateKey. After the outer SEQUENCE and its version the two are told apart
// by what comes next - a SEQUENCE is PKCS#8's AlgorithmIdentifier, an INTEGER is PKCS#1's modulus.
// The CRT factors after d go unread.
static proto_bool rsa_key_parse(const uint8_t *der, size_t len, uint8_t *d)
{
    size_t off = 0;
    size_t end = 0;
    size_t skip = 0;
    size_t skip_len = 0;
    if (!der_enter(der, len, &off, &end))
    {
        return PROTO_FALSE;
    }
    if (!der_step(der, end, &off, &skip, &skip_len)) // version
    {
        return PROTO_FALSE;
    }
    if (off < end && der[off] == SSH_RSA_DER_SEQUENCE)
    {
        if (!der_step(der, end, &off, &skip, &skip_len) || // AlgorithmIdentifier
            !der_enter(der, end, &off, &end) ||            // privateKey
            !der_enter(der, end, &off, &end) ||            // the RSAPrivateKey inside it
            !der_step(der, end, &off, &skip, &skip_len))   // its version
        {
            return PROTO_FALSE;
        }
    }
    return der_int(der, end, &off, ssh_host_pubkey.n, PC_RSA_KEY_BYTES) &&
           der_int(der, end, &off, ssh_host_pubkey.e_bytes, sizeof(ssh_host_pubkey.e_bytes)) &&
           der_int(der, end, &off, d, PC_RSA_KEY_BYTES);
}

int pc_ssh_rsa_load_pubkey(void)
{
    if (!pc_span_has_storage(s_rsa.d))
    {
        s_rsa.d = pc_secure_persist_span(PC_RSA_KEY_BYTES);
    }
    if (!pc_span_has_storage(s_rsa.d))
    {
        return -1;
    }
    s_rsa.ready = PROTO_FALSE;
    ssh_host_pubkey.loaded = PROTO_FALSE;

    // The DER is a working set: borrowed for this call, wiped by the release on every path out.
    const size_t mark = pc_secure_mark();
    pc_span der = pc_secure_span(SSH_RSA_KEY_DER_MAX, 0);
    if (!pc_span_has_storage(der))
    {
        pc_secure_release(mark);
        return -1;
    }
    const size_t der_len = pc_nvs_get_blob(PC_SSH_HOST_KEY_NS, PC_SSH_HOST_KEY_ITEM, der.buf, der.cap);
    if (der_len != 0 && rsa_key_parse(der.buf, der_len, s_rsa.d.buf))
    {
        s_rsa.ready = PROTO_TRUE;
        ssh_host_pubkey.loaded = PROTO_TRUE;
    }
    pc_secure_release(mark);
    if (!s_rsa.ready)
    {
        return -1;
    }
    return 0;
}

int ssh_rsa_sign(const uint8_t *msg, size_t msg_len, pc_rsa_hash hash, uint8_t sig[PC_RSA_SIG_BYTES])
{
    // Reuse the key parsed once at startup; lazy-load as a fallback if the sketch never did.
    if (!s_rsa.ready && pc_ssh_rsa_load_pubkey() != 0)
    {
        return -1;
    }
    return pc_rsa_sign_sw(ssh_host_pubkey.n, s_rsa.d.buf, msg, msg_len, hash, sig);
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
