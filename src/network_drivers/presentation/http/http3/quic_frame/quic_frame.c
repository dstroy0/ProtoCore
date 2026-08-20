// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_quic_frame.c
 * @brief QUIC frame parsing and building - implementation. See protocore_quic_frame.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HTTP3

static uint8_t quic_varint_work[16]; // the borrow an entry takes; QuicVarint never reads it

#include "mmgr/protomem/protomem.h"
#include "network_drivers/presentation/http/http3/quic_frame/quic_frame.h"

#include "network_drivers/presentation/http/http3/quic_varint/quic_varint.h"

PROTOCORE_BEGIN_DECLS

// Decode a varint at buf[*pos], advancing *pos. Returns false on truncation.
static proto_bool rd(const uint8_t *buf, size_t len, size_t *pos, uint64_t *v)
{
    size_t c = 0;
    QuicVarint.decode_args.in = buf + *pos;
    QuicVarint.decode_args.len = len - *pos;
    QuicVarint.decode_args.value = v;
    QuicVarint.decode_args.consumed = &c;
    QuicVarint.decode(quic_varint_work);
    if (!QuicVarint.ok)
    {
        return PROTO_FALSE;
    }
    *pos += c;
    return PROTO_TRUE;
}

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void quic_frame_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = QuicFrame.parse_args.buf;
    size_t len = QuicFrame.parse_args.len;
    QuicFrameHeader *out = QuicFrame.parse_args.out;

    size_t pos = 0;
    uint64_t type = 0;
    if (!rd(buf, len, &pos, &type))
    {
        QuicFrame.n = 0;
        return;
    }
    out->type = type;

    if (type == QUIC_FT_PADDING || type == QUIC_FT_PING || type == QUIC_FT_HANDSHAKE_DONE)
    {
        QuicFrame.n = pos;
        return;
    }

    if (type == QUIC_FT_ACK || type == QUIC_FT_ACK_ECN)
    {
        if (!rd(buf, len, &pos, &out->ack.largest) || !rd(buf, len, &pos, &out->ack.delay) ||
            !rd(buf, len, &pos, &out->ack.range_count) || !rd(buf, len, &pos, &out->ack.first_range))
        {
            QuicFrame.n = 0;
            return;
        }
        for (uint64_t i = 0; i < out->ack.range_count; i++) // skip Gap + ACK Range Length pairs
        {
            uint64_t tmp = 0;
            if (!rd(buf, len, &pos, &tmp) || !rd(buf, len, &pos, &tmp))
            {
                QuicFrame.n = 0;
                return;
            }
        }
        if (type == QUIC_FT_ACK_ECN) // skip the three ECN counts
        {
            uint64_t tmp = 0;
            if (!rd(buf, len, &pos, &tmp) || !rd(buf, len, &pos, &tmp) || !rd(buf, len, &pos, &tmp))
            {
                QuicFrame.n = 0;
                return;
            }
        }
        QuicFrame.n = pos;
        return;
    }

    if (type == QUIC_FT_CRYPTO)
    {
        if (!rd(buf, len, &pos, &out->crypto.offset) || !rd(buf, len, &pos, &out->crypto.length))
        {
            QuicFrame.n = 0;
            return;
        }
        if (pos + out->crypto.length > len)
        {
            QuicFrame.n = 0;
            return;
        }
        out->crypto.data = buf + pos;
        pos += out->crypto.length;
        QuicFrame.n = pos;
        return;
    }

    if (type >= QUIC_FT_STREAM && type <= 0x0f)
    {
        if (!rd(buf, len, &pos, &out->stream.id))
        {
            QuicFrame.n = 0;
            return;
        }
        out->stream.offset = 0;
        if (type & QUIC_STREAM_OFF)
        {
            if (!rd(buf, len, &pos, &out->stream.offset))
            {
                QuicFrame.n = 0;
                return;
            }
        }
        if (type & QUIC_STREAM_LEN)
        {
            if (!rd(buf, len, &pos, &out->stream.length))
            {
                QuicFrame.n = 0;
                return;
            }
        }
        else
        {
            out->stream.length = len - pos; // absent Length -> Stream Data runs to the packet end
        }
        if (pos + out->stream.length > len)
        {
            QuicFrame.n = 0;
            return;
        }
        out->stream.data = buf + pos;
        out->stream.fin = (uint8_t)((type & QUIC_STREAM_FIN) ? 1 : 0);
        pos += out->stream.length;
        QuicFrame.n = pos;
        return;
    }

    if (type == QUIC_FT_MAX_DATA)
    {
        if (!rd(buf, len, &pos, &out->max_data.max))
        {
            QuicFrame.n = 0;
            return;
        }
        QuicFrame.n = pos;
        return;
    }

    if (type == QUIC_FT_CONNECTION_CLOSE || type == QUIC_FT_CONNECTION_CLOSE_APP)
    {
        out->close.app = (uint8_t)((type == QUIC_FT_CONNECTION_CLOSE_APP) ? 1 : 0);
        out->close.frame_type = 0;
        if (!rd(buf, len, &pos, &out->close.error_code))
        {
            QuicFrame.n = 0;
            return;
        }
        if (type == QUIC_FT_CONNECTION_CLOSE) // the transport variant carries the triggering frame type
        {
            if (!rd(buf, len, &pos, &out->close.frame_type))
            {
                QuicFrame.n = 0;
                return;
            }
        }
        if (!rd(buf, len, &pos, &out->close.reason_len))
        {
            QuicFrame.n = 0;
            return;
        }
        if (pos + out->close.reason_len > len)
        {
            QuicFrame.n = 0;
            return;
        }
        out->close.reason = buf + pos;
        pos += out->close.reason_len;
        QuicFrame.n = pos;
        return;
    }

    // Frames the server does not act on but MUST still parse so a well-formed frame from a real client is
    // not rejected as FRAME_ENCODING_ERROR (RFC 9000 sec 12.4: parse the whole grammar even to ignore it).
    // A real client sends these right after the handshake alongside the first request (MAX_STREAMS,
    // NEW_CONNECTION_ID, MAX_STREAM_DATA, ...). Consume each frame's fields by its wire shape; the
    // dispatcher then ignores the type. out->type is already set above.
    if (type == QUIC_FT_MAX_STREAMS_BIDI || type == QUIC_FT_MAX_STREAMS_UNI || type == QUIC_FT_DATA_BLOCKED ||
        type == QUIC_FT_STREAMS_BLOCKED_BIDI || type == QUIC_FT_STREAMS_BLOCKED_UNI ||
        type == QUIC_FT_RETIRE_CONNECTION_ID)
    {
        uint64_t v = 0; // one varint
        if (!rd(buf, len, &pos, &v))
        {
            QuicFrame.n = 0;
            return;
        }
        QuicFrame.n = pos;
        return;
    }
    if (type == QUIC_FT_STOP_SENDING || type == QUIC_FT_MAX_STREAM_DATA || type == QUIC_FT_STREAM_DATA_BLOCKED)
    {
        uint64_t v = 0; // two varints
        if (!rd(buf, len, &pos, &v) || !rd(buf, len, &pos, &v))
        {
            QuicFrame.n = 0;
            return;
        }
        QuicFrame.n = pos;
        return;
    }
    if (type == QUIC_FT_RESET_STREAM)
    {
        uint64_t v = 0; // stream id, app error code, final size
        if (!rd(buf, len, &pos, &v) || !rd(buf, len, &pos, &v) || !rd(buf, len, &pos, &v))
        {
            QuicFrame.n = 0;
            return;
        }
        QuicFrame.n = pos;
        return;
    }
    if (type == QUIC_FT_NEW_TOKEN)
    {
        uint64_t tlen = 0; // token length + token bytes
        if (!rd(buf, len, &pos, &tlen) || pos + tlen > len)
        {
            QuicFrame.n = 0;
            return;
        }
        pos += tlen;
        QuicFrame.n = pos;
        return;
    }
    if (type == QUIC_FT_NEW_CONNECTION_ID)
    {
        uint64_t seq = 0;    // sequence number
        uint64_t retire = 0; // retire-prior-to, then a 1-byte CID length follows
        if (!rd(buf, len, &pos, &seq) || !rd(buf, len, &pos, &retire) || pos >= len)
        {
            QuicFrame.n = 0;
            return;
        }
        uint8_t cidlen = buf[pos++];
        if (pos + (size_t)cidlen + 16 > len) // connection id + 16-byte stateless reset token
        {
            QuicFrame.n = 0;
            return;
        }
        pos += (size_t)cidlen + 16;
        QuicFrame.n = pos;
        return;
    }
    if (type == QUIC_FT_PATH_CHALLENGE || type == QUIC_FT_PATH_RESPONSE)
    {
        if (pos + 8 > len) // 8 bytes of opaque data
        {
            QuicFrame.n = 0;
            return;
        }
        pos += 8;
        QuicFrame.n = pos;
        return;
    }

    QuicFrame.n = 0; // a genuinely unknown / reserved frame type
}

static void quic_frame_build_padding(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = QuicFrame.build_padding_args.out;
    size_t cap = QuicFrame.build_padding_args.cap;
    size_t n = QuicFrame.build_padding_args.n;

    if (n > cap)
    {
        QuicFrame.n = 0;
        return;
    }
    mem.set(out, 0, n);
    QuicFrame.n = n;
}

static void quic_frame_build_ping(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = QuicFrame.build_ping_args.out;
    size_t cap = QuicFrame.build_ping_args.cap;

    if (cap < 1)
    {
        QuicFrame.n = 0;
        return;
    }
    out[0] = QUIC_FT_PING;
    QuicFrame.n = 1;
}

static void quic_frame_build_handshake_done(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = QuicFrame.build_handshake_done_args.out;
    size_t cap = QuicFrame.build_handshake_done_args.cap;

    if (cap < 1)
    {
        QuicFrame.n = 0;
        return;
    }
    out[0] = QUIC_FT_HANDSHAKE_DONE;
    QuicFrame.n = 1;
}

// Append a varint; returns false on overflow.
static proto_bool wr(uint8_t *out, size_t cap, size_t *pos, uint64_t v)
{
    QuicVarint.encode_args.out = out + *pos;
    QuicVarint.encode_args.cap = cap - *pos;
    QuicVarint.encode_args.value = v;
    QuicVarint.encode(quic_varint_work);
    size_t c = QuicVarint.n;
    if (!c)
    {
        return PROTO_FALSE;
    }
    *pos += c;
    return PROTO_TRUE;
}

static void quic_frame_build_ack(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = QuicFrame.build_ack_args.out;
    size_t cap = QuicFrame.build_ack_args.cap;
    uint64_t largest = QuicFrame.build_ack_args.largest;
    uint64_t delay = QuicFrame.build_ack_args.delay;
    uint64_t first_range = QuicFrame.build_ack_args.first_range;

    size_t pos = 0;
    if (!wr(out, cap, &pos, QUIC_FT_ACK) || !wr(out, cap, &pos, largest) || !wr(out, cap, &pos, delay) ||
        !wr(out, cap, &pos, 0) /* ACK Range Count */ || !wr(out, cap, &pos, first_range))
    {
        QuicFrame.n = 0;
        return;
    }
    QuicFrame.n = pos;
}

static void quic_frame_build_crypto(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = QuicFrame.build_crypto_args.out;
    size_t cap = QuicFrame.build_crypto_args.cap;
    uint64_t offset = QuicFrame.build_crypto_args.offset;
    const uint8_t *data = QuicFrame.build_crypto_args.data;
    size_t len = QuicFrame.build_crypto_args.len;

    size_t pos = 0;
    if (!wr(out, cap, &pos, QUIC_FT_CRYPTO) || !wr(out, cap, &pos, offset) || !wr(out, cap, &pos, len))
    {
        QuicFrame.n = 0;
        return;
    }
    if (pos + len > cap)
    {
        QuicFrame.n = 0;
        return;
    }
    if (len)
    {
        mem.cpy(out + pos, data, len);
    }
    QuicFrame.n = pos + len;
}

static void quic_frame_build_stream(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = QuicFrame.build_stream_args.out;
    size_t cap = QuicFrame.build_stream_args.cap;
    uint64_t id = QuicFrame.build_stream_args.id;
    uint64_t offset = QuicFrame.build_stream_args.offset;
    const uint8_t *data = QuicFrame.build_stream_args.data;
    size_t len = QuicFrame.build_stream_args.len;
    proto_bool fin = QuicFrame.build_stream_args.fin;

    uint64_t type = QUIC_FT_STREAM | QUIC_STREAM_LEN | (offset ? QUIC_STREAM_OFF : 0) | (fin ? QUIC_STREAM_FIN : 0);
    size_t pos = 0;
    if (!wr(out, cap, &pos, type) || !wr(out, cap, &pos, id))
    {
        QuicFrame.n = 0;
        return;
    }
    if (offset && !wr(out, cap, &pos, offset))
    {
        QuicFrame.n = 0;
        return;
    }
    if (!wr(out, cap, &pos, len))
    {
        QuicFrame.n = 0;
        return;
    }
    if (pos + len > cap)
    {
        QuicFrame.n = 0;
        return;
    }
    if (len)
    {
        mem.cpy(out + pos, data, len);
    }
    QuicFrame.n = pos + len;
}

static void quic_frame_build_max_data(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = QuicFrame.build_max_data_args.out;
    size_t cap = QuicFrame.build_max_data_args.cap;
    uint64_t max = QuicFrame.build_max_data_args.max;

    size_t pos = 0;
    if (!wr(out, cap, &pos, QUIC_FT_MAX_DATA) || !wr(out, cap, &pos, max))
    {
        QuicFrame.n = 0;
        return;
    }
    QuicFrame.n = pos;
}

static void quic_frame_build_connection_close(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = QuicFrame.build_connection_close_args.out;
    size_t cap = QuicFrame.build_connection_close_args.cap;
    proto_bool app = QuicFrame.build_connection_close_args.app;
    uint64_t error_code = QuicFrame.build_connection_close_args.error_code;
    uint64_t frame_type = QuicFrame.build_connection_close_args.frame_type;
    const char *reason = QuicFrame.build_connection_close_args.reason;
    size_t reason_len = QuicFrame.build_connection_close_args.reason_len;

    size_t pos = 0;
    // RFC 9000 sec 19.19: the application variant (0x1d) carries error codes from the application
    // protocol's own space and omits the Frame Type field entirely.
    uint64_t type = QUIC_FT_CONNECTION_CLOSE;
    if (app)
    {
        type = QUIC_FT_CONNECTION_CLOSE_APP;
    }
    if (!wr(out, cap, &pos, type) || !wr(out, cap, &pos, error_code))
    {
        QuicFrame.n = 0;
        return;
    }
    if (!app && !wr(out, cap, &pos, frame_type))
    {
        QuicFrame.n = 0;
        return;
    }
    if (!wr(out, cap, &pos, reason_len))
    {
        QuicFrame.n = 0;
        return;
    }
    if (pos + reason_len > cap)
    {
        QuicFrame.n = 0;
        return;
    }
    if (reason_len)
    {
        mem.cpy(out + pos, reason, reason_len);
    }
    QuicFrame.n = pos + reason_len;
}

QuicFrameNs QuicFrame = {
    .parse = quic_frame_parse,
    .build_padding = quic_frame_build_padding,
    .build_ping = quic_frame_build_ping,
    .build_handshake_done = quic_frame_build_handshake_done,
    .build_ack = quic_frame_build_ack,
    .build_crypto = quic_frame_build_crypto,
    .build_stream = quic_frame_build_stream,
    .build_max_data = quic_frame_build_max_data,
    .build_connection_close = quic_frame_build_connection_close,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP3
