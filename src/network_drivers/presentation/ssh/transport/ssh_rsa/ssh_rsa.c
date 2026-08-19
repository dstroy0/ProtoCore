// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_rsa.c
 * @brief SSH RSA host-key layer: NVS/fixture host key, host-key signing, "ssh-rsa" blob (see ssh_rsa.h).
 *
 * The RSASSA-PKCS1-v1.5 math lives in crypto/rsa; this file owns the SSH host key and calls into it.
 * The accelerated arm of that math is the RSA/MPI HAL under test/core_setup/hal, so no arm is named here.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_SSH_RSA

#include "network_drivers/presentation/ssh/transport/ssh_rsa/ssh_rsa.h"
#include "crypto/asymmetric/rsa/rsa.h" // Rsa: the signer, and the key widths
#include "mmgr/protomem/protomem.h"
#include "mmgr/secure/secure.h"
#include "test/core_setup/hal/nvs.h" // the host key is read from non-volatile storage

// Public host key (BSS - no secret material).
SshRsaPubKey ssh_host_pubkey;

// The private exponent, borrowed from the secure pool. n and e are public and live in
// ssh_host_pubkey; only d is secret, so only d comes from here. The persistent end is walked by no
// mark and no release, so the key stays for the life of the program.
typedef struct
{
    protocore_span d; ///< private exponent, PROTOCORE_RSA_KEY_BYTES big-endian
    proto_bool ready; ///< d holds a parsed key
} SshRsaCtx;
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define SSH_RSA_OFF_CTX 0u
static_assert(SSH_RSA_OFF_CTX + sizeof(SshRsaCtx) <= PROTOCORE_SSH_RSA_BORROW,
              "PROTOCORE_SSH_RSA_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define SSH_RSA_CTX(w) ((SshRsaCtx *)(void *)((w) + SSH_RSA_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_SSH_RSA_BORROW persistent bytes, or null while the pool was short
} SshRsaOwnCtx;
static SshRsaOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_ssh_rsa_span(void)
{
    if (s_own.span == NULL)
    {
        protocore_span sp = protocore_secure_persist_span(PROTOCORE_SSH_RSA_BORROW);
        if (span.ok(sp))
        {
            s_own.span = sp.buf;
        }
    }
    return s_own.span; // null while the pool was short, which every entry refuses
}

_Static_assert(PROTOCORE_RSA_KEY_BYTES + SSH_RSA_KEY_DER_MAX <= PROTOCORE_WORK_SSH_HOST_KEY,
               "PROTOCORE_WORK_SSH_HOST_KEY must cover the private exponent and the DER walked over it");

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

// Read n, e and d out of an RSA private key, in either shape the key file comes in.
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
        if (!der_step(der, end, &off, &skip, &skip_len)) // AlgorithmIdentifier
        {
            return PROTO_FALSE;
        }
        if (!der_enter(der, end, &off, &end)) // privateKey
        {
            return PROTO_FALSE;
        }
        if (!der_enter(der, end, &off, &end)) // the RSAPrivateKey inside it
        {
            return PROTO_FALSE;
        }
        if (!der_step(der, end, &off, &skip, &skip_len)) // its version
        {
            return PROTO_FALSE;
        }
    }
    return der_int(der, end, &off, ssh_host_pubkey.n, PROTOCORE_RSA_KEY_BYTES) &&
           der_int(der, end, &off, ssh_host_pubkey.e_bytes, sizeof(ssh_host_pubkey.e_bytes)) &&
           der_int(der, end, &off, d, PROTOCORE_RSA_KEY_BYTES);
}

static void ssh_rsa_load_pubkey(uint8_t *restrict work)
{
    if (!span.has_storage(SSH_RSA_CTX(work)->d))
    {
        SSH_RSA_CTX(work)->d = protocore_secure_persist_span(PROTOCORE_RSA_KEY_BYTES);
    }
    if (!span.has_storage(SSH_RSA_CTX(work)->d))
    {
        SshRsa.n = -1;
        return;
    }
    SSH_RSA_CTX(work)->ready = PROTO_FALSE;
    ssh_host_pubkey.loaded = PROTO_FALSE;

    // The DER is a working set: borrowed for this call, wiped by the release on every path out.
    const size_t mark = protocore_secure_mark();
    protocore_span der = protocore_secure_span(SSH_RSA_KEY_DER_MAX, 0);
    if (!span.has_storage(der))
    {
        protocore_secure_release(mark);
        SshRsa.n = -1;
        return;
    }
    const size_t der_len =
        protocore_nvs_get_blob(PROTOCORE_SSH_HOST_KEY_NS, PROTOCORE_SSH_HOST_KEY_ITEM, der.buf, der.cap);
    if (der_len != 0 && rsa_key_parse(der.buf, der_len, SSH_RSA_CTX(work)->d.buf))
    {
        SSH_RSA_CTX(work)->ready = PROTO_TRUE;
        ssh_host_pubkey.loaded = PROTO_TRUE;
    }
    protocore_secure_release(mark);
    if (!SSH_RSA_CTX(work)->ready)
    {
        SshRsa.n = -1;
        return;
    }
    SshRsa.n = 0;
}

static void ssh_rsa_sign(uint8_t *restrict work)
{
    uint8_t *crypto_work = SshRsa.sign_args.crypto_work;
    const uint8_t *msg = SshRsa.sign_args.msg;
    size_t msg_len = SshRsa.sign_args.msg_len;
    protocore_rsa_hash hash = SshRsa.sign_args.hash;
    uint8_t *sig = SshRsa.sign_args.sig;

    // Reuse the key parsed once at startup; lazy-load as a fallback if the sketch never did. The
    // load is staged inside the guard: as the right operand of `&&` it must not run when the key is
    // already parsed.
    if (!SSH_RSA_CTX(work)->ready)
    {
        SshRsa.load_pubkey(work);
        if (SshRsa.n != 0)
        {
            SshRsa.n = -1;
            return;
        }
    }
    Rsa.sign_args.n = ssh_host_pubkey.n;
    Rsa.sign_args.d = SSH_RSA_CTX(work)->d.buf;
    Rsa.sign_args.msg = msg;
    Rsa.sign_args.msg_len = msg_len;
    Rsa.sign_args.hash = hash;
    Rsa.sign_args.sig = sig;
    Rsa.sign(crypto_work);
    if (!Rsa.ok)
    {
        SshRsa.n = -1;
        return;
    }
    SshRsa.n = 0;
}

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
    mem.cpy(p, src, src_len);
    return p + src_len;
}

static void ssh_rsa_encode_pubkey(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = SshRsa.encode_pubkey_args.out;
    size_t *out_len = SshRsa.encode_pubkey_args.out_len;
    size_t out_cap = SshRsa.encode_pubkey_args.out_cap;

    if (!ssh_host_pubkey.loaded)
    {
        SshRsa.n = -1;
        return;
    }
    if (out_cap < SSH_RSA_PUBKEY_BLOB_MAX)
    {
        SshRsa.n = -1;
        return;
    }

    const char *alg = SSH_RSA_PUBKEY_ALG; // "ssh-rsa" (RFC 8332 §3)
    size_t alg_len = SSH_RSA_PUBKEY_ALG_LEN;

    uint8_t *p = out;
    p = put_u32(p, (uint32_t)alg_len);
    mem.cpy(p, alg, alg_len);
    p += alg_len;
    p = put_mpint(p, ssh_host_pubkey.e_bytes, sizeof(ssh_host_pubkey.e_bytes));
    p = put_mpint(p, ssh_host_pubkey.n, PROTOCORE_RSA_KEY_BYTES);

    *out_len = (size_t)(p - out);
    SshRsa.n = 0;
}
SshRsaNs SshRsa = {
    .load_pubkey = ssh_rsa_load_pubkey,
    .sign = ssh_rsa_sign,
    .encode_pubkey = ssh_rsa_encode_pubkey,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_SSH_RSA
