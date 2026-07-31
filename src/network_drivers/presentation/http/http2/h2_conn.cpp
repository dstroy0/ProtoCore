// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_h2_conn.cpp
 * @brief HTTP/2 connection + stream engine - implementation. See pc_h2_conn.h.
 */

#include "network_drivers/presentation/http/http2/h2_conn.h"
#include "shared_primitives/strbuf.h" // pc_sb frame builder

#if PC_ENABLE_HTTP2

#include <stdio.h>
#include <string.h>

namespace
{
uint32_t rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

void wr(H2Conn *c, const uint8_t *data, size_t len)
{
    if (c->cb.write)
    {
        c->cb.write(c->cb.io, data, len);
    }
}

H2Stream *find_stream(H2Conn *c, uint32_t id)
{
    for (int i = 0; i < PC_H2_MAX_STREAMS; i++)
    {
        if (c->streams[i].id == id && id != 0)
        {
            return &c->streams[i];
        }
    }
    return nullptr;
}

H2Stream *alloc_stream(H2Conn *c, uint32_t id)
{
    for (int i = 0; i < PC_H2_MAX_STREAMS; i++)
    {
        if (c->streams[i].id == 0)
        {
            c->streams[i].id = id;
            c->streams[i].state = H2StreamState::H2_ST_OPEN;
            c->streams[i].send_window = (int32_t)c->peer.initial_window_size;
            return &c->streams[i];
        }
    }
    return nullptr; // at MAX_CONCURRENT_STREAMS
}

void send_our_settings(H2Conn *c)
{
    static const uint16_t ids[4] = {H2Setting::H2_SETTINGS_ENABLE_PUSH, H2Setting::H2_SETTINGS_MAX_CONCURRENT_STREAMS,
                                    H2Setting::H2_SETTINGS_INITIAL_WINDOW_SIZE, H2Setting::H2_SETTINGS_MAX_FRAME_SIZE};
    static const uint32_t vals[4] = {0, PC_H2_MAX_STREAMS, 65535, PC_H2_MAX_FRAME};
    uint8_t buf[H2_FRAME_HEADER_LEN + 4 * 6];
    size_t n = pc_h2_build_settings(buf, sizeof buf, ids, vals, 4);
    wr(c, buf, n);
}

void send_control(H2Conn *c, size_t (*build)(uint8_t *, size_t))
{
    uint8_t buf[H2_FRAME_HEADER_LEN + 16];
    size_t n = build(buf, sizeof buf);
    // Both builders passed here (SETTINGS ACK, 9 bytes; RST_STREAM, 13) fit this 25-byte buffer, so the
    // zero return is unreachable; kept so a future builder that can fail is not silently written short.
    if (n) // GCOVR_EXCL_LINE
    {
        wr(c, buf, n);
    }
}

// HPACK emit context: routes each decoded header to the app callback with the stream id.
struct EmitCtx
{
    H2Conn *c;
    uint32_t stream_id;
};
bool emit_header(void *ctx, const char *name, size_t nl, const char *val, size_t vl)
{
    EmitCtx *e = (EmitCtx *)ctx;
    if (e->c->cb.on_header)
    {
        e->c->cb.on_header(e->c->cb.app, e->stream_id, name, nl, val, vl);
    }
    return true;
}

// Decode a complete request header block and deliver it to the application.
bool decode_block(H2Conn *c, uint32_t stream_id, const uint8_t *block, size_t len, bool end_stream)
{
    EmitCtx e = {c, stream_id};
    if (!pc_hpack_decode(&c->hdec, block, len, c->hscratch, sizeof c->hscratch, emit_header, &e))
    {
        return false; // COMPRESSION_ERROR
    }
    if (c->cb.on_headers_end)
    {
        c->cb.on_headers_end(c->cb.app, stream_id, end_stream);
    }
    H2Stream *s = find_stream(c, stream_id);
    if (s)
    {
        s->state = end_stream ? H2StreamState::H2_ST_HALF_CLOSED : H2StreamState::H2_ST_OPEN;
    }
    return true;
}

bool handle_headers(H2Conn *c, const H2FrameHeader *h, const uint8_t *payload)
{
    if (h->stream_id == 0 || (h->stream_id & 1) == 0)
    {
        return false; // requests are client-initiated odd stream ids (RFC 9113 sec 5.1.1)
    }
    const uint8_t *p = payload;
    size_t plen = h->length;
    uint8_t pad = 0;
    if (h->flags & H2_FLAG_PADDED)
    {
        if (plen < 1)
        {
            return false;
        }
        pad = p[0];
        p++;
        plen--;
    }
    if (h->flags & H2_FLAG_PRIORITY)
    {
        if (plen < 5)
        {
            return false;
        }
        p += 5;
        plen -= 5; // priority info accepted and ignored
    }
    if (pad > plen)
    {
        return false;
    }
    plen -= pad; // strip trailing padding

    bool end_stream = (h->flags & H2_FLAG_END_STREAM) != 0;
    if (h->stream_id <= c->last_peer_stream)
    {
        return false; // stream ids must increase
    }
    c->last_peer_stream = h->stream_id;
    if (!alloc_stream(c, h->stream_id))
    {
        send_control(c, [](uint8_t *b, size_t cap) { return pc_h2_build_rst_stream(b, cap, 0, 0); });
        return true; // refuse quietly is fine; keep the connection
    }

    if (h->flags & H2_FLAG_END_HEADERS)
    {
        return decode_block(c, h->stream_id, p, plen, end_stream);
    }
    // Spans CONTINUATION frames: buffer the fragment.
    if (plen > sizeof c->hblock)
    {
        return false;
    }
    memcpy(c->hblock, p, plen);
    c->hblock_len = plen;
    c->hblock_stream = h->stream_id;
    c->hblock_end_stream = end_stream;
    c->in_header_block = true;
    return true;
}

bool handle_continuation(H2Conn *c, const H2FrameHeader *h, const uint8_t *payload)
{
    if (!c->in_header_block || h->stream_id != c->hblock_stream)
    {
        return false;
    }
    if (c->hblock_len + h->length > sizeof c->hblock)
    {
        return false;
    }
    memcpy(c->hblock + c->hblock_len, payload, h->length);
    c->hblock_len += h->length;
    if (h->flags & H2_FLAG_END_HEADERS)
    {
        c->in_header_block = false;
        return decode_block(c, c->hblock_stream, c->hblock, c->hblock_len, c->hblock_end_stream);
    }
    return true;
}

bool handle_data(H2Conn *c, const H2FrameHeader *h, const uint8_t *payload)
{
    if (h->stream_id == 0)
    {
        return false;
    }
    const uint8_t *p = payload;
    size_t plen = h->length;
    uint8_t pad = 0;
    if (h->flags & H2_FLAG_PADDED)
    {
        if (plen < 1)
        {
            return false;
        }
        pad = p[0];
        p++;
        plen--;
    }
    if (pad > plen)
    {
        return false;
    }
    plen -= pad;

    bool end_stream = (h->flags & H2_FLAG_END_STREAM) != 0;
    if (c->cb.on_data)
    {
        c->cb.on_data(c->cb.app, h->stream_id, p, plen, end_stream);
    }
    H2Stream *s = find_stream(c, h->stream_id);
    if (s && end_stream)
    {
        s->state = H2StreamState::H2_ST_HALF_CLOSED;
    }
    // Replenish flow-control windows for the bytes we consumed (whole frame length).
    if (h->length > 0)
    {
        uint8_t wu[H2_FRAME_HEADER_LEN + 4];
        size_t n = pc_h2_build_window_update(wu, sizeof wu, 0, h->length);
        wr(c, wu, n);
        n = pc_h2_build_window_update(wu, sizeof wu, h->stream_id, h->length);
        wr(c, wu, n);
    }
    return true;
}

bool process_frame(H2Conn *c)
{
    H2FrameHeader h;
    pc_h2_parse_header(c->fbuf, H2_FRAME_HEADER_LEN, &h);
    const uint8_t *payload = c->fbuf + H2_FRAME_HEADER_LEN;

    // A header block must be continued only by CONTINUATION on the same stream (sec 6.10).
    if (c->in_header_block && h.type != H2FrameType::H2_CONTINUATION)
    {
        return false;
    }

    switch (h.type)
    {
    case H2FrameType::H2_SETTINGS:
        if (h.flags & H2_FLAG_ACK)
        {
            return h.length == 0; // ACK of our settings
        }
        if (!pc_h2_parse_settings(payload, h.length, &c->peer))
        {
            return false;
        }
        send_control(c, pc_h2_build_settings_ack);
        return true;
    case H2FrameType::H2_PING:
        if (h.flags & H2_FLAG_ACK)
        {
            return true;
        }
        if (h.length != 8)
        {
            return false;
        }
        {
            uint8_t pg[H2_FRAME_HEADER_LEN + 8];
            size_t n = pc_h2_build_ping_ack(pg, sizeof pg, payload);
            wr(c, pg, n);
        }
        return true;
    case H2FrameType::H2_WINDOW_UPDATE: {
        if (h.length != 4)
        {
            return false;
        }
        uint32_t inc = rd32(payload) & 0x7FFFFFFF;
        if (h.stream_id == 0)
        {
            c->conn_send_window += (int32_t)inc;
        }
        else
        {
            H2Stream *s = find_stream(c, h.stream_id);
            if (s)
            {
                s->send_window += (int32_t)inc;
            }
        }
        return true;
    }
    case H2FrameType::H2_HEADERS:
        return handle_headers(c, &h, payload);
    case H2FrameType::H2_CONTINUATION:
        return handle_continuation(c, &h, payload);
    case H2FrameType::H2_DATA:
        return handle_data(c, &h, payload);
    case H2FrameType::H2_RST_STREAM: {
        H2Stream *s = find_stream(c, h.stream_id);
        if (s)
        {
            s->id = 0; // free the slot
        }
        return true;
    }
    case H2FrameType::H2_PRIORITY:
        return true; // accepted, ignored
    case H2FrameType::H2_GOAWAY:
        c->phase = 2;
        return true;
    case H2FrameType::H2_PUSH_PROMISE:
        return false; // a server never receives PUSH_PROMISE (sec 8.4)
    default:
        return true; // unknown frame types are ignored (sec 4.1)
    }
}
} // namespace

void pc_h2_conn_init(H2Conn *c, const H2Callbacks *cb)
{
    memset(c, 0, sizeof(*c));
    c->cb = *cb;
    c->phase = 0;
    pc_h2_settings_defaults(&c->peer);
    c->conn_send_window = 65535;
    pc_hpack_dyn_init(&c->hdec, PC_HPACK_TABLE_BYTES);
    send_our_settings(c);
}

bool pc_h2_conn_recv(H2Conn *c, const uint8_t *data, size_t len)
{
    size_t off = 0;
    if (c->phase == 0)
    {
        while (off < len && c->pre < H2_PREFACE_LEN)
        {
            if (data[off] != (uint8_t)H2_PREFACE[c->pre])
            {
                return false; // malformed preface
            }
            c->pre++;
            off++;
        }
        if (c->pre < H2_PREFACE_LEN)
        {
            return true; // preface still incomplete
        }
        c->phase = 1;
    }
    if (c->phase == 2)
    {
        return true; // closing; ignore further input
    }

    while (off < len)
    {
        if (c->fhave < H2_FRAME_HEADER_LEN)
        {
            size_t take = H2_FRAME_HEADER_LEN - c->fhave;
            if (take > len - off)
            {
                take = len - off;
            }
            memcpy(c->fbuf + c->fhave, data + off, take);
            c->fhave += take;
            off += take;
            if (c->fhave < H2_FRAME_HEADER_LEN)
            {
                return true;
            }
        }
        uint32_t plen = ((uint32_t)c->fbuf[0] << 16) | ((uint32_t)c->fbuf[1] << 8) | c->fbuf[2];
        if (plen > PC_H2_MAX_FRAME)
        {
            return false; // FRAME_SIZE_ERROR
        }
        size_t total = H2_FRAME_HEADER_LEN + plen;
        size_t take = total - c->fhave;
        if (take > len - off)
        {
            take = len - off;
        }
        memcpy(c->fbuf + c->fhave, data + off, take);
        c->fhave += take;
        off += take;
        if (c->fhave < total)
        {
            return true; // frame incomplete
        }
        if (!process_frame(c))
        {
            return false;
        }
        c->fhave = 0;
    }
    return true;
}

bool pc_h2_conn_respond(H2Conn *c, uint32_t stream_id, int status, const char *content_type, const char *body,
                        size_t body_len)
{
    H2Stream *s = find_stream(c, stream_id);
    if (!s)
    {
        return false;
    }

    // Build the HPACK header block: :status, optional content-type, content-length.
    uint8_t block[256];
    size_t bo = 0;
    char num[16];
    pc_sb sb_num = {num, sizeof num, 0, true};
    pc_sb_i64(&sb_num, (int64_t)(status));
    int nl = (int)pc_sb_finish(&sb_num);
    size_t w = pc_hpack_encode_header(block + bo, sizeof block - bo, ":status", 7, num, (size_t)nl);
    // GCOVR_EXCL_START  :status is a decimal int (<=11 chars) into a fresh 256B block; the encode cannot overflow
    if (!w)
    {
        return false;
    }
    // GCOVR_EXCL_STOP
    bo += w;
    if (content_type)
    {
        // Cap above the largest content-type that can fit this block even at HPACK-Huffman's best
        // 5-bit/char (~sizeof block * 8/5): a longer value can never fit, so measuring it as `2*block`
        // still trips the encode's reject below instead of being truncated into a fittable length.
        w = pc_hpack_encode_header(block + bo, sizeof block - bo, "content-type", 12, content_type,
                                   strnlen(content_type, sizeof block * 2));
        if (!w)
        {
            return false;
        }
        bo += w;
    }
    pc_sb sb_num2 = {num, sizeof num, 0, true};
    pc_sb_u32(&sb_num2, (uint32_t)((unsigned)body_len));
    int cl = (int)pc_sb_finish(&sb_num2);
    w = pc_hpack_encode_header(block + bo, sizeof block - bo, "content-length", 14, num, (size_t)cl);
    // Reachable: bo has already been advanced by the caller-supplied content-type, which can consume nearly
    // the whole block, leaving too little room for content-length even though it is only a few octets.
    if (!w)
    {
        return false;
    }
    bo += w;

    uint8_t frame[H2_FRAME_HEADER_LEN + sizeof block];
    size_t n = pc_h2_build_headers(frame, sizeof frame, stream_id, block, bo, body_len == 0);
    // GCOVR_EXCL_START  frame is H2_FRAME_HEADER_LEN + sizeof block; 9 + bo (bo <= 256) always fits
    if (!n)
    {
        return false;
    }
    // GCOVR_EXCL_STOP
    wr(c, frame, n);

    // Body as DATA frames, split to the peer's max frame size, END_STREAM on the last.
    size_t sent = 0;
    uint32_t chunk_max = c->peer.max_frame_size ? c->peer.max_frame_size : 16384;
    while (sent < body_len)
    {
        size_t chunk = body_len - sent;
        if (chunk > chunk_max)
        {
            chunk = chunk_max;
        }
        bool last = (sent + chunk == body_len);
        uint8_t dh[H2_FRAME_HEADER_LEN];
        size_t hn = pc_h2_write_header(dh, sizeof dh, (uint32_t)chunk, H2FrameType::H2_DATA,
                                       last ? H2_FLAG_END_STREAM : 0, stream_id);
        // GCOVR_EXCL_START  dh is exactly H2_FRAME_HEADER_LEN; a 9-byte frame header always fits
        if (!hn)
        {
            return false;
        }
        // GCOVR_EXCL_STOP
        wr(c, dh, hn);
        wr(c, (const uint8_t *)(body + sent), chunk);
        c->conn_send_window -= (int32_t)chunk;
        s->send_window -= (int32_t)chunk;
        sent += chunk;
    }
    s->id = 0; // stream complete; free the slot
    return true;
}

void pc_h2_conn_goaway(H2Conn *c, uint32_t error)
{
    uint8_t buf[H2_FRAME_HEADER_LEN + 8];
    size_t n = pc_h2_build_goaway(buf, sizeof buf, c->last_peer_stream, error);
    wr(c, buf, n);
    c->phase = 2;
}

#endif // PC_ENABLE_HTTP2
