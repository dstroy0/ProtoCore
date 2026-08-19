// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_h3_conn.c
 * @brief HTTP/3 application engine over QUIC streams (see protocore_h3_conn.h).
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

static uint8_t qpack_work[16]; // the borrow an entry takes; Qpack never reads it

static uint8_t h3_frame_work[16]; // the borrow an entry takes; H3Frame never reads it

static uint8_t quic_varint_work[16]; // the borrow an entry takes; QuicVarint never reads it

#if PROTOCORE_ENABLE_HTTP3

#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h"
#include "network_drivers/presentation/http/http3/h3_conn/h3_conn.h"
#include "network_drivers/presentation/http/http3/h3_frame/h3_frame.h"
#include "network_drivers/presentation/http/http3/qpack/qpack.h"
#include "network_drivers/presentation/http/http3/quic_conn/quic_conn.h"
#include "network_drivers/presentation/http/http3/quic_varint/quic_varint.h"

PROTOCORE_BEGIN_DECLS

// HTTP/3 stream roles (a mutually-exclusive internal role, not a wire value).
typedef enum PROTO_ENUM_PACKED
{
    H3_ROLE_FREE = 0,
    H3_ROLE_REQUEST,   ///< client-initiated bidirectional request stream
    H3_ROLE_CONTROL,   ///< client control stream (type 0x00)
    H3_ROLE_QPACK_ENC, ///< client QPACK encoder stream (type 0x02)
    H3_ROLE_QPACK_DEC, ///< client QPACK decoder stream (type 0x03)
    H3_ROLE_OTHER_UNI, ///< an unknown unidirectional stream (drained/ignored)
} H3StreamRole;

// Per-stream state. The bytes it reassembles into are the span at a fixed offset, so only what is
// not derivable lives here.
typedef struct
{
    uint64_t id;          ///< stream id (UINT64_MAX = free)
    H3StreamRole role;    ///< stream role
    proto_bool type_read; ///< a unidirectional stream's type varint has been consumed
    proto_bool responded; ///< a response has been sent on this request stream
    uint8_t *buf;         ///< PROTOCORE_H3_STREAM_BUF bytes of the connection's span
    size_t buf_len;
    char *method;            ///< PROTOCORE_H3_METHOD_LEN bytes of the connection's span
    char *path;              ///< PROTOCORE_H3_PATH_LEN bytes of the connection's span
    char *authority;         ///< PROTOCORE_H3_AUTHORITY_LEN bytes of the connection's span
    proto_bool have_headers; ///< a HEADERS frame has been decoded
    size_t body_off;         ///< where the accumulated body begins within buf (after the last HEADERS)
} H3Stream;

// The one definition, private to this TU. It sits at H3_OFF_CTX in the caller's span, so its size
// never leaves this file and no consumer can name it.
typedef struct
{
    uint8_t *b;  ///< the connection's span, bound once and held for its life
    uint8_t *qc; ///< the QUIC connection's context span, under this one
    H3RequestFn on_request;
    void *app;
    H3Settings peer_settings;
    proto_bool control_opened;     ///< our control + QPACK streams have been opened
    proto_bool peer_settings_seen; ///< the peer's one SETTINGS frame has arrived (sec 6.2.1)
    uint64_t next_uni_id;          ///< next server-initiated unidirectional stream id (3, 7, 11, ...)
    H3Stream streams[PROTOCORE_H3_MAX_STREAMS];
} H3ConnCtx;

// The handle a caller sets a call's members on, and the connection the bound span holds.
struct H3ConnInternal
{
    H3ConnCtx *c; ///< the connection, resolved from the bound span
    H3ConnNs *ns; ///< the handle a caller sets a call's members on
};

// H3ConnNs::respond builds its response HEADERS frame from a fixed 256-byte QPACK block into a
// PROTOCORE_H3_STREAM_BUF output buffer, which is why that builder's failure guard carries a coverage
// exclusion. PROTOCORE_H3_STREAM_BUF is an overridable macro (h3_conn.h), so pin the relationship here
// rather than let a shrunken buffer silently make the excluded path reachable: 256 bytes of QPACK
// plus the frame's type + length varints (at most 8 each).
static_assert(PROTOCORE_H3_STREAM_BUF >= 256 + 16,
              "PROTOCORE_H3_STREAM_BUF must hold a whole response HEADERS frame: the 256-byte QPACK field section "
              "H3ConnNs::respond builds plus the H3 frame type and length varints");

// The caller's span, split: the context, then the streams grouped by field so each region's stride
// is a power of two and stream i reaches its bytes with a shift rather than a multiply.
#define H3_OFF_CTX 0u
#define H3_OFF_BUF ((size_t)PROTOCORE_H3_CONN_CTX)
#define H3_OFF_PATH (H3_OFF_BUF + (size_t)PROTOCORE_H3_MAX_STREAMS * PROTOCORE_H3_STREAM_BUF)
#define H3_OFF_AUTHORITY (H3_OFF_PATH + (size_t)PROTOCORE_H3_MAX_STREAMS * PROTOCORE_H3_PATH_LEN)
#define H3_OFF_METHOD (H3_OFF_AUTHORITY + (size_t)PROTOCORE_H3_MAX_STREAMS * PROTOCORE_H3_AUTHORITY_LEN)
#define H3_OFF_BODY (H3_OFF_METHOD + (size_t)PROTOCORE_H3_MAX_STREAMS * PROTOCORE_H3_METHOD_LEN)
#define H3_OFF_QPACK (H3_OFF_BODY + (size_t)PROTOCORE_H3_STREAM_BUF)
#define H3_OFF_BLOCK (H3_OFF_QPACK + (size_t)PROTOCORE_H3_QPACK_SCRATCH)
#define H3_OFF_OUT (H3_OFF_BLOCK + (size_t)PROTOCORE_H3_QPACK_BLOCK)
static_assert(sizeof(H3ConnCtx) <= PROTOCORE_H3_CONN_CTX,
              "PROTOCORE_H3_CONN_CTX is short of the connection context - raise it in protocore_config.h, "
              "which derives PROTOCORE_H3_CONN_BORROW and PROTOCORE_PLAINTEXT_ARENA_SIZE from it");
static_assert(H3_OFF_OUT + (size_t)PROTOCORE_H3_STREAM_BUF <= PROTOCORE_H3_CONN_BORROW,
              "PROTOCORE_H3_CONN_BORROW is short of the context, one reassembly + pseudo-header region per "
              "stream, and the body and QPACK regions a dispatch reads - raise it in protocore_config.h");

// The regions, at their offsets in the caller's span.
#define H3_CTX(w) ((H3ConnCtx *)(void *)((w) + H3_OFF_CTX))
// H3_OFF_CTX is 0, so a callback is handed back the span it was bound with.
#define H3_SPAN(c) ((uint8_t *)(void *)(c))

// The connection's bytes, split by offset over its streams. Idempotent: a connection initialised
// again keeps the borrow it already holds, because the persistent end is never given back.
static proto_bool h3_conn_slot_storage(H3ConnCtx *h3)
{
    uint8_t *base = h3->b;
    if (base == NULL)
    {
        return PROTO_FALSE;
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

static H3Stream *protocore_h3_stream_get(H3ConnCtx *h3, uint64_t id, proto_bool create)
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
static void h3_fail(H3ConnCtx *h3, uint64_t error_code)
{
    if (h3->qc)
    {
        QuicConn.bind.ctx = h3->qc;
        QuicConn.close_args.error_code = error_code;
        QuicConn.close_app(QuicConn.internal);
    }
}

// Parse the accumulated request stream: decode HEADERS, coalesce DATA into a body, and dispatch.
static void dispatch_request(H3ConnCtx *h3, H3Stream *st)
{
    // The bytes this dispatch works out of: the coalesced body and what QPACK decodes through. They
    // live for the call, so they come from the transient end and go back at every exit.
    uint8_t *body = h3->b + H3_OFF_BODY;
    char *scratch = (char *)(h3->b + H3_OFF_QPACK);
    size_t body_len = 0;

    size_t off = 0;
    while (off < st->buf_len)
    {
        H3FrameHeader fr;
        H3Frame.parse_header_args.buf = st->buf + off;
        H3Frame.parse_header_args.len = st->buf_len - off;
        H3Frame.parse_header_args.out = &fr;
        H3Frame.parse_header(h3_frame_work);
        if (!H3Frame.ok)
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
            h3_fail(h3, H3_FRAME_UNEXPECTED);
            return;
        }
        if (fr.type == H3_DATA && !st->have_headers)
        {
            h3_fail(h3, H3_FRAME_UNEXPECTED);
            return;
        }
        if (fr.type == H3_HEADERS)
        {
            ReqEmit e = {st};
            Qpack.decode_args.block = fp;
            Qpack.decode_args.len = (size_t)fr.length;
            Qpack.decode_args.scratch = scratch;
            Qpack.decode_args.scratch_cap = PROTOCORE_H3_QPACK_SCRATCH;
            Qpack.decode_args.emit = req_emit;
            Qpack.decode_args.ctx = &e;
            Qpack.decode(qpack_work);
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
        h3->on_request(h3->app, H3_SPAN(h3), st->id, st->method, st->path, st->authority, body, body_len);
    }
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
static proto_bool protocore_h3_classify_uni_stream(H3ConnCtx *h3, H3Stream *st)
{
    uint64_t type = 0;
    size_t c = 0;
    QuicVarint.decode_args.in = st->buf;
    QuicVarint.decode_args.len = st->buf_len;
    QuicVarint.decode_args.value = &type;
    QuicVarint.decode_args.consumed = &c;
    QuicVarint.decode(quic_varint_work);
    if (!QuicVarint.ok)
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
static void protocore_h3_consume_control(H3ConnCtx *h3, H3Stream *st)
{
    size_t off = 0;
    while (off < st->buf_len)
    {
        H3FrameHeader fr;
        H3Frame.parse_header_args.buf = st->buf + off;
        H3Frame.parse_header_args.len = st->buf_len - off;
        H3Frame.parse_header_args.out = &fr;
        H3Frame.parse_header(h3_frame_work);
        if (!H3Frame.ok)
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
            H3Frame.settings_defaults_args.s = &h3->peer_settings;
            H3Frame.settings_defaults(h3_frame_work);
            H3Frame.parse_settings_args.payload = st->buf + off + fr.header_len;
            H3Frame.parse_settings_args.len = (size_t)fr.length;
            H3Frame.parse_settings_args.s = &h3->peer_settings;
            H3Frame.parse_settings(h3_frame_work);
            if (!H3Frame.ok)
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

static void on_stream_data(void *app, uint8_t *, uint64_t stream_id, const uint8_t *data, size_t len, proto_bool fin)
{
    H3ConnCtx *h3 = (H3ConnCtx *)app;
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

static void on_handshake_done(void *app, uint8_t *qc)
{
    H3ConnCtx *h3 = (H3ConnCtx *)app;
    if (h3->control_opened)
    {
        return;
    }
    h3->control_opened = PROTO_TRUE;

    // Server control stream (id 3): stream type 0x00 + SETTINGS.
    uint8_t buf[64];
    QuicVarint.encode_args.out = buf;
    QuicVarint.encode_args.cap = sizeof(buf);
    QuicVarint.encode_args.value = 0x00;
    QuicVarint.encode(quic_varint_work);
    size_t p = QuicVarint.n;
    static const uint64_t ids[] = {H3_SETTINGS_QPACK_MAX_TABLE_CAPACITY, H3_SETTINGS_QPACK_BLOCKED_STREAMS};
    static const uint64_t vals[] = {0, 0};
    H3Frame.build_settings_args.out = buf + p;
    H3Frame.build_settings_args.cap = sizeof(buf) - p;
    H3Frame.build_settings_args.ids = ids;
    H3Frame.build_settings_args.vals = vals;
    H3Frame.build_settings_args.n = 2;
    H3Frame.build_settings(h3_frame_work);
    p += H3Frame.n;
    QuicConn.bind.ctx = qc;
    QuicConn.stream_send_args.stream_id = 3;
    QuicConn.stream_send_args.data = buf;
    QuicConn.stream_send_args.len = p;
    QuicConn.stream_send_args.fin = PROTO_FALSE;
    QuicConn.stream_send(QuicConn.internal);

    // QPACK encoder (id 7, type 0x02) and decoder (id 11, type 0x03) streams: type byte only.
    uint8_t t;
    QuicVarint.encode_args.out = &t;
    QuicVarint.encode_args.cap = 1;
    QuicVarint.encode_args.value = 0x02;
    QuicVarint.encode(quic_varint_work);
    size_t n = QuicVarint.n;
    QuicConn.bind.ctx = qc;
    QuicConn.stream_send_args.stream_id = 7;
    QuicConn.stream_send_args.data = &t;
    QuicConn.stream_send_args.len = n;
    QuicConn.stream_send_args.fin = PROTO_FALSE;
    QuicConn.stream_send(QuicConn.internal);
    QuicVarint.encode_args.out = &t;
    QuicVarint.encode_args.cap = 1;
    QuicVarint.encode_args.value = 0x03;
    QuicVarint.encode(quic_varint_work);
    n = QuicVarint.n;
    QuicConn.bind.ctx = qc;
    QuicConn.stream_send_args.stream_id = 11;
    QuicConn.stream_send_args.data = &t;
    QuicConn.stream_send_args.len = n;
    QuicConn.stream_send_args.fin = PROTO_FALSE;
    QuicConn.stream_send(QuicConn.internal);
    h3->next_uni_id = 15;
}

static void h3_conn_open(H3ConnCtx *h3, uint8_t *qc, H3RequestFn on_request, void *app)
{
    uint8_t *base = h3->b; // the span is the connection's, bound before this call
    mem.set(h3, 0, sizeof(*h3));
    h3->b = base;
    if (!h3_conn_slot_storage(h3))
    {
        return; // no bytes to run out of; the connection answers nothing
    }
    // The span carries the previous connection's requests; the context leads it, so only the stream
    // regions past it are cleared.
    mem.set(h3->b + H3_OFF_BUF, 0, PROTOCORE_H3_CONN_BORROW - H3_OFF_BUF);
    h3->qc = qc;
    h3->on_request = on_request;
    h3->app = app;
    h3->next_uni_id = 3;
    for (size_t i = 0; i < PROTOCORE_H3_MAX_STREAMS; i++)
    {
        h3->streams[i].id = UINT64_MAX;
    }
    H3Frame.settings_defaults_args.s = &h3->peer_settings;
    H3Frame.settings_defaults(h3_frame_work);

    QuicConnCallbacks cb = {on_stream_data, on_handshake_done, h3};
    QuicConn.bind.ctx = qc;
    QuicConn.cb = cb;
    QuicConn.callbacks(QuicConn.internal);
}

static proto_bool h3_conn_reply(H3ConnCtx *h3, uint64_t stream_id, int status, const char *content_type,
                                const uint8_t *body, size_t body_len)
{
    H3Stream *st = protocore_h3_stream_get(h3, stream_id, PROTO_FALSE);
    if (st)
    {
        st->responded = PROTO_TRUE;
    }

    // The bytes this response is built out of: the QPACK field section and the frames carrying it.
    // Both live for the call.
    uint8_t *block = h3->b + H3_OFF_BLOCK;
    uint8_t *out = h3->b + H3_OFF_OUT;

    // QPACK field section: prefix + :status + optional content-type + content-length.
    Qpack.encode_prefix_args.out = block;
    Qpack.encode_prefix_args.cap = PROTOCORE_H3_QPACK_BLOCK;
    Qpack.encode_prefix(qpack_work);
    size_t bp = Qpack.n;
    char st3[4];
    st3[0] = (char)('0' + (status / 100) % 10);
    st3[1] = (char)('0' + (status / 10) % 10);
    st3[2] = (char)('0' + status % 10);
    st3[3] = '\0';
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = PROTOCORE_H3_QPACK_BLOCK - bp;
    Qpack.encode_header_args.name = ":status";
    Qpack.encode_header_args.name_len = 7;
    Qpack.encode_header_args.value = st3;
    Qpack.encode_header_args.value_len = 3;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;
    if (content_type)
    {
        // Cap above the largest content-type that can fit this block even at QPACK-Huffman's best
        // 5-bit/char (~PROTOCORE_H3_QPACK_BLOCK * 8/5), so an over-long value trips the encode's reject
        // below instead of being truncated into a fittable length (see the matching protocore_h2_conn note).
        Qpack.encode_header_args.out = block + bp;
        Qpack.encode_header_args.cap = PROTOCORE_H3_QPACK_BLOCK - bp;
        Qpack.encode_header_args.name = "content-type";
        Qpack.encode_header_args.name_len = 12;
        Qpack.encode_header_args.value = content_type;
        Qpack.encode_header_args.value_len = str.len(content_type, (size_t)PROTOCORE_H3_QPACK_BLOCK * 2);
        Qpack.encode_header(qpack_work);
        bp += Qpack.n;
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
    Qpack.encode_header_args.out = block + bp;
    Qpack.encode_header_args.cap = PROTOCORE_H3_QPACK_BLOCK - bp;
    Qpack.encode_header_args.name = "content-length";
    Qpack.encode_header_args.name_len = 14;
    Qpack.encode_header_args.value = clen;
    Qpack.encode_header_args.value_len = cl;
    Qpack.encode_header(qpack_work);
    bp += Qpack.n;

    // HEADERS frame + DATA frame, sent on the request stream with FIN.
    H3Frame.build_headers_args.out = out;
    H3Frame.build_headers_args.cap = PROTOCORE_H3_STREAM_BUF;
    H3Frame.build_headers_args.block = block;
    H3Frame.build_headers_args.len = bp;
    H3Frame.build_headers(h3_frame_work);
    size_t op = H3Frame.n;
    if (!op)
    {
        return PROTO_FALSE;
        // fits
    }
    if (body_len)
    {
        H3Frame.build_data_args.out = out + op;
        H3Frame.build_data_args.cap = PROTOCORE_H3_STREAM_BUF - op;
        H3Frame.build_data_args.data = body;
        H3Frame.build_data_args.len = body_len;
        H3Frame.build_data(h3_frame_work);
        size_t dn = H3Frame.n;
        if (!dn)
        {
            return PROTO_FALSE;
        }
        op += dn;
    }
    QuicConn.bind.ctx = h3->qc;
    QuicConn.stream_send_args.stream_id = stream_id;
    QuicConn.stream_send_args.data = out;
    QuicConn.stream_send_args.len = op;
    QuicConn.stream_send_args.fin = PROTO_TRUE;
    QuicConn.stream_send(QuicConn.internal);
    const proto_bool sent = (QuicConn.n == op);
    return sent;
}

// --- the entries -----------------------------------------------------------

// The bound span, as this file's connection. Every entry starts here.
static H3ConnCtx *h3_bound(struct H3ConnInternal *restrict ctx)
{
    if (!ctx || !ctx->ns->bind.b)
    {
        return NULL;
    }
    ctx->c = H3_CTX(ctx->ns->bind.b);
    return ctx->c;
}

static void h3_conn_init(struct H3ConnInternal *restrict ctx)
{
    H3ConnCtx *h3 = h3_bound(ctx);
    H3Conn.ok = PROTO_FALSE;
    if (!h3 || !H3Conn.bind.qc)
    {
        return;
    }
    h3->b = H3Conn.bind.b; // survives the wipe inside h3_conn_open
    h3_conn_open(h3, H3Conn.bind.qc, H3Conn.app_args.on_request, H3Conn.app_args.app);
    H3Conn.ok = (h3->b != NULL);
}

static void h3_conn_respond(struct H3ConnInternal *restrict ctx)
{
    H3ConnCtx *h3 = h3_bound(ctx);
    H3Conn.ok = PROTO_FALSE;
    if (!h3)
    {
        return;
    }
    H3Conn.ok = h3_conn_reply(h3, H3Conn.respond_args.stream_id, H3Conn.respond_args.status,
                              H3Conn.respond_args.content_type, H3Conn.respond_args.body, H3Conn.respond_args.body_len);
}

static struct H3ConnInternal s_h3 = {.ns = &H3Conn};

H3ConnNs H3Conn = {.init = h3_conn_init, .respond = h3_conn_respond, .internal = &s_h3};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP3
