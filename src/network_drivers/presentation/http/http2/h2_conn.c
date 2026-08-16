// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_h2_conn.c
 * @brief HTTP/2 connection + stream engine - implementation. See protocore_h2_conn.h.
 */

#include "network_drivers/presentation/http/http2/h2_conn.h"
#include "mmgr/membuild.h"  // protocore_sb frame builder
#include "mmgr/protomem.h"
#include "mmgr/protostr.h"

/** @brief Per-stream state (RFC 9113 sec 5.1, server side of a client-initiated stream). A
 *  mutually-exclusive internal lifecycle state, not a wire value. */
typedef enum PROTO_ENUM_PACKED
{
    H2_ST_IDLE = 0,
    H2_ST_OPEN,        ///< receiving (headers seen, no END_STREAM yet)
    H2_ST_HALF_CLOSED, ///< client finished (END_STREAM); we may still respond
    H2_ST_CLOSED,
} H2StreamState;

typedef struct
{
    uint32_t id;                   ///< stream identifier (0 = free slot)
    H2StreamState state;           ///< lifecycle state
    int32_t send_window;           ///< our remaining DATA flow window for this stream
    proto_bool has_content_length; ///< the request declared a content-length
    proto_bool content_length_bad; ///< that declaration was not a plain decimal number
    uint32_t content_length;       ///< the declared value
    uint32_t data_seen;            ///< DATA payload octets received on this stream
} H2Stream;

// One connection's engine state. Only what is not derivable: the frame buffer, the header block,
// the HPACK scratch and the frame header live at fixed offsets in the caller's borrow, so the
// macros below compute them from the pointer rather than the context storing them.
typedef struct
{
    uint8_t phase; ///< 0 = awaiting preface, 1 = running, 2 = closed
    H2Callbacks cb;

    size_t fhave; ///< bytes buffered for the current frame, header included
    size_t pre;   ///< preface bytes matched so far

    size_t hblock_len;
    uint32_t hblock_stream;
    proto_bool hblock_end_stream;
    proto_bool hblock_trailers; ///< the block is a sec 8.1 trailer section, not the request
    uint8_t hblock_frames;      ///< CONTINUATION frames this block has spanned
    proto_bool in_header_block; ///< between a non-END_HEADERS HEADERS and its END_HEADERS CONTINUATION

    uint8_t hdec[PROTOCORE_HPACK_BORROW]; ///< HPACK decoder table (the peer encoder's state)

    H2Settings peer;          ///< the peer's settings (affect how we send)
    int32_t conn_send_window; ///< our connection-level DATA flow window

    H2Stream streams[PROTOCORE_H2_MAX_STREAMS];
    uint32_t last_peer_stream; ///< highest client (odd) stream id accepted
} H2ConnCtx;

// The caller's borrow, split: the context, then the regions it works out of. One pointer arrives
// and every region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define H2_CONN_OFF_CTX 0u
#define H2_CONN_OFF_FBUF (H2_CONN_OFF_CTX + sizeof(H2ConnCtx))
#define H2_CONN_OFF_HBLOCK (H2_CONN_OFF_FBUF + PROTOCORE_H2_MAX_FRAME)
#define H2_CONN_OFF_HSCRATCH (H2_CONN_OFF_HBLOCK + PROTOCORE_H2_HDR_BLOCK)
#define H2_CONN_OFF_FHDR (H2_CONN_OFF_HSCRATCH + PROTOCORE_H2_HDR_BLOCK)
static_assert(H2_CONN_OFF_FHDR + PROTOCORE_H2_FRAME_HDR_CAP <= PROTOCORE_H2_CONN_BORROW,
              "PROTOCORE_H2_CONN_BORROW is short of one connection - raise PROTOCORE_H2_CONN_RECORD"
              " in protocore_config.h, which sums it into its arena");

// The regions, at their offsets in the caller's borrow.
#define H2_CONN_CTX(w) ((H2ConnCtx *)(void *)((w) + H2_CONN_OFF_CTX))
#define H2_CONN_FBUF(w) ((w) + H2_CONN_OFF_FBUF)
#define H2_CONN_HBLOCK(w) ((w) + H2_CONN_OFF_HBLOCK)
#define H2_CONN_HSCRATCH(w) ((char *)(void *)((w) + H2_CONN_OFF_HSCRATCH))
#define H2_CONN_FHDR(w) ((w) + H2_CONN_OFF_FHDR)

#if PROTOCORE_ENABLE_HTTP2

static uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void wr(uint8_t *restrict work, const uint8_t *data, size_t len)
{
    if (H2_CONN_CTX(work)->cb.write)
    {
        H2_CONN_CTX(work)->cb.write(H2_CONN_CTX(work)->cb.io, data, len);
    }
}

// Offsets into the one borrow. Each region is a power of two, so each offset is a multiple of one.

static H2Stream *find_stream(uint8_t *restrict work, uint32_t id)
{
    for (int i = 0; i < PROTOCORE_H2_MAX_STREAMS; i++)
    {
        if (H2_CONN_CTX(work)->streams[i].id == id && id != 0)
        {
            return &H2_CONN_CTX(work)->streams[i];
        }
    }
    return NULL;
}

static H2Stream *alloc_stream(uint8_t *restrict work, uint32_t id)
{
    for (int i = 0; i < PROTOCORE_H2_MAX_STREAMS; i++)
    {
        if (H2_CONN_CTX(work)->streams[i].id == 0)
        {
            mem.set(&H2_CONN_CTX(work)->streams[i], 0, sizeof H2_CONN_CTX(work)->streams[i]);
            H2_CONN_CTX(work)->streams[i].id = id;
            H2_CONN_CTX(work)->streams[i].state = H2_ST_OPEN;
            H2_CONN_CTX(work)->streams[i].send_window = (int32_t)H2_CONN_CTX(work)->peer.initial_window_size;
            return &H2_CONN_CTX(work)->streams[i];
        }
    }
    return NULL; // at MAX_CONCURRENT_STREAMS
}

static void send_our_settings(uint8_t *restrict work)
{
    static const uint16_t ids[4] = {H2_SETTINGS_ENABLE_PUSH, H2_SETTINGS_MAX_CONCURRENT_STREAMS,
                                    H2_SETTINGS_INITIAL_WINDOW_SIZE, H2_SETTINGS_MAX_FRAME_SIZE};
    static const uint32_t vals[4] = {0, PROTOCORE_H2_MAX_STREAMS, 65535, PROTOCORE_H2_MAX_FRAME};
    uint8_t buf[H2_FRAME_HEADER_LEN + 4 * 6];
    size_t n = (H2Frame.build_settings_args.buf = buf, H2Frame.build_settings_args.cap = sizeof buf,
                H2Frame.build_settings_args.ids = ids, H2Frame.build_settings_args.vals = vals,
                H2Frame.build_settings_args.n = 4, H2Frame.build_settings(NULL), H2Frame.n);
    printf("DIAG_H2 settings n=%u write=%p\n", (unsigned)n, (void *)(uintptr_t)H2_CONN_CTX(work)->cb.write);
    wr(work, buf, n);
}

// Builds a RST_STREAM naming stream 0 with error code 0, in the shape send_control takes.
static size_t build_rst_refuse(uint8_t *b, size_t cap)
{
    return (H2Frame.rst_args.buf = b, H2Frame.rst_args.cap = cap, H2Frame.rst_args.stream_id = 0,
            H2Frame.rst_args.error = 0, H2Frame.build_rst_stream(NULL), H2Frame.n);
}

// send_control takes a plain builder, so the entry is reached through one.
static size_t build_settings_ack(uint8_t *out, size_t cap)
{
    H2Frame.ack_args.buf = out;
    H2Frame.ack_args.cap = cap;
    H2Frame.build_settings_ack(NULL);
    return H2Frame.n;
}

static void send_control(uint8_t *restrict work, size_t (*build)(uint8_t *, size_t))
{
    uint8_t buf[H2_FRAME_HEADER_LEN + 16];
    size_t n = build(buf, sizeof buf);
    // Both builders passed here (SETTINGS ACK, 9 bytes; RST_STREAM, 13) fit this 25-byte buffer, so the
    // zero return is unreachable; kept so a future builder that can fail is not silently written short.
    if (n)
    {
        wr(work, buf, n);
    }
}

// One size covers every control frame a received frame can provoke, fixed at compile time: a PING
// ACK carries the 8-byte opaque payload, RST_STREAM and WINDOW_UPDATE carry 4, a SETTINGS ACK none.
#define H2_CTL_FRAME_MAX (H2_FRAME_HEADER_LEN + 8)

// Reset one stream (RFC 9113 sec 5.4.2: a stream error kills the stream, not the connection). The
// frame is built in the dispatcher's borrow, which is where every outbound control frame is staged.
static void send_rst(uint8_t *restrict work, protocore_span f, uint32_t stream_id, uint32_t err)
{
    size_t n = (H2Frame.rst_args.buf = f.buf, H2Frame.rst_args.cap = f.cap, H2Frame.rst_args.stream_id = stream_id,
                H2Frame.rst_args.error = err, H2Frame.build_rst_stream(NULL), H2Frame.n);
    wr(work, f.buf, n);
}

// RFC 9113 sec 8.1.1: a declared content-length must equal the sum of the DATA payloads, and must
// have been a number to begin with. A body past the declared length settles the moment it goes
// over; a short one settles when the stream ends. Either way the request is malformed, which is a
// stream error. @return false when the stream has been reset.
static proto_bool content_length_holds(uint8_t *restrict work, H2Stream *s, protocore_span f, proto_bool end_stream)
{
    if (!s->has_content_length)
    {
        return PROTO_TRUE;
    }
    proto_bool malformed = s->content_length_bad;
    if (s->data_seen > s->content_length)
    {
        malformed = PROTO_TRUE;
    }
    if (end_stream && s->data_seen != s->content_length)
    {
        malformed = PROTO_TRUE;
    }
    if (!malformed)
    {
        return PROTO_TRUE;
    }
    send_rst(work, f, s->id, H2_PROTOCOL_ERROR);
    s->id = 0; // free the slot
    return PROTO_FALSE;
}

// One header block being decoded: the connection, the stream it arrived on, the dispatcher's borrow
// that any resulting control frame is staged in, whether the block ends the stream, whether it is a
// sec 8.1 trailer section rather than the request, and whether a pseudo-header appeared in one.
typedef struct
{
    uint8_t *restrict work; ///< the connection's borrow this block is being read into
    uint32_t stream_id;
    protocore_span f;
    proto_bool end_stream;
    proto_bool trailers;
    proto_bool pseudo_in_trailer;
} H2Block;

// A trailer section is decoded but never delivered: the HPACK dynamic table has to track every
// block on the connection, while the request it trails has already been dispatched.
// Records a request's declared content-length on its stream. sec 8.1.1 measures it against the
// DATA that follows, so the value is kept here and settled when the stream ends. A value that is
// not a plain decimal number, or does not fit 32 bits, is itself malformed.
static void note_content_length(uint8_t *restrict work, uint32_t stream_id, const char *val, size_t vl)
{
    H2Stream *s = find_stream(work, stream_id);
    if (!s)
    {
        return;
    }
    if (s->has_content_length)
    {
        s->content_length_bad = PROTO_TRUE; // sec 8.1.1: at most one content-length
        return;
    }
    s->has_content_length = PROTO_TRUE;
    if (vl == 0)
    {
        s->content_length_bad = PROTO_TRUE;
        return;
    }
    uint32_t n = 0;
    for (size_t i = 0; i < vl; i++)
    {
        const uint8_t d = (uint8_t)(val[i] - '0');
        if (d > 9u || n > (0xFFFFFFFFu - d) / 10u)
        {
            s->content_length_bad = PROTO_TRUE;
            return;
        }
        n = n * 10u + d;
    }
    s->content_length = n;
}

static proto_bool emit_header(void *ctx, const char *name, size_t nl, const char *val, size_t vl)
{
    H2Block *b = (H2Block *)ctx;
    if (b->trailers)
    {
        if (nl > 0 && name[0] == ':')
        {
            b->pseudo_in_trailer = PROTO_TRUE; // sec 8.1: no pseudo-header may appear in a trailer
        }
        return PROTO_TRUE;
    }
    if (nl == 14 && mem.cmp(name, "content-length", 14) == 0)
    {
        note_content_length(b->work, b->stream_id, val, vl);
    }
    if (H2_CONN_CTX(b->work)->cb.on_header)
    {
        H2_CONN_CTX(b->work)->cb.on_header(H2_CONN_CTX(b->work)->cb.app, b->stream_id, name, nl, val, vl);
    }
    return PROTO_TRUE;
}

// Decode a complete request header block and deliver it to the application.
static proto_bool decode_block(H2Block *b, const uint8_t *block, size_t len)
{
    uint8_t *restrict work = b->work;
    if (!(Hpack.decode_args.block = block, Hpack.decode_args.len = len,
          Hpack.decode_args.scratch = H2_CONN_HSCRATCH(work), Hpack.decode_args.scratch_cap = PROTOCORE_H2_HDR_BLOCK,
          Hpack.decode_args.emit = emit_header, Hpack.decode_args.ctx = b, Hpack.decode(H2_CONN_CTX(work)->hdec),
          Hpack.ok))
    {
        return PROTO_FALSE; // COMPRESSION_ERROR
    }
    proto_bool well_formed = !b->pseudo_in_trailer;
    if (!b->trailers && H2_CONN_CTX(work)->cb.on_headers_end)
    {
        well_formed = H2_CONN_CTX(work)->cb.on_headers_end(H2_CONN_CTX(work)->cb.app, b->stream_id, b->end_stream);
    }
    if (!well_formed)
    {
        // RFC 9113 sec 8.1.1: a malformed request kills the stream, not the connection.
        send_rst(work, b->f, b->stream_id, H2_PROTOCOL_ERROR);
        H2Stream *dead = find_stream(work, b->stream_id);
        if (dead)
        {
            dead->id = 0; // free the slot
        }
        return PROTO_TRUE;
    }
    H2Stream *s = find_stream(work, b->stream_id);
    if (s)
    {
        if (b->end_stream)
        {
            s->state = H2_ST_HALF_CLOSED;
            // A request that ends with its headers carries no body, so a declared content-length
            // has to be zero (sec 8.1.1).
            content_length_holds(work, s, b->f, PROTO_TRUE);
        }
        else
        {
            s->state = H2_ST_OPEN;
        }
    }
    return PROTO_TRUE;
}

static proto_bool handle_headers(uint8_t *restrict work, const H2FrameHeader *h, const uint8_t *payload,
                                 protocore_span f)
{
    if (h->stream_id == 0 || (h->stream_id & 1) == 0)
    {
        return PROTO_FALSE; // requests are client-initiated odd stream ids (RFC 9113 sec 5.1.1)
    }
    const uint8_t *p = payload;
    size_t plen = h->length;
    uint8_t pad = 0;
    if (h->flags & H2_FLAG_PADDED)
    {
        if (plen < 1)
        {
            return PROTO_FALSE;
        }
        pad = p[0];
        p++;
        plen--;
    }
    if (h->flags & H2_FLAG_PRIORITY)
    {
        if (plen < 5)
        {
            return PROTO_FALSE;
        }
        p += 5;
        plen -= 5; // priority info accepted and ignored
    }
    if (pad > plen)
    {
        return PROTO_FALSE;
    }
    plen -= pad; // strip trailing padding

    proto_bool end_stream = (h->flags & H2_FLAG_END_STREAM) != 0;
    proto_bool trailers = PROTO_FALSE;
    if (h->stream_id <= H2_CONN_CTX(work)->last_peer_stream)
    {
        // sec 5.1.1 governs a new stream; sec 8.1 lets a second HEADERS trail an open one.
        H2Stream *open = find_stream(work, h->stream_id);
        if (!open || open->state != H2_ST_OPEN)
        {
            return PROTO_FALSE; // a new stream id must exceed every one already seen
        }
        if (!end_stream)
        {
            // sec 8.1: a trailer section is the last thing the peer sends on the stream.
            send_rst(work, f, h->stream_id, H2_PROTOCOL_ERROR);
            open->id = 0;
            return PROTO_TRUE;
        }
        trailers = PROTO_TRUE;
    }
    else
    {
        H2_CONN_CTX(work)->last_peer_stream = h->stream_id;
        if (!alloc_stream(work, h->stream_id))
        {
            send_control(work, build_rst_refuse);
            return PROTO_TRUE; // refuse quietly is fine; keep the connection
        }
    }

    if (h->flags & H2_FLAG_END_HEADERS)
    {
        H2Block b = {work, h->stream_id, f, end_stream, trailers, PROTO_FALSE};
        return decode_block(&b, p, plen);
    }
    // Spans CONTINUATION frames: buffer the fragment.
    if (plen > PROTOCORE_H2_HDR_BLOCK)
    {
        return PROTO_FALSE;
    }
    mem.cpy(H2_CONN_HBLOCK(work), p, plen);
    H2_CONN_CTX(work)->hblock_len = plen;
    H2_CONN_CTX(work)->hblock_stream = h->stream_id;
    H2_CONN_CTX(work)->hblock_end_stream = end_stream;
    H2_CONN_CTX(work)->hblock_trailers = trailers;
    H2_CONN_CTX(work)->hblock_frames = 0;
    H2_CONN_CTX(work)->in_header_block = PROTO_TRUE;
    return PROTO_TRUE;
}

static proto_bool handle_continuation(uint8_t *restrict work, const H2FrameHeader *h, const uint8_t *payload,
                                      protocore_span f)
{
    if (!H2_CONN_CTX(work)->in_header_block || h->stream_id != H2_CONN_CTX(work)->hblock_stream)
    {
        return PROTO_FALSE;
    }
    H2_CONN_CTX(work)->hblock_frames++;
    if (H2_CONN_CTX(work)->hblock_frames > PROTOCORE_H2_MAX_CONTINUATION)
    {
        return PROTO_FALSE; // sec 6.10: an empty CONTINUATION adds no bytes, so cap the count too
    }
    if (H2_CONN_CTX(work)->hblock_len + h->length > PROTOCORE_H2_HDR_BLOCK)
    {
        return PROTO_FALSE;
    }
    mem.cpy(H2_CONN_HBLOCK(work) + H2_CONN_CTX(work)->hblock_len, payload, h->length);
    H2_CONN_CTX(work)->hblock_len += h->length;
    if (h->flags & H2_FLAG_END_HEADERS)
    {
        H2_CONN_CTX(work)->in_header_block = PROTO_FALSE;
        H2Block b = {work,
                     H2_CONN_CTX(work)->hblock_stream,
                     f,
                     H2_CONN_CTX(work)->hblock_end_stream,
                     H2_CONN_CTX(work)->hblock_trailers,
                     PROTO_FALSE};
        return decode_block(&b, H2_CONN_HBLOCK(work), H2_CONN_CTX(work)->hblock_len);
    }
    return PROTO_TRUE;
}

static proto_bool handle_data(uint8_t *restrict work, const H2FrameHeader *h, const uint8_t *payload, protocore_span f)
{
    if (h->stream_id == 0)
    {
        return PROTO_FALSE;
    }
    const uint8_t *p = payload;
    size_t plen = h->length;
    uint8_t pad = 0;
    if (h->flags & H2_FLAG_PADDED)
    {
        if (plen < 1)
        {
            return PROTO_FALSE;
        }
        pad = p[0];
        p++;
        plen--;
    }
    if (pad > plen)
    {
        return PROTO_FALSE;
    }
    plen -= pad;

    // The stream decides whether these bytes may be delivered at all, so it is resolved before the
    // application sees them. RFC 9113 sec 5.1: a DATA frame on an idle stream - one no HEADERS ever
    // opened - is a connection error of type PROTOCOL_ERROR. Sec 6.1: on a stream that is no longer
    // open it is a stream error of type STREAM_CLOSED, which is the client having already sent
    // END_STREAM and then sent more.
    H2Stream *s = find_stream(work, h->stream_id);
    if (!s)
    {
        return PROTO_FALSE;
    }
    if (s->state != H2_ST_OPEN)
    {
        send_rst(work, f, h->stream_id, H2_STREAM_CLOSED);
        return PROTO_TRUE; // the stream dies, the connection lives
    }

    proto_bool end_stream = (h->flags & H2_FLAG_END_STREAM) != 0;
    // The body is measured against the declared content-length before any of it is delivered: a
    // request whose two accounts of its own length disagree is what a smuggling attempt looks like.
    s->data_seen += (uint32_t)plen;
    if (!content_length_holds(work, s, f, end_stream))
    {
        return PROTO_TRUE;
    }
    if (H2_CONN_CTX(work)->cb.on_data)
    {
        H2_CONN_CTX(work)->cb.on_data(H2_CONN_CTX(work)->cb.app, h->stream_id, p, plen, end_stream);
    }
    if (end_stream)
    {
        s->state = H2_ST_HALF_CLOSED;
    }
    // Replenish flow-control windows for the bytes we consumed (whole frame length).
    if (h->length > 0)
    {
        size_t n = (H2Frame.window_args.buf = f.buf, H2Frame.window_args.cap = f.cap, H2Frame.window_args.stream_id = 0,
                    H2Frame.window_args.increment = h->length, H2Frame.build_window_update(NULL), H2Frame.n);
        wr(work, f.buf, n);
        n = (H2Frame.window_args.buf = f.buf, H2Frame.window_args.cap = f.cap,
             H2Frame.window_args.stream_id = h->stream_id, H2Frame.window_args.increment = h->length,
             H2Frame.build_window_update(NULL), H2Frame.n);
        wr(work, f.buf, n);
    }
    return PROTO_TRUE;
}

// Route one parsed frame. Every outbound control frame it produces is staged in @p f, the one borrow
// the dispatcher below takes for this frame.
static proto_bool dispatch_frame(uint8_t *restrict work, H2FrameHeader h, const uint8_t *payload, protocore_span f)
{
    switch (h.type)
    {
    case H2_SETTINGS:
        if (h.stream_id != 0)
        {
            return PROTO_FALSE; // sec 6.5: SETTINGS belongs to the connection, stream 0
        }
        if (h.flags & H2_FLAG_ACK)
        {
            return h.length == 0; // ACK of our settings
        }
        if (!(H2Frame.settings_args.payload = payload, H2Frame.settings_args.len = h.length,
              H2Frame.settings_args.s = &H2_CONN_CTX(work)->peer, H2Frame.parse_settings(NULL), H2Frame.ok))
        {
            return PROTO_FALSE;
        }
        send_control(work, build_settings_ack);
        return PROTO_TRUE;
    case H2_PING:
        if (h.stream_id != 0)
        {
            return PROTO_FALSE; // sec 6.7: PING belongs to the connection, stream 0
        }
        if (h.flags & H2_FLAG_ACK)
        {
            return PROTO_TRUE;
        }
        if (h.length != 8)
        {
            return PROTO_FALSE;
        }
        {
            size_t n = (H2Frame.ping_args.buf = f.buf, H2Frame.ping_args.cap = f.cap,
                        H2Frame.ping_args.opaque = payload, H2Frame.build_ping_ack(NULL), H2Frame.n);
            wr(work, f.buf, n);
        }
        return PROTO_TRUE;
    case H2_WINDOW_UPDATE: {
        if (h.length != 4)
        {
            return PROTO_FALSE;
        }
        uint32_t inc = rd32(payload) & 0x7FFFFFFF;
        // RFC 9113 sec 6.9: a zero increment is an error, and sec 6.9.1 caps a window at 2^31-1 - a
        // WINDOW_UPDATE that would carry it past that is a FLOW_CONTROL_ERROR. Both are connection
        // errors on the connection window and stream errors on a stream's. The cap is tested by
        // subtracting from the ceiling rather than adding to the window, so it cannot itself overflow.
        if (h.stream_id == 0)
        {
            if (inc == 0 || H2_CONN_CTX(work)->conn_send_window > (int32_t)(0x7FFFFFFFu - inc))
            {
                return PROTO_FALSE;
            }
            H2_CONN_CTX(work)->conn_send_window += (int32_t)inc;
        }
        else
        {
            // sec 5.1: a stream id past the highest one opened is idle, and WINDOW_UPDATE there is
            // a connection error. At or below it the stream has been opened and since closed,
            // where sec 6.9 allows the frame to arrive late and be ignored.
            if (h.stream_id > H2_CONN_CTX(work)->last_peer_stream)
            {
                return PROTO_FALSE;
            }
            H2Stream *s = find_stream(work, h.stream_id);
            if (s)
            {
                if (inc == 0 || s->send_window > (int32_t)(0x7FFFFFFFu - inc))
                {
                    uint32_t err = H2_FLOW_CONTROL_ERROR;
                    if (inc == 0)
                    {
                        err = H2_PROTOCOL_ERROR;
                    }
                    send_rst(work, f, h.stream_id, err);
                    return PROTO_TRUE; // the stream dies, the connection lives
                }
                s->send_window += (int32_t)inc;
            }
        }
        return PROTO_TRUE;
    }
    case H2_HEADERS:
        return handle_headers(work, &h, payload, f);
    case H2_CONTINUATION:
        return handle_continuation(work, &h, payload, f);
    case H2_DATA:
        return handle_data(work, &h, payload, f);
    case H2_RST_STREAM: {
        // sec 6.4: RST_STREAM names a stream and is exactly four octets. sec 5.1 adds that a
        // stream id past the highest one opened is idle, where only HEADERS and PRIORITY belong.
        // Each is a connection error.
        if (h.stream_id == 0 || h.stream_id > H2_CONN_CTX(work)->last_peer_stream || h.length != 4)
        {
            return PROTO_FALSE;
        }
        H2Stream *s = find_stream(work, h.stream_id);
        if (s)
        {
            s->id = 0; // free the slot
        }
        return PROTO_TRUE;
    }
    case H2_PRIORITY:
        if (h.stream_id == 0)
        {
            return PROTO_FALSE; // sec 6.3: PRIORITY on stream 0 is a connection error
        }
        if (h.length != 5)
        {
            // sec 6.3: a wrong length is a stream error, so the connection survives it.
            send_rst(work, f, h.stream_id, H2_FRAME_SIZE_ERROR);
            return PROTO_TRUE;
        }
        return PROTO_TRUE; // priority info accepted and ignored
    case H2_GOAWAY:
        if (h.stream_id != 0 || h.length < 8)
        {
            return PROTO_FALSE; // sec 6.8: stream 0, and at least the two 32-bit fields
        }
        H2_CONN_CTX(work)->phase = 2;
        return PROTO_TRUE;
    case H2_PUSH_PROMISE:
        return PROTO_FALSE; // a server never receives PUSH_PROMISE (sec 8.4)
    default:
        return PROTO_TRUE; // unknown frame types are ignored (sec 4.1)
    }
}

static proto_bool process_frame(uint8_t *restrict work)
{
    H2FrameHeader h;
    (H2Frame.parse_args.buf = H2_CONN_FHDR(work), H2Frame.parse_args.len = H2_FRAME_HEADER_LEN,
     H2Frame.parse_header(NULL), *(&h) = H2Frame.header, H2Frame.ok);
    const uint8_t *payload = H2_CONN_FBUF(work);

    // A header block must be continued only by CONTINUATION on the same stream (sec 6.10).
    if (H2_CONN_CTX(work)->in_header_block && h.type != H2_CONTINUATION)
    {
        return PROTO_FALSE;
    }

    // The one borrow for whatever this frame provokes us to send. It belongs here, at the frame's
    // owner, so no handler below stages a frame of its own.
    const size_t mark = protocore_plaintext_mark();
    protocore_span f = protocore_plaintext_span(H2_CTL_FRAME_MAX, 4);
    if (!span.ok(f))
    {
        protocore_plaintext_release(mark);
        return PROTO_FALSE; // arena exhausted: fail closed
    }
    const proto_bool ok = dispatch_frame(work, h, payload, f);
    protocore_plaintext_release(mark);
    return ok;
}

static void h2_conn_init(uint8_t *restrict work)
{
    mem.set(H2_CONN_CTX(work), 0, sizeof(H2ConnCtx));
    H2_CONN_CTX(work)->cb = *H2Conn.init_args.cb;
    H2_CONN_CTX(work)->phase = 0;
    (H2Frame.settings_args.s = &H2_CONN_CTX(work)->peer, H2Frame.settings_defaults(NULL));
    H2_CONN_CTX(work)->conn_send_window = 65535;
    (Hpack.init_args.max_bytes = PROTOCORE_HPACK_TABLE_BYTES, Hpack.dyn_init(H2_CONN_CTX(work)->hdec));
    send_our_settings(work);
}

static proto_bool h2_recv_run(uint8_t *restrict work, const uint8_t *data, size_t len)
{
    size_t off = 0;
    if (H2_CONN_CTX(work)->phase == 0)
    {
        while (off < len && H2_CONN_CTX(work)->pre < H2_PREFACE_LEN)
        {
            if (data[off] != (uint8_t)H2_PREFACE[H2_CONN_CTX(work)->pre])
            {
                return PROTO_FALSE; // malformed preface
            }
            H2_CONN_CTX(work)->pre++;
            off++;
        }
        if (H2_CONN_CTX(work)->pre < H2_PREFACE_LEN)
        {
            return PROTO_TRUE; // preface still incomplete
        }
        H2_CONN_CTX(work)->phase = 1;
    }
    if (H2_CONN_CTX(work)->phase == 2)
    {
        return PROTO_TRUE; // closing; ignore further input
    }

    while (off < len)
    {
        if (H2_CONN_CTX(work)->fhave < H2_FRAME_HEADER_LEN)
        {
            size_t take = H2_FRAME_HEADER_LEN - H2_CONN_CTX(work)->fhave;
            if (take > len - off)
            {
                take = len - off;
            }
            mem.cpy(H2_CONN_FHDR(work) + H2_CONN_CTX(work)->fhave, data + off, take);
            H2_CONN_CTX(work)->fhave += take;
            off += take;
            if (H2_CONN_CTX(work)->fhave < H2_FRAME_HEADER_LEN)
            {
                return PROTO_TRUE;
            }
        }
        uint32_t plen =
            ((uint32_t)H2_CONN_FHDR(work)[0] << 16) | ((uint32_t)H2_CONN_FHDR(work)[1] << 8) | H2_CONN_FHDR(work)[2];
        if (plen > PROTOCORE_H2_MAX_FRAME)
        {
            return PROTO_FALSE; // FRAME_SIZE_ERROR
        }
        size_t total = H2_FRAME_HEADER_LEN + plen;
        size_t take = total - H2_CONN_CTX(work)->fhave;
        if (take > len - off)
        {
            take = len - off;
        }
        mem.cpy(H2_CONN_FBUF(work) + (H2_CONN_CTX(work)->fhave - H2_FRAME_HEADER_LEN), data + off, take);
        H2_CONN_CTX(work)->fhave += take;
        off += take;
        if (H2_CONN_CTX(work)->fhave < total)
        {
            return PROTO_TRUE; // frame incomplete
        }
        if (!process_frame(work))
        {
            return PROTO_FALSE;
        }
        H2_CONN_CTX(work)->fhave = 0;
    }
    return PROTO_TRUE;
}

static proto_bool h2_respond_run(uint8_t *restrict work, uint32_t stream_id, int status, const char *content_type,
                                 const char *body, size_t body_len)
{
    H2Stream *s = find_stream(work, stream_id);
    if (!s)
    {
        return PROTO_FALSE;
    }

    // Build the HPACK header block: :status, optional content-type, content-length.
    uint8_t block[256];
    size_t bo = 0;
    char num[16];
    protocore_sb sb_num = {num, sizeof num, 0, PROTO_TRUE};
    Sb.i64(&sb_num, (int64_t)(status));
    int nl = (int)Sb.finish(&sb_num);
    size_t w = (Hpack.encode_args.out = block + bo, Hpack.encode_args.cap = sizeof block - bo,
                Hpack.encode_args.name = ":status", Hpack.encode_args.name_len = 7, Hpack.encode_args.value = num,
                Hpack.encode_args.value_len = (size_t)nl, Hpack.encode_header(NULL), Hpack.n);
    if (!w)
    {
        return PROTO_FALSE;
    }
    bo += w;
    if (content_type)
    {
        // Cap above the largest content-type that can fit this block even at HPACK-Huffman's best
        // 5-bit/char (~sizeof block * 8/5): a longer value can never fit, so measuring it as `2*block`
        // still trips the encode's reject below instead of being truncated into a fittable length.
        w = (Hpack.encode_args.out = block + bo, Hpack.encode_args.cap = sizeof block - bo,
             Hpack.encode_args.name = "content-type", Hpack.encode_args.name_len = 12,
             Hpack.encode_args.value = content_type,
             Hpack.encode_args.value_len = str.len(content_type, sizeof block * 2), Hpack.encode_header(NULL), Hpack.n);
        if (!w)
        {
            return PROTO_FALSE;
        }
        bo += w;
    }
    protocore_sb sb_num2 = {num, sizeof num, 0, PROTO_TRUE};
    Sb.u32(&sb_num2, (uint32_t)((unsigned)body_len));
    int cl = (int)Sb.finish(&sb_num2);
    w = (Hpack.encode_args.out = block + bo, Hpack.encode_args.cap = sizeof block - bo,
         Hpack.encode_args.name = "content-length", Hpack.encode_args.name_len = 14, Hpack.encode_args.value = num,
         Hpack.encode_args.value_len = (size_t)cl, Hpack.encode_header(NULL), Hpack.n);
    // Reachable: bo has already been advanced by the caller-supplied content-type, which can consume nearly
    // the whole block, leaving too little room for content-length even though it is only a few octets.
    if (!w)
    {
        return PROTO_FALSE;
    }
    bo += w;

    uint8_t frame[H2_FRAME_HEADER_LEN + sizeof block];
    size_t n = (H2Frame.headers_args.buf = frame, H2Frame.headers_args.cap = sizeof frame,
                H2Frame.headers_args.stream_id = stream_id, H2Frame.headers_args.block = block,
                H2Frame.headers_args.block_len = bo, H2Frame.headers_args.end_stream = body_len == 0,
                H2Frame.build_headers(NULL), H2Frame.n);
    if (!n)
    {
        return PROTO_FALSE;
    }
    wr(work, frame, n);

    // Body as DATA frames, split to the peer's max frame size, END_STREAM on the last.
    size_t sent = 0;
    uint32_t chunk_max = H2_CONN_CTX(work)->peer.max_frame_size ? H2_CONN_CTX(work)->peer.max_frame_size : 16384;
    while (sent < body_len)
    {
        size_t chunk = body_len - sent;
        if (chunk > chunk_max)
        {
            chunk = chunk_max;
        }
        proto_bool last = (sent + chunk == body_len);
        uint8_t dh[H2_FRAME_HEADER_LEN];
        size_t hn = (H2Frame.write_args.buf = dh, H2Frame.write_args.cap = sizeof dh,
                     H2Frame.write_args.length = (uint32_t)chunk, H2Frame.write_args.type = H2_DATA,
                     H2Frame.write_args.flags = last ? H2_FLAG_END_STREAM : 0, H2Frame.write_args.stream_id = stream_id,
                     H2Frame.write_header(NULL), H2Frame.n);
        if (!hn)
        {
            return PROTO_FALSE;
        }
        wr(work, dh, hn);
        wr(work, (const uint8_t *)(body + sent), chunk);
        H2_CONN_CTX(work)->conn_send_window -= (int32_t)chunk;
        s->send_window -= (int32_t)chunk;
        sent += chunk;
    }
    s->id = 0; // stream complete; free the slot
    return PROTO_TRUE;
}

static void h2_goaway_run(uint8_t *restrict work, uint32_t error)
{
    uint8_t buf[H2_FRAME_HEADER_LEN + 8];
    size_t n = (H2Frame.goaway_args.buf = buf, H2Frame.goaway_args.cap = sizeof buf,
                H2Frame.goaway_args.last_stream_id = H2_CONN_CTX(work)->last_peer_stream,
                H2Frame.goaway_args.error = error, H2Frame.build_goaway(NULL), H2Frame.n);
    wr(work, buf, n);
    H2_CONN_CTX(work)->phase = 2;
}

// --- the entries ---

static void h2_conn_recv(uint8_t *restrict work)
{
    H2Conn.ok = h2_recv_run(work, H2Conn.recv_args.data, H2Conn.recv_args.len);
}

static void h2_conn_respond(uint8_t *restrict work)
{
    H2Conn.ok =
        h2_respond_run(work, H2Conn.respond_args.stream_id, H2Conn.respond_args.status,
                       H2Conn.respond_args.content_type, H2Conn.respond_args.body, H2Conn.respond_args.body_len);
}

static void h2_conn_goaway(uint8_t *restrict work)
{
    h2_goaway_run(work, H2Conn.goaway_args.error);
    H2Conn.ok = PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
H2ConnNs H2Conn = {.init = h2_conn_init, .recv = h2_conn_recv, .respond = h2_conn_respond, .goaway = h2_conn_goaway};

#endif // PROTOCORE_ENABLE_HTTP2
