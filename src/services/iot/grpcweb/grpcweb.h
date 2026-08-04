// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file grpcweb.h
 * @brief gRPC-Web message framing (PC_ENABLE_GRPC_WEB) - zero-heap length-prefixed frame
 *        builder + parser, the HTTP/1.1-reachable subset of gRPC that wraps the Protobuf
 *        codec (services/iot/protobuf). gRPC proper needs HTTP/2; gRPC-Web rides the shipped
 *        HTTP/1.1 server/client.
 *
 * Each gRPC / gRPC-Web message is a 5-octet prefix then the body:
 * @code
 *   [flags(1)][length(4, big-endian)][body...]
 * @endcode
 *  - flags bit 0 = compressed (per the Message-Encoding header); bit 7 (0x80) marks a
 *    gRPC-Web trailers frame whose body is an HTTP/1.1-style trailer block
 *    (`grpc-status:0\r\ngrpc-message:...\r\n`) instead of a Protobuf message.
 *  - A response is zero or more message frames followed by exactly one trailers frame.
 *
 * The message body is an encoded Protobuf message (build it with the protobuf codec, then
 * frame it here). This is the framing layer only.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_GRPCWEB_H
#define PROTOCORE_GRPCWEB_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

#if PC_ENABLE_GRPC_WEB

#define GRPCWEB_FLAG_COMPRESSED 0x01
#define GRPCWEB_FLAG_TRAILER 0x80
#define GRPCWEB_PREFIX_LEN 5

/** @brief Frame a body: `[flags][len BE32][body]`. Returns total octets, or 0 on overflow. */
size_t pc_grpcweb_frame(uint8_t *buf, size_t cap, uint8_t flags, const uint8_t *body, size_t body_len);

/** @brief Frame a (Protobuf) message; @p compressed sets the compressed flag. */
size_t pc_grpcweb_frame_message(uint8_t *buf, size_t cap, const uint8_t *msg, size_t msg_len, proto_bool compressed);

/**
 * @brief Build a trailers frame: `grpc-status:<status>\r\n` plus, when @p message is given,
 *        `grpc-message:<message>\r\n`, wrapped with the 0x80 trailer flag.
 * @return total octets written, or 0 on overflow.
 */
size_t pc_grpcweb_frame_trailer(uint8_t *buf, size_t cap, int status, const char *message);

/** @brief One parsed frame; @ref body points INTO the source buffer. */
typedef struct
{
    uint8_t flags;
    proto_bool compressed; ///< flags & 0x01
    proto_bool trailer;    ///< flags & 0x80
    const uint8_t *body;
    size_t body_len;
} GrpcWebFrame;

/**
 * @brief Parse one frame at the head of [buf, buf+len).
 * @param consumed receives the frame's total length so the caller can advance.
 * @return true on a complete frame; false if fewer than the prefix + body octets are buffered.
 */
proto_bool pc_grpcweb_parse(const uint8_t *buf, size_t len, GrpcWebFrame *out, size_t *consumed);

/** @brief Extract `grpc-status` (an integer) from a trailers-frame body. */
proto_bool pc_grpcweb_trailer_status(const uint8_t *body, size_t len, int *status);

/**
 * @brief Extract `grpc-message` (the human-readable status text) from a trailers-frame body. The value is
 *        the slice up to the end of its line; per the gRPC spec it is percent-encoded on the wire (any
 *        decoding is the caller's). @p msg / @p msg_len point INTO @p body. @return true iff the key exists.
 */
proto_bool pc_grpcweb_trailer_message(const uint8_t *body, size_t len, const char **msg, size_t *msg_len);

#endif // PC_ENABLE_GRPC_WEB

PROTO_END_DECLS

#endif // PROTOCORE_GRPCWEB_H
