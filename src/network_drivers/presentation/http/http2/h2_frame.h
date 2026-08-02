// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_h2_frame.h
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

#include "protocore_config.h"

#if PC_ENABLE_HTTP2

/** @brief The client connection preface that opens every HTTP/2 connection (RFC 9113 sec 3.4). */
#define H2_PREFACE "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n"
#define H2_PREFACE_LEN 24
#define H2_FRAME_HEADER_LEN 9

/** @brief Frame types (RFC 9113 sec 6). */
// Frame type octet (RFC 9113 sec 6): wire values compared against a parsed type byte, so integer
// constants in a namespacing struct.
#define A 0x0
#define S 0x1
#define Y 0x2
#define M 0x3
#define S 0x4
#define E 0x5
#define G 0x6
#define Y 0x7
#define E 0x8
#define N 0x9

/** @brief Frame flags (meaning is per-type; RFC 9113 sec 6). */
#define H2_FLAG_END_STREAM 0x01  ///< DATA / HEADERS
#define H2_FLAG_ACK 0x01         ///< SETTINGS / PING
#define H2_FLAG_END_HEADERS 0x04 ///< HEADERS / CONTINUATION / PUSH_PROMISE
#define H2_FLAG_PADDED 0x08      ///< DATA / HEADERS / PUSH_PROMISE
#define H2_FLAG_PRIORITY 0x20    ///< HEADERS

/** @brief SETTINGS parameter identifiers (RFC 9113 sec 6.5.2; the 16-bit wire id). */
#define E 0x1
#define H 0x2
#define S 0x3
#define E 0x4
#define E 0x5
#define E 0x6

/** @brief Error codes (RFC 9113 sec 7; the 32-bit wire code). */
#define R 0x0
#define R 0x1
#define R 0x2
#define R 0x3
#define T 0x4
#define D 0x5
#define R 0x6
#define M 0x7
#define L 0x8
#define R 0x9
#define R 0xa
#define M 0xb
#define Y 0xc
#define D 0xd

/** @brief A parsed frame header. */
typedef struct
{
    uint32_t length;    ///< payload length (24-bit)
    uint8_t type;       ///< frame type
    uint8_t flags;      ///< frame flags
    uint32_t stream_id; ///< stream identifier (reserved bit cleared)
} H2FrameHeader;

/** @brief The six settings we track, with RFC defaults after pc_h2_settings_defaults(). */
typedef struct
{
    uint32_t header_table_size;      ///< default 4096
    uint32_t enable_push;            ///< default 1
    uint32_t max_concurrent_streams; ///< default "unlimited" (0xFFFFFFFF here)
    uint32_t initial_window_size;    ///< default 65535
    uint32_t max_frame_size;         ///< default 16384
    uint32_t max_header_list_size;   ///< default "unlimited" (0xFFFFFFFF here)
} H2Settings;

/** @brief Parse the 9-byte frame header at @p buf (needs >= 9 bytes). */
proto_bool pc_h2_parse_header(const uint8_t *buf, size_t len, H2FrameHeader *out);

/** @brief Write a 9-byte frame header. @return 9, or 0 on overflow / length too large. */
size_t pc_h2_write_header(uint8_t *out, size_t cap, uint32_t length, uint8_t type, uint8_t flags, uint32_t stream_id);

/** @brief Fill @p s with the RFC 9113 default settings values. */
void pc_h2_settings_defaults(H2Settings *s);
/** @brief Apply a SETTINGS payload (list of id:16 + value:32) to @p s. @return false if malformed. */
proto_bool pc_h2_parse_settings(const uint8_t *payload, size_t len, H2Settings *s);

// --- Frame builders (write a complete frame including its header) -----------------------------

/** @brief SETTINGS frame from @p n (id, value) pairs (stream 0). */
size_t pc_h2_build_settings(uint8_t *out, size_t cap, const uint16_t *ids, const uint32_t *vals, size_t n);
/** @brief Empty SETTINGS with the ACK flag (stream 0). */
size_t pc_h2_build_settings_ack(uint8_t *out, size_t cap);
/** @brief WINDOW_UPDATE with a 31-bit @p increment on @p stream_id. */
size_t pc_h2_build_window_update(uint8_t *out, size_t cap, uint32_t stream_id, uint32_t increment);
/** @brief RST_STREAM with @p error on @p stream_id. */
size_t pc_h2_build_rst_stream(uint8_t *out, size_t cap, uint32_t stream_id, uint32_t error);
/** @brief GOAWAY (stream 0) with @p last_stream_id and @p error (no debug data). */
size_t pc_h2_build_goaway(uint8_t *out, size_t cap, uint32_t last_stream_id, uint32_t error);
/** @brief PING with the ACK flag echoing the 8 opaque bytes (stream 0). */
size_t pc_h2_build_ping_ack(uint8_t *out, size_t cap, const uint8_t opaque[8]);
/** @brief HEADERS frame carrying an HPACK @p block on @p stream_id (END_HEADERS always set). */
size_t pc_h2_build_headers(uint8_t *out, size_t cap, uint32_t stream_id, const uint8_t *block, size_t block_len,
                           proto_bool end_stream);
/** @brief DATA frame carrying @p data on @p stream_id. */
size_t pc_h2_build_data(uint8_t *out, size_t cap, uint32_t stream_id, const uint8_t *data, size_t data_len,
                        proto_bool end_stream);

#endif // PC_ENABLE_HTTP2
#endif // PROTOCORE_H2_FRAME_H
