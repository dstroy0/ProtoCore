// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_h3_conn.cpp
 * @brief HTTP/3 application engine over QUIC streams (see pc_h3_conn.h).
 */

#include "network_drivers/presentation/http/http3/h3_conn.h"

#if PC_ENABLE_HTTP3

#include "network_drivers/presentation/http/http3/qpack.h"
#include "network_drivers/presentation/http/http3/quic_varint.h"
#include <string.h>

// pc_h3_conn_respond builds its response HEADERS frame from a fixed 256-byte QPACK block into a
// PC_H3_STREAM_BUF output buffer, which is why that builder's failure guard carries a coverage
// exclusion. PC_H3_STREAM_BUF is an overridable macro (h3_conn.h), so pin the relationship here
// rather than let a shrunken buffer silently make the excluded path reachable: 256 bytes of QPACK
// plus the frame's type + length varints (at most 8 each).
static_assert(PC_H3_STREAM_BUF >= 256 + 16,
              "PC_H3_STREAM_BUF must hold a whole response HEADERS frame: the 256-byte QPACK field section "
              "pc_h3_conn_respond builds plus the H3 frame type and length varints");

static H3Stream *pc_h3_stream_get(H3Conn *h3, uint64_t id, proto_bool create)
{
    H3Stream *free_slot = NULL;
    for (size_t i = 0; i < PC_H3_MAX_STREAMS; i++)
    {
        if (h3->streams[i].role != H3_ROLE_FREE && h3->streams[i].id == id)
        {
            return &h3->streams[i];
        }
        if (!free_slot && h3->streams[i].role == H3_ROLE_FREE)
        {
            free_slot = &h3->streams[i];
        }
    }
    if (!create || !free_slot)
    {
        return NULL;
    }
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->id = id;
    return free_slot;
}

// Copy a bounded, NUL-terminated field.
static void set_field(char *dst, size_t cap, const char *src, size_t len)
{
    if (len >= cap)
    {
        len = cap - 1;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

// QPACK emit target: capture the request pseudo-headers.
typedef struct
{
    H3Stream *st;
} ReqEmit;
static proto_bool req_emit(void *ctx, const char *name, size_t nlen, const char *value, size_t vlen)
{
    H3Stream *st = ((ReqEmit *)ctx)->st;
    if (nlen == 7 && memcmp(name, ":method", 7) == 0)
    {
        set_field(st->method, sizeof(st->method), value, vlen);
    }
    else if (nlen == 5 && memcmp(name, ":path", 5) == 0)
    {
        set_field(st->path, sizeof(st->path), value, vlen);
    }
    else if (nlen == 10 && memcmp(name, ":authority", 10) == 0)
    {
        set_field(st->authority, sizeof(st->authority), value, vlen);
    }
    return PROTO_TRUE; // ignore regular headers for now (routing is by method + path)
}

// Parse the accumulated request stream: decode HEADERS, coalesce DATA into a body, and dispatch.
static void dispatch_request(H3Conn *h3, H3Stream *st)
{
    static uint8_t body[PC_H3_STREAM_BUF];
    static char scratch[PC_H3_PATH_LEN + PC_H3_AUTHORITY_LEN + 64];
    size_t body_len = 0;

    size_t off = 0;
    while (off < st->buf_len)
    {
        H3Frame fr;
        if (!pc_h3_frame_parse(st->buf + off, st->buf_len - off, &fr))
        {
            break;
        }
        size_t payload = off + fr.header_len;
        if (payload + fr.length > st->buf_len)
        {
            break; // incomplete frame
        }
        const uint8_t *fp = st->buf + payload;
        if (fr.type == H3FrameType::H3_HEADERS)
        {
            ReqEmit e = {st};
            pc_qpack_decode(fp, (size_t)fr.length, scratch, sizeof(scratch), req_emit, &e);
            st->have_headers = PROTO_TRUE;
        }
        else if (fr.type == H3FrameType::H3_DATA)
        {
            // Copy only while there is room left in body. room is 0 once body is full (no underflow),
            // and take is clamped to it, so body_len + take <= sizeof(body). Both arms of both guards
            // are defensive: body and st->buf are the same PC_H3_STREAM_BUF size, and every DATA
            // payload counted into body_len sits behind a frame header inside st->buf, so the running
            // total is always strictly below sizeof(body) and take never exceeds room.
            size_t room = (body_len < sizeof(body)) ? sizeof(body) - body_len : 0; // GCOVR_EXCL_LINE
            size_t take = (size_t)fr.length;
            if (take > room) // GCOVR_EXCL_LINE
            {
                take = room; // GCOVR_EXCL_LINE
            }
            if (take)
            {
                memcpy(body + body_len, fp, take);
            }
            body_len += take;
        }
        off = payload + (size_t)fr.length;
    }

    if (st->have_headers && h3->on_request)
    {
        h3->on_request(h3->app, h3, st->id, st->method, st->path, st->authority, body, body_len);
    }
}

static void append(H3Stream *st, const uint8_t *data, size_t len)
{
    if (len > sizeof(st->buf) - st->buf_len)
    {
        len = sizeof(st->buf) - st->buf_len;
    }
    memcpy(st->buf + st->buf_len, data, len);
    st->buf_len += len;
}

// Read the leading stream-type varint of a uni stream and set st->role; consumes it from the buffer.
// Returns false if more bytes are needed (nothing consumed).
static proto_bool pc_h3_classify_uni_stream(H3Stream *st)
{
    uint64_t type = 0;
    size_t c = 0;
    if (!pc_quic_varint_decode(st->buf, st->buf_len, &type, &c))
    {
        return PROTO_FALSE; // need more bytes for the varint
    }
    st->type_read = PROTO_TRUE;
    if (type == 0x00)
    {
        st->role = H3_ROLE_CONTROL;
    }
    else if (type == 0x02)
    {
        st->role = H3_ROLE_QPACK_ENC;
    }
    else if (type == 0x03)
    {
        st->role = H3_ROLE_QPACK_DEC;
    }
    else
    {
        st->role = H3_ROLE_OTHER_UNI;
    }
    memmove(st->buf, st->buf + c, st->buf_len - c);
    st->buf_len -= c;
    return PROTO_TRUE;
}

// Parse whatever complete frames the control stream holds (SETTINGS first), consuming them.
static void pc_h3_consume_control(H3Conn *h3, H3Stream *st)
{
    size_t off = 0;
    while (off < st->buf_len)
    {
        H3Frame fr;
        if (!pc_h3_frame_parse(st->buf + off, st->buf_len - off, &fr))
        {
            break;
        }
        if (off + fr.header_len + fr.length > st->buf_len)
        {
            break;
        }
        if (fr.type == H3FrameType::H3_SETTINGS)
        {
            pc_h3_settings_defaults(&h3->peer_settings);
            pc_h3_parse_settings(st->buf + off + fr.header_len, (size_t)fr.length, &h3->peer_settings);
        }
        off += fr.header_len + (size_t)fr.length;
    }
    memmove(st->buf, st->buf + off, st->buf_len - off);
    st->buf_len -= off;
}

static void on_stream_data(void *app, QuicConn *, uint64_t stream_id, const uint8_t *data, size_t len, proto_bool fin)
{
    H3Conn *h3 = (H3Conn *)app;
    H3Stream *st = pc_h3_stream_get(h3, stream_id, PROTO_TRUE);
    if (!st)
    {
        return;
    }

    if (st->role == H3_ROLE_FREE)
    {
        st->role = (stream_id & 0x03) == 0x00 ? H3_ROLE_REQUEST : H3_ROLE_OTHER_UNI;
    }

    append(st, data, len);

    // A unidirectional stream begins with a stream-type varint; classify it once.
    if (st->role != H3_ROLE_REQUEST && !st->type_read && st->buf_len >= 1 && !pc_h3_classify_uni_stream(st))
    {
        return; // need more bytes for the varint
    }

    if (st->role == H3_ROLE_CONTROL)
    {
        pc_h3_consume_control(h3, st);
        return;
    }
    if (st->role != H3_ROLE_REQUEST)
    {
        st->buf_len = 0; // QPACK/other uni streams: nothing to do (static-table only)
        return;
    }

    if (fin)
    {
        dispatch_request(h3, st);
    }
}

static void on_handshake_done(void *app, QuicConn *qc)
{
    H3Conn *h3 = (H3Conn *)app;
    if (h3->control_opened)
    {
        return;
    }
    h3->control_opened = PROTO_TRUE;

    // Server control stream (id 3): stream type 0x00 + SETTINGS.
    uint8_t buf[64];
    size_t p = pc_quic_varint_encode(buf, sizeof(buf), 0x00);
    static const uint64_t ids[] = {H3Setting::H3_SETTINGS_QPACK_MAX_TABLE_CAPACITY,
                                   H3Setting::H3_SETTINGS_QPACK_BLOCKED_STREAMS};
    static const uint64_t vals[] = {0, 0};
    p += pc_h3_build_settings(buf + p, sizeof(buf) - p, ids, vals, 2);
    pc_quic_conn_stream_send(qc, 3, buf, p, PROTO_FALSE);

    // QPACK encoder (id 7, type 0x02) and decoder (id 11, type 0x03) streams: type byte only.
    uint8_t t;
    size_t n = pc_quic_varint_encode(&t, 1, 0x02);
    pc_quic_conn_stream_send(qc, 7, &t, n, PROTO_FALSE);
    n = pc_quic_varint_encode(&t, 1, 0x03);
    pc_quic_conn_stream_send(qc, 11, &t, n, PROTO_FALSE);
    h3->next_uni_id = 15;
}

void pc_h3_conn_init(H3Conn *h3, QuicConn *qc, H3RequestFn on_request, void *app)
{
    memset(h3, 0, sizeof(*h3));
    h3->qc = qc;
    h3->on_request = on_request;
    h3->app = app;
    h3->next_uni_id = 3;
    for (size_t i = 0; i < PC_H3_MAX_STREAMS; i++)
    {
        h3->streams[i].id = UINT64_MAX;
    }
    pc_h3_settings_defaults(&h3->peer_settings);

    QuicConnCallbacks cb = {on_stream_data, on_handshake_done, h3};
    qc->cb = cb;
}

proto_bool pc_h3_conn_respond(H3Conn *h3, uint64_t stream_id, int status, const char *content_type, const uint8_t *body,
                              size_t body_len)
{
    H3Stream *st = pc_h3_stream_get(h3, stream_id, PROTO_FALSE);
    if (st)
    {
        st->responded = PROTO_TRUE;
    }

    // QPACK field section: prefix + :status + optional content-type + content-length.
    uint8_t block[256];
    size_t bp = pc_qpack_encode_prefix(block, sizeof(block));
    char st3[4];
    st3[0] = (char)('0' + (status / 100) % 10);
    st3[1] = (char)('0' + (status / 10) % 10);
    st3[2] = (char)('0' + status % 10);
    st3[3] = '\0';
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, ":status", 7, st3, 3);
    if (content_type)
    {
        // Cap above the largest content-type that can fit this block even at QPACK-Huffman's best
        // 5-bit/char (~sizeof block * 8/5), so an over-long value trips the encode's reject below
        // instead of being truncated into a fittable length (see the matching pc_h2_conn note).
        bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, "content-type", 12, content_type,
                                     strnlen(content_type, sizeof(block) * 2));
    }
    char clen[16];
    size_t cl = 0;
    {
        // decimal content-length without stdlib
        char tmp[16];
        size_t n = 0;
        size_t v = body_len;
        do
        {
            tmp[n++] = (char)('0' + v % 10);
            v /= 10;
        } while (v);
        while (n)
        {
            clen[cl++] = tmp[--n];
        }
    }
    bp += pc_qpack_encode_header(block + bp, sizeof(block) - bp, "content-length", 14, clen, cl);

    // HEADERS frame + DATA frame, sent on the request stream with FIN.
    uint8_t out[PC_H3_STREAM_BUF];
    size_t op = pc_h3_build_headers(out, sizeof(out), block, bp);
    if (!op) // GCOVR_EXCL_LINE  block is a fixed 256 bytes and out is PC_H3_STREAM_BUF, whose floor the
    {
        return PROTO_FALSE; // GCOVR_EXCL_LINE  static_assert at the top of this file pins: the HEADERS frame always
                            // fits
    }
    if (body_len)
    {
        size_t dn = pc_h3_build_data(out + op, sizeof(out) - op, body, body_len);
        if (!dn)
        {
            return PROTO_FALSE;
        }
        op += dn;
    }
    return pc_quic_conn_stream_send(h3->qc, stream_id, out, op, PROTO_TRUE) == op;
}

#endif // PC_ENABLE_HTTP3
