// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file grpcweb.c
 * @brief gRPC-Web message framing builder + parser (pure, host-tested).
 */

#include "services/iot/grpcweb/grpcweb.h"

#if PC_ENABLE_GRPC_WEB

#include <string.h>

size_t pc_grpcweb_frame(uint8_t *buf, size_t cap, uint8_t flags, const uint8_t *body, size_t body_len)
{
    if (!buf || (body_len && !body) || body_len > 0xFFFFFFFFu)
    {
        return 0;
    }
    size_t total = GRPCWEB_PREFIX_LEN + body_len;
    if (total > cap)
    {
        return 0;
    }
    buf[0] = flags;
    buf[1] = (uint8_t)(body_len >> 24);
    buf[2] = (uint8_t)(body_len >> 16);
    buf[3] = (uint8_t)(body_len >> 8);
    buf[4] = (uint8_t)(body_len);
    if (body_len)
    {
        memcpy(buf + GRPCWEB_PREFIX_LEN, body, body_len);
    }
    return total;
}

size_t pc_grpcweb_frame_message(uint8_t *buf, size_t cap, const uint8_t *msg, size_t msg_len, proto_bool compressed)
{
    return pc_grpcweb_frame(buf, cap, compressed ? GRPCWEB_FLAG_COMPRESSED : 0, msg, msg_len);
}

// Append a NUL-terminated string at *pos with bounds check; advance *pos. False on overflow.
static proto_bool put_str(uint8_t *buf, size_t cap, size_t *pos, const char *s)
{
    size_t n = strnlen(s, cap + 1);
    if (*pos + n > cap)
    {
        return PROTO_FALSE;
    }
    memcpy(buf + *pos, s, n);
    *pos += n;
    return PROTO_TRUE;
}

// Append a non-negative integer as decimal at *pos. False on overflow.
static proto_bool put_int(uint8_t *buf, size_t cap, size_t *pos, int v)
{
    if (v < 0)
    {
        return PROTO_FALSE;
    }
    char tmp[12];
    size_t n = 0;
    char rev[12];
    size_t r = 0;
    if (v == 0)
    {
        rev[r++] = '0';
    }
    while (v)
    {
        rev[r++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (r)
    {
        tmp[n++] = rev[--r];
    }
    if (*pos + n > cap)
    {
        return PROTO_FALSE;
    }
    memcpy(buf + *pos, tmp, n);
    *pos += n;
    return PROTO_TRUE;
}

size_t pc_grpcweb_frame_trailer(uint8_t *buf, size_t cap, int status, const char *message)
{
    if (!buf || cap < GRPCWEB_PREFIX_LEN)
    {
        return 0;
    }
    size_t pos = GRPCWEB_PREFIX_LEN; // reserve the prefix; patch the length after the body
    if (!put_str(buf, cap, &pos, "grpc-status:") || !put_int(buf, cap, &pos, status) ||
        !put_str(buf, cap, &pos, "\r\n"))
    {
        return 0;
    }
    if (message && *message)
    {
        if (!put_str(buf, cap, &pos, "grpc-message:") || !put_str(buf, cap, &pos, message) ||
            !put_str(buf, cap, &pos, "\r\n"))
        {
            return 0;
        }
    }
    size_t body_len = pos - GRPCWEB_PREFIX_LEN;
    buf[0] = GRPCWEB_FLAG_TRAILER;
    buf[1] = (uint8_t)(body_len >> 24);
    buf[2] = (uint8_t)(body_len >> 16);
    buf[3] = (uint8_t)(body_len >> 8);
    buf[4] = (uint8_t)(body_len);
    return pos;
}

proto_bool pc_grpcweb_parse(const uint8_t *buf, size_t len, GrpcWebFrame *out, size_t *consumed)
{
    if (!buf || !out || !consumed || len < GRPCWEB_PREFIX_LEN)
    {
        return PROTO_FALSE;
    }
    uint32_t body_len = ((uint32_t)buf[1] << 24) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 8) | buf[4];
    if ((size_t)GRPCWEB_PREFIX_LEN + body_len > len)
    {
        return PROTO_FALSE; // frame not fully buffered
    }
    out->flags = buf[0];
    out->compressed = (buf[0] & GRPCWEB_FLAG_COMPRESSED) != 0;
    out->trailer = (buf[0] & GRPCWEB_FLAG_TRAILER) != 0;
    out->body = buf + GRPCWEB_PREFIX_LEN;
    out->body_len = body_len;
    *consumed = GRPCWEB_PREFIX_LEN + body_len;
    return PROTO_TRUE;
}

proto_bool pc_grpcweb_trailer_status(const uint8_t *body, size_t len, int *status)
{
    if (!body)
    {
        return PROTO_FALSE;
    }
    static const char key[] = "grpc-status:";
    const size_t klen = sizeof(key) - 1;
    for (size_t i = 0; i + klen <= len; i++)
    {
        // Match at the start of a line (i == 0 or preceded by '\n').
        if ((i == 0 || body[i - 1] == '\n') && memcmp(body + i, key, klen) == 0)
        {
            size_t j = i + klen;
            if (j >= len || body[j] < '0' || body[j] > '9')
            {
                return PROTO_FALSE;
            }
            int v = 0;
            for (; j < len && body[j] >= '0' && body[j] <= '9'; j++)
            {
                v = v * 10 + (body[j] - '0');
            }
            if (status)
            {
                *status = v;
            }
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

proto_bool pc_grpcweb_trailer_message(const uint8_t *body, size_t len, const char **msg, size_t *msg_len)
{
    if (!body)
    {
        return PROTO_FALSE;
    }
    static const char key[] = "grpc-message:";
    const size_t klen = sizeof(key) - 1;
    for (size_t i = 0; i + klen <= len; i++)
    {
        // Match at the start of a line (i == 0 or preceded by '\n').
        if ((i == 0 || body[i - 1] == '\n') && memcmp(body + i, key, klen) == 0)
        {
            size_t start = i + klen;
            size_t j = start;
            while (j < len && body[j] != '\r' && body[j] != '\n') // the value runs to end-of-line
            {
                j++;
            }
            if (msg)
            {
                *msg = (const char *)(body + start);
            }
            if (msg_len)
            {
                *msg_len = j - start;
            }
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

#endif // PC_ENABLE_GRPC_WEB
