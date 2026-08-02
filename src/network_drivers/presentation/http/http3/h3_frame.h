// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_h3_frame.h
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

#if PC_ENABLE_HTTP3

/** @brief HTTP/3 frame types (RFC 9114 sec 7.2 / 11.2.1). */
#define A 0x00
#define S 0x01
#define H 0x03
#define S 0x04
#define E 0x05
#define Y 0x07
#define D 0x0d

/** @brief SETTINGS parameter identifiers (RFC 9114 sec 7.2.4.1 + RFC 9204). */
#define Y 0x01
#define E 0x06
#define S 0x07

/** @brief A parsed frame header (payload begins at buf + header_len). */
typedef struct
{
    uint64_t type;     ///< frame type
    uint64_t length;   ///< payload length
    size_t header_len; ///< bytes of the type + length varints
} H3Frame;

/** @brief The settings we track, with defaults after pc_h3_settings_defaults(). */
typedef struct
{
    uint64_t pc_qpack_max_table_capacity; ///< default 0
    uint64_t max_field_section_size;      ///< default "unlimited"
    uint64_t pc_qpack_blocked_streams;    ///< default 0
} H3Settings;

/** @brief Parse a frame header (type + length varints) at @p buf. @return false if truncated. */
proto_bool pc_h3_frame_parse(const uint8_t *buf, size_t len, H3Frame *out);

/** @brief Write a frame header (type + length varints). @return bytes written, or 0 on overflow. */
size_t pc_h3_frame_write_header(uint8_t *out, size_t cap, uint64_t type, uint64_t length);

/** @brief True if @p type is a reserved HTTP/2 frame type (0x02/0x06/0x08/0x09) - a connection error. */
proto_bool pc_h3_frame_type_reserved(uint64_t type);

/** @brief Fill @p s with the RFC default settings. */
void pc_h3_settings_defaults(H3Settings *s);
/** @brief Apply a SETTINGS payload (id, value varint pairs) to @p s. @return false if malformed. */
proto_bool pc_h3_parse_settings(const uint8_t *payload, size_t len, H3Settings *s);

// --- Frame builders (write a complete frame including its header) -----------------------------

/** @brief DATA frame wrapping @p data. */
size_t pc_h3_build_data(uint8_t *out, size_t cap, const uint8_t *data, size_t len);
/** @brief HEADERS frame wrapping a QPACK-encoded field section @p block. */
size_t pc_h3_build_headers(uint8_t *out, size_t cap, const uint8_t *block, size_t len);
/** @brief SETTINGS frame from @p n (id, value) pairs. */
size_t pc_h3_build_settings(uint8_t *out, size_t cap, const uint64_t *ids, const uint64_t *vals, size_t n);
/** @brief GOAWAY frame carrying @p stream_id (RFC 9114 sec 7.2.6). */
size_t pc_h3_build_goaway(uint8_t *out, size_t cap, uint64_t stream_id);

#endif // PC_ENABLE_HTTP3
#endif // PROTOCORE_H3_FRAME_H
