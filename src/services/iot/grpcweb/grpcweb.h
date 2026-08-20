// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file grpcweb.h
 * @brief gRPC-Web message framing (PROTOCORE_ENABLE_GRPC_WEB): the zero-heap length-prefixed
 *        message builder and parser.
 *
 * THE GOVERNING STANDARD IS NOT AN IETF RFC. gRPC-Web is specified by the gRPC project (CNCF) in
 * `grpc/grpc doc/PROTOCOL-WEB.md`, layered on the gRPC over HTTP/2 protocol in
 * `grpc/grpc doc/PROTOCOL-HTTP2.md`. Both are cited below by document and section name. The
 * transport underneath is HTTP, which IS IETF: RFC 9110 and RFC 9112.
 *
 * PROTOCOL-HTTP2.md "Requests" gives the frame every call here builds or reads:
 * @code
 *   Length-Prefixed-Message -> Compressed-Flag Message-Length Message
 *   Compressed-Flag         -> 0 / 1                  ; encoded as 1 byte unsigned integer
 *   Message-Length          -> {length of Message}    ; 4 byte unsigned integer (big endian)
 *   Message                 -> *{binary octet}
 * @endcode
 *
 * PROTOCOL-WEB.md "Protocol differences vs gRPC over HTTP2", under "Message framing", reads the
 * 8th (MSB) bit of the 1st gRPC frame byte as the frame type, 0 for data and 1 for trailers, so
 * `10000000b` is an uncompressed trailer and `10000001b` a compressed one. That trailers frame
 * carries the response status in the body as "Key-value pairs encoded as a HTTP/1 headers block
 * (without the terminating newline)", which is RFC 9112 sec 7.1.2
 * `trailer-section = *( field-line CRLF )` over the RFC 9112 sec 5 `field-line = field-name ":"
 * OWS field-value OWS`. PROTOCOL-WEB.md "HTTP wire protocols" item 2 requires those names to be
 * lower-case in the last length-prefixed message, and PROTOCOL-HTTP2.md "Responses" names them:
 * @code
 *   Trailers       -> Status [Status-Message] [Status-Details] *Custom-Metadata
 *   Status         -> "grpc-status" 1*DIGIT
 *   Status-Message -> "grpc-message" Percent-Encoded
 * @endcode
 * Trailers are the last message of a response, after zero or more data frames.
 *
 * A Message is an encoded Protobuf message: services/iot/protobuf builds it, this frames it. The
 * media type a response carries it under is `application/grpc-web+proto` (RFC 9110 sec 8.3),
 * over the shipped HTTP/1.1 server and client.
 *
 * The module exports one symbol, @ref GrpcWeb. Everything in grpcweb.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_GRPCWEB_H
#define PROTOCORE_GRPCWEB_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_GRPC_WEB

PROTOCORE_BEGIN_DECLS

/** @brief Compressed-Flag set in the frame byte: the Message uses the Message-Encoding. */
#define PROTOCORE_GRPCWEB_COMPRESSED 0x01
/** @brief The MSB of the frame byte: the body is a trailer-section, not a Message. */
#define PROTOCORE_GRPCWEB_TRAILERS 0x80
/** @brief Compressed-Flag plus Message-Length: the octets ahead of every Message. */
#define PROTOCORE_GRPCWEB_PREFIX_LEN 5

/** @brief One decoded Length-Prefixed-Message. @c body points INTO the parsed buffer. */
typedef struct
{
    uint8_t flags;         ///< the 1st gRPC frame byte as it arrived
    proto_bool compressed; ///< its Compressed-Flag, bit 0
    proto_bool trailers;   ///< its 8th (MSB) bit: the body is a trailer-section
    const uint8_t *body;   ///< the Message, or the trailer-section when @c trailers is set
    size_t body_len;       ///< Message-Length, the octets @c body spans
} GrpcWebFrame;

/** @brief Where a builder writes the Length-Prefixed-Message it assembles. */
typedef struct
{
    uint8_t *buf; ///< the octets a builder writes into
    size_t cap;   ///< how many octets it may write
} GrpcWebOutArgs;

/** @brief The frame byte and the Message a data frame carries (PROTOCOL-HTTP2.md "Requests"). */
typedef struct
{
    const uint8_t *body;   ///< Message, the octets Message-Length measures
    size_t body_len;       ///< Message-Length, capped at what a 4-byte big-endian field expresses
    uint8_t flags;         ///< the whole 1st gRPC frame byte, when a caller sets it outright
    proto_bool compressed; ///< Compressed-Flag, the bit a frame_message sets in @c flags
} GrpcWebMessageArgs;

/** @brief The trailer-section a trailers frame carries (PROTOCOL-HTTP2.md "Responses"). */
typedef struct
{
    int32_t status;      ///< Status, the "grpc-status" value as 1*DIGIT
    const char *message; ///< Status-Message, the "grpc-message" value; NULL or "" omits the line
} GrpcWebTrailersArgs;

/** @brief The octets a parse decodes, or a Trailers read scans. */
typedef struct
{
    const uint8_t *data; ///< a frame stream, or one decoded trailer-section
    size_t len;          ///< how many octets are buffered there
} GrpcWebInArgs;

/**
 * @brief The gRPC-Web framing codec.
 *
 * A caller sets the members a call takes, invokes it through ::GrpcWeb, and reads the outcome off
 * the same handle.
 *
 * No slot member: the codec owns no rows, so no call names one.
 *
 * @var GrpcWebNs::out              where a builder writes the frame
 * @var GrpcWebNs::msg              the frame byte and the Message a data frame carries
 * @var GrpcWebNs::trailers         the Status and Status-Message a trailers frame carries
 * @var GrpcWebNs::in               the octets a parse decodes or a Trailers read scans
 * @var GrpcWebNs::ok               a call's true/false outcome
 * @var GrpcWebNs::n                octets a builder wrote, or a parse consumed; 0 when a call failed
 * @var GrpcWebNs::parsed           the Length-Prefixed-Message a parse decoded
 * @var GrpcWebNs::i32              the Status a trailers_status read
 * @var GrpcWebNs::text             the Status-Message slice, pointing into @c in.data
 * @var GrpcWebNs::text_len         its length in octets
 * @var GrpcWebNs::frame            frame @c msg.body under the frame byte in @c msg.flags
 * @var GrpcWebNs::frame_message    frame @c msg.body with @c msg.compressed as the Compressed-Flag
 * @var GrpcWebNs::frame_trailers   frame @c trailers as a trailer-section under the MSB
 * @var GrpcWebNs::parse            decode the frame at the head of @c in
 * @var GrpcWebNs::trailers_status  read Status ("grpc-status") out of the trailer-section in @c in
 * @var GrpcWebNs::trailers_message read Status-Message ("grpc-message") out of that same section
 */
typedef struct
{
    GrpcWebOutArgs out;           ///< where a builder writes
    GrpcWebMessageArgs msg;       ///< what a data frame carries
    GrpcWebTrailersArgs trailers; ///< what a trailers frame carries
    GrpcWebInArgs in;             ///< what a read consumes
    proto_bool ok;
    size_t n;
    GrpcWebFrame parsed;
    int32_t i32;
    const char *text;
    size_t text_len;
} GrpcWebVars;

/** @brief The operands and the outcome. */
extern GrpcWebVars GrpcWebV;

/** @brief The entries. */
typedef struct
{
    void (*const frame)(uint8_t *restrict work);
    void (*const frame_message)(uint8_t *restrict work);
    void (*const frame_trailers)(uint8_t *restrict work);
    void (*const parse)(uint8_t *restrict work);
    void (*const trailers_status)(uint8_t *restrict work);
    void (*const trailers_message)(uint8_t *restrict work);
} GrpcWebNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in GrpcWebV or a region of the borrow at a fixed offset.
void protocore_grpc_web_frame(uint8_t *restrict work);
void protocore_grpc_web_frame_message(uint8_t *restrict work);
void protocore_grpc_web_frame_trailers(uint8_t *restrict work);
void protocore_grpc_web_parse(uint8_t *restrict work);
void protocore_grpc_web_trailers_status(uint8_t *restrict work);
void protocore_grpc_web_trailers_message(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `GrpcWeb.frame(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const GrpcWebNs GrpcWeb __attribute__((unused)) = {
    .frame = protocore_grpc_web_frame,
    .frame_message = protocore_grpc_web_frame_message,
    .frame_trailers = protocore_grpc_web_frame_trailers,
    .parse = protocore_grpc_web_parse,
    .trailers_status = protocore_grpc_web_trailers_status,
    .trailers_message = protocore_grpc_web_trailers_message,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_GRPC_WEB

#endif // PROTOCORE_GRPCWEB_H
