// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_h3_conn.c
 * @brief HTTP/3 application engine over QUIC streams (see protocore_h3_conn.h).
 */

#include "network_drivers/presentation/http/http3/h3_conn.h"
#include "mmgr/plaintext.h" // HTTP is plaintext; its streams borrow from that arena
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_HTTP3

#include "network_drivers/presentation/http/http3/qpack.h"
#include "network_drivers/presentation/http/http3/quic_varint.h"

// protocore_h3_conn_respond builds its response HEADERS frame from a fixed 256-byte QPACK block into a
// PROTOCORE_H3_STREAM_BUF output buffer, which is why that builder's failure guard carries a coverage
// exclusion. PROTOCORE_H3_STREAM_BUF is an overridable macro (h3_conn.h), so pin the relationship here
// rather than let a shrunken buffer silently make the excluded path reachable: 256 bytes of QPACK
// plus the frame's type + length varints (at most 8 each).
static_assert(PROTOCORE_H3_STREAM_BUF >= 256 + 16,
              "PROTOCORE_H3_STREAM_BUF must hold a whole response HEADERS frame: the 256-byte QPACK field section "
              "protocore_h3_conn_respond builds plus the H3 frame type and length varints");

// The plaintext-pool term this file declares: one borrow per HTTP/3 connection, taken from the
// persistent end on first use and held for the connection's life.
static_assert(PROTOCORE_WORK_H3_CONN >= (size_t)PROTOCORE_QUIC_MAX_CONNS * PROTOCORE_H3_CONN_BORROW,
              "PROTOCORE_WORK_H3_CONN must cover one reassembly + pseudo-header borrow per HTTP/3 stream on every "
              "connection: raise it in protocore_config.h");

// Offsets into the one borrow. Grouped by field, so each region's stride is a power of two.
#define H3_OFF_BUF 0u
#define H3_OFF_PATH (H3_OFF_BUF + (size_t)PROTOCORE_H3_MAX_STREAMS * PROTOCORE_H3_STREAM_BUF)
#define H3_OFF_AUTHORITY (H3_OFF_PATH + (size_t)PROTOCORE_H3_MAX_STREAMS * PROTOCORE_H3_PATH_LEN)
#define H3_OFF_METHOD (H3_OFF_AUTHORITY + (size_t)PROTOCORE_H3_MAX_STREAMS * PROTOCORE_H3_AUTHORITY_LEN)

// The connection's bytes, split by offset over its streams. Idempotent: a connection initialised
// again keeps the borrow it already holds, because the persistent end is never given back.
static proto_bool h3_conn_slot_storage(H3Conn *h3)
{
    uint8_t *base = h3->streams[0].buf; // H3_OFF_BUF is 0, so the borrow is recoverable from it
    if (base == NULL)
    {
        protocore_span b = protocore_plaintext_persist_span(PROTOCORE_H3_CONN_BORROW);
        if (!protocore_span_ok(b))
        {
            return PROTO_FALSE;
        }
        base = b.buf;
    }
    for (size_t i = 0; i < PROTOCORE_H3_MAX_STREAMS; i++)
    {
        h3->streams[i].buf = base + H3_OFF_BUF + i * PROTOCORE_H3_STREAM_BUF;
        h3->streams[i].path = (char *)(base + H3_OFF_PATH + i * PROTOCORE_H3_PATH_LEN);
        h3->streams[i].authority = (char *)(base + H3_OFF_AUTHORITY + i * PROTOCORE_H3_AUTHORITY_LEN);
        h3->streams[i].method = (char *)(base + H3_OFF_METHOD + i * PROTOCORE_H3_METHOD_LEN);
    }
    return PROTO_TRUE;
}

static H3Stream *protocore_h3_stream_get(H3Conn *h3, uint64_t id, proto_bool create)
{
    H3Stream *free_slot = NULL;
    for (size_t i = 0; i < PROTOCORE_H3_MAX_STREAMS; i++)
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
    uint8_t *buf = free_slot->buf;
    char *method = free_slot->method;
    char *path = free_slot->path;
    char *authority = free_slot->authority;
    mem.set(free_slot, 0, sizeof(*free_slot));
    free_slot->buf = buf;
    free_slot->method = method;
    free_slot->path = path;
    free_slot->authority = authority;
    // The bytes are the connection's and outlive the stream that last held them. A claimed slot
    // starts empty, or this request reads the previous one's method, path and authority.
    mem.set(method, 0, PROTOCORE_H3_METHOD_LEN);
    mem.set(path, 0, PROTOCORE_H3_PATH_LEN);
    mem.set(authority, 0, PROTOCORE_H3_AUTHORITY_LEN);
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
    mem.cpy(dst, src, len);
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
    if (nlen == 7 && mem.cmp(name, ":method", 7) == 0)
    {
        set_field(st->method, PROTOCORE_H3_METHOD_LEN, value, vlen);
    }
    else if (nlen == 5 && mem.cmp(name, ":path", 5) == 0)
    {
        set_field(st->path, PROTOCORE_H3_PATH_LEN, value, vlen);
    }
    else if (nlen == 10 && mem.cmp(name, ":authority", 10) == 0)
    {
        set_field(st->authority, PROTOCORE_H3_AUTHORITY_LEN, value, vlen);
    }
    return PROTO_TRUE; // ignore regular headers for now (routing is by method + path)
}

// Close the connection with an RFC 9114 sec 8.1 error code. HTTP/3 errors are the application's,
// so they travel in a CONNECTION_CLOSE of type 0x1d; in the transport variant the same number
// would name a completely different condition.
static void h3_fail(H3Conn *h3, uint64_t error_code)
{
    if (h3->qc)
    {
        protocore_quic_conn_close_app(h3->qc, error_code);
    }
}

// Parse the accumulated request stream: decode HEADERS, coalesce DATA into a body, and dispatch.
static void dispatch_request(H3Conn *h3, H3Stream *st)
{
    // The bytes this dispatch works out of: the coalesced body and what QPACK decodes through. They
    // live for the call, so they come from the transient end and go back at every exit.
    const size_t mark = protocore_plaintext_mark();
    protocore_span bs = protocore_plaintext_span(PROTOCORE_H3_STREAM_BUF, 4);
    protocore_span sc = protocore_plaintext_span(PROTOCORE_H3_QPACK_SCRATCH, 4);
    if (!protocore_span_ok(bs) || !protocore_span_ok(sc))
    {
        protocore_plaintext_release(mark);
        h3_fail(h3, H3_INTERNAL_ERROR);
        return;
    }
    uint8_t *body = bs.buf;
    char *scratch = (char *)sc.buf;
    size_t body_len = 0;

    size_t off = 0;
    while (off < st->buf_len)
    {
        H3Frame fr;
        if (!protocore_h3_frame_parse(st->buf + off, st->buf_len - off, &fr))
        {
            break;
        }
        size_t payload = off + fr.header_len;
        if (payload + fr.length > st->buf_len)
        {
            break; // incomplete frame
        }
        const uint8_t *fp = st->buf + payload;
        // sec 7.2.4: SETTINGS belongs to the control stream alone. sec 4.1: DATA before any
        // HEADERS is an invalid frame sequence. Both are connection errors, and both were
        // previously skipped over as if the frame were an unknown type.
        if (fr.type == H3_SETTINGS || fr.type == H3_GOAWAY || fr.type == H3_MAX_PUSH_ID || fr.type == H3_CANCEL_PUSH)
        {
            protocore_plaintext_release(mark);
            h3_fail(h3, H3_FRAME_UNEXPECTED);
            return;
        }
        if (fr.type == H3_DATA && !st->have_headers)
        {
            protocore_plaintext_release(mark);
            h3_fail(h3, H3_FRAME_UNEXPECTED);
            return;
        }
        if (fr.type == H3_HEADERS)
        {
            ReqEmit e = {st};
            protocore_qpack_decode(fp, (size_t)fr.length, scratch, PROTOCORE_H3_QPACK_SCRATCH, req_emit, &e);
            st->have_headers = PROTO_TRUE;
        }
        else if (fr.type == H3_DATA)
        {
            // Copy only while there is room left in body. room is 0 once body is full (no underflow),
            // and take is clamped to it, so body_len + take <= PROTOCORE_H3_STREAM_BUF. Both arms of both
            // guards are defensive: body and st->buf are the same PROTOCORE_H3_STREAM_BUF size, and every
            // DATA payload counted into body_len sits behind a frame header inside st->buf, so the
            // running total is always strictly below it and take never exceeds room.
            size_t room = (body_len < PROTOCORE_H3_STREAM_BUF) ? PROTOCORE_H3_STREAM_BUF - body_len : 0;
            size_t take = (size_t)fr.length;
            if (take > room)
            {
                take = room;
            }
            if (take)
            {
                mem.cpy(body + body_len, fp, take);
            }
            body_len += take;
        }
        off = payload + (size_t)fr.length;
    }

    if (st->have_headers && h3->on_request)
    {
        h3->on_request(h3->app, h3, st->id, st->method, st->path, st->authority, body, body_len);
    }
    protocore_plaintext_release(mark);
}

static void append(H3Stream *st, const uint8_t *data, size_t len)
{
    if (len > PROTOCORE_H3_STREAM_BUF - st->buf_len)
    {
        len = PROTOCORE_H3_STREAM_BUF - st->buf_len;
    }
    mem.cpy(st->buf + st->buf_len, data, len);
    st->buf_len += len;
}

// Read the leading stream-type varint of a uni stream and set st->role; consumes it from the buffer.
// Returns false if more bytes are needed (nothing consumed).
static proto_bool protocore_h3_classify_uni_stream(H3Conn *h3, H3Stream *st)
{
    uint64_t type = 0;
    size_t c = 0;
    if (!protocore_quic_varint_decode(st->buf, st->buf_len, &type, &c))
    {
        return PROTO_FALSE; // need more bytes for the varint
    }
    st->type_read = PROTO_TRUE;
    if (type == 0x00)
    {
        // sec 6.2.1: one control stream per peer. A second one claiming the role is fatal.
        for (size_t i = 0; i < PROTOCORE_H3_MAX_STREAMS; i++)
        {
            if (&h3->streams[i] != st && h3->streams[i].role == H3_ROLE_CONTROL)
            {
                h3_fail(h3, H3_STREAM_CREATION_ERROR);
                return PROTO_FALSE;
            }
        }
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
    mem.move(st->buf, st->buf + c, st->buf_len - c);
    st->buf_len -= c;
    return PROTO_TRUE;
}

// Parse whatever complete frames the control stream holds (SETTINGS first), consuming them.
static void protocore_h3_consume_control(H3Conn *h3, H3Stream *st)
{
    size_t off = 0;
    while (off < st->buf_len)
    {
        H3Frame fr;
        if (!protocore_h3_frame_parse(st->buf + off, st->buf_len - off, &fr))
        {
            break;
        }
        if (off + fr.header_len + fr.length > st->buf_len)
        {
            break;
        }
        // sec 6.2.1: SETTINGS is the first frame on the control stream, and sec 7.2.4 permits
        // exactly one - a second would let the peer reconfigure the connection mid-flight.
        if (fr.type != H3_SETTINGS && !h3->peer_settings_seen)
        {
            h3_fail(h3, H3_MISSING_SETTINGS);
            return;
        }
        if (fr.type == H3_SETTINGS)
        {
            if (h3->peer_settings_seen)
            {
                h3_fail(h3, H3_FRAME_UNEXPECTED);
                return;
            }
            h3->peer_settings_seen = PROTO_TRUE;
            protocore_h3_settings_defaults(&h3->peer_settings);
            if (!protocore_h3_parse_settings(st->buf + off + fr.header_len, (size_t)fr.length, &h3->peer_settings))
            {
                h3_fail(h3, H3_SETTINGS_ERROR);
                return;
            }
        }
        off += fr.header_len + (size_t)fr.length;
    }
    mem.move(st->buf, st->buf + off, st->buf_len - off);
    st->buf_len -= off;
}

static void on_stream_data(void *app, struct QuicConn *, uint64_t stream_id, const uint8_t *data, size_t len,
                           proto_bool fin)
{
    H3Conn *h3 = (H3Conn *)app;
    H3Stream *st = protocore_h3_stream_get(h3, stream_id, PROTO_TRUE);
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
    if (st->role != H3_ROLE_REQUEST && !st->type_read && st->buf_len >= 1 && !protocore_h3_classify_uni_stream(h3, st))
    {
        return; // need more bytes for the varint, or the stream was refused
    }

    if (st->role == H3_ROLE_CONTROL)
    {
        protocore_h3_consume_control(h3, st);
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

static void on_handshake_done(void *app, struct QuicConn *qc)
{
    H3Conn *h3 = (H3Conn *)app;
    if (h3->control_opened)
    {
        return;
    }
    h3->control_opened = PROTO_TRUE;

    // Server control stream (id 3): stream type 0x00 + SETTINGS.
    uint8_t buf[64];
    size_t p = protocore_quic_varint_encode(buf, sizeof(buf), 0x00);
    static const uint64_t ids[] = {H3_SETTINGS_QPACK_MAX_TABLE_CAPACITY, H3_SETTINGS_QPACK_BLOCKED_STREAMS};
    static const uint64_t vals[] = {0, 0};
    p += protocore_h3_build_settings(buf + p, sizeof(buf) - p, ids, vals, 2);
    protocore_quic_conn_stream_send(qc, 3, buf, p, PROTO_FALSE);

    // QPACK encoder (id 7, type 0x02) and decoder (id 11, type 0x03) streams: type byte only.
    uint8_t t;
    size_t n = protocore_quic_varint_encode(&t, 1, 0x02);
    protocore_quic_conn_stream_send(qc, 7, &t, n, PROTO_FALSE);
    n = protocore_quic_varint_encode(&t, 1, 0x03);
    protocore_quic_conn_stream_send(qc, 11, &t, n, PROTO_FALSE);
    h3->next_uni_id = 15;
}

void protocore_h3_conn_init(H3Conn *h3, struct QuicConn *qc, H3RequestFn on_request, void *app)
{
    uint8_t *base = h3->streams[0].buf; // the borrow is the connection's, not the call's
    mem.set(h3, 0, sizeof(*h3));
    h3->streams[0].buf = base;
    if (!h3_conn_slot_storage(h3))
    {
        return; // no bytes to run out of; the connection answers nothing
    }
    // The borrow carries the previous connection's requests; the pointers to it stay, or the next
    // init would ask the persistent end for a second borrow it never gives back.
    mem.set(h3->streams[0].buf, 0, PROTOCORE_H3_CONN_BORROW);
    h3->qc = qc;
    h3->on_request = on_request;
    h3->app = app;
    h3->next_uni_id = 3;
    for (size_t i = 0; i < PROTOCORE_H3_MAX_STREAMS; i++)
    {
        h3->streams[i].id = UINT64_MAX;
    }
    protocore_h3_settings_defaults(&h3->peer_settings);

    QuicConnCallbacks cb = {on_stream_data, on_handshake_done, h3};
    qc->cb = cb;
}

proto_bool protocore_h3_conn_respond(H3Conn *h3, uint64_t stream_id, int status, const char *content_type, const uint8_t *body,
                              size_t body_len)
{
    H3Stream *st = protocore_h3_stream_get(h3, stream_id, PROTO_FALSE);
    if (st)
    {
        st->responded = PROTO_TRUE;
    }

    // The bytes this response is built out of: the QPACK field section and the frames carrying it.
    // Both live for the call.
    const size_t mark = protocore_plaintext_mark();
    protocore_span bl = protocore_plaintext_span(PROTOCORE_H3_QPACK_BLOCK, 4);
    protocore_span ob = protocore_plaintext_span(PROTOCORE_H3_STREAM_BUF, 4);
    if (!protocore_span_ok(bl) || !protocore_span_ok(ob))
    {
        protocore_plaintext_release(mark);
        return PROTO_FALSE;
    }
    uint8_t *block = bl.buf;
    uint8_t *out = ob.buf;

    // QPACK field section: prefix + :status + optional content-type + content-length.
    size_t bp = protocore_qpack_encode_prefix(block, PROTOCORE_H3_QPACK_BLOCK);
    char st3[4];
    st3[0] = (char)('0' + (status / 100) % 10);
    st3[1] = (char)('0' + (status / 10) % 10);
    st3[2] = (char)('0' + status % 10);
    st3[3] = '\0';
    bp += protocore_qpack_encode_header(block + bp, PROTOCORE_H3_QPACK_BLOCK - bp, ":status", 7, st3, 3);
    if (content_type)
    {
        // Cap above the largest content-type that can fit this block even at QPACK-Huffman's best
        // 5-bit/char (~PROTOCORE_H3_QPACK_BLOCK * 8/5), so an over-long value trips the encode's reject
        // below instead of being truncated into a fittable length (see the matching protocore_h2_conn note).
        bp += protocore_qpack_encode_header(block + bp, PROTOCORE_H3_QPACK_BLOCK - bp, "content-type", 12, content_type,
                                     strnlen(content_type, (size_t)PROTOCORE_H3_QPACK_BLOCK * 2));
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
    bp += protocore_qpack_encode_header(block + bp, PROTOCORE_H3_QPACK_BLOCK - bp, "content-length", 14, clen, cl);

    // HEADERS frame + DATA frame, sent on the request stream with FIN.
    size_t op = protocore_h3_build_headers(out, PROTOCORE_H3_STREAM_BUF, block, bp);
    if (!op)
    {
        protocore_plaintext_release(mark);
        return PROTO_FALSE;
        // fits
    }
    if (body_len)
    {
        size_t dn = protocore_h3_build_data(out + op, PROTOCORE_H3_STREAM_BUF - op, body, body_len);
        if (!dn)
        {
            protocore_plaintext_release(mark);
            return PROTO_FALSE;
        }
        op += dn;
    }
    const proto_bool sent = protocore_quic_conn_stream_send(h3->qc, stream_id, out, op, PROTO_TRUE) == op;
    protocore_plaintext_release(mark);
    return sent;
}

#endif // PROTOCORE_ENABLE_HTTP3
