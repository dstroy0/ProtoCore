// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file esp.h
 * @brief ESP (RFC 4303) packet transform with AES-256-GCM (RFC 4106) - the IPsec datapath's crypto core.
 *
 * Tier 3 of the IPsec roadmap item is the ESP datapath. Its two halves separate cleanly: this pure,
 * host-testable PACKET transform (encapsulate a payload into an ESP packet / verify + decapsulate one),
 * and the device-side network-layer integration (hooking lwIP's IP input/output + the SAD/SPD), which is
 * a separate, later track. This file is only the transform, gated with the IKEv2 feature (its Child-SA
 * keys - SK_ei / SK_er from pc_ike_child_keymat - drive it) and reusing the library's AES-256-GCM.
 *
 * Wire layout (RFC 4303 §2, AES-GCM per RFC 4106):
 *   SPI(4) | Sequence Number(4) | IV(8, explicit) | { AES-GCM: Payload | Padding | Pad Length | Next
 *   Header } | ICV(16).
 * The AEAD authenticates SPI | Seq as additional data; the nonce is the 4-byte salt (from the ESP key)
 * concatenated with the 8-byte explicit IV. Padding right-aligns Pad Length + Next Header to a 4-octet
 * boundary and holds the RFC 4303 monotonic bytes 1, 2, 3 ...
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_ESP_H
#define PROTOCORE_ESP_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_IKEV2

/** @brief ESP header size: SPI(4) + Sequence Number(4). */
#define PC_ESP_HDR_LEN 8
/** @brief Explicit IV length carried in the packet (AES-GCM, RFC 4106). */
#define PC_ESP_IV_LEN 8
/** @brief Implicit salt length (the tail of the ESP key, not on the wire). */
#define PC_ESP_SALT_LEN 4
/** @brief AES-GCM authentication tag / ICV length. */
#define PC_ESP_ICV_LEN 16
/** @brief AES-256 key length. */
#define PC_ESP_KEY_LEN 32

/**
 * @brief Encapsulate @p payload in an RFC 4303 ESP packet with AES-256-GCM.
 *
 * Writes SPI | Seq | @p iv | AES-GCM(payload | Padding | Pad Length | @p next_header) | ICV into @p out.
 * The AAD is SPI | Seq; the GCM nonce is @p salt | @p iv; the plaintext is padded (RFC 4303 §2.4) so that
 * Pad Length + Next Header land on a 4-octet boundary.
 * @param key   32-byte AES-256 key (SK_ei / SK_er without the salt).
 * @param salt  the 4-byte salt (the ESP key's tail).
 * @param iv    the 8-byte explicit IV (unique per packet under a key - e.g. the sequence number).
 * @return the ESP packet length written, or 0 on a bad argument / overflow.
 */
size_t pc_esp_gcm_encapsulate(uint32_t spi, uint32_t seq, const uint8_t key[PC_ESP_KEY_LEN],
                              const uint8_t salt[PC_ESP_SALT_LEN], const uint8_t iv[PC_ESP_IV_LEN], uint8_t next_header,
                              const uint8_t *payload, size_t payload_len, uint8_t *out, size_t out_cap);

/**
 * @brief Verify + decapsulate an ESP packet in place (the ciphertext is decrypted within @p packet).
 *
 * Verifies the ICV over SPI | Seq in constant time, decrypts, and strips the RFC 4303 trailer, exposing
 * the inner payload plus its Next Header and the SPI / Sequence Number.
 * @param packet  the ESP packet (mutated: decrypted in place). @param payload_out points into it on success.
 * @return true iff the ICV verifies (a forged / truncated packet returns false and writes no payload).
 */
proto_bool pc_esp_gcm_decapsulate(const uint8_t key[PC_ESP_KEY_LEN], const uint8_t salt[PC_ESP_SALT_LEN],
                                  uint8_t *packet, size_t len, uint32_t *spi_out, uint32_t *seq_out,
                                  uint8_t *next_header_out, const uint8_t **payload_out, size_t *payload_len_out);

// ── ESP anti-replay window (RFC 4303 §3.4.3) ───────────────────────────────────────────────────
//
// One receiver-side sliding window per inbound SA rejects replayed or too-old sequence numbers. This is
// the 32-bit-sequence (non-ESN) window; the bitmap is a single 64-bit word, so the window is 64 packets.

/** @brief ESP anti-replay window size (fixed by the 64-bit bitmap). */
#define PC_ESP_REPLAY_WINDOW 64

/** @brief Anti-replay sliding-window state for one inbound SA (zero-heap). */
typedef struct
{
    uint32_t highest;    ///< highest accepted sequence number so far
    uint64_t bitmap;     ///< bit i set = (highest - i) already accepted (bit 0 = highest itself)
    proto_bool seen_any; ///< false until the first packet is accepted
} EspReplay;

/** @brief Reset an anti-replay window (no packets seen yet). */
void pc_esp_replay_init(EspReplay *r);

/**
 * @brief Anti-replay check + record for a received sequence number @p seq (RFC 4303 §3.4.3).
 *
 * Accepts and records a fresh @p seq (advancing the window when it is the new highest); rejects a
 * duplicate already inside the window (a replay) or one that falls left of the window (too old).
 * Sequence number 0 is invalid (ESP counts from 1) and is rejected.
 * @return true to accept the packet, false to drop it.
 */
proto_bool pc_esp_replay_check(EspReplay *r, uint32_t seq);

#endif // PC_ENABLE_IKEV2

PROTO_END_DECLS

#endif // PROTOCORE_ESP_H
