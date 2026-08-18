// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file record.c
 * @brief TLS 1.3 record layer over a reliable stream (RFC 8446 sec 5). See record.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_TLS_SOFTWARE

#include "network_drivers/tls/record/record.h"

#include "crypto/aead/aes128gcm.h" // Aes128Gcm - the 0x1301 record AEAD
#include "crypto/aead/aesgcm.h"    // AesGcm - the 0x1302 record AEAD
#include "mmgr/protomem.h"         // mem.cpy / mem.zero
#include "mmgr/secure.h"           // the secure pool: key/iv material during derivation
#include "network_drivers/tls/key_schedule/key_schedule.h"

/**
 * @brief The record layer's calls - what TlsRecordNs points at.
 *
 * @var TlsRecordInternal::ns  the handle a caller sets a call's members on
 */

// Build the AEAD nonce: the 64-bit record number, right-aligned in the 12-byte write IV, XOR the IV
// (RFC 8446 sec 5.3). The high 4 bytes of the IV are left as they are. Same construction as QUIC and
// DTLS, which is why those three record layers agree on the shape and differ only in the counter.
static void build_nonce(TlsRecordKeys *keys)
{
    mem.cpy(keys->nonce, keys->iv, PROTOCORE_TLS_RECORD_IV_LEN);
    for (int i = 0; i < 8; i++)
    {
        keys->nonce[PROTOCORE_TLS_RECORD_IV_LEN - 1 - i] ^= (uint8_t)(keys->seq >> (8 * i));
    }
}

// The two record suites, and the four places they differ: the key length, the keyed context, the seal
// and the open. Everything else about a record - the header, the nonce, the padding, the inner type -
// is the same either way, so the suite is read here and nowhere else.
static_assert(PROTOCORE_AES128GCM_BORROW <= PROTOCORE_TLS_RECORD_AEAD_BORROW &&
                  PROTOCORE_AESGCM_BORROW <= PROTOCORE_TLS_RECORD_AEAD_BORROW,
              "PROTOCORE_TLS_RECORD_AEAD_BORROW must cover both record AEAD contexts - raise it in "
              "protocore_config.h");
static_assert(PROTOCORE_AES128GCM_IV_LEN == PROTOCORE_TLS_RECORD_IV_LEN &&
                  PROTOCORE_AESGCM_IV_LEN == PROTOCORE_TLS_RECORD_IV_LEN,
              "both record AEADs must take the same 12-octet nonce the sec 5.3 construction builds");
static_assert(PROTOCORE_AES128GCM_TAG_LEN == PROTOCORE_AESGCM_TAG_LEN,
              "both record AEADs must produce the same tag length the record body is sized on");

// Whether the suite's key schedule and transcript run on SHA-384 (RFC 8446 sec 7.1).
static proto_bool cipher_is384(TlsCipher c)
{
    return c == TLS_CIPHER_AES_256_GCM_SHA384 ? PROTO_TRUE : PROTO_FALSE;
}

// The AEAD key length the suite expands out of the traffic secret (RFC 8446 sec 7.3).
static size_t cipher_key_len(TlsCipher c)
{
    return c == TLS_CIPHER_AES_256_GCM_SHA384 ? (size_t)PROTOCORE_AESGCM_KEY_LEN : (size_t)PROTOCORE_AES128GCM_KEY_LEN;
}

// Bind the key into the suite's context; the outcome is whether the arm accepted it.
static proto_bool aead_key_init(TlsCipher c, uint8_t *ctx, const uint8_t *key)
{
    if (c == TLS_CIPHER_AES_256_GCM_SHA384)
    {
        AesGcm.key_args.key = key;
        AesGcm.key_init(ctx);
        return AesGcm.ok;
    }
    Aes128Gcm.key_args.key = key;
    Aes128Gcm.key_init(ctx);
    return Aes128Gcm.ok;
}

static void aead_key_wipe(TlsCipher c, uint8_t *ctx)
{
    if (c == TLS_CIPHER_AES_256_GCM_SHA384)
    {
        AesGcm.key_wipe(ctx);
        return;
    }
    Aes128Gcm.key_wipe(ctx);
}

// Open ct_len octets into out under the bound key; false when the tag does not check.
static proto_bool aead_open(TlsRecordKeys *keys, const uint8_t *aad, size_t aad_len, const uint8_t *ct, size_t ct_len,
                            const uint8_t *tag, uint8_t *out)
{
    if (keys->cipher == TLS_CIPHER_AES_256_GCM_SHA384)
    {
        AesGcm.open_args.nonce = keys->nonce;
        AesGcm.open_args.aad = aad;
        AesGcm.open_args.aad_len = aad_len;
        AesGcm.open_args.ct = ct;
        AesGcm.open_args.ct_len = ct_len;
        AesGcm.open_args.tag = tag;
        AesGcm.open_args.out = out;
        AesGcm.open(keys->gcm);
        return AesGcm.ok;
    }
    Aes128Gcm.open_args.nonce = keys->nonce;
    Aes128Gcm.open_args.aad = aad;
    Aes128Gcm.open_args.aad_len = aad_len;
    Aes128Gcm.open_args.ct = ct;
    Aes128Gcm.open_args.ct_len = ct_len;
    Aes128Gcm.open_args.tag = tag;
    Aes128Gcm.open_args.out = out;
    Aes128Gcm.open(keys->gcm);
    return Aes128Gcm.ok;
}

// Seal pt_len octets in place under the bound key, tag detached.
static proto_bool aead_seal(TlsRecordKeys *keys, const uint8_t *aad, size_t aad_len, uint8_t *pt, size_t pt_len,
                            uint8_t *tag_out)
{
    if (keys->cipher == TLS_CIPHER_AES_256_GCM_SHA384)
    {
        AesGcm.seal_args.nonce = keys->nonce;
        AesGcm.seal_args.aad = aad;
        AesGcm.seal_args.aad_len = aad_len;
        AesGcm.seal_args.pt = pt;
        AesGcm.seal_args.pt_len = pt_len;
        AesGcm.seal_args.ct_out = pt;
        AesGcm.seal_args.tag_out = tag_out;
        AesGcm.seal(keys->gcm);
        return AesGcm.ok;
    }
    Aes128Gcm.seal_args.nonce = keys->nonce;
    Aes128Gcm.seal_args.aad = aad;
    Aes128Gcm.seal_args.aad_len = aad_len;
    Aes128Gcm.seal_args.pt = pt;
    Aes128Gcm.seal_args.pt_len = pt_len;
    Aes128Gcm.seal_args.ct_out = pt;
    Aes128Gcm.seal_args.tag_out = tag_out;
    Aes128Gcm.seal(keys->gcm);
    return Aes128Gcm.ok;
}

// Write the 5-byte record header: type, legacy_record_version, and the body length.
static void hdr_write(uint8_t *out, uint8_t content_type, size_t body_len)
{
    out[0] = content_type;
    out[1] = (uint8_t)(PROTOCORE_TLS_LEGACY_VERSION >> 8);
    out[2] = (uint8_t)PROTOCORE_TLS_LEGACY_VERSION;
    out[3] = (uint8_t)(body_len >> 8);
    out[4] = (uint8_t)body_len;
}

// HKDF-Expand-Label of the traffic secret under the "tls13 " prefix, into out.
static void expand_label(TlsCipher cipher, uint8_t *work, const uint8_t *secret, const char *label, uint8_t *out,
                         size_t out_len)
{
    Tls13Ks.bind.kdf = &TLS13_KDF;
    Tls13Ks.bind.is384 = cipher_is384(cipher);
    Tls13Ks.derive_args.work = work;
    Tls13Ks.derive_args.secret = secret;
    Tls13Ks.derive_args.label = label;
    Tls13Ks.derive_args.out = out;
    Tls13Ks.derive_args.out_len = out_len;
    Tls13Ks.expand_label(NULL);
}

static void record_keys_derive(uint8_t *restrict work)
{
    TlsRecordKeys *out = TlsRecord.key.keys;
    out->cipher = TlsRecord.key.cipher;
    out->seq = 0;
    out->ready = PROTO_FALSE;

    // The suite's key and a 12-byte IV, each HKDF-Expand-Label of the traffic secret under the
    // "tls13 " prefix (RFC 8446 sec 7.3): 16 octets of key for AEAD_AES_128_GCM, 32 for
    // AEAD_AES_256_GCM. The key is borrowed and wiped: the expanded schedule is what the AEAD needs
    // afterwards, so no raw key stays resident. The HKDF's own bytes are the wider suite's, since the
    // schedule under a SHA-384 connection runs HKDF-SHA384.
    const size_t key_len = cipher_key_len(out->cipher);
    const size_t mark = protocore_secure_mark();
    protocore_span k = protocore_secure_span(key_len, 8);
    protocore_span ws = protocore_secure_span(PROTOCORE_HKDF_SHA384_BORROW, _Alignof(uint32_t));
    if (!span.ok(k) || !span.ok(ws))
    {
        protocore_secure_release(mark);
        mem.zero(out->iv, sizeof(out->iv));
        return; // no key material: every protect/unprotect below fails closed on the unkeyed context
    }
    expand_label(out->cipher, ws.buf, TlsRecord.key.secret, "key", k.buf, key_len);
    expand_label(out->cipher, ws.buf, TlsRecord.key.secret, "iv", out->iv, sizeof(out->iv));
    // The arm may refuse the key; without a keyed context every record operation must refuse too.
    out->ready = aead_key_init(out->cipher, out->gcm, k.buf);
    protocore_secure_release(mark);
}

static void record_keys_wipe(uint8_t *restrict work)
{
    TlsRecordKeys *keys = TlsRecord.key.keys;
    aead_key_wipe(keys->cipher, keys->gcm);
    protocore_secure_wipe(keys->iv, sizeof(keys->iv));
    protocore_secure_wipe(keys->nonce, sizeof(keys->nonce));
    keys->ready = PROTO_FALSE;
    keys->seq = 0;
}

// ---------------------------------------------------------------------------
// TLSPlaintext (RFC 8446 sec 5.1): unencrypted record
// ---------------------------------------------------------------------------

static void plaintext_build(uint8_t *restrict work)
{
    const size_t frag_len = TlsRecord.plain.frag_len;
    uint8_t *out = TlsRecord.out_args.out;
    const size_t total = PROTOCORE_TLS_PLAINTEXT_HDR_LEN + frag_len;
    TlsRecord.n = 0;
    if (total > TlsRecord.out_args.out_cap || frag_len > PROTOCORE_TLS_MAX_PLAINTEXT)
    {
        return;
    }
    hdr_write(out, TlsRecord.content_type, frag_len);
    if (frag_len != 0)
    {
        mem.cpy(out + PROTOCORE_TLS_PLAINTEXT_HDR_LEN, TlsRecord.plain.fragment, frag_len);
    }
    TlsRecord.n = total;
}

static void plaintext_parse(uint8_t *restrict work)
{
    const uint8_t *rec = TlsRecord.sealed.rec;
    const size_t rec_len = TlsRecord.sealed.rec_len;
    TlsRecord.n = 0;
    if (rec_len < PROTOCORE_TLS_PLAINTEXT_HDR_LEN)
    {
        return;
    }
    const size_t length = ((size_t)rec[3] << 8) | rec[4];
    if (PROTOCORE_TLS_PLAINTEXT_HDR_LEN + length > rec_len)
    {
        return; // truncated: the stream has not delivered the whole record yet
    }
    // legacy_record_version is not checked: RFC 8446 sec 5.1 requires receivers to ignore it, and a
    // real ClientHello arrives carrying 0x0301.
    TlsRecord.plain.view->content_type = rec[0];
    TlsRecord.plain.view->fragment = rec + PROTOCORE_TLS_PLAINTEXT_HDR_LEN;
    TlsRecord.plain.view->frag_len = length;
    TlsRecord.n = PROTOCORE_TLS_PLAINTEXT_HDR_LEN + length;
}

// ---------------------------------------------------------------------------
// TLSCiphertext (RFC 8446 sec 5.2): AEAD-protected record
// ---------------------------------------------------------------------------

static void record_protect(uint8_t *restrict work)
{
    TlsRecordKeys *keys = TlsRecord.key.keys;
    const uint8_t content_type = TlsRecord.content_type;
    const uint8_t *pt = TlsRecord.sealed.pt;
    const size_t pt_len = TlsRecord.sealed.pt_len;
    uint8_t *out = TlsRecord.out_args.out;

    TlsRecord.n = 0;
    if (!keys->ready || pt_len > PROTOCORE_TLS_MAX_PLAINTEXT)
    {
        return;
    }
    // RFC 8446 sec 5.4: a Handshake or Alert record never carries a zero-length
    // TLSInnerPlaintext.content. Application data may, and is how a sender pads the stream.
    if (pt_len == 0 && (content_type == PROTOCORE_TLS_CT_HANDSHAKE || content_type == PROTOCORE_TLS_CT_ALERT))
    {
        return;
    }
    // TLSInnerPlaintext is content || content_type; the AEAD adds the tag. No padding is added.
    const size_t inner_len = pt_len + 1;
    const size_t body_len = inner_len + PROTOCORE_TLS_TAG_LEN;
    const size_t total = PROTOCORE_TLS_PLAINTEXT_HDR_LEN + body_len;
    if (total > TlsRecord.out_args.out_cap)
    {
        return;
    }

    // The header carries application_data(23) whatever the real type is, and the length of the sealed
    // body. It is the associated data, so it is written before the seal reads it.
    hdr_write(out, PROTOCORE_TLS_CT_APPLICATION_DATA, body_len);

    // Assemble the inner plaintext where it will be sealed (seal permits out == pt).
    if (pt_len != 0)
    {
        mem.cpy(out + PROTOCORE_TLS_PLAINTEXT_HDR_LEN, pt, pt_len);
    }
    out[PROTOCORE_TLS_PLAINTEXT_HDR_LEN + pt_len] = content_type;

    build_nonce(keys);
    aead_seal(keys, out, PROTOCORE_TLS_PLAINTEXT_HDR_LEN, out + PROTOCORE_TLS_PLAINTEXT_HDR_LEN, inner_len,
              out + PROTOCORE_TLS_PLAINTEXT_HDR_LEN + inner_len);
    keys->seq++;
    TlsRecord.n = total;
}

static void record_unprotect(uint8_t *restrict work)
{
    TlsRecordKeys *keys = TlsRecord.key.keys;
    const uint8_t *rec = TlsRecord.sealed.rec;
    const size_t rec_len = TlsRecord.sealed.rec_len;
    uint8_t *out = TlsRecord.out_args.out;
    TlsCiphertext *out_info = TlsRecord.sealed.info;

    TlsRecord.ok = PROTO_FALSE;
    if (!keys->ready || rec_len < PROTOCORE_TLS_PLAINTEXT_HDR_LEN)
    {
        return;
    }
    const size_t body_len = ((size_t)rec[3] << 8) | rec[4];
    if (PROTOCORE_TLS_PLAINTEXT_HDR_LEN + body_len > rec_len || body_len <= PROTOCORE_TLS_TAG_LEN)
    {
        return;
    }
    const size_t inner_len = body_len - PROTOCORE_TLS_TAG_LEN;
    if (inner_len > TlsRecord.out_args.out_cap || inner_len > PROTOCORE_TLS_MAX_PLAINTEXT + 1)
    {
        return;
    }

    build_nonce(keys);
    const uint8_t *ct = rec + PROTOCORE_TLS_PLAINTEXT_HDR_LEN;
    if (!aead_open(keys, rec, PROTOCORE_TLS_PLAINTEXT_HDR_LEN, ct, inner_len, ct + inner_len, out))
    {
        return; // seq does not advance: a forged record must not desynchronize the count
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
        return;
    }
    out_info->content_type = out[n - 1];
    out_info->pt_len = n - 1;
    // sec 5.4: the same rule on receipt - a zero-length Handshake or Alert record is fatal, and the
    // sequence number does not advance for a record the connection is about to terminate on.
    if (out_info->pt_len == 0 &&
        (out_info->content_type == PROTOCORE_TLS_CT_HANDSHAKE || out_info->content_type == PROTOCORE_TLS_CT_ALERT))
    {
        return;
    }
    keys->seq++;
    TlsRecord.ok = PROTO_TRUE;
}

TlsRecordNs TlsRecord = {.keys_derive = record_keys_derive,
                         .plaintext_build = plaintext_build,
                         .plaintext_parse = plaintext_parse,
                         .protect = record_protect,
                         .unprotect = record_unprotect,
                         .keys_wipe = record_keys_wipe};

#endif // PROTOCORE_TLS_SOFTWARE
