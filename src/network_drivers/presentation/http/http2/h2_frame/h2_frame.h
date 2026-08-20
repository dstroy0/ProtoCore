// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_h2_frame.h
 * @brief HTTP/2 binary framing (RFC 9113 sec 4 + sec 6).
 *
 * Every HTTP/2 frame is a 9-byte header (24-bit length, 8-bit type, 8-bit flags, 1 reserved bit
 * + 31-bit stream id) followed by a type-specific payload. This module parses that header and
 * builds the frames the server sends (SETTINGS + its ACK, WINDOW_UPDATE, RST_STREAM, GOAWAY,
 * PING ACK, HEADERS, DATA) and reads a SETTINGS payload. Pure and host-tested; no I/O.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_H2_FRAME_H
#define PROTOCORE_H2_FRAME_H

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HTTP2

PROTOCORE_BEGIN_DECLS

/** @brief The client connection preface that opens every HTTP/2 connection (RFC 9113 sec 3.4). */
#define H2_PREFACE "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
#define H2_PREFACE_LEN 24
#define H2_FRAME_HEADER_LEN 9

/** @brief Frame types (RFC 9113 sec 6). */
// Frame type octet (RFC 9113 sec 6): wire values compared against a parsed type byte, so integer
// constants in a namespacing struct.
#define H2_DATA 0x0
#define H2_HEADERS 0x1
#define H2_PRIORITY 0x2
#define H2_RST_STREAM 0x3
#define H2_SETTINGS 0x4
#define H2_PUSH_PROMISE 0x5
#define H2_PING 0x6
#define H2_GOAWAY 0x7
#define H2_WINDOW_UPDATE 0x8
#define H2_CONTINUATION 0x9

/** @brief Frame flags (meaning is per-type; RFC 9113 sec 6). */
#define H2_FLAG_END_STREAM 0x01  ///< DATA / HEADERS
#define H2_FLAG_ACK 0x01         ///< SETTINGS / PING
#define H2_FLAG_END_HEADERS 0x04 ///< HEADERS / CONTINUATION / PUSH_PROMISE
#define H2_FLAG_PADDED 0x08      ///< DATA / HEADERS / PUSH_PROMISE
#define H2_FLAG_PRIORITY 0x20    ///< HEADERS

/** @brief SETTINGS parameter identifiers (RFC 9113 sec 6.5.2; the 16-bit wire id). */
#define H2_SETTINGS_HEADER_TABLE_SIZE 0x1
#define H2_SETTINGS_ENABLE_PUSH 0x2
#define H2_SETTINGS_MAX_CONCURRENT_STREAMS 0x3
#define H2_SETTINGS_INITIAL_WINDOW_SIZE 0x4
#define H2_SETTINGS_MAX_FRAME_SIZE 0x5
#define H2_SETTINGS_MAX_HEADER_LIST_SIZE 0x6

/** @brief Error codes (RFC 9113 sec 7; the 32-bit wire code). */
#define H2_NO_ERROR 0x0
#define H2_PROTOCOL_ERROR 0x1
#define H2_INTERNAL_ERROR 0x2
#define H2_FLOW_CONTROL_ERROR 0x3
#define H2_SETTINGS_TIMEOUT 0x4
#define H2_STREAM_CLOSED 0x5
#define H2_FRAME_SIZE_ERROR 0x6
#define H2_REFUSED_STREAM 0x7
#define H2_CANCEL 0x8
#define H2_COMPRESSION_ERROR 0x9
#define H2_CONNECT_ERROR 0xa
#define H2_ENHANCE_YOUR_CALM 0xb
#define H2_INADEQUATE_SECURITY 0xc
#define H2_HTTP_1_1_REQUIRED 0xd

/** @brief A parsed frame header. */
typedef struct
{
    uint32_t length;    ///< payload length (24-bit)
    uint8_t type;       ///< frame type
    uint8_t flags;      ///< frame flags
    uint32_t stream_id; ///< stream identifier (reserved bit cleared)
} H2FrameHeader;
/** @brief The six settings we track, with RFC defaults after ::H2FrameNs::settings_defaults. */
typedef struct
{
    uint32_t header_table_size;      ///< default 4096
    uint32_t enable_push;            ///< default 1
    uint32_t max_concurrent_streams; ///< default "unlimited" (0xFFFFFFFF here)
    uint32_t initial_window_size;    ///< default 65535
    uint32_t max_frame_size;         ///< default 16384
    uint32_t max_header_list_size;   ///< default "unlimited" (0xFFFFFFFF here)
} H2Settings;
/** @brief RFC 9113 sec 4.1: the 9-octet header a parse reads. */
typedef struct
{
    const uint8_t *buf; ///< the bytes to read
    size_t len;         ///< how many are available
} H2FrameParseArgs;
/** @brief RFC 9113 sec 4.1: the 9-octet header a write emits. */
typedef struct
{
    uint8_t *buf;       ///< where the header is written
    size_t cap;         ///< how much room it has
    uint32_t length;    ///< the payload length it declares
    uint8_t type;       ///< the frame type
    uint8_t flags;      ///< the frame flags
    uint32_t stream_id; ///< the stream it belongs to
} H2FrameWriteArgs;
/** @brief The settings block a defaults fill or a parse applies to. */
typedef struct
{
    const uint8_t *payload; ///< the SETTINGS payload to apply; unused by a defaults fill
    size_t len;             ///< how many bytes it carries
    H2Settings *s;          ///< the block written into
} H2FrameSettingsArgs;
/** @brief RFC 9113 sec 6.5: the (id, value) pairs a SETTINGS frame carries. */
typedef struct
{
    uint8_t *buf;         ///< where the frame is written
    size_t cap;           ///< how much room it has
    const uint16_t *ids;  ///< the parameter ids
    const uint32_t *vals; ///< their values
    size_t n;             ///< how many pairs
} H2FrameBuildSettingsArgs;
/** @brief Where a frame that carries nothing else is written. */
typedef struct
{
    uint8_t *buf; ///< where the frame is written
    size_t cap;   ///< how much room it has
} H2FrameAckArgs;
/** @brief RFC 9113 sec 6.9: the increment a WINDOW_UPDATE carries. */
typedef struct
{
    uint8_t *buf;       ///< where the frame is written
    size_t cap;         ///< how much room it has
    uint32_t stream_id; ///< the stream it credits
    uint32_t increment; ///< the 31-bit credit
} H2FrameWindowArgs;
/** @brief RFC 9113 sec 6.4: the error a RST_STREAM carries. */
typedef struct
{
    uint8_t *buf;       ///< where the frame is written
    size_t cap;         ///< how much room it has
    uint32_t stream_id; ///< the stream it resets
    uint32_t error;     ///< the code it reports
} H2FrameRstArgs;
/** @brief RFC 9113 sec 6.8: what a GOAWAY reports. */
typedef struct
{
    uint8_t *buf;            ///< where the frame is written
    size_t cap;              ///< how much room it has
    uint32_t last_stream_id; ///< the highest stream that will be processed
    uint32_t error;          ///< the code it reports
} H2FrameGoawayArgs;
/** @brief RFC 9113 sec 6.7: the opaque data a PING ACK echoes. */
typedef struct
{
    uint8_t *buf;          ///< where the frame is written
    size_t cap;            ///< how much room it has
    const uint8_t *opaque; ///< the 8 octets echoed back
} H2FramePingArgs;
/** @brief RFC 9113 sec 6.2: the HPACK block a HEADERS frame carries. */
typedef struct
{
    uint8_t *buf;          ///< where the frame is written
    size_t cap;            ///< how much room it has
    uint32_t stream_id;    ///< the stream it opens
    const uint8_t *block;  ///< the HPACK block
    size_t block_len;      ///< how many bytes
    proto_bool end_stream; ///< whether it closes the stream
} H2FrameHeadersArgs;
/** @brief RFC 9113 sec 6.1: the payload a DATA frame carries. */
typedef struct
{
    uint8_t *buf;          ///< where the frame is written
    size_t cap;            ///< how much room it has
    uint32_t stream_id;    ///< the stream it belongs to
    const uint8_t *data;   ///< the payload
    size_t data_len;       ///< how many bytes
    proto_bool end_stream; ///< whether it closes the stream
} H2FrameDataArgs;
/**
 * @brief HTTP/2 frame headers, settings and builders (RFC 9113 sec 4 and 6).
 *
 * A caller sets the members a call takes, invokes it through ::H2Frame, and reads the outcome off
 * the same handle.
 *
 * @var H2FrameNs::parse_args      the bytes a header parse reads
 * @var H2FrameNs::write_args      what a header write emits
 * @var H2FrameNs::settings_args   the block a defaults fill or a settings parse writes
 * @var H2FrameNs::build_settings_args  the pairs a SETTINGS frame carries
 * @var H2FrameNs::ack_args        where a frame carrying nothing else is written
 * @var H2FrameNs::window_args     the increment a WINDOW_UPDATE carries
 * @var H2FrameNs::rst_args        the error a RST_STREAM carries
 * @var H2FrameNs::goaway_args     what a GOAWAY reports
 * @var H2FrameNs::ping_args       the opaque data a PING ACK echoes
 * @var H2FrameNs::headers_args    the HPACK block a HEADERS frame carries
 * @var H2FrameNs::data_args       the payload a DATA frame carries
 * @var H2FrameNs::ok              whether a parse read a well-formed frame
 * @var H2FrameNs::n               bytes a build wrote, or 0 on overflow
 * @var H2FrameNs::header          the header a parse read
 * @var H2FrameNs::parse_header       read the 9-octet header
 * @var H2FrameNs::write_header       write the 9-octet header
 * @var H2FrameNs::settings_defaults  fill a block with the RFC defaults
 * @var H2FrameNs::parse_settings     apply a SETTINGS payload to a block
 * @var H2FrameNs::build_settings     a SETTINGS frame from (id, value) pairs
 * @var H2FrameNs::build_settings_ack an empty SETTINGS with the ACK flag
 * @var H2FrameNs::build_window_update a WINDOW_UPDATE
 * @var H2FrameNs::build_rst_stream   a RST_STREAM
 * @var H2FrameNs::build_goaway       a GOAWAY
 * @var H2FrameNs::build_ping_ack     a PING with the ACK flag
 * @var H2FrameNs::build_headers      a HEADERS frame
 * @var H2FrameNs::build_data         a DATA frame
 *
 * Every entry takes a borrow to keep one calling convention across the tree; this module reads and
 * writes only the caller's buffers, so nothing is read through it.
 */
typedef struct
{
    H2FrameParseArgs parse_args;                  ///< the members ::H2FrameNs::parse_header takes
    H2FrameWriteArgs write_args;                  ///< the members ::H2FrameNs::write_header takes
    H2FrameSettingsArgs settings_args;            ///< the members the settings calls take
    H2FrameBuildSettingsArgs build_settings_args; ///< the members ::H2FrameNs::build_settings takes
    H2FrameAckArgs ack_args;                      ///< the members ::H2FrameNs::build_settings_ack takes
    H2FrameWindowArgs window_args;                ///< the members ::H2FrameNs::build_window_update takes
    H2FrameRstArgs rst_args;                      ///< the members ::H2FrameNs::build_rst_stream takes
    H2FrameGoawayArgs goaway_args;                ///< the members ::H2FrameNs::build_goaway takes
    H2FramePingArgs ping_args;                    ///< the members ::H2FrameNs::build_ping_ack takes
    H2FrameHeadersArgs headers_args;              ///< the members ::H2FrameNs::build_headers takes
    H2FrameDataArgs data_args;                    ///< the members ::H2FrameNs::build_data takes
    proto_bool ok;                                ///< whether a parse read a well-formed frame
    size_t n;                                     ///< bytes a build wrote, or 0 on overflow
    H2FrameHeader header;                         ///< the header a parse read
} H2FrameVars;

/** @brief The operands and the outcome. */
extern H2FrameVars H2FrameV;

/** @brief The entries. */
typedef struct
{
    void (*const parse_header)(uint8_t *restrict work);
    void (*const write_header)(uint8_t *restrict work);
    void (*const settings_defaults)(uint8_t *restrict work);
    void (*const parse_settings)(uint8_t *restrict work);
    void (*const build_settings)(uint8_t *restrict work);
    void (*const build_settings_ack)(uint8_t *restrict work);
    void (*const build_window_update)(uint8_t *restrict work);
    void (*const build_rst_stream)(uint8_t *restrict work);
    void (*const build_goaway)(uint8_t *restrict work);
    void (*const build_ping_ack)(uint8_t *restrict work);
    void (*const build_headers)(uint8_t *restrict work);
    void (*const build_data)(uint8_t *restrict work);
} H2FrameNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in H2FrameV or a region of the borrow at a fixed offset.
void protocore_h2_frame_parse_header(uint8_t *restrict work);
void protocore_h2_frame_write_header(uint8_t *restrict work);
void protocore_h2_frame_settings_defaults(uint8_t *restrict work);
void protocore_h2_frame_parse_settings(uint8_t *restrict work);
void protocore_h2_frame_build_settings(uint8_t *restrict work);
void protocore_h2_frame_build_settings_ack(uint8_t *restrict work);
void protocore_h2_frame_build_window_update(uint8_t *restrict work);
void protocore_h2_frame_build_rst_stream(uint8_t *restrict work);
void protocore_h2_frame_build_goaway(uint8_t *restrict work);
void protocore_h2_frame_build_ping_ack(uint8_t *restrict work);
void protocore_h2_frame_build_headers(uint8_t *restrict work);
void protocore_h2_frame_build_data(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `H2Frame.parse_header(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const H2FrameNs H2Frame __attribute__((unused)) = {
    .parse_header = protocore_h2_frame_parse_header,
    .write_header = protocore_h2_frame_write_header,
    .settings_defaults = protocore_h2_frame_settings_defaults,
    .parse_settings = protocore_h2_frame_parse_settings,
    .build_settings = protocore_h2_frame_build_settings,
    .build_settings_ack = protocore_h2_frame_build_settings_ack,
    .build_window_update = protocore_h2_frame_build_window_update,
    .build_rst_stream = protocore_h2_frame_build_rst_stream,
    .build_goaway = protocore_h2_frame_build_goaway,
    .build_ping_ack = protocore_h2_frame_build_ping_ack,
    .build_headers = protocore_h2_frame_build_headers,
    .build_data = protocore_h2_frame_build_data,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP2

#endif // PROTOCORE_H2_FRAME_H
