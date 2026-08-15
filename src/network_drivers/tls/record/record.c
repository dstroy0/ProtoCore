// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file record.c
 * @brief TLS 1.3 record layer over a reliable stream (RFC 8446 sec 5). See record.h.
 */

#include "network_drivers/tls/record/record.h"

#if PROTOCORE_TLS_SOFTWARE

#include "mmgr/protomem.h" // mem.cpy / mem.zero
#include "mmgr/secure.h"   // the secure pool: key/iv material during derivation
#include "network_drivers/tls/key_schedule/key_schedule.h"

/**
 * @brief The record layer's calls - what TlsRecordNs points at.
 *
 * @var TlsRecordInternal::ns  the handle a caller sets a call's members on
 */
struct TlsRecordInternal
{
    TlsRecordNs *ns;
};

static struct TlsRecordInternal s_record = {.ns = &TlsRecord};

// Build the AEAD nonce: the 64-bit record number, right-aligned in the 12-byte write IV, XOR the IV
// (RFC 8446 sec 5.3). The high 4 bytes of the IV are left as they are. Same construction as QUIC and
// DTLS, which is why those three record layers agree on the shape and differ only in the counter.
static void build_nonce(TlsRecordKeys *keys)
{
    mem.cpy(keys->nonce, keys->iv, PROTOCORE_AES128GCM_IV_LEN);
    for (int i = 0; i < 8; i++)
    {
        keys->nonce[PROTOCORE_AES128GCM_IV_LEN - 1 - i] ^= (uint8_t)(keys->seq >> (8 * i));
    }
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
static void expand_label(uint8_t *work, const uint8_t *secret, const char *label, uint8_t *out, size_t out_len)
{
    Tls13Ks.bind.kdf = &TLS13_KDF;
    Tls13Ks.derive_args.work = work;
    Tls13Ks.derive_args.secret = secret;
    Tls13Ks.derive_args.label = label;
    Tls13Ks.derive_args.out = out;
    Tls13Ks.derive_args.out_len = out_len;
    Tls13Ks.expand_label(Tls13Ks.internal);
}

static void record_keys_derive(struct TlsRecordInternal *restrict ctx)
{
    TlsRecordKeys *out = ctx->ns->key.keys;
    out->cipher = ctx->ns->key.cipher;
    out->seq = 0;
    out->ready = PROTO_FALSE;

    // AEAD_AES_128_GCM: a 16-byte key and a 12-byte IV, each HKDF-Expand-Label of the traffic secret
    // under the "tls13 " prefix (RFC 8446 sec 7.3). The key is borrowed and wiped: the expanded
    // schedule is what the AEAD needs afterwards, so no raw key stays resident.
    const size_t mark = protocore_secure_mark();
    protocore_span k = protocore_secure_span(PROTOCORE_AES128GCM_KEY_LEN, 8);
    protocore_span ws = protocore_secure_span(PROTOCORE_HKDF_BORROW, _Alignof(uint32_t));
    if (!span.ok(k) || !span.ok(ws))
    {
        protocore_secure_release(mark);
        mem.zero(out->iv, sizeof(out->iv));
        return; // no key material: every protect/unprotect below fails closed on the unkeyed context
    }
    expand_label(ws.buf, ctx->ns->key.secret, "key", k.buf, PROTOCORE_AES128GCM_KEY_LEN);
    expand_label(ws.buf, ctx->ns->key.secret, "iv", out->iv, sizeof(out->iv));
    // The vendor may refuse the key; without a keyed context every record operation must refuse too.
    out->ready = (protocore_aes128gcm_key_init(out->gcm, k.buf) != NULL);
    protocore_secure_release(mark);
}

static void record_keys_wipe(struct TlsRecordInternal *restrict ctx)
{
    TlsRecordKeys *keys = ctx->ns->key.keys;
    protocore_aes128gcm_key_wipe((struct protocore_aes128gcm_key *)(keys->gcm));
    protocore_secure_wipe(keys->iv, sizeof(keys->iv));
    protocore_secure_wipe(keys->nonce, sizeof(keys->nonce));
    keys->ready = PROTO_FALSE;
    keys->seq = 0;
}

// ---------------------------------------------------------------------------
// TLSPlaintext (RFC 8446 sec 5.1): unencrypted record
// ---------------------------------------------------------------------------

static void plaintext_build(struct TlsRecordInternal *restrict ctx)
{
    const size_t frag_len = ctx->ns->plain.frag_len;
    uint8_t *out = ctx->ns->out_args.out;
    const size_t total = PROTOCORE_TLS_PLAINTEXT_HDR_LEN + frag_len;
    ctx->ns->n = 0;
    if (total > ctx->ns->out_args.out_cap || frag_len > PROTOCORE_TLS_MAX_PLAINTEXT)
    {
        return;
    }
    hdr_write(out, ctx->ns->content_type, frag_len);
    if (frag_len != 0)
    {
        mem.cpy(out + PROTOCORE_TLS_PLAINTEXT_HDR_LEN, ctx->ns->plain.fragment, frag_len);
    }
    ctx->ns->n = total;
}

static void plaintext_parse(struct TlsRecordInternal *restrict ctx)
{
    const uint8_t *rec = ctx->ns->sealed.rec;
    const size_t rec_len = ctx->ns->sealed.rec_len;
    ctx->ns->n = 0;
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
    ctx->ns->plain.view->content_type = rec[0];
    ctx->ns->plain.view->fragment = rec + PROTOCORE_TLS_PLAINTEXT_HDR_LEN;
    ctx->ns->plain.view->frag_len = length;
    ctx->ns->n = PROTOCORE_TLS_PLAINTEXT_HDR_LEN + length;
}

// ---------------------------------------------------------------------------
// TLSCiphertext (RFC 8446 sec 5.2): AEAD-protected record
// ---------------------------------------------------------------------------

static void record_protect(struct TlsRecordInternal *restrict ctx)
{
    TlsRecordKeys *keys = ctx->ns->key.keys;
    const uint8_t content_type = ctx->ns->content_type;
    const uint8_t *pt = ctx->ns->sealed.pt;
    const size_t pt_len = ctx->ns->sealed.pt_len;
    uint8_t *out = ctx->ns->out_args.out;

    ctx->ns->n = 0;
    if (!keys->ready || keys->cipher != TLS_CIPHER_AES_128_GCM_SHA256 || pt_len > PROTOCORE_TLS_MAX_PLAINTEXT)
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
    if (total > ctx->ns->out_args.out_cap)
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
    (void)protocore_aes128gcm_seal((struct protocore_aes128gcm_key *)(keys->gcm), keys->nonce, out,
                                   PROTOCORE_TLS_PLAINTEXT_HDR_LEN, out + PROTOCORE_TLS_PLAINTEXT_HDR_LEN, inner_len,
                                   out + PROTOCORE_TLS_PLAINTEXT_HDR_LEN,
                                   out + PROTOCORE_TLS_PLAINTEXT_HDR_LEN + inner_len);
    keys->seq++;
    ctx->ns->n = total;
}

static void record_unprotect(struct TlsRecordInternal *restrict ctx)
{
    TlsRecordKeys *keys = ctx->ns->key.keys;
    const uint8_t *rec = ctx->ns->sealed.rec;
    const size_t rec_len = ctx->ns->sealed.rec_len;
    uint8_t *out = ctx->ns->out_args.out;
    TlsCiphertext *out_info = ctx->ns->sealed.info;

    ctx->ns->ok = PROTO_FALSE;
    if (!keys->ready || keys->cipher != TLS_CIPHER_AES_128_GCM_SHA256 || rec_len < PROTOCORE_TLS_PLAINTEXT_HDR_LEN)
    {
        return;
    }
    const size_t body_len = ((size_t)rec[3] << 8) | rec[4];
    if (PROTOCORE_TLS_PLAINTEXT_HDR_LEN + body_len > rec_len || body_len <= PROTOCORE_TLS_TAG_LEN)
    {
        return;
    }
    const size_t inner_len = body_len - PROTOCORE_TLS_TAG_LEN;
    if (inner_len > ctx->ns->out_args.out_cap || inner_len > PROTOCORE_TLS_MAX_PLAINTEXT + 1)
    {
        return;
    }

    build_nonce(keys);
    const uint8_t *ct = rec + PROTOCORE_TLS_PLAINTEXT_HDR_LEN;
    if (!protocore_aes128gcm_open((struct protocore_aes128gcm_key *)(keys->gcm), keys->nonce, rec,
                                  PROTOCORE_TLS_PLAINTEXT_HDR_LEN, ct, inner_len, ct + inner_len, out))
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
    ctx->ns->ok = PROTO_TRUE;
}

TlsRecordNs TlsRecord = {.keys_derive = record_keys_derive,
                         .plaintext_build = plaintext_build,
                         .plaintext_parse = plaintext_parse,
                         .protect = record_protect,
                         .unprotect = record_unprotect,
                         .keys_wipe = record_keys_wipe,
                         .internal = &s_record};

#endif // PROTOCORE_TLS_SOFTWARE
