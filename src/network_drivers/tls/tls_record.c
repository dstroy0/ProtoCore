// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file tls_record.c
 * @brief TLS 1.3 record layer over a reliable stream (RFC 8446 sec 5). See tls_record.h.
 */

#include "network_drivers/tls/tls_record.h"

#if PC_TLS_SOFTWARE

#include "mmgr/protomem.h" // mem.cpy / mem.zero
#include "mmgr/secure.h"   // the secure pool: key/iv material during derivation
#include "network_drivers/tls/tls13_kdf.h"

// Build the AEAD nonce: the 64-bit record number, right-aligned in the 12-byte write IV, XOR the IV
// (RFC 8446 sec 5.3). The high 4 bytes of the IV are left as they are. Same construction as QUIC and
// DTLS, which is why those three record layers agree on the shape and differ only in the counter.
static void build_nonce(TlsRecordKeys *keys)
{
    mem.cpy(keys->nonce, keys->iv, PC_AES128GCM_IV_LEN);
    for (int i = 0; i < 8; i++)
    {
        keys->nonce[PC_AES128GCM_IV_LEN - 1 - i] ^= (uint8_t)(keys->seq >> (8 * i));
    }
}

// Write the 5-byte record header: type, legacy_record_version, and the body length.
static void hdr_write(uint8_t *out, uint8_t content_type, size_t body_len)
{
    out[0] = content_type;
    out[1] = (uint8_t)(PC_TLS_LEGACY_VERSION >> 8);
    out[2] = (uint8_t)PC_TLS_LEGACY_VERSION;
    out[3] = (uint8_t)(body_len >> 8);
    out[4] = (uint8_t)body_len;
}

static void pc_tls_record_keys_derive(TlsRecordKeys *out, TlsCipher cipher, const uint8_t secret[32])
{
    out->cipher = cipher;
    out->seq = 0;
    out->ready = PROTO_FALSE;

    // AEAD_AES_128_GCM: a 16-byte key and a 12-byte IV, each HKDF-Expand-Label of the traffic secret
    // under the "tls13 " prefix (RFC 8446 sec 7.3). The key is borrowed and wiped: the expanded
    // schedule is what the AEAD needs afterwards, so no raw key stays resident.
    const size_t mark = pc_secure_mark();
    pc_span k = pc_secure_span(PC_AES128GCM_KEY_LEN, 8);
    if (!pc_span_ok(k))
    {
        pc_secure_release(mark);
        mem.zero(out->iv, sizeof(out->iv));
        return; // no key material: every protect/unprotect below fails closed on the unkeyed context
    }
    pc_tls13_kdf_expand_label(&TLS13_KDF, secret, "key", k.buf, PC_AES128GCM_KEY_LEN);
    pc_tls13_kdf_expand_label(&TLS13_KDF, secret, "iv", out->iv, sizeof(out->iv));
    // The vendor may refuse the key; without a keyed context every record operation must refuse too.
    out->ready = (pc_aes128gcm_key_init(out->gcm, k.buf) != NULL);
    pc_secure_release(mark);
}

static void pc_tls_record_keys_wipe(TlsRecordKeys *keys)
{
    pc_aes128gcm_key_wipe((struct pc_aes128gcm_key *)(keys->gcm));
    pc_secure_wipe(keys->iv, sizeof(keys->iv));
    pc_secure_wipe(keys->nonce, sizeof(keys->nonce));
    keys->ready = PROTO_FALSE;
    keys->seq = 0;
}

// ---------------------------------------------------------------------------
// TLSPlaintext (RFC 8446 sec 5.1): unencrypted record
// ---------------------------------------------------------------------------

static size_t pc_tls_plaintext_build(uint8_t content_type, const uint8_t *fragment, size_t frag_len, uint8_t *out,
                                     size_t out_cap)
{
    const size_t total = PC_TLS_PLAINTEXT_HDR_LEN + frag_len;
    if (total > out_cap || frag_len > PC_TLS_MAX_PLAINTEXT)
    {
        return 0;
    }
    hdr_write(out, content_type, frag_len);
    if (frag_len != 0)
    {
        mem.cpy(out + PC_TLS_PLAINTEXT_HDR_LEN, fragment, frag_len);
    }
    return total;
}

static size_t pc_tls_plaintext_parse(const uint8_t *rec, size_t rec_len, TlsPlaintext *out)
{
    if (rec_len < PC_TLS_PLAINTEXT_HDR_LEN)
    {
        return 0;
    }
    const size_t length = ((size_t)rec[3] << 8) | rec[4];
    if (PC_TLS_PLAINTEXT_HDR_LEN + length > rec_len)
    {
        return 0; // truncated: the stream has not delivered the whole record yet
    }
    // legacy_record_version is not checked: RFC 8446 sec 5.1 requires receivers to ignore it, and a
    // real ClientHello arrives carrying 0x0301.
    out->content_type = rec[0];
    out->fragment = rec + PC_TLS_PLAINTEXT_HDR_LEN;
    out->frag_len = length;
    return PC_TLS_PLAINTEXT_HDR_LEN + length;
}

// ---------------------------------------------------------------------------
// TLSCiphertext (RFC 8446 sec 5.2): AEAD-protected record
// ---------------------------------------------------------------------------

static size_t pc_tls_record_protect(TlsRecordKeys *keys, uint8_t content_type, const uint8_t *pt, size_t pt_len,
                                    uint8_t *out, size_t out_cap)
{
    if (!keys->ready || keys->cipher != TLS_CIPHER_AES_128_GCM_SHA256 || pt_len > PC_TLS_MAX_PLAINTEXT)
    {
        return 0;
    }
    // TLSInnerPlaintext is content || content_type; the AEAD adds the tag. No padding is added.
    const size_t inner_len = pt_len + 1;
    const size_t body_len = inner_len + PC_TLS_TAG_LEN;
    const size_t total = PC_TLS_PLAINTEXT_HDR_LEN + body_len;
    if (total > out_cap)
    {
        return 0;
    }

    // The header carries application_data(23) whatever the real type is, and the length of the sealed
    // body. It is the associated data, so it is written before the seal reads it.
    hdr_write(out, PC_TLS_CT_APPLICATION_DATA, body_len);

    // Assemble the inner plaintext where it will be sealed (seal permits out == pt).
    if (pt_len != 0)
    {
        mem.cpy(out + PC_TLS_PLAINTEXT_HDR_LEN, pt, pt_len);
    }
    out[PC_TLS_PLAINTEXT_HDR_LEN + pt_len] = content_type;

    build_nonce(keys);
    (void)pc_aes128gcm_seal((struct pc_aes128gcm_key *)(keys->gcm), keys->nonce, out, PC_TLS_PLAINTEXT_HDR_LEN,
                            out + PC_TLS_PLAINTEXT_HDR_LEN, inner_len, out + PC_TLS_PLAINTEXT_HDR_LEN,
                            out + PC_TLS_PLAINTEXT_HDR_LEN + inner_len);
    keys->seq++;
    return total;
}

static proto_bool pc_tls_record_unprotect(TlsRecordKeys *keys, const uint8_t *rec, size_t rec_len, uint8_t *out,
                                          size_t out_cap, TlsCiphertext *out_info)
{
    if (!keys->ready || keys->cipher != TLS_CIPHER_AES_128_GCM_SHA256 || rec_len < PC_TLS_PLAINTEXT_HDR_LEN)
    {
        return PROTO_FALSE;
    }
    const size_t body_len = ((size_t)rec[3] << 8) | rec[4];
    if (PC_TLS_PLAINTEXT_HDR_LEN + body_len > rec_len || body_len <= PC_TLS_TAG_LEN)
    {
        return PROTO_FALSE;
    }
    const size_t inner_len = body_len - PC_TLS_TAG_LEN;
    if (inner_len > out_cap || inner_len > PC_TLS_MAX_PLAINTEXT + 1)
    {
        return PROTO_FALSE;
    }

    build_nonce(keys);
    const uint8_t *ct = rec + PC_TLS_PLAINTEXT_HDR_LEN;
    if (!pc_aes128gcm_open((struct pc_aes128gcm_key *)(keys->gcm), keys->nonce, rec, PC_TLS_PLAINTEXT_HDR_LEN, ct,
                           inner_len, ct + inner_len, out))
    {
        return PROTO_FALSE; // seq does not advance: a forged record must not desynchronize the count
    }

    // The real content type is the last non-zero byte; everything after it is padding (sec 5.2). An
    // all-zero inner plaintext names no type and is a fatal unexpected_message (sec 5.4).
    size_t n = inner_len;
    while (n != 0 && out[n - 1] == 0)
    {
        n--;
    }
    if (n == 0)
    {
        return PROTO_FALSE;
    }
    out_info->content_type = out[n - 1];
    out_info->pt_len = n - 1;
    keys->seq++;
    return PROTO_TRUE;
}

const TlsRecordNs TlsRecord = {pc_tls_record_keys_derive, pc_tls_plaintext_build,  pc_tls_plaintext_parse,
                               pc_tls_record_protect,     pc_tls_record_unprotect, pc_tls_record_keys_wipe};

#endif // PC_TLS_SOFTWARE
