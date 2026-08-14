// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file grpcweb.c
 * @brief The gRPC-Web framing codec: the Length-Prefixed-Message builders and the frame parser.
 *
 * The builders write Compressed-Flag, Message-Length and Message into the caller's buffer
 * (grpc/grpc doc/PROTOCOL-HTTP2.md "Requests"). frame_trailers sets the 8th (MSB) bit of the frame
 * byte and lays the trailer-section down as `*( field-line CRLF )` with lower-case names
 * (grpc/grpc doc/PROTOCOL-WEB.md "Protocol differences vs gRPC over HTTP2", RFC 9112 sec 7.1.2).
 * parse reads one frame back, and the two Trailers reads pull Status and Status-Message out of a
 * decoded section. Every call works on the caller's octets and touches no socket.
 */

#include "services/iot/grpcweb/grpcweb.h"

#if PROTOCORE_ENABLE_GRPC_WEB

#include "mmgr/protomem.h" // mem.cpy / mem.cmp: the spans a frame is assembled from and matched on
#include "mmgr/protostr.h" // str.len: the bounded length of a field-line's text

/**
 * @brief The codec's calls and the handle they read - what GrpcWebNs points at.
 *
 * No storage member: every call reads and writes the caller's buffers and holds nothing of its own.
 *
 * @var GrpcWebInternal::ns  the handle a caller sets a call's members on
 */
struct GrpcWebInternal
{
    GrpcWebNs *ns;
};

static struct GrpcWebInternal s_grpcweb = {.ns = &GrpcWeb};

// Write Message-Length as a 4 byte unsigned integer, big endian.
static void put_be32(uint8_t *buf, uint32_t v)
{
    buf[0] = (uint8_t)(v >> 24);
    buf[1] = (uint8_t)(v >> 16);
    buf[2] = (uint8_t)(v >> 8);
    buf[3] = (uint8_t)v;
}

// Read a 4 byte big-endian unsigned integer.
static uint32_t get_be32(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) | ((uint32_t)buf[2] << 8) | (uint32_t)buf[3];
}

// Append a NUL-terminated string at *pos and advance it. False when it would pass cap. The length
// is read no further than cap, and *pos is past the prefix, so a longer string fails the test.
static proto_bool put_str(uint8_t *buf, size_t cap, size_t *pos, const char *s)
{
    const size_t n = str.len(s, cap);
    if (*pos + n > cap)
    {
        return PROTO_FALSE;
    }
    mem.cpy(buf + *pos, s, n);
    *pos += n;
    return PROTO_TRUE;
}

// Append a non-negative value at *pos as 1*DIGIT, no leading zero, and advance it. False on a
// negative value or when the digits would pass cap.
static proto_bool put_int(uint8_t *buf, size_t cap, size_t *pos, int32_t v)
{
    if (v < 0)
    {
        return PROTO_FALSE;
    }
    const uint32_t u = (uint32_t)v;
    uint32_t scale = 1;
    size_t n = 1;
    while (u / scale >= 10u)
    {
        scale *= 10u;
        n++;
    }
    if (*pos + n > cap)
    {
        return PROTO_FALSE;
    }
    for (size_t i = 0; i < n; i++)
    {
        buf[*pos + i] = (uint8_t)('0' + (u / scale) % 10u);
        scale /= 10u;
    }
    *pos += n;
    return PROTO_TRUE;
}

// Find a lower-case field-name at the start of a field-line in [body, body+len) and report the
// offset of its field-value. A line starts at offset 0 or just past a '\n'. False when absent.
static proto_bool find_key(const uint8_t *body, size_t len, const char *key, size_t klen, size_t *value_at)
{
    for (size_t i = 0; i + klen <= len; i++)
    {
        if ((i == 0 || body[i - 1] == '\n') && mem.cmp(body + i, key, klen) == 0)
        {
            *value_at = i + klen;
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

// Build one Length-Prefixed-Message from ns->msg into ns->out, under the frame byte in msg.flags.
static void grpcweb_frame(struct GrpcWebInternal *restrict ctx)
{
    uint8_t *buf = ctx->ns->out.buf;
    const size_t body_len = ctx->ns->msg.body_len;
    ctx->ns->ok = PROTO_FALSE;
    ctx->ns->n = 0;
    // Message-Length is a 4 byte field, so a longer body has no frame that can carry it.
    if (!buf || (body_len && !ctx->ns->msg.body) || body_len > 0xFFFFFFFFu)
    {
        return;
    }
    const size_t total = (size_t)PROTOCORE_GRPCWEB_PREFIX_LEN + body_len;
    if (total > ctx->ns->out.cap)
    {
        return;
    }
    buf[0] = ctx->ns->msg.flags;
    put_be32(buf + 1, (uint32_t)body_len);
    if (body_len)
    {
        mem.cpy(buf + PROTOCORE_GRPCWEB_PREFIX_LEN, ctx->ns->msg.body, body_len);
    }
    ctx->ns->n = total;
    ctx->ns->ok = PROTO_TRUE;
}

// Frame a Message, taking its Compressed-Flag from msg.compressed and leaving the MSB clear.
static void grpcweb_frame_message(struct GrpcWebInternal *restrict ctx)
{
    ctx->ns->msg.flags = 0;
    if (ctx->ns->msg.compressed)
    {
        ctx->ns->msg.flags = PROTOCORE_GRPCWEB_COMPRESSED;
    }
    grpcweb_frame(ctx);
}

// Build a trailers frame: the prefix, then `grpc-status:<Status>\r\n` and, when a Status-Message is
// given, `grpc-message:<text>\r\n`. The prefix is reserved first and patched once the section ends.
static void grpcweb_frame_trailers(struct GrpcWebInternal *restrict ctx)
{
    uint8_t *buf = ctx->ns->out.buf;
    const size_t cap = ctx->ns->out.cap;
    ctx->ns->ok = PROTO_FALSE;
    ctx->ns->n = 0;
    if (!buf || cap < PROTOCORE_GRPCWEB_PREFIX_LEN)
    {
        return;
    }
    size_t pos = PROTOCORE_GRPCWEB_PREFIX_LEN;
    if (!put_str(buf, cap, &pos, "grpc-status:") || !put_int(buf, cap, &pos, ctx->ns->trailers.status) ||
        !put_str(buf, cap, &pos, "\r\n"))
    {
        return;
    }
    const char *message = ctx->ns->trailers.message;
    if (message && message[0])
    {
        if (!put_str(buf, cap, &pos, "grpc-message:") || !put_str(buf, cap, &pos, message) ||
            !put_str(buf, cap, &pos, "\r\n"))
        {
            return;
        }
    }
    buf[0] = PROTOCORE_GRPCWEB_TRAILERS;
    put_be32(buf + 1, (uint32_t)(pos - PROTOCORE_GRPCWEB_PREFIX_LEN));
    ctx->ns->n = pos;
    ctx->ns->ok = PROTO_TRUE;
}

// Decode the Length-Prefixed-Message at the head of ns->in into ns->parsed, and report the octets
// it spans in ns->n. False while fewer than the prefix plus Message-Length octets are buffered.
static void grpcweb_parse(struct GrpcWebInternal *restrict ctx)
{
    const uint8_t *buf = ctx->ns->in.data;
    const size_t len = ctx->ns->in.len;
    ctx->ns->ok = PROTO_FALSE;
    ctx->ns->n = 0;
    if (!buf || len < PROTOCORE_GRPCWEB_PREFIX_LEN)
    {
        return;
    }
    const uint32_t body_len = get_be32(buf + 1);
    if ((size_t)PROTOCORE_GRPCWEB_PREFIX_LEN + body_len > len)
    {
        return;
    }
    ctx->ns->parsed.flags = buf[0];
    ctx->ns->parsed.compressed = (buf[0] & PROTOCORE_GRPCWEB_COMPRESSED) != 0;
    ctx->ns->parsed.trailers = (buf[0] & PROTOCORE_GRPCWEB_TRAILERS) != 0;
    ctx->ns->parsed.body = buf + PROTOCORE_GRPCWEB_PREFIX_LEN;
    ctx->ns->parsed.body_len = body_len;
    ctx->ns->n = (size_t)PROTOCORE_GRPCWEB_PREFIX_LEN + body_len;
    ctx->ns->ok = PROTO_TRUE;
}

// Read Status, the "grpc-status" 1*DIGIT field-value, out of the trailer-section in ns->in.
static void grpcweb_trailers_status(struct GrpcWebInternal *restrict ctx)
{
    static const char key[] = "grpc-status:";
    const uint8_t *body = ctx->ns->in.data;
    const size_t len = ctx->ns->in.len;
    size_t j = 0;
    ctx->ns->ok = PROTO_FALSE;
    ctx->ns->i32 = 0;
    if (!body || !find_key(body, len, key, sizeof(key) - 1, &j))
    {
        return;
    }
    if (j >= len || body[j] < '0' || body[j] > '9')
    {
        return;
    }
    // Clamped on every digit, so the accumulator stays at or below INT32_MAX and the next
    // multiply-add stays inside int64_t however long a digit run the section carries.
    int64_t v = 0;
    for (; j < len && body[j] >= '0' && body[j] <= '9'; j++)
    {
        v = v * 10 + (body[j] - '0');
        if (v > INT32_MAX)
        {
            v = INT32_MAX;
        }
    }
    ctx->ns->i32 = (int32_t)v;
    ctx->ns->ok = PROTO_TRUE;
}

// Read Status-Message, the "grpc-message" field-value, out of the trailer-section in ns->in. The
// slice runs to the end of its field-line and stays Percent-Encoded, so decoding is the caller's.
static void grpcweb_trailers_message(struct GrpcWebInternal *restrict ctx)
{
    static const char key[] = "grpc-message:";
    const uint8_t *body = ctx->ns->in.data;
    const size_t len = ctx->ns->in.len;
    size_t start = 0;
    ctx->ns->ok = PROTO_FALSE;
    ctx->ns->text = NULL;
    ctx->ns->text_len = 0;
    if (!body || !find_key(body, len, key, sizeof(key) - 1, &start))
    {
        return;
    }
    size_t j = start;
    while (j < len && body[j] != '\r' && body[j] != '\n')
    {
        j++;
    }
    ctx->ns->text = (const char *)(body + start);
    ctx->ns->text_len = j - start;
    ctx->ns->ok = PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
GrpcWebNs GrpcWeb = {.frame = grpcweb_frame,
                     .frame_message = grpcweb_frame_message,
                     .frame_trailers = grpcweb_frame_trailers,
                     .parse = grpcweb_parse,
                     .trailers_status = grpcweb_trailers_status,
                     .trailers_message = grpcweb_trailers_message,
                     .internal = &s_grpcweb};

#endif // PROTOCORE_ENABLE_GRPC_WEB
