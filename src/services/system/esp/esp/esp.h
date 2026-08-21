// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_ESP_H
#define PROTOCORE_ESP_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file esp.h
 * @brief ESP (RFC 4303) packet transform with AES-256-GCM (RFC 4106) - the IPsec datapath's crypto core.
 *
 * Tier 3 of the IPsec roadmap item is the ESP datapath. Its two halves separate cleanly: this pure,
 * host-testable PACKET transform (encapsulate a payload into an ESP packet / verify + decapsulate one),
 * and the device-side network-layer integration (hooking lwIP's IP input/output + the SAD/SPD), which is
 * a separate, later track. This file is only the transform, gated with the IKEv2 feature (its Child-SA
 * keys - SK_ei / SK_er from Ike.child_keymat - drive it) and reusing the library's AES-256-GCM.
 *
 * Wire layout (RFC 4303 §2, AES-GCM per RFC 4106):
 * SPI(4) | Sequence Number(4) | IV(8, explicit) | { AES-GCM: Payload | Padding | Pad Length | Next
 * Header } | ICV(16).
 * The AEAD authenticates SPI | Seq as additional data; the nonce is the 4-byte salt (from the ESP key)
 * concatenated with the 8-byte explicit IV. Padding right-aligns Pad Length + Next Header to a 4-octet
 * boundary and holds the RFC 4303 monotonic bytes 1, 2, 3 ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_ESP_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

/** @brief ESP header size: SPI(4) + Sequence Number(4). */
#define PROTOCORE_ESP_HDR_LEN 8

/** @brief Explicit IV length carried in the packet (AES-GCM, RFC 4106). */
#define PROTOCORE_ESP_IV_LEN 8

/** @brief Implicit salt length (the tail of the ESP key, not on the wire). */
#define PROTOCORE_ESP_SALT_LEN 4

/** @brief AES-GCM authentication tag / ICV length. */
#define PROTOCORE_ESP_ICV_LEN 16

/** @brief AES-256 key length. */
#define PROTOCORE_ESP_KEY_LEN 32

/** @brief ESP anti-replay window size (fixed by the 64-bit bitmap). */
#define PROTOCORE_ESP_REPLAY_WINDOW 64

/** @brief Anti-replay sliding-window state for one inbound SA (zero-heap). */
typedef struct
{
    uint32_t highest;    ///< highest accepted sequence number so far
    uint64_t bitmap;     ///< bit i set = (highest - i) already accepted (bit 0 = highest itself)
    proto_bool seen_any; ///< false until the first packet is accepted
} EspReplay;

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    size_t (*gcm_encapsulate)(uint8_t *restrict, uint32_t, uint32_t, const uint8_t *, const uint8_t *, const uint8_t *,
                              uint8_t, const uint8_t *, size_t, uint8_t *, size_t);
    proto_bool (*gcm_decapsulate)(uint8_t *restrict, const uint8_t *, const uint8_t *, uint8_t *, size_t, uint32_t *,
                                  uint32_t *, uint8_t *, const uint8_t **, size_t *);
    void (*replay_init)(uint8_t *restrict, EspReplay *);
    proto_bool (*replay_check)(uint8_t *restrict, EspReplay *, uint32_t);
} EspNs;
PROTOCORE_NS_LAYOUT(EspNs, gcm_encapsulate, gcm_decapsulate, replay_init, replay_check);

/**
 * @brief Encapsulate payload in an RFC 4303 ESP packet with AES-256-GCM. .
 * @param work PROTOCORE_ESP_BORROW bytes the caller took. Not held past the call.
 * @param spi Spi
 * @param seq Seq
 * @param key 32-byte AES-256 key (SK_ei / SK_er without the salt) PROTOCORE_ESP_KEY_LEN bytes
 * @param salt the 4-byte salt (the ESP key's tail) PROTOCORE_ESP_SALT_LEN bytes
 * @param iv the 8-byte explicit IV (unique per packet under a key - e.g. the sequence number)
 * @param next_header Next header
 * @param payload Payload
 * @param payload_len Payload len
 * @param out Out
 * @param out_cap Out cap
 * @return The size_t.
 */
size_t protocore_esp_gcm_encapsulate(uint8_t *restrict work, uint32_t spi, uint32_t seq, const uint8_t *key,
                                     const uint8_t *salt, const uint8_t *iv, uint8_t next_header,
                                     const uint8_t *payload, size_t payload_len, uint8_t *out, size_t out_cap);
/**
 * @brief Verify + decapsulate an ESP packet in place (the ciphertext is .
 * @param work PROTOCORE_ESP_BORROW bytes the caller took. Not held past the call.
 * @param key PROTOCORE_ESP_KEY_LEN bytes
 * @param salt PROTOCORE_ESP_SALT_LEN bytes
 * @param packet the ESP packet (mutated: decrypted in place). payload_out points into it on success
 * @param len Len
 * @param spi_out Spi out
 * @param seq_out Seq out
 * @param next_header_out Next header out
 * @param payload_out Payload out
 * @param payload_len_out Payload len out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_esp_gcm_decapsulate(uint8_t *restrict work, const uint8_t *key, const uint8_t *salt,
                                         uint8_t *packet, size_t len, uint32_t *spi_out, uint32_t *seq_out,
                                         uint8_t *next_header_out, const uint8_t **payload_out,
                                         size_t *payload_len_out);
/**
 * @brief Reset an anti-replay window (no packets seen yet).
 * @param work PROTOCORE_ESP_BORROW bytes the caller took. Not held past the call.
 * @param r R
 */
void protocore_esp_replay_init(uint8_t *restrict work, EspReplay *r);
/**
 * @brief Anti-replay check + record for a received sequence number seq (RFC .
 * @param work PROTOCORE_ESP_BORROW bytes the caller took. Not held past the call.
 * @param r R
 * @param seq Seq
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_esp_replay_check(uint8_t *restrict work, EspReplay *r, uint32_t seq);

/** @brief Module namespace. */
PROTOCORE_NS EspNs Esp PROTOCORE_UNUSED = {.gcm_encapsulate = protocore_esp_gcm_encapsulate,
                                           .gcm_decapsulate = protocore_esp_gcm_decapsulate,
                                           .replay_init = protocore_esp_replay_init,
                                           .replay_check = protocore_esp_replay_check};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ESP_H
