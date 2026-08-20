// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file esp.c
 * @brief ESP (RFC 4303) packet transform with AES-256-GCM (RFC 4106) - see esp.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_IKEV2

#include "mmgr/protomem/protomem.h"
#include "services/system/esp/esp/esp.h"

#include "crypto/aead/aesgcm/aesgcm.h"
#include "mmgr/secure/secure.h" // the per-call GCM context borrow

PROTOCORE_BEGIN_DECLS

static void put32be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}
static uint32_t get32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static void esp_nonce(uint8_t nonce[PROTOCORE_AESGCM_IV_LEN], const uint8_t *salt, const uint8_t *iv)
{
    mem.cpy(nonce, salt, PROTOCORE_ESP_SALT_LEN);
    mem.cpy(nonce + PROTOCORE_ESP_SALT_LEN, iv, PROTOCORE_ESP_IV_LEN);
}
// Bytes the ESP header + IV occupy before the ciphertext (also the AAD length = SPI|Seq).
#define PROTOCORE_ESP_CT_OFF (PROTOCORE_ESP_HDR_LEN + PROTOCORE_ESP_IV_LEN)

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void esp_gcm_encapsulate(uint8_t *restrict work)
{
    (void)work;
    uint32_t spi = Esp.gcm_encapsulate_args.spi;
    uint32_t seq = Esp.gcm_encapsulate_args.seq;
    const uint8_t *key = Esp.gcm_encapsulate_args.key;
    const uint8_t *salt = Esp.gcm_encapsulate_args.salt;
    const uint8_t *iv = Esp.gcm_encapsulate_args.iv;
    uint8_t next_header = Esp.gcm_encapsulate_args.next_header;
    const uint8_t *payload = Esp.gcm_encapsulate_args.payload;
    size_t payload_len = Esp.gcm_encapsulate_args.payload_len;
    uint8_t *out = Esp.gcm_encapsulate_args.out;
    size_t out_cap = Esp.gcm_encapsulate_args.out_cap;

    if (!key || !salt || !iv || !out || (payload_len && !payload))
    {
        Esp.n = 0;
        return;
    }

    // Plaintext = Payload | Padding | Pad Length | Next Header, padded so Pad Length + Next Header (the
    // 2-octet trailer) land on a 4-octet boundary (RFC 4303 §2.4).
    size_t padn = (4 - (payload_len + 2) % 4) % 4;
    size_t pt_len = payload_len + padn + 2;
    size_t total = PROTOCORE_ESP_CT_OFF + pt_len + PROTOCORE_ESP_ICV_LEN;
    if (out_cap < total)
    {
        Esp.n = 0;
        return;
    }

    put32be(out, spi);
    put32be(out + 4, seq);
    mem.cpy(out + PROTOCORE_ESP_HDR_LEN, iv, PROTOCORE_ESP_IV_LEN);

    uint8_t *pt = out + PROTOCORE_ESP_CT_OFF;
    if (payload_len)
    {
        mem.cpy(pt, payload, payload_len);
    }
    for (size_t i = 0; i < padn; i++)
    {
        pt[payload_len + i] = (uint8_t)(i + 1); // RFC 4303 monotonic padding 1, 2, 3 ...
    }
    pt[payload_len + padn] = (uint8_t)padn;   // Pad Length
    pt[payload_len + padn + 1] = next_header; // Next Header

    // Encrypt in place: AAD = SPI | Seq (the first 8 octets), plaintext -> ciphertext || ICV at PROTOCORE_ESP_CT_OFF.
    uint8_t nonce[PROTOCORE_AESGCM_IV_LEN];
    esp_nonce(nonce, salt, iv);
    {
        // Per-call context: this path is not hot enough to justify a per-session one. The cost is the
        // ~9,200-cycle lifecycle documented in aesgcm.h - hoist the context into session state if it
        // ever shows up in a profile.
        size_t mark = protocore_secure_mark();
        uint8_t *gcm = protocore_secure_span(PROTOCORE_AESGCM_BORROW, 8).buf;
        AesGcm.key_args.key = key;
        AesGcm.key_init(gcm);
        AesGcm.seal_args.nonce = nonce;
        AesGcm.seal_args.aad = out;
        AesGcm.seal_args.aad_len = PROTOCORE_ESP_HDR_LEN;
        AesGcm.seal_args.pt = pt;
        AesGcm.seal_args.pt_len = pt_len;
        AesGcm.seal_args.ct_out = pt;
        AesGcm.seal_args.tag_out = pt + pt_len;
        AesGcm.seal(gcm);
        AesGcm.key_wipe(gcm);
        protocore_secure_release(mark);
    }
    Esp.n = total;
}

static void esp_gcm_decapsulate(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *key = Esp.gcm_decapsulate_args.key;
    const uint8_t *salt = Esp.gcm_decapsulate_args.salt;
    uint8_t *packet = Esp.gcm_decapsulate_args.packet;
    size_t len = Esp.gcm_decapsulate_args.len;
    uint32_t *spi_out = Esp.gcm_decapsulate_args.spi_out;
    uint32_t *seq_out = Esp.gcm_decapsulate_args.seq_out;
    uint8_t *next_header_out = Esp.gcm_decapsulate_args.next_header_out;
    const uint8_t **payload_out = Esp.gcm_decapsulate_args.payload_out;
    size_t *payload_len_out = Esp.gcm_decapsulate_args.payload_len_out;

    if (!key || !salt || !packet || !payload_out || !payload_len_out)
    {
        Esp.ok = PROTO_FALSE;
        return;
    }
    // Minimum: header + IV + at least the 2-octet trailer (Pad Length + Next Header) + ICV.
    if (len < PROTOCORE_ESP_CT_OFF + 2 + PROTOCORE_ESP_ICV_LEN)
    {
        Esp.ok = PROTO_FALSE;
        return;
    }

    const uint8_t *iv = packet + PROTOCORE_ESP_HDR_LEN;
    uint8_t *ct = packet + PROTOCORE_ESP_CT_OFF;
    size_t ct_len = len - PROTOCORE_ESP_CT_OFF - PROTOCORE_ESP_ICV_LEN;
    const uint8_t *tag = ct + ct_len;

    uint8_t nonce[PROTOCORE_AESGCM_IV_LEN];
    esp_nonce(nonce, salt, iv);
    proto_bool ok = PROTO_FALSE;
    {
        // Per-call context: this path is not hot enough to justify a per-session one. The cost is the
        // ~9,200-cycle lifecycle documented in aesgcm.h - hoist the context into session state if it
        // ever shows up in a profile.
        size_t mark = protocore_secure_mark();
        uint8_t *gcm = protocore_secure_span(PROTOCORE_AESGCM_BORROW, 8).buf;
        AesGcm.key_args.key = key;
        AesGcm.key_init(gcm);
        AesGcm.open_args.nonce = nonce;
        AesGcm.open_args.aad = packet; // AAD = SPI | Seq
        AesGcm.open_args.aad_len = PROTOCORE_ESP_HDR_LEN;
        AesGcm.open_args.ct = ct;
        AesGcm.open_args.ct_len = ct_len;
        AesGcm.open_args.tag = tag;
        AesGcm.open_args.out = ct;
        AesGcm.open(gcm);
        ok = AesGcm.ok;
        AesGcm.key_wipe(gcm);
        protocore_secure_release(mark);
    }
    if (!ok)
    {
        Esp.ok = PROTO_FALSE;
        return;
    }

    // Trailer: the last octet is Next Header, the one before it is Pad Length.
    uint8_t next_header = ct[ct_len - 1];
    uint8_t pad_len = ct[ct_len - 2];
    if ((size_t)pad_len + 2 > ct_len) // padding + trailer cannot exceed the plaintext
    {
        Esp.ok = PROTO_FALSE;
        return;
    }

    if (spi_out)
    {
        *spi_out = get32be(packet);
    }
    if (seq_out)
    {
        *seq_out = get32be(packet + 4);
    }
    if (next_header_out)
    {
        *next_header_out = next_header;
    }
    *payload_out = ct;
    *payload_len_out = ct_len - 2 - pad_len;
    Esp.ok = PROTO_TRUE;
}

// ── ESP anti-replay window (RFC 4303 §3.4.3) ───────────────────────────────────────────────────

static void esp_replay_init(uint8_t *restrict work)
{
    (void)work;
    EspReplay *r = Esp.replay_init_args.r;

    if (!r)
    {
        return;
    }
    r->highest = 0;
    r->bitmap = 0;
    r->seen_any = PROTO_FALSE;
}

static void esp_replay_check(uint8_t *restrict work)
{
    (void)work;
    EspReplay *r = Esp.replay_check_args.r;
    uint32_t seq = Esp.replay_check_args.seq;

    if (!r || seq == 0) // sequence 0 is never valid (ESP counts from 1)
    {
        Esp.ok = PROTO_FALSE;
        return;
    }

    if (!r->seen_any)
    {
        r->highest = seq;
        r->bitmap = 1; // bit 0 = this (the new highest)
        r->seen_any = PROTO_TRUE;
        Esp.ok = PROTO_TRUE;
        return;
    }

    if (seq > r->highest)
    {
        // A new highest: slide the window up, then mark the new top bit. A jump >= the window clears it.
        uint32_t shift = seq - r->highest;
        r->bitmap = (shift >= PROTOCORE_ESP_REPLAY_WINDOW) ? 0u : (r->bitmap << shift);
        r->bitmap |= 1u;
        r->highest = seq;
        Esp.ok = PROTO_TRUE;
        return;
    }

    uint32_t offset = r->highest - seq;
    if (offset >= PROTOCORE_ESP_REPLAY_WINDOW) // left of the window -> too old
    {
        Esp.ok = PROTO_FALSE;
        return;
    }
    uint64_t mask = (uint64_t)1 << offset;
    if (r->bitmap & mask) // already accepted -> replay
    {
        Esp.ok = PROTO_FALSE;
        return;
    }
    r->bitmap |= mask;
    Esp.ok = PROTO_TRUE;
}

EspNs Esp = {
    .gcm_encapsulate = esp_gcm_encapsulate,
    .gcm_decapsulate = esp_gcm_decapsulate,
    .replay_init = esp_replay_init,
    .replay_check = esp_replay_check,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_IKEV2
