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

#include "protocore_config.h" // the entry point: protocore_types.h for the widths

#if PROTOCORE_ENABLE_HTTP3

PROTOCORE_BEGIN_DECLS

// This module holds nothing between calls, so it carves no borrow and states none. An entry
// takes one all the same, and never reads it, so every namespace in the tree is invoked the
// same way.

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
/** @brief What parse_header takes: buf, len, out. */
typedef struct
{
    const uint8_t *buf;
    size_t len;
    H3FrameHeader *out;
} H3FrameParseHeaderArgs;
/** @brief What write_header takes: out, cap, type, length. */
typedef struct
{
    uint8_t *out;
    size_t cap;
    uint64_t type;
    uint64_t length;
} H3FrameWriteHeaderArgs;
/** @brief What type_reserved takes: type. */
typedef struct
{
    uint64_t type;
} H3FrameTypeReservedArgs;
/** @brief What settings_defaults takes: s. */
typedef struct
{
    H3Settings *s;
} H3FrameSettingsDefaultsArgs;
/** @brief What parse_settings takes: payload, len, s. */
typedef struct
{
    const uint8_t *payload;
    size_t len;
    H3Settings *s;
} H3FrameParseSettingsArgs;
/** @brief What build_data takes: out, cap, data, len. */
typedef struct
{
    uint8_t *out;
    size_t cap;
    const uint8_t *data;
    size_t len;
} H3FrameBuildDataArgs;
/** @brief What build_headers takes: out, cap, block, len. */
typedef struct
{
    uint8_t *out;
    size_t cap;
    const uint8_t *block;
    size_t len;
} H3FrameBuildHeadersArgs;
/** @brief What build_settings takes: out, cap, ids, vals, n. */
typedef struct
{
    uint8_t *out;
    size_t cap;
    const uint64_t *ids;
    const uint64_t *vals;
    size_t n;
} H3FrameBuildSettingsArgs;
/** @brief What build_goaway takes: out, cap, stream_id. */
typedef struct
{
    uint8_t *out;
    size_t cap;
    uint64_t stream_id;
} H3FrameBuildGoawayArgs;
/**
 * @brief HTTP/3 framing (RFC 9114 sec 7) over QUIC varints.
 *
 * A caller sets the members a call takes, invokes it through ::H3Frame with the bytes it runs
 * out of, and reads the outcome off the same handle.
 *
 *   H3Frame.parse_header_args.buf = ...;
 *   H3Frame.parse_header_args.len = ...;
 *   H3Frame.parse_header_args.out = ...;
 *   H3Frame.parse_header(work);
 *   // H3Frame.ok is what the call reports
 *
 * @var H3FrameNs::parse_header_args  what parse_header takes: buf, len, out
 * @var H3FrameNs::write_header_args  what write_header takes: out, cap, type, length
 * @var H3FrameNs::type_reserved_args  what type_reserved takes: type
 * @var H3FrameNs::settings_defaults_args  what settings_defaults takes: s
 * @var H3FrameNs::parse_settings_args  what parse_settings takes: payload, len, s
 * @var H3FrameNs::build_data_args  what build_data takes: out, cap, data, len
 * @var H3FrameNs::build_headers_args  what build_headers takes: out, cap, block, len
 * @var H3FrameNs::build_settings_args  what build_settings takes: out, cap, ids, vals, n
 * @var H3FrameNs::build_goaway_args  what build_goaway takes: out, cap, stream_id
 * @var H3FrameNs::ok  a call's true/false outcome
 * @var H3FrameNs::n  the count a call reports
 * @var H3FrameNs::parse_header  parse a frame header (type + length varints) at buf. false if ...
 * @var H3FrameNs::write_header  write a frame header (type + length varints). bytes written, or 0 ...
 * @var H3FrameNs::type_reserved  true if type is a reserved HTTP/2 frame type (0x02/0x06/0x08/0x09) ...
 * @var H3FrameNs::settings_defaults  fill s with the RFC default settings
 * @var H3FrameNs::parse_settings  apply a SETTINGS payload (id, value varint pairs) to s. false if ...
 * @var H3FrameNs::build_data  DATA frame wrapping data
 * @var H3FrameNs::build_headers  HEADERS frame wrapping a QPACK-encoded field section block
 * @var H3FrameNs::build_settings  SETTINGS frame from n (id, value) pairs
 * @var H3FrameNs::build_goaway  GOAWAY frame carrying stream_id (RFC 9114 sec 7.2.6)
 *
 * @c work is bytes the CALLER holds. This module reads none of them: it carries nothing
 * between calls, so there is no state to keep and nothing to wipe. The parameter is there so
 * a caller drives every namespace the same way.
 */
typedef struct
{
    H3FrameParseHeaderArgs parse_header_args;
    H3FrameWriteHeaderArgs write_header_args;
    H3FrameTypeReservedArgs type_reserved_args;
    H3FrameSettingsDefaultsArgs settings_defaults_args;
    H3FrameParseSettingsArgs parse_settings_args;
    H3FrameBuildDataArgs build_data_args;
    H3FrameBuildHeadersArgs build_headers_args;
    H3FrameBuildSettingsArgs build_settings_args;
    H3FrameBuildGoawayArgs build_goaway_args;
    proto_bool ok;
    size_t n;
} H3FrameVars;

/** @brief The operands and the outcome. */
extern H3FrameVars H3FrameV;

/** @brief The entries. */
typedef struct
{
    void (*const parse_header)(uint8_t *restrict work);
    void (*const write_header)(uint8_t *restrict work);
    void (*const type_reserved)(uint8_t *restrict work);
    void (*const settings_defaults)(uint8_t *restrict work);
    void (*const parse_settings)(uint8_t *restrict work);
    void (*const build_data)(uint8_t *restrict work);
    void (*const build_headers)(uint8_t *restrict work);
    void (*const build_settings)(uint8_t *restrict work);
    void (*const build_goaway)(uint8_t *restrict work);
} H3FrameNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in H3FrameV or a region of the borrow at a fixed offset.
void protocore_h3_frame_parse_header(uint8_t *restrict work);
void protocore_h3_frame_write_header(uint8_t *restrict work);
void protocore_h3_frame_type_reserved(uint8_t *restrict work);
void protocore_h3_frame_settings_defaults(uint8_t *restrict work);
void protocore_h3_frame_parse_settings(uint8_t *restrict work);
void protocore_h3_frame_build_data(uint8_t *restrict work);
void protocore_h3_frame_build_headers(uint8_t *restrict work);
void protocore_h3_frame_build_settings(uint8_t *restrict work);
void protocore_h3_frame_build_goaway(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `H3Frame.parse_header(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const H3FrameNs H3Frame __attribute__((unused)) = {
    .parse_header = protocore_h3_frame_parse_header,
    .write_header = protocore_h3_frame_write_header,
    .type_reserved = protocore_h3_frame_type_reserved,
    .settings_defaults = protocore_h3_frame_settings_defaults,
    .parse_settings = protocore_h3_frame_parse_settings,
    .build_data = protocore_h3_frame_build_data,
    .build_headers = protocore_h3_frame_build_headers,
    .build_settings = protocore_h3_frame_build_settings,
    .build_goaway = protocore_h3_frame_build_goaway,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP3

#endif // PROTOCORE_H3_FRAME_H
