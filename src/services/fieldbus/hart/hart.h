// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file hart.h
 * @brief HART / HART-IP process-instrument protocol codec (PROTOCORE_ENABLE_HART).
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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_HART

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

/** @brief HART frame delimiter frame-type bits (low 3 bits) + long-address bit (bit 7). Wire values,
 *  the LONG_ADDR bit is OR'd in, so integer constants in a namespacing struct (cast-free). */
#define HART_DELIM_BACK 0x01      ///< burst (field device, unsolicited).
#define HART_DELIM_STX 0x02       ///< master -> field device (request).
#define HART_DELIM_ACK 0x06       ///< field device -> master (response).
#define HART_DELIM_LONG_ADDR 0x80 ///< OR into the delimiter for 5-byte unique-ID addressing.

/** @brief HART-IP message types + common message ids (wire constants). */
#define HARTIP_MSG_REQUEST 0
#define HARTIP_MSG_RESPONSE 1
#define HARTIP_MSG_PUBLISH 2
#define HARTIP_ID_SESSION_INIT 0
#define HARTIP_ID_SESSION_CLOSE 1
#define HARTIP_ID_KEEPALIVE 2
#define HARTIP_ID_TOKEN_PDU 3 ///< a HART token-passing PDU (a HART frame) is the payload.
#define HARTIP_HEADER_LEN 8

/** @brief A parsed HART frame (pointers into the input buffer). */
typedef struct
{
    uint8_t delimiter;
    const uint8_t *addr;
    size_t addr_len; ///< 1 (short) or 5 (long), derived from the delimiter's long-address bit.
    uint8_t command;
    uint8_t byte_count;
    const uint8_t *data;
    size_t data_len;
} HartFrame;

/** @brief A parsed HART-IP message header + payload slice (the payload points into the input buffer). */
typedef struct
{
    uint8_t version;        ///< HART-IP protocol version (1)
    uint8_t msg_type;       ///< message type (HartIp::HARTIP_MSG_*)
    uint8_t msg_id;         ///< message id (HartIp::HARTIP_ID_*)
    uint8_t status;         ///< status / error byte (0 in a request)
    uint16_t seq;           ///< sequence number
    uint16_t total_len;     ///< total message length (header + payload) declared in the header
    const uint8_t *payload; ///< the payload after the 8-octet header, or nullptr if none
    size_t payload_len;     ///< payload length (total_len - 8)
} HartIpHeader;

/** @brief What checksum takes: bytes, len. */
typedef struct
{
    const uint8_t *bytes;
    size_t len;
} HartChecksumArgs;

/** @brief What build takes: delimiter, addr, addr_len, command, data, ... */
typedef struct
{
    uint8_t delimiter;   ///< frame delimiter (e.g. HART_DELIM_STX, OR HART_DELIM_LONG_ADDR for long addressing)
    const uint8_t *addr; ///< address bytes (1 for short, 5 for long)
    size_t addr_len;     ///< 1 or 5
    uint8_t command;     ///< HART command number
    const uint8_t *data; ///< data bytes (may be null when data_len == 0)
    size_t data_len;     ///< number of data bytes (also the frame's byte-count field)
    uint8_t *out;        ///< output buffer
    size_t cap;          ///< capacity of out
} HartBuildArgs;

/** @brief What parse takes: frame, len, out. */
typedef struct
{
    const uint8_t *frame;
    size_t len;
    HartFrame *out;
} HartParseArgs;

/** @brief What ip_build_header takes: msg_type, msg_id, status, seq, ... */
typedef struct
{
    uint8_t msg_type;   ///< HARTIP_MSG_*
    uint8_t msg_id;     ///< HARTIP_ID_*
    uint8_t status;     ///< status byte (0 in a request)
    uint16_t seq;       ///< sequence number
    uint16_t total_len; ///< total message length including this header (header + payload)
    uint8_t *out;
    size_t cap;
} HartIpBuildHeaderArgs;

/** @brief What ip_parse_header takes: buf, len, out. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    HartIpHeader *out;
} HartIpParseHeaderArgs;

/**
 * @brief HART / HART-IP process-instrument protocol codec (PROTOCORE_ENABLE_HART).
 *
 * A caller sets the members a call takes, invokes it through ::Hart with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   Hart.checksum_args.bytes = ...;
 *   Hart.checksum_args.len = ...;
 *   Hart.checksum(work);
 *   // Hart.value is what the call reports
 *
 * @var HartNs::checksum_args  what checksum takes: bytes, len
 * @var HartNs::build_args  what build takes: delimiter, addr, addr_len, command, data,
 * @var HartNs::parse_args  what parse takes: frame, len, out
 * @var HartNs::ip_build_header_args  what ip_build_header takes: msg_type, msg_id, status, seq,
 * @var HartNs::ip_parse_header_args  what ip_parse_header takes: buf, len, out
 * @var HartNs::ok  true if the frame is well-formed and the checksum matches; fills out
 * @var HartNs::value  the value a call reports
 * @var HartNs::n  the frame length written, or 0 if it would not fit or addr_len is ...
 * @var HartNs::checksum  longitudinal XOR checksum of len bytes (the HART frame check byte)
 * @var HartNs::build  build a HART command frame (no preamble - the transport prepends ...
 * @var HartNs::parse  validate + parse a HART frame (checksum checked)
 * @var HartNs::ip_build_header  build the 8-octet HART-IP message header into out (>= 8 bytes)
 * @var HartNs::ip_parse_header  parse an 8-octet HART-IP message header and expose its payload ...
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    HartChecksumArgs checksum_args;
    HartBuildArgs build_args;
    HartParseArgs parse_args;
    HartIpBuildHeaderArgs ip_build_header_args;
    HartIpParseHeaderArgs ip_parse_header_args;

    proto_bool ok;
    uint8_t value;
    size_t n;

    void (*const checksum)(uint8_t *restrict work);
    void (*const build)(uint8_t *restrict work);
    void (*const parse)(uint8_t *restrict work);
    void (*const ip_build_header)(uint8_t *restrict work);
    void (*const ip_parse_header)(uint8_t *restrict work);
} HartNs;

/** @brief The one symbol this module exports. */
extern HartNs Hart;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HART

#endif // PROTOCORE_HART_H
