// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file espnow.h
 * @brief ESP-NOW peer messaging with a typed envelope (PC_ENABLE_ESPNOW).
 *
 * Connectionless ESP32 peer-to-peer radio messaging (no AP, no IP) wrapped in a
 * small typed envelope so a receiver can demux by message type and reject a
 * truncated frame. Two layers:
 *
 *  - **Host-testable core (pure):** the 3-byte envelope codec
 *    (pc_espnow_encode / decode) and a bounded peer registry
 *    (PC_ESPNOW_MAX_PEERS, no heap). Same on host and ESP32.
 *  - **ESP32 binding (ARDUINO):** pc_espnow_begin / add_peer / send /
 *    broadcast over the esp_now API, delivering decoded frames to a callback -
 *    which the application can bridge to WebSocket/SSE.
 *
 * ESP-NOW's raw payload is capped at 250 bytes; the 3-byte envelope leaves
 * PC_ESPNOW_MAX_PAYLOAD for data. No stdlib, no heap.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_ESPNOW_H
#define PROTOCORE_ESPNOW_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_ESPNOW

/** @brief Envelope header size (magic + type + length). */
#define PC_ESPNOW_HDR 3
/** @brief Magic byte marking a library envelope. */
#define PC_ESPNOW_MAGIC 0xE5
/** @brief Max application payload per frame (250-byte radio MTU minus the header). */
#define PC_ESPNOW_MAX_PAYLOAD 247

/** @brief 6-byte broadcast address (send to all peers in range). */
extern const uint8_t PC_ESPNOW_BROADCAST[6];

// ---------------------------------------------------------------------------
// Host-testable core: envelope codec
// ---------------------------------------------------------------------------

/**
 * @brief Frame a message: [magic][type][len] + payload.
 * @return total bytes written to @p out, or 0 if it does not fit / payload too big.
 */
size_t pc_espnow_encode(uint8_t type, const uint8_t *payload, size_t len, uint8_t *out, size_t cap);

/**
 * @brief Validate and unpack a framed message.
 *
 * Checks the magic and that the declared length matches @p len exactly.
 * @param payload  set to point inside @p buf (no copy).
 * @return true on a well-formed envelope.
 */
bool pc_espnow_decode(const uint8_t *buf, size_t len, uint8_t *type, const uint8_t **payload, size_t *plen);

// ---------------------------------------------------------------------------
// Host-testable core: bounded peer registry
// ---------------------------------------------------------------------------

/** @brief Forget all registered peers. */
void pc_espnow_peers_reset(void);
/** @brief Register @p mac (idempotent). @return false if the table is full. */
bool pc_espnow_peer_add(const uint8_t mac[6]);
/** @brief @return true if @p mac is in the peer registry. */
bool pc_espnow_peer_has(const uint8_t mac[6]);
/** @brief Remove @p mac from the registry. @return true if it was present. */
bool pc_espnow_peer_remove(const uint8_t mac[6]);
/** @brief @return the number of registered peers. */
int pc_espnow_peer_count(void);

// ---------------------------------------------------------------------------
// ESP32 radio binding (returns false on host)
// ---------------------------------------------------------------------------

/** @brief Decoded-frame callback: sender MAC, message type, payload. */
typedef void (*pc_espnow_recv_fn)(const uint8_t mac[6], uint8_t type, const uint8_t *payload, size_t len);

/**
 * @brief Initialize ESP-NOW on @p channel and deliver decoded frames to @p cb.
 *
 * WiFi must already be started (STA or AP). Registers the broadcast peer.
 * @return true on success (ESP32 only).
 */
bool pc_espnow_begin(uint8_t channel, pc_espnow_recv_fn cb);

/** @brief Add a unicast peer to both the registry and the radio. */
bool pc_espnow_add_peer(const uint8_t mac[6]);

/** @brief Encode and transmit a message to @p mac. @return true if queued to the radio. */
bool pc_espnow_send(const uint8_t mac[6], uint8_t type, const uint8_t *payload, size_t len);

/** @brief Send to the broadcast address (all peers in range). */
bool pc_espnow_broadcast(uint8_t type, const uint8_t *payload, size_t len);

#endif // PC_ENABLE_ESPNOW
#endif // PROTOCORE_ESPNOW_H
