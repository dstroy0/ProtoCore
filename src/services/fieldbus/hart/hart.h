// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hart.h
 * @brief HART / HART-IP process-instrument protocol codec (PC_ENABLE_HART).
 *
 * HART (Highway Addressable Remote Transducer, FieldComm) is the field-instrument protocol that rides
 * the 4-20 mA current loop as an FSK signal, and - as **HART-IP** - travels over UDP/TCP 5094 as the
 * gateway-friendly, front-end-free path. This is the wire codec for both:
 *
 *  - The **HART command frame**: `[delimiter][address...][command][byte-count][data...][checksum]`, where
 *    the checksum is the longitudinal XOR parity of every byte from the delimiter through the last data
 *    byte (the preamble of 0xFF sync bytes is transport, not checksummed). Short (1-byte polling) and
 *    long (5-byte unique-ID) addressing are both handled by passing the address bytes.
 *  - The **HART-IP message header** (8 octets): version, message type, message id, status, a 2-byte
 *    sequence number, and the 2-byte total message length - wraps a HART PDU for UDP/TCP transport.
 *
 * Pure, zero heap, no stdlib, host-testable. The FSK physical layer (a HART modem IC over UART) is the
 * hardware-gated path; HART-IP needs no front end.
 */

#ifndef PROTOCORE_HART_H
#define PROTOCORE_HART_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_HART

/** @brief HART frame delimiter frame-type bits (low 3 bits) + long-address bit (bit 7). Wire values,
 *  the LONG_ADDR bit is OR'd in, so integer constants in a namespacing struct (cast-free). */
struct HartDelim
{
    static constexpr uint8_t HART_DELIM_BACK = 0x01;      ///< burst (field device, unsolicited).
    static constexpr uint8_t HART_DELIM_STX = 0x02;       ///< master -> field device (request).
    static constexpr uint8_t HART_DELIM_ACK = 0x06;       ///< field device -> master (response).
    static constexpr uint8_t HART_DELIM_LONG_ADDR = 0x80; ///< OR into the delimiter for 5-byte unique-ID addressing.
};

/** @brief HART-IP message types + common message ids (wire constants). */
struct HartIp
{
    static constexpr uint8_t HARTIP_MSG_REQUEST = 0;
    static constexpr uint8_t HARTIP_MSG_RESPONSE = 1;
    static constexpr uint8_t HARTIP_MSG_PUBLISH = 2;
    static constexpr uint8_t HARTIP_ID_SESSION_INIT = 0;
    static constexpr uint8_t HARTIP_ID_SESSION_CLOSE = 1;
    static constexpr uint8_t HARTIP_ID_KEEPALIVE = 2;
    static constexpr uint8_t HARTIP_ID_TOKEN_PDU = 3; ///< a HART token-passing PDU (a HART frame) is the payload.
    static constexpr uint8_t HARTIP_HEADER_LEN = 8;
};

/** @brief Longitudinal XOR checksum of @p len bytes (the HART frame check byte). */
uint8_t pc_hart_checksum(const uint8_t *bytes, size_t len);

/**
 * @brief Build a HART command frame (no preamble - the transport prepends the 0xFF sync bytes).
 * @param delimiter frame delimiter (e.g. HART_DELIM_STX, OR HART_DELIM_LONG_ADDR for long addressing).
 * @param addr      address bytes (1 for short, 5 for long).
 * @param addr_len  1 or 5.
 * @param command   HART command number.
 * @param data      data bytes (may be null when data_len == 0).
 * @param data_len  number of data bytes (also the frame's byte-count field).
 * @param out       output buffer.
 * @param cap       capacity of @p out.
 * @return the frame length written, or 0 if it would not fit or addr_len is invalid.
 */
size_t pc_hart_build(uint8_t delimiter, const uint8_t *addr, size_t addr_len, uint8_t command, const uint8_t *data,
                     size_t data_len, uint8_t *out, size_t cap);

/** @brief A parsed HART frame (pointers into the input buffer). */
struct HartFrame
{
    uint8_t delimiter;
    const uint8_t *addr;
    size_t addr_len; ///< 1 (short) or 5 (long), derived from the delimiter's long-address bit.
    uint8_t command;
    uint8_t byte_count;
    const uint8_t *data;
    size_t data_len;
};

/**
 * @brief Validate + parse a HART frame (checksum checked).
 * @return true if the frame is well-formed and the checksum matches; fills @p out.
 */
bool pc_hart_parse(const uint8_t *frame, size_t len, HartFrame *out);

/**
 * @brief Build the 8-octet HART-IP message header into @p out (>= 8 bytes).
 * @param msg_type    HARTIP_MSG_*.
 * @param msg_id      HARTIP_ID_*.
 * @param status      status byte (0 in a request).
 * @param seq         sequence number.
 * @param total_len   total message length including this header (header + payload).
 * @return 8, or 0 if @p cap < 8.
 */
size_t pc_hartip_build_header(uint8_t msg_type, uint8_t msg_id, uint8_t status, uint16_t seq, uint16_t total_len,
                              uint8_t *out, size_t cap);

/** @brief A parsed HART-IP message header + payload slice (the payload points into the input buffer). */
struct HartIpHeader
{
    uint8_t version;        ///< HART-IP protocol version (1)
    uint8_t msg_type;       ///< message type (HartIp::HARTIP_MSG_*)
    uint8_t msg_id;         ///< message id (HartIp::HARTIP_ID_*)
    uint8_t status;         ///< status / error byte (0 in a request)
    uint16_t seq;           ///< sequence number
    uint16_t total_len;     ///< total message length (header + payload) declared in the header
    const uint8_t *payload; ///< the payload after the 8-octet header, or nullptr if none
    size_t payload_len;     ///< payload length (total_len - 8)
};

/**
 * @brief Parse an 8-octet HART-IP message header and expose its payload slice (e.g. the token-passing PDU).
 * @return true iff @p len >= 8 and the declared total length is 8..len (the whole message is present); false
 *         on a short buffer, a total length below the header, or a truncated message.
 */
bool pc_hartip_parse_header(const uint8_t *buf, size_t len, HartIpHeader *out);

#endif // PC_ENABLE_HART
#endif // PROTOCORE_HART_H
