// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_IKEV2

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

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
/** @brief What gcm_encapsulate takes: spi, seq, key, salt, iv, ... */
typedef struct
{
    uint32_t spi;
    uint32_t seq;
    const uint8_t *key;  ///< 32-byte AES-256 key (SK_ei / SK_er without the salt) PROTOCORE_ESP_KEY_LEN bytes.
    const uint8_t *salt; ///< the 4-byte salt (the ESP key's tail) PROTOCORE_ESP_SALT_LEN bytes.
    const uint8_t *iv;   ///< the 8-byte explicit IV (unique per packet under a key - e.g. the sequence number)
                         ///< PROTOCORE_ESP_IV_LEN bytes.
    uint8_t next_header;
    const uint8_t *payload;
    size_t payload_len;
    uint8_t *out;
    size_t out_cap;
} EspGcmEncapsulateArgs;
/** @brief What gcm_decapsulate takes: key, salt, packet, len, ... */
typedef struct
{
    const uint8_t *key;  ///< PROTOCORE_ESP_KEY_LEN bytes.
    const uint8_t *salt; ///< PROTOCORE_ESP_SALT_LEN bytes.
    uint8_t *packet;     ///< the ESP packet (mutated: decrypted in place). payload_out points into it on success
    size_t len;
    uint32_t *spi_out;
    uint32_t *seq_out;
    uint8_t *next_header_out;
    const uint8_t **payload_out;
    size_t *payload_len_out;
} EspGcmDecapsulateArgs;
/** @brief What replay_init takes: r. */
typedef struct
{
    EspReplay *r;
} EspReplayInitArgs;
/** @brief What replay_check takes: r, seq. */
typedef struct
{
    EspReplay *r;
    uint32_t seq;
} EspReplayCheckArgs;
/**
 * @brief ESP (RFC 4303) packet transform with AES-256-GCM (RFC 4106) - the IPsec datapath's crypto core.
 *
 * A caller sets the members a call takes, invokes it through ::Esp with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Esp.gcm_encapsulate_args.spi = ...;
 *   Esp.gcm_encapsulate_args.seq = ...;
 *   Esp.gcm_encapsulate_args.key = ...;
 *   Esp.gcm_encapsulate_args.salt = ...;
 *   Esp.gcm_encapsulate_args.iv = ...;
 *   Esp.gcm_encapsulate_args.next_header = ...;
 *   Esp.gcm_encapsulate_args.payload = ...;
 *   Esp.gcm_encapsulate_args.payload_len = ...;
 *   Esp.gcm_encapsulate_args.out = ...;
 *   Esp.gcm_encapsulate_args.out_cap = ...;
 *   Esp.gcm_encapsulate(work);
 *   // Esp.n is what the call reports
 *
 * @var EspNs::gcm_encapsulate_args  what gcm_encapsulate takes: spi, seq, key, salt, iv,
 * @var EspNs::gcm_decapsulate_args  what gcm_decapsulate takes: key, salt, packet, len,
 * @var EspNs::replay_init_args  what replay_init takes: r
 * @var EspNs::replay_check_args  what replay_check takes: r, seq
 * @var EspNs::ok  true iff the ICV verifies (a forged / truncated packet returns ...
 * @var EspNs::n  the ESP packet length written, or 0 on a bad argument / overflow
 * @var EspNs::gcm_encapsulate  encapsulate payload in an RFC 4303 ESP packet with AES-256-GCM. ...
 * @var EspNs::gcm_decapsulate  verify + decapsulate an ESP packet in place (the ciphertext is ...
 * @var EspNs::replay_init  reset an anti-replay window (no packets seen yet)
 * @var EspNs::replay_check  anti-replay check + record for a received sequence number seq (RFC ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    EspGcmEncapsulateArgs gcm_encapsulate_args;
    EspGcmDecapsulateArgs gcm_decapsulate_args;
    EspReplayInitArgs replay_init_args;
    EspReplayCheckArgs replay_check_args;
    proto_bool ok;
    size_t n;
} EspVars;

/** @brief The operands and the outcome. */
extern EspVars EspV;

/** @brief The entries. */
typedef struct
{
    void (*const gcm_encapsulate)(uint8_t *restrict work);
    void (*const gcm_decapsulate)(uint8_t *restrict work);
    void (*const replay_init)(uint8_t *restrict work);
    void (*const replay_check)(uint8_t *restrict work);
} EspNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in EspV or a region of the borrow at a fixed offset.
void protocore_esp_gcm_encapsulate(uint8_t *restrict work);
void protocore_esp_gcm_decapsulate(uint8_t *restrict work);
void protocore_esp_replay_init(uint8_t *restrict work);
void protocore_esp_replay_check(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Esp.gcm_encapsulate(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const EspNs Esp __attribute__((unused)) = {
    .gcm_encapsulate = protocore_esp_gcm_encapsulate,
    .gcm_decapsulate = protocore_esp_gcm_decapsulate,
    .replay_init = protocore_esp_replay_init,
    .replay_check = protocore_esp_replay_check,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_IKEV2

#endif // PROTOCORE_ESP_H
