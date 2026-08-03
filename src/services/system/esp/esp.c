// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file esp.c
 * @brief ESP (RFC 4303) packet transform with AES-256-GCM (RFC 4106) - see esp.h.
 */

#include "services/system/esp/esp.h"

#if PC_ENABLE_IKEV2

#include "crypto/aead/aesgcm.h"
#include "mmgr/secure.h" // the per-call GCM context borrow
#include <string.h>

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
static void esp_nonce(uint8_t nonce[PC_AESGCM_IV_LEN], const uint8_t *salt, const uint8_t *iv)
{
    memcpy(nonce, salt, PC_ESP_SALT_LEN);
    memcpy(nonce + PC_ESP_SALT_LEN, iv, PC_ESP_IV_LEN);
}
// Bytes the ESP header + IV occupy before the ciphertext (also the AAD length = SPI|Seq).
#define PC_ESP_CT_OFF (PC_ESP_HDR_LEN + PC_ESP_IV_LEN)

size_t pc_esp_gcm_encapsulate(uint32_t spi, uint32_t seq, const uint8_t key[PC_ESP_KEY_LEN],
                              const uint8_t salt[PC_ESP_SALT_LEN], const uint8_t iv[PC_ESP_IV_LEN], uint8_t next_header,
                              const uint8_t *payload, size_t payload_len, uint8_t *out, size_t out_cap)
{
    if (!key || !salt || !iv || !out || (payload_len && !payload))
    {
        return 0;
    }

    // Plaintext = Payload | Padding | Pad Length | Next Header, padded so Pad Length + Next Header (the
    // 2-octet trailer) land on a 4-octet boundary (RFC 4303 §2.4).
    size_t padn = (4 - (payload_len + 2) % 4) % 4;
    size_t pt_len = payload_len + padn + 2;
    size_t total = PC_ESP_CT_OFF + pt_len + PC_ESP_ICV_LEN;
    if (out_cap < total)
    {
        return 0;
    }

    put32be(out, spi);
    put32be(out + 4, seq);
    memcpy(out + PC_ESP_HDR_LEN, iv, PC_ESP_IV_LEN);

    uint8_t *pt = out + PC_ESP_CT_OFF;
    if (payload_len)
    {
        memcpy(pt, payload, payload_len);
    }
    for (size_t i = 0; i < padn; i++)
    {
        pt[payload_len + i] = (uint8_t)(i + 1); // RFC 4303 monotonic padding 1, 2, 3 ...
    }
    pt[payload_len + padn] = (uint8_t)padn;   // Pad Length
    pt[payload_len + padn + 1] = next_header; // Next Header

    // Encrypt in place: AAD = SPI | Seq (the first 8 octets), plaintext -> ciphertext || ICV at PC_ESP_CT_OFF.
    uint8_t nonce[PC_AESGCM_IV_LEN];
    esp_nonce(nonce, salt, iv);
    {
        // Per-call context: this path is not hot enough to justify a per-session one. The cost is the
        // ~9,200-cycle lifecycle documented in aesgcm.h - hoist the context into session state if it
        // ever shows up in a profile.
        size_t mark = pc_secure_mark();
        struct pc_aesgcm_key *gcm = pc_aesgcm_key_init(pc_secure_span(PC_WORK_AESGCM, 8).buf, key);
        pc_aesgcm_seal(gcm, nonce, out, PC_ESP_HDR_LEN, pt, pt_len, pt, pt + pt_len);
        pc_aesgcm_key_wipe(gcm);
        pc_secure_release(mark);
    }
    return total;
}

proto_bool pc_esp_gcm_decapsulate(const uint8_t key[PC_ESP_KEY_LEN], const uint8_t salt[PC_ESP_SALT_LEN],
                                  uint8_t *packet, size_t len, uint32_t *spi_out, uint32_t *seq_out,
                                  uint8_t *next_header_out, const uint8_t **payload_out, size_t *payload_len_out)
{
    if (!key || !salt || !packet || !payload_out || !payload_len_out)
    {
        return PROTO_FALSE;
    }
    // Minimum: header + IV + at least the 2-octet trailer (Pad Length + Next Header) + ICV.
    if (len < PC_ESP_CT_OFF + 2 + PC_ESP_ICV_LEN)
    {
        return PROTO_FALSE;
    }

    const uint8_t *iv = packet + PC_ESP_HDR_LEN;
    uint8_t *ct = packet + PC_ESP_CT_OFF;
    size_t ct_len = len - PC_ESP_CT_OFF - PC_ESP_ICV_LEN;
    const uint8_t *tag = ct + ct_len;

    uint8_t nonce[PC_AESGCM_IV_LEN];
    esp_nonce(nonce, salt, iv);
    proto_bool ok = PROTO_FALSE;
    {
        // Per-call context: this path is not hot enough to justify a per-session one. The cost is the
        // ~9,200-cycle lifecycle documented in aesgcm.h - hoist the context into session state if it
        // ever shows up in a profile.
        size_t mark = pc_secure_mark();
        struct pc_aesgcm_key *gcm = pc_aesgcm_key_init(pc_secure_span(PC_WORK_AESGCM, 8).buf, key);
        ok = pc_aesgcm_open(gcm, nonce, packet, PC_ESP_HDR_LEN, ct, ct_len, tag, ct); // AAD = SPI | Seq
        pc_aesgcm_key_wipe(gcm);
        pc_secure_release(mark);
    }
    if (!ok)
    {
        return PROTO_FALSE;
    }

    // Trailer: the last octet is Next Header, the one before it is Pad Length.
    uint8_t next_header = ct[ct_len - 1];
    uint8_t pad_len = ct[ct_len - 2];
    if ((size_t)pad_len + 2 > ct_len) // padding + trailer cannot exceed the plaintext
    {
        return PROTO_FALSE;
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
    return PROTO_TRUE;
}

// ── ESP anti-replay window (RFC 4303 §3.4.3) ───────────────────────────────────────────────────

void pc_esp_replay_init(EspReplay *r)
{
    if (!r)
    {
        return;
    }
    r->highest = 0;
    r->bitmap = 0;
    r->seen_any = PROTO_FALSE;
}

proto_bool pc_esp_replay_check(EspReplay *r, uint32_t seq)
{
    if (!r || seq == 0) // sequence 0 is never valid (ESP counts from 1)
    {
        return PROTO_FALSE;
    }

    if (!r->seen_any)
    {
        r->highest = seq;
        r->bitmap = 1; // bit 0 = this (the new highest)
        r->seen_any = PROTO_TRUE;
        return PROTO_TRUE;
    }

    if (seq > r->highest)
    {
        // A new highest: slide the window up, then mark the new top bit. A jump >= the window clears it.
        uint32_t shift = seq - r->highest;
        r->bitmap = (shift >= PC_ESP_REPLAY_WINDOW) ? 0u : (r->bitmap << shift);
        r->bitmap |= 1u;
        r->highest = seq;
        return PROTO_TRUE;
    }

    uint32_t offset = r->highest - seq;
    if (offset >= PC_ESP_REPLAY_WINDOW) // left of the window -> too old
    {
        return PROTO_FALSE;
    }
    uint64_t mask = (uint64_t)1 << offset;
    if (r->bitmap & mask) // already accepted -> replay
    {
        return PROTO_FALSE;
    }
    r->bitmap |= mask;
    return PROTO_TRUE;
}

#endif // PC_ENABLE_IKEV2
