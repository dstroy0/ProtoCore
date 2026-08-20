// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_quic_frame.c
 * @brief QUIC frame parsing and building - implementation. See protocore_quic_frame.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_HTTP3

#include "mmgr/protomem/protomem.h"
#include "network_drivers/presentation/http/http3/quic_frame/quic_frame.h"

#include "network_drivers/presentation/http/http3/quic_varint/quic_varint.h"

PROTOCORE_BEGIN_DECLS

// Decode a varint at buf[*pos], advancing *pos. Returns false on truncation.
static proto_bool rd(uint8_t *restrict work, const uint8_t *buf, size_t len, size_t *pos, uint64_t *v)
{
    size_t c = 0;
    QuicVarintV.decode_args.in = buf + *pos;
    QuicVarintV.decode_args.len = len - *pos;
    QuicVarintV.decode_args.value = v;
    QuicVarintV.decode_args.consumed = &c;
    QuicVarint.decode(work);
    if (!QuicVarintV.ok)
    {
        return PROTO_FALSE;
    }
    *pos += c;
    return PROTO_TRUE;
}

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

void protocore_quic_frame_parse(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = QuicFrameV.parse_args.buf;
    size_t len = QuicFrameV.parse_args.len;
    QuicFrameHeader *out = QuicFrameV.parse_args.out;

    size_t pos = 0;
    uint64_t type = 0;
    if (!rd(work, buf, len, &pos, &type))
    {
        QuicFrameV.n = 0;
        return;
    }
    out->type = type;

    if (type == QUIC_FT_PADDING || type == QUIC_FT_PING || type == QUIC_FT_HANDSHAKE_DONE)
    {
        QuicFrameV.n = pos;
        return;
    }

    if (type == QUIC_FT_ACK || type == QUIC_FT_ACK_ECN)
    {
        if (!rd(work, buf, len, &pos, &out->ack.largest) || !rd(work, buf, len, &pos, &out->ack.delay) ||
            !rd(work, buf, len, &pos, &out->ack.range_count) || !rd(work, buf, len, &pos, &out->ack.first_range))
        {
            QuicFrameV.n = 0;
            return;
        }
        for (uint64_t i = 0; i < out->ack.range_count; i++) // skip Gap + ACK Range Length pairs
        {
            uint64_t tmp = 0;
            if (!rd(work, buf, len, &pos, &tmp) || !rd(work, buf, len, &pos, &tmp))
            {
                QuicFrameV.n = 0;
                return;
            }
        }
        if (type == QUIC_FT_ACK_ECN) // skip the three ECN counts
        {
            uint64_t tmp = 0;
            if (!rd(work, buf, len, &pos, &tmp) || !rd(work, buf, len, &pos, &tmp) || !rd(work, buf, len, &pos, &tmp))
            {
                QuicFrameV.n = 0;
                return;
            }
        }
        QuicFrameV.n = pos;
        return;
    }

    if (type == QUIC_FT_CRYPTO)
    {
        if (!rd(work, buf, len, &pos, &out->crypto.offset) || !rd(work, buf, len, &pos, &out->crypto.length))
        {
            QuicFrameV.n = 0;
            return;
        }
        if (pos + out->crypto.length > len)
        {
            QuicFrameV.n = 0;
            return;
        }
        out->crypto.data = buf + pos;
        pos += out->crypto.length;
        QuicFrameV.n = pos;
        return;
    }

    if (type >= QUIC_FT_STREAM && type <= 0x0f)
    {
        if (!rd(work, buf, len, &pos, &out->stream.id))
        {
            QuicFrameV.n = 0;
            return;
        }
        out->stream.offset = 0;
        if (type & QUIC_STREAM_OFF)
        {
            if (!rd(work, buf, len, &pos, &out->stream.offset))
            {
                QuicFrameV.n = 0;
                return;
            }
        }
        if (type & QUIC_STREAM_LEN)
        {
            if (!rd(work, buf, len, &pos, &out->stream.length))
            {
                QuicFrameV.n = 0;
                return;
            }
        }
        else
        {
            out->stream.length = len - pos; // absent Length -> Stream Data runs to the packet end
        }
        if (pos + out->stream.length > len)
        {
            QuicFrameV.n = 0;
            return;
        }
        out->stream.data = buf + pos;
        out->stream.fin = (uint8_t)((type & QUIC_STREAM_FIN) ? 1 : 0);
        pos += out->stream.length;
        QuicFrameV.n = pos;
        return;
    }

    if (type == QUIC_FT_MAX_DATA)
    {
        if (!rd(work, buf, len, &pos, &out->max_data.max))
        {
            QuicFrameV.n = 0;
            return;
        }
        QuicFrameV.n = pos;
        return;
    }

    if (type == QUIC_FT_CONNECTION_CLOSE || type == QUIC_FT_CONNECTION_CLOSE_APP)
    {
        out->close.app = (uint8_t)((type == QUIC_FT_CONNECTION_CLOSE_APP) ? 1 : 0);
        out->close.frame_type = 0;
        if (!rd(work, buf, len, &pos, &out->close.error_code))
        {
            QuicFrameV.n = 0;
            return;
        }
        if (type == QUIC_FT_CONNECTION_CLOSE) // the transport variant carries the triggering frame type
        {
            if (!rd(work, buf, len, &pos, &out->close.frame_type))
            {
                QuicFrameV.n = 0;
                return;
            }
        }
        if (!rd(work, buf, len, &pos, &out->close.reason_len))
        {
            QuicFrameV.n = 0;
            return;
        }
        if (pos + out->close.reason_len > len)
        {
            QuicFrameV.n = 0;
            return;
        }
        out->close.reason = buf + pos;
        pos += out->close.reason_len;
        QuicFrameV.n = pos;
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
        if (!rd(work, buf, len, &pos, &v))
        {
            QuicFrameV.n = 0;
            return;
        }
        QuicFrameV.n = pos;
        return;
    }
    if (type == QUIC_FT_STOP_SENDING || type == QUIC_FT_MAX_STREAM_DATA || type == QUIC_FT_STREAM_DATA_BLOCKED)
    {
        uint64_t v = 0; // two varints
        if (!rd(work, buf, len, &pos, &v) || !rd(work, buf, len, &pos, &v))
        {
            QuicFrameV.n = 0;
            return;
        }
        QuicFrameV.n = pos;
        return;
    }
    if (type == QUIC_FT_RESET_STREAM)
    {
        uint64_t v = 0; // stream id, app error code, final size
        if (!rd(work, buf, len, &pos, &v) || !rd(work, buf, len, &pos, &v) || !rd(work, buf, len, &pos, &v))
        {
            QuicFrameV.n = 0;
            return;
        }
        QuicFrameV.n = pos;
        return;
    }
    if (type == QUIC_FT_NEW_TOKEN)
    {
        uint64_t tlen = 0; // token length + token bytes
        if (!rd(work, buf, len, &pos, &tlen) || pos + tlen > len)
        {
            QuicFrameV.n = 0;
            return;
        }
        pos += tlen;
        QuicFrameV.n = pos;
        return;
    }
    if (type == QUIC_FT_NEW_CONNECTION_ID)
    {
        uint64_t seq = 0;    // sequence number
        uint64_t retire = 0; // retire-prior-to, then a 1-byte CID length follows
        if (!rd(work, buf, len, &pos, &seq) || !rd(work, buf, len, &pos, &retire) || pos >= len)
        {
            QuicFrameV.n = 0;
            return;
        }
        uint8_t cidlen = buf[pos++];
        if (pos + (size_t)cidlen + 16 > len) // connection id + 16-byte stateless reset token
        {
            QuicFrameV.n = 0;
            return;
        }
        pos += (size_t)cidlen + 16;
        QuicFrameV.n = pos;
        return;
    }
    if (type == QUIC_FT_PATH_CHALLENGE || type == QUIC_FT_PATH_RESPONSE)
    {
        if (pos + 8 > len) // 8 bytes of opaque data
        {
            QuicFrameV.n = 0;
            return;
        }
        pos += 8;
        QuicFrameV.n = pos;
        return;
    }

    QuicFrameV.n = 0; // a genuinely unknown / reserved frame type
}

void protocore_quic_frame_build_padding(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = QuicFrameV.build_padding_args.out;
    size_t cap = QuicFrameV.build_padding_args.cap;
    size_t n = QuicFrameV.build_padding_args.n;

    if (n > cap)
    {
        QuicFrameV.n = 0;
        return;
    }
    mem.set(out, 0, n);
    QuicFrameV.n = n;
}

void protocore_quic_frame_build_ping(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = QuicFrameV.build_ping_args.out;
    size_t cap = QuicFrameV.build_ping_args.cap;

    if (cap < 1)
    {
        QuicFrameV.n = 0;
        return;
    }
    out[0] = QUIC_FT_PING;
    QuicFrameV.n = 1;
}

void protocore_quic_frame_build_handshake_done(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = QuicFrameV.build_handshake_done_args.out;
    size_t cap = QuicFrameV.build_handshake_done_args.cap;

    if (cap < 1)
    {
        QuicFrameV.n = 0;
        return;
    }
    out[0] = QUIC_FT_HANDSHAKE_DONE;
    QuicFrameV.n = 1;
}

// Append a varint; returns false on overflow.
static proto_bool wr(uint8_t *restrict work, uint8_t *out, size_t cap, size_t *pos, uint64_t v)
{
    QuicVarintV.encode_args.out = out + *pos;
    QuicVarintV.encode_args.cap = cap - *pos;
    QuicVarintV.encode_args.value = v;
    QuicVarint.encode(work);
    size_t c = QuicVarintV.n;
    if (!c)
    {
        return PROTO_FALSE;
    }
    *pos += c;
    return PROTO_TRUE;
}

void protocore_quic_frame_build_ack(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = QuicFrameV.build_ack_args.out;
    size_t cap = QuicFrameV.build_ack_args.cap;
    uint64_t largest = QuicFrameV.build_ack_args.largest;
    uint64_t delay = QuicFrameV.build_ack_args.delay;
    uint64_t first_range = QuicFrameV.build_ack_args.first_range;

    size_t pos = 0;
    if (!wr(work, out, cap, &pos, QUIC_FT_ACK) || !wr(work, out, cap, &pos, largest) ||
        !wr(work, out, cap, &pos, delay) || !wr(work, out, cap, &pos, 0) /* ACK Range Count */ ||
        !wr(work, out, cap, &pos, first_range))
    {
        QuicFrameV.n = 0;
        return;
    }
    QuicFrameV.n = pos;
}

void protocore_quic_frame_build_crypto(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = QuicFrameV.build_crypto_args.out;
    size_t cap = QuicFrameV.build_crypto_args.cap;
    uint64_t offset = QuicFrameV.build_crypto_args.offset;
    const uint8_t *data = QuicFrameV.build_crypto_args.data;
    size_t len = QuicFrameV.build_crypto_args.len;

    size_t pos = 0;
    if (!wr(work, out, cap, &pos, QUIC_FT_CRYPTO) || !wr(work, out, cap, &pos, offset) ||
        !wr(work, out, cap, &pos, len))
    {
        QuicFrameV.n = 0;
        return;
    }
    if (pos + len > cap)
    {
        QuicFrameV.n = 0;
        return;
    }
    if (len)
    {
        mem.cpy(out + pos, data, len);
    }
    QuicFrameV.n = pos + len;
}

void protocore_quic_frame_build_stream(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = QuicFrameV.build_stream_args.out;
    size_t cap = QuicFrameV.build_stream_args.cap;
    uint64_t id = QuicFrameV.build_stream_args.id;
    uint64_t offset = QuicFrameV.build_stream_args.offset;
    const uint8_t *data = QuicFrameV.build_stream_args.data;
    size_t len = QuicFrameV.build_stream_args.len;
    proto_bool fin = QuicFrameV.build_stream_args.fin;

    uint64_t type = QUIC_FT_STREAM | QUIC_STREAM_LEN | (offset ? QUIC_STREAM_OFF : 0) | (fin ? QUIC_STREAM_FIN : 0);
    size_t pos = 0;
    if (!wr(work, out, cap, &pos, type) || !wr(work, out, cap, &pos, id))
    {
        QuicFrameV.n = 0;
        return;
    }
    if (offset && !wr(work, out, cap, &pos, offset))
    {
        QuicFrameV.n = 0;
        return;
    }
    if (!wr(work, out, cap, &pos, len))
    {
        QuicFrameV.n = 0;
        return;
    }
    if (pos + len > cap)
    {
        QuicFrameV.n = 0;
        return;
    }
    if (len)
    {
        mem.cpy(out + pos, data, len);
    }
    QuicFrameV.n = pos + len;
}

void protocore_quic_frame_build_max_data(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = QuicFrameV.build_max_data_args.out;
    size_t cap = QuicFrameV.build_max_data_args.cap;
    uint64_t max = QuicFrameV.build_max_data_args.max;

    size_t pos = 0;
    if (!wr(work, out, cap, &pos, QUIC_FT_MAX_DATA) || !wr(work, out, cap, &pos, max))
    {
        QuicFrameV.n = 0;
        return;
    }
    QuicFrameV.n = pos;
}

void protocore_quic_frame_build_connection_close(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = QuicFrameV.build_connection_close_args.out;
    size_t cap = QuicFrameV.build_connection_close_args.cap;
    proto_bool app = QuicFrameV.build_connection_close_args.app;
    uint64_t error_code = QuicFrameV.build_connection_close_args.error_code;
    uint64_t frame_type = QuicFrameV.build_connection_close_args.frame_type;
    const char *reason = QuicFrameV.build_connection_close_args.reason;
    size_t reason_len = QuicFrameV.build_connection_close_args.reason_len;

    size_t pos = 0;
    // RFC 9000 sec 19.19: the application variant (0x1d) carries error codes from the application
    // protocol's own space and omits the Frame Type field entirely.
    uint64_t type = QUIC_FT_CONNECTION_CLOSE;
    if (app)
    {
        type = QUIC_FT_CONNECTION_CLOSE_APP;
    }
    if (!wr(work, out, cap, &pos, type) || !wr(work, out, cap, &pos, error_code))
    {
        QuicFrameV.n = 0;
        return;
    }
    if (!app && !wr(work, out, cap, &pos, frame_type))
    {
        QuicFrameV.n = 0;
        return;
    }
    if (!wr(work, out, cap, &pos, reason_len))
    {
        QuicFrameV.n = 0;
        return;
    }
    if (pos + reason_len > cap)
    {
        QuicFrameV.n = 0;
        return;
    }
    if (reason_len)
    {
        mem.cpy(out + pos, reason, reason_len);
    }
    QuicFrameV.n = pos + reason_len;
}

/** @brief The operands and the outcome. */
QuicFrameVars QuicFrameV;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP3
