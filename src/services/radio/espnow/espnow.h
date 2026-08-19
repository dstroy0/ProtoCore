// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file espnow.h
 * @brief ESP-NOW peer messaging with a typed envelope (PROTOCORE_ENABLE_ESPNOW).
 *
 * Connectionless ESP32 peer-to-peer radio messaging (no AP, no IP) wrapped in a
 * small typed envelope so a receiver can demux by message type and reject a
 * truncated frame. Two layers:
 *
 *  - **Host-testable core (pure):** the 3-byte envelope codec
 *    (protocore_espnow_encode / decode) and a bounded peer registry
 *    (PROTOCORE_ESPNOW_MAX_PEERS, no heap). Same on host and ESP32.
 *  - **ESP32 binding (ARDUINO):** protocore_espnow_begin / add_peer / send /
 *    broadcast over the esp_now API, delivering decoded frames to a callback -
 *    which the application can bridge to WebSocket/SSE.
 *
 * ESP-NOW's raw payload is capped at 250 bytes; the 3-byte envelope leaves
 * PROTOCORE_ESPNOW_MAX_PAYLOAD for data. No stdlib, no heap.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_ESPNOW_H
#define PROTOCORE_ESPNOW_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_ESPNOW

PROTOCORE_BEGIN_DECLS

// PROTOCORE_ESPNOW_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. A caller takes them once and passes the pointer to every call. How they
// are carved is this module's and is never named here.

/** @brief Envelope header size (magic + type + length). */
#define PROTOCORE_ESPNOW_HDR 3

/** @brief Magic byte marking a library envelope. */
#define PROTOCORE_ESPNOW_MAGIC 0xE5

/** @brief Max application payload per frame (250-byte radio MTU minus the header). */
#define PROTOCORE_ESPNOW_MAX_PAYLOAD 247

/** @brief Decoded-frame callback: sender MAC, message type, payload. */
typedef void (*protocore_espnow_recv_fn)(const uint8_t mac[6], uint8_t type, const uint8_t *payload, size_t len);

/** @brief What encode takes: type, payload, len, out, cap. */
typedef struct
{
    uint8_t type;
    const uint8_t *payload;
    size_t len;
    uint8_t *out;
    size_t cap;
} EspnowEncodeArgs;

/** @brief What decode takes: buf, len, type, payload, plen. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    uint8_t *type;
    const uint8_t **payload; ///< set to point inside buf (no copy)
    size_t *plen;
} EspnowDecodeArgs;

/** @brief What peer_add takes: mac. */
typedef struct
{
    const uint8_t *mac; ///< 6 bytes.
} EspnowPeerAddArgs;

/** @brief What peer_has takes: mac. */
typedef struct
{
    const uint8_t *mac; ///< 6 bytes.
} EspnowPeerHasArgs;

/** @brief What peer_remove takes: mac. */
typedef struct
{
    const uint8_t *mac; ///< 6 bytes.
} EspnowPeerRemoveArgs;

/** @brief What begin takes: channel, cb. */
typedef struct
{
    uint8_t channel;
    protocore_espnow_recv_fn cb;
} EspnowBeginArgs;

/** @brief What add_peer takes: mac. */
typedef struct
{
    const uint8_t *mac; ///< 6 bytes.
} EspnowAddPeerArgs;

/** @brief What send takes: mac, type, payload, len. */
typedef struct
{
    const uint8_t *mac; ///< 6 bytes.
    uint8_t type;
    const uint8_t *payload;
    size_t len;
} EspnowSendArgs;

/** @brief What broadcast takes: type, payload, len. */
typedef struct
{
    uint8_t type;
    const uint8_t *payload;
    size_t len;
} EspnowBroadcastArgs;

/**
 * @brief ESP-NOW peer messaging with a typed envelope (PROTOCORE_ENABLE_ESPNOW).
 *
 * A caller sets the members a call takes, invokes it through ::Espnow with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Espnow.encode_args.type = ...;
 *   Espnow.encode_args.payload = ...;
 *   Espnow.encode_args.len = ...;
 *   Espnow.encode_args.out = ...;
 *   Espnow.encode_args.cap = ...;
 *   Espnow.encode(work);
 *   // Espnow.n is what the call reports
 *
 * @var EspnowNs::encode_args  what encode takes: type, payload, len, out, cap
 * @var EspnowNs::decode_args  what decode takes: buf, len, type, payload, plen
 * @var EspnowNs::peer_add_args  what peer_add takes: mac
 * @var EspnowNs::peer_has_args  what peer_has takes: mac
 * @var EspnowNs::peer_remove_args  what peer_remove takes: mac
 * @var EspnowNs::begin_args  what begin takes: channel, cb
 * @var EspnowNs::add_peer_args  what add_peer takes: mac
 * @var EspnowNs::send_args  what send takes: mac, type, payload, len
 * @var EspnowNs::broadcast_args  what broadcast takes: type, payload, len
 * @var EspnowNs::ok  true on a well-formed envelope
 * @var EspnowNs::n  total bytes written to out, or 0 if it does not fit / payload too ...
 * @var EspnowNs::encode  frame a message: [magic][type][len] + payload
 * @var EspnowNs::decode  validate and unpack a framed message. Checks the magic and that the ...
 * @var EspnowNs::peers_reset  forget all registered peers
 * @var EspnowNs::peer_add  register mac (idempotent). false if the table is full
 * @var EspnowNs::peer_has  true if mac is in the peer registry
 * @var EspnowNs::peer_remove  remove mac from the registry. true if it was present
 * @var EspnowNs::peer_count  the number of registered peers
 * @var EspnowNs::begin  initialize ESP-NOW on channel and deliver decoded frames to cb. ...
 * @var EspnowNs::add_peer  add a unicast peer to both the registry and the radio
 * @var EspnowNs::send  encode and transmit a message to mac. true if queued to the radio
 * @var EspnowNs::broadcast  send to the broadcast address (all peers in range)
 *
 * @c work is PROTOCORE_ESPNOW_BORROW bytes the CALLER took, at an address it knows. It arrives
 * @c restrict and is not held past the call, so nothing here aliases it. How those bytes are
 * carved is this module's and is never named here.
 */
typedef struct
{
    EspnowEncodeArgs encode_args;
    EspnowDecodeArgs decode_args;
    EspnowPeerAddArgs peer_add_args;
    EspnowPeerHasArgs peer_has_args;
    EspnowPeerRemoveArgs peer_remove_args;
    EspnowBeginArgs begin_args;
    EspnowAddPeerArgs add_peer_args;
    EspnowSendArgs send_args;
    EspnowBroadcastArgs broadcast_args;

    proto_bool ok;
    size_t n;

    void (*const encode)(uint8_t *restrict work);
    void (*const decode)(uint8_t *restrict work);
    void (*const peers_reset)(uint8_t *restrict work);
    void (*const peer_add)(uint8_t *restrict work);
    void (*const peer_has)(uint8_t *restrict work);
    void (*const peer_remove)(uint8_t *restrict work);
    void (*const peer_count)(uint8_t *restrict work);
    void (*const begin)(uint8_t *restrict work);
    void (*const add_peer)(uint8_t *restrict work);
    void (*const send)(uint8_t *restrict work);
    void (*const broadcast)(uint8_t *restrict work);
} EspnowNs;

/** @brief The 6-octet broadcast address every peer accepts, and always a peer itself. */
extern const uint8_t PROTOCORE_ESPNOW_BROADCAST[6];

/** @brief The one symbol this module exports. */
extern EspnowNs Espnow;

/**
 * @brief The PROTOCORE_ESPNOW_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_espnow_span(void);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_ESPNOW

#endif // PROTOCORE_ESPNOW_H
