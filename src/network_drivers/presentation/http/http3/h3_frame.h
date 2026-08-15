// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_h3_frame.h
 * @brief HTTP/3 framing (RFC 9114 sec 7) over QUIC varints.
 *
 * An HTTP/3 frame is `Type (varint) | Length (varint) | Frame Payload`. This module parses that
 * header and builds the frames a server uses (DATA, HEADERS carrying a QPACK field section,
 * SETTINGS, GOAWAY), reads a SETTINGS payload, and flags the reserved HTTP/2 frame types that
 * must be treated as a connection error. Pure and host-tested.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_H3_FRAME_H
#define PROTOCORE_H3_FRAME_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_HTTP3

PROTOCORE_BEGIN_DECLS

/** @brief HTTP/3 frame types (RFC 9114 sec 7.2 / 11.2.1). */
#define H3_DATA 0x00
#define H3_HEADERS 0x01
#define H3_CANCEL_PUSH 0x03
#define H3_SETTINGS 0x04
#define H3_PUSH_PROMISE 0x05
#define H3_GOAWAY 0x07
#define H3_MAX_PUSH_ID 0x0d

/**
 * @brief HTTP/3 error codes (RFC 9114 sec 8.1).
 *
 * These travel in a CONNECTION_CLOSE of type 0x1d, whose error code comes from the application
 * protocol's space. Sent in the transport variant (0x1c) they would be read against RFC 9000
 * sec 20.1 instead, where 0x0100-0x01ff is CRYPTO_ERROR plus a TLS alert.
 */
#define H3_NO_ERROR 0x0100
#define H3_GENERAL_PROTOCOL_ERROR 0x0101
#define H3_INTERNAL_ERROR 0x0102
#define H3_STREAM_CREATION_ERROR 0x0103
#define H3_CLOSED_CRITICAL_STREAM 0x0104
#define H3_FRAME_UNEXPECTED 0x0105
#define H3_FRAME_ERROR 0x0106
#define H3_EXCESSIVE_LOAD 0x0107
#define H3_ID_ERROR 0x0108
#define H3_SETTINGS_ERROR 0x0109
#define H3_MISSING_SETTINGS 0x010a
#define H3_REQUEST_REJECTED 0x010b
#define H3_REQUEST_CANCELLED 0x010c
#define H3_REQUEST_INCOMPLETE 0x010d
#define H3_MESSAGE_ERROR 0x010e
#define H3_CONNECT_ERROR 0x010f
#define H3_VERSION_FALLBACK 0x0110

/** @brief SETTINGS parameter identifiers (RFC 9114 sec 7.2.4.1 + RFC 9204). */
#define H3_SETTINGS_QPACK_MAX_TABLE_CAPACITY 0x01
#define H3_SETTINGS_MAX_FIELD_SECTION_SIZE 0x06
#define H3_SETTINGS_QPACK_BLOCKED_STREAMS 0x07

/** @brief A parsed frame header (payload begins at buf + header_len). */
typedef struct
{
    uint64_t type;     ///< frame type
    uint64_t length;   ///< payload length
    size_t header_len; ///< bytes of the type + length varints
} H3Frame;

/** @brief The settings we track, with defaults after protocore_h3_settings_defaults(). */
typedef struct
{
    uint64_t protocore_qpack_max_table_capacity; ///< default 0
    uint64_t max_field_section_size;             ///< default "unlimited"
    uint64_t protocore_qpack_blocked_streams;    ///< default 0
} H3Settings;

/** @brief Parse a frame header (type + length varints) at @p buf. @return false if truncated. */
proto_bool protocore_h3_frame_parse(const uint8_t *buf, size_t len, H3Frame *out);

/** @brief Write a frame header (type + length varints). @return bytes written, or 0 on overflow. */
size_t protocore_h3_frame_write_header(uint8_t *out, size_t cap, uint64_t type, uint64_t length);

/** @brief True if @p type is a reserved HTTP/2 frame type (0x02/0x06/0x08/0x09) - a connection error. */
proto_bool protocore_h3_frame_type_reserved(uint64_t type);

/** @brief Fill @p s with the RFC default settings. */
void protocore_h3_settings_defaults(H3Settings *s);
/** @brief Apply a SETTINGS payload (id, value varint pairs) to @p s. @return false if malformed. */
proto_bool protocore_h3_parse_settings(const uint8_t *payload, size_t len, H3Settings *s);

// --- Frame builders (write a complete frame including its header) -----------------------------

/** @brief DATA frame wrapping @p data. */
size_t protocore_h3_build_data(uint8_t *out, size_t cap, const uint8_t *data, size_t len);
/** @brief HEADERS frame wrapping a QPACK-encoded field section @p block. */
size_t protocore_h3_build_headers(uint8_t *out, size_t cap, const uint8_t *block, size_t len);
/** @brief SETTINGS frame from @p n (id, value) pairs. */
size_t protocore_h3_build_settings(uint8_t *out, size_t cap, const uint64_t *ids, const uint64_t *vals, size_t n);
/** @brief GOAWAY frame carrying @p stream_id (RFC 9114 sec 7.2.6). */
size_t protocore_h3_build_goaway(uint8_t *out, size_t cap, uint64_t stream_id);

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP3

#endif // PROTOCORE_H3_FRAME_H
