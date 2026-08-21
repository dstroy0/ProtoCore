// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#ifndef PROTOCORE_H3_FRAME_H
#define PROTOCORE_H3_FRAME_H

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

PROTOCORE_BEGIN_DECLS

/**
 * @file h3_frame.h
 * @brief HTTP/3 framing (RFC 9114 sec 7) over QUIC varints.
 *
 * An HTTP/3 frame is `Type (varint) | Length (varint) | Frame Payload`. This module parses that
 * header and builds the frames a server uses (DATA, HEADERS carrying a QPACK field section,
 * SETTINGS, GOAWAY), reads a SETTINGS payload, and flags the reserved HTTP/2 frame types that
 * must be treated as a connection error. Pure and host-tested.
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

// PROTOCORE_H3_FRAME_BORROW - the bytes this module runs out of - is stated in protocore_config.h, which sums
// it into its arena. Its size and its offset are each a static_assert, so a feature
// combination that does not fit fails to compile rather than overrunning at run time.

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
} H3FrameHeader;

/** @brief The settings we track, with defaults after ::H3FrameNs::settings_defaults. */
typedef struct
{
    uint64_t qpack_max_table_capacity; ///< default 0
    uint64_t max_field_section_size;   ///< default "unlimited"
    uint64_t qpack_blocked_streams;    ///< default 0
} H3Settings;

/** @brief Dispatch table. Addressed by offset, so the layout is asserted below. */
typedef struct
{
    proto_bool (*parse_header)(uint8_t *, const uint8_t *, size_t, H3FrameHeader *);
    size_t (*write_header)(uint8_t *, uint8_t *, size_t, uint64_t, uint64_t);
    proto_bool (*type_reserved)(uint8_t *, uint64_t);
    void (*settings_defaults)(uint8_t *, H3Settings *);
    proto_bool (*parse_settings)(uint8_t *, const uint8_t *, size_t, H3Settings *);
    size_t (*build_data)(uint8_t *, uint8_t *, size_t, const uint8_t *, size_t);
    size_t (*build_headers)(uint8_t *, uint8_t *, size_t, const uint8_t *, size_t);
    size_t (*build_settings)(uint8_t *, uint8_t *, size_t, const uint64_t *, const uint64_t *, size_t);
    size_t (*build_goaway)(uint8_t *, uint8_t *, size_t, uint64_t);
} H3FrameNs;
PROTOCORE_NS_LAYOUT(H3FrameNs, parse_header, write_header, type_reserved, settings_defaults, parse_settings, build_data,
                    build_headers, build_settings, build_goaway);

/**
 * @brief Parse a frame header (type + length varints) at buf. false if .
 * @param work PROTOCORE_H3_FRAME_BORROW bytes the caller took. Not held past the call.
 * @param buf Buf
 * @param len Len
 * @param out Out
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_h3_frame_parse_header(uint8_t *work, const uint8_t *buf, size_t len, H3FrameHeader *out);
/**
 * @brief Write a frame header (type + length varints). bytes written, or 0 .
 * @param work PROTOCORE_H3_FRAME_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @param type Type
 * @param length Length
 * @return The size_t.
 */
size_t protocore_h3_frame_write_header(uint8_t *work, uint8_t *out, size_t cap, uint64_t type, uint64_t length);
/**
 * @brief True if type is a reserved HTTP/2 frame type (0x02/0x06/0x08/0x09) .
 * @param work PROTOCORE_H3_FRAME_BORROW bytes the caller took. Not held past the call.
 * @param type Type
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_h3_frame_type_reserved(uint8_t *work, uint64_t type);
/**
 * @brief Fill s with the RFC default settings.
 * @param work PROTOCORE_H3_FRAME_BORROW bytes the caller took. Not held past the call.
 * @param s S
 */
void protocore_h3_frame_settings_defaults(uint8_t *work, H3Settings *s);
/**
 * @brief Apply a SETTINGS payload (id, value varint pairs) to s. false if .
 * @param work PROTOCORE_H3_FRAME_BORROW bytes the caller took. Not held past the call.
 * @param payload Payload
 * @param len Len
 * @param s S
 * @return PROTO_TRUE on success.
 */
proto_bool protocore_h3_frame_parse_settings(uint8_t *work, const uint8_t *payload, size_t len, H3Settings *s);
/**
 * @brief DATA frame wrapping data.
 * @param work PROTOCORE_H3_FRAME_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @param data Data
 * @param len Len
 * @return The size_t.
 */
size_t protocore_h3_frame_build_data(uint8_t *work, uint8_t *out, size_t cap, const uint8_t *data, size_t len);
/**
 * @brief HEADERS frame wrapping a QPACK-encoded field section block.
 * @param work PROTOCORE_H3_FRAME_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @param block Block
 * @param len Len
 * @return The size_t.
 */
size_t protocore_h3_frame_build_headers(uint8_t *work, uint8_t *out, size_t cap, const uint8_t *block, size_t len);
/**
 * @brief SETTINGS frame from n (id, value) pairs.
 * @param work PROTOCORE_H3_FRAME_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @param ids Ids
 * @param vals Vals
 * @param n N
 * @return The size_t.
 */
size_t protocore_h3_frame_build_settings(uint8_t *work, uint8_t *out, size_t cap, const uint64_t *ids,
                                         const uint64_t *vals, size_t n);
/**
 * @brief GOAWAY frame carrying stream_id (RFC 9114 sec 7.2.6).
 * @param work PROTOCORE_H3_FRAME_BORROW bytes the caller took. Not held past the call.
 * @param out Out
 * @param cap Cap
 * @param stream_id Stream id
 * @return The size_t.
 */
size_t protocore_h3_frame_build_goaway(uint8_t *work, uint8_t *out, size_t cap, uint64_t stream_id);

/** @brief Module namespace. */
PROTOCORE_NS H3FrameNs H3Frame PROTOCORE_UNUSED = {.parse_header = protocore_h3_frame_parse_header,
                                                   .write_header = protocore_h3_frame_write_header,
                                                   .type_reserved = protocore_h3_frame_type_reserved,
                                                   .settings_defaults = protocore_h3_frame_settings_defaults,
                                                   .parse_settings = protocore_h3_frame_parse_settings,
                                                   .build_data = protocore_h3_frame_build_data,
                                                   .build_headers = protocore_h3_frame_build_headers,
                                                   .build_settings = protocore_h3_frame_build_settings,
                                                   .build_goaway = protocore_h3_frame_build_goaway};

PROTOCORE_END_DECLS

#endif // PROTOCORE_H3_FRAME_H
