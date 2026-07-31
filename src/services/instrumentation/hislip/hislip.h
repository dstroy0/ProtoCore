// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hislip.h
 * @brief HiSLIP (High-Speed LAN Instrument Protocol) message codec (PC_ENABLE_HISLIP) - a zero-heap
 *        codec for the IVI Foundation's modern LXI instrument transport (IVI-6.1, HiSLIP 2.0) on
 *        TCP port 4880, the successor to VXI-11 that carries SCPI at higher throughput.
 *
 * A HiSLIP session runs over TWO TCP connections to the same port 4880 - a synchronous channel
 * (the ordered SCPI command/response stream: Data / DataEND, Trigger, device-clear) and an
 * asynchronous channel (out-of-band control: lock, status/SRQ, remote-local, interrupt) - bound by
 * a 16-bit SessionID negotiated in the handshake.
 *
 * Every message is a fixed 16-byte header optionally followed by a payload:
 * @code
 *   "HS" (2)  MessageType (1)  ControlCode (1)  MessageParameter (4, BE)  PayloadLength (8, BE)
 * @endcode
 * This codec builds + parses that header (@ref pc_hislip_build_header / @ref pc_hislip_parse_header),
 * the Initialize / AsyncInitialize handshake (the MessageParameter carries the protocol version +
 * vendor id, then the negotiated version + SessionID), and the Data / DataEND messages that carry a
 * SCPI payload keyed by a MessageID. Pairs with @c PC_ENABLE_SCPI (the payload). Pure codec,
 * host-tested; the two TCP connections are the application's.
 *
 * Reference: IVI-6.1 "IVI High-Speed LAN Instrument Protocol (HiSLIP)" v2.0 (2020-04-23).
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_HISLIP_H
#define PROTOCORE_HISLIP_H

#include "protocore_config.h"

#if PC_ENABLE_HISLIP

#include <stddef.h>
#include <stdint.h>

/** @brief The IANA-assigned HiSLIP TCP port (both channels connect here). */
#define PC_HISLIP_PORT 4880

/** @brief The fixed header length: prologue(2) + type(1) + control(1) + parameter(4) + length(8). */
#define PC_HISLIP_HEADER_LEN 16

/** @brief Protocol version words (`<major><minor>`), encoded in the high 16 bits of the handshake
 *  MessageParameter. The client offers its max; the server returns min(client, server). */
#define PC_HISLIP_VERSION_1_0 0x0100
#define PC_HISLIP_VERSION_1_1 0x0101
#define PC_HISLIP_VERSION_2_0 0x0200

/** @brief The MessageID a client starts at; each subsequent Data/DataEND/Trigger increments by 2
 *  (unsigned 32-bit, wraps). A server response echoes the request's MessageID. */
#define PC_HISLIP_MESSAGE_ID_INIT 0xFFFFFF00u

// ControlCode bits carried by an InitializeResponse:
#define PC_HISLIP_INITRESP_OVERLAP 0x01       ///< bit 0: prefer overlapped (vs synchronized) mode
#define PC_HISLIP_INITRESP_ENC_MANDATORY 0x02 ///< bit 1: encryption mandatory (2.0)
#define PC_HISLIP_INITRESP_ENC_INITIAL 0x04   ///< bit 2: initial encryption required (2.0)
// ControlCode bit carried by a Data or DataEND message:
#define PC_HISLIP_DATA_RMT_DELIVERED 0x01 ///< bit 0: message delivered following a Response Message Terminator

/** @brief HiSLIP MessageType codes (IVI-6.1). Codes 0-24 are HiSLIP 1.x; 25-38 were added in 2.0. */
enum class HislipMsg : uint8_t
{
    INITIALIZE = 0,
    INITIALIZE_RESPONSE = 1,
    FATAL_ERROR = 2,
    ERROR = 3,
    ASYNC_LOCK = 4,
    ASYNC_LOCK_RESPONSE = 5,
    DATA = 6,
    DATA_END = 7,
    DEVICE_CLEAR_COMPLETE = 8,
    DEVICE_CLEAR_ACKNOWLEDGE = 9,
    ASYNC_REMOTE_LOCAL_CONTROL = 10,
    ASYNC_REMOTE_LOCAL_RESPONSE = 11,
    TRIGGER = 12,
    INTERRUPTED = 13,
    ASYNC_INTERRUPTED = 14,
    ASYNC_MAX_MSG_SIZE = 15,
    ASYNC_MAX_MSG_SIZE_RESPONSE = 16,
    ASYNC_INITIALIZE = 17,
    ASYNC_INITIALIZE_RESPONSE = 18,
    ASYNC_DEVICE_CLEAR = 19,
    ASYNC_SERVICE_REQUEST = 20,
    ASYNC_STATUS_QUERY = 21,
    ASYNC_STATUS_RESPONSE = 22,
    ASYNC_DEVICE_CLEAR_ACKNOWLEDGE = 23,
    ASYNC_LOCK_INFO = 24,
    ASYNC_LOCK_INFO_RESPONSE = 25,
    GET_DESCRIPTORS = 26,
    GET_DESCRIPTORS_RESPONSE = 27,
    START_TLS = 28,
    ASYNC_START_TLS = 29,
    ASYNC_START_TLS_RESPONSE = 30,
    END_TLS = 31,
    ASYNC_END_TLS = 32,
    ASYNC_END_TLS_RESPONSE = 33,
    GET_SASL_MECHANISM_LIST = 34,
    GET_SASL_MECHANISM_LIST_RESPONSE = 35,
    AUTHENTICATION_START = 36,
    AUTHENTICATION_EXCHANGE = 37,
    AUTHENTICATION_RESULT = 38,
};

/** @brief A decoded HiSLIP header. */
struct HislipHeader
{
    HislipMsg type;
    uint8_t control;      ///< ControlCode (message-specific flag; 0 when undefined)
    uint32_t parameter;   ///< MessageParameter (message-specific; 0 when undefined)
    uint64_t payload_len; ///< byte length of the payload that follows the 16-byte header
};

/**
 * @brief Build the 16-byte header into @p buf.
 * @return 16 (@ref PC_HISLIP_HEADER_LEN), or 0 if @p cap < 16 or @p buf is null.
 */
size_t pc_hislip_build_header(uint8_t *buf, size_t cap, HislipMsg type, uint8_t control, uint32_t parameter,
                              uint64_t payload_len);

/**
 * @brief Parse a 16-byte header from the head of [buf, buf+len).
 * @return true on a valid `"HS"` prologue with @p len >= 16; false otherwise.
 * @note The message type is copied through even if beyond 38 (forward-compat); the caller decides.
 */
bool pc_hislip_parse_header(const uint8_t *buf, size_t len, HislipHeader *out);

// ── handshake builders ─────────────────────────────────────────────────────────────────────────

/**
 * @brief Build an Initialize message (client -> server, sync channel): parameter = (version << 16)
 *        | vendor_id, payload = the sub-address string (e.g. "hislip0").
 * @return total bytes written (16 + sub-address length), or 0 on overflow / bad input.
 */
size_t pc_hislip_build_initialize(uint8_t *buf, size_t cap, uint16_t protocol_version, uint16_t vendor_id,
                                  const char *sub_address);

/**
 * @brief Build an InitializeResponse (server -> client): control (overlap / encryption bits),
 *        parameter = (negotiated version << 16) | session_id, no payload.
 * @return 16, or 0 on overflow.
 */
size_t pc_hislip_build_initialize_response(uint8_t *buf, size_t cap, uint8_t control, uint16_t protocol_version,
                                           uint16_t session_id);

/**
 * @brief Build an AsyncInitialize (client -> server, async channel): parameter = session_id, no payload.
 * @return 16, or 0 on overflow.
 */
size_t pc_hislip_build_async_initialize(uint8_t *buf, size_t cap, uint16_t session_id);

/**
 * @brief Build an AsyncInitializeResponse (server -> client): parameter = server_vendor_id, no payload.
 * @return 16, or 0 on overflow.
 */
size_t pc_hislip_build_async_initialize_response(uint8_t *buf, size_t cap, uint8_t control, uint16_t server_vendor_id);

/**
 * @brief Build a Data (@p is_end false) or DataEND (@p is_end true) message carrying @p payload
 *        keyed by @p message_id (parameter). @p control is usually 0 (set @ref
 *        PC_HISLIP_DATA_RMT_DELIVERED on a server response after a terminator).
 * @return total bytes written (16 + payload_len), or 0 on overflow / bad input.
 */
size_t pc_hislip_build_data(uint8_t *buf, size_t cap, bool is_end, uint8_t control, uint32_t message_id,
                            const uint8_t *payload, size_t payload_len);

/** @brief The next client MessageID (increments by 2, wraps) - see @ref PC_HISLIP_MESSAGE_ID_INIT. */
uint32_t pc_hislip_next_message_id(uint32_t id);

// ── handshake parsers ──────────────────────────────────────────────────────────────────────────

/** @brief A decoded Initialize message. @ref sub_address points INTO the source buffer. */
struct HislipInitialize
{
    uint16_t protocol_version;
    uint16_t vendor_id;
    const char *sub_address;
    size_t sub_address_len;
};

/**
 * @brief Parse a full Initialize message (header + payload) from [buf, buf+len).
 * @return true on a complete, well-formed Initialize; false otherwise.
 */
bool pc_hislip_parse_initialize(const uint8_t *buf, size_t len, HislipInitialize *out);

/** @brief A decoded InitializeResponse message. */
struct HislipInitializeResponse
{
    uint16_t protocol_version;
    uint16_t session_id;
    bool overlap;              ///< ControlCode bit 0 (prefer overlapped)
    bool encryption_mandatory; ///< ControlCode bit 1 (2.0)
};

/**
 * @brief Parse an InitializeResponse header from [buf, buf+len).
 * @return true on a well-formed InitializeResponse; false otherwise.
 */
bool pc_hislip_parse_initialize_response(const uint8_t *buf, size_t len, HislipInitializeResponse *out);

#endif // PC_ENABLE_HISLIP

#endif // PROTOCORE_HISLIP_H
