// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file amqp.c
 * @brief AMQP 0-9-1 wire-level framing: the 7 octet frame header, the payloads that ride behind it,
 *        and the %xCE frame-end (AMQP Working Group, Protocol Specification v0-9-1, sec 4.2).
 *
 * A build writes the whole frame into ns->out and reports its length in ns->n. A parse checks the
 * frame-end before decoding (sec 4.2.3), then points ns->payload into the caller's octets.
 */

#include "services/iot/amqp/amqp.h"

#if PROTOCORE_ENABLE_AMQP

#include "mmgr/endian/endian.h"   // endian.wr16be / rd32be: the network byte order of sec 4.2.5.1
#include "mmgr/protomem/protomem.h" // mem.cpy: the payload spans a frame carries

// Write the 7 octet frame header at buf: type, channel, payload size (sec 4.2.3). Returns 7.
static size_t write_frame_header(uint8_t *buf, uint8_t type, uint16_t channel, uint32_t size)
{
    size_t p = 0;
    buf[p++] = type;
    p += endian.wr16be(buf + p, channel);
    p += endian.wr32be(buf + p, size);
    return p;
}

// The 8 octet protocol-header, "AMQP" %d0 %d0.9.1 (sec 4.2.2).
static void amqp_protocol_header(uint8_t *restrict work)
{
    (void)work;
    static const uint8_t hdr[8] = {'A', 'M', 'Q', 'P', 0, 0, 9, 1};
    Amqp.n = 0;
    Amqp.ok = PROTO_FALSE;
    if (!Amqp.out.buf || Amqp.out.cap < sizeof(hdr))
    {
        return;
    }
    mem.cpy(Amqp.out.buf, hdr, sizeof(hdr));
    Amqp.n = sizeof(hdr);
    Amqp.ok = PROTO_TRUE;
}

// One frame: header, ns->payload, frame-end (sec 4.2.3). The size field is a long-uint, so a
// payload wider than 32 bits has no size to write.
static void amqp_build_frame(uint8_t *restrict work)
{
    (void)work;
    Amqp.n = 0;
    Amqp.ok = PROTO_FALSE;
    uint8_t *buf = Amqp.out.buf;
    const size_t payload_len = Amqp.payload.len;
    if (!buf || (payload_len && !Amqp.payload.data) || payload_len > 0xFFFFFFFFu)
    {
        return;
    }
    size_t total = AMQP_FRAME_OVERHEAD + payload_len;
    if (total > Amqp.out.cap)
    {
        return;
    }
    size_t p = write_frame_header(buf, Amqp.frame.type, Amqp.frame.channel, (uint32_t)payload_len);
    if (payload_len)
    {
        mem.cpy(buf + p, Amqp.payload.data, payload_len);
        p += payload_len;
    }
    buf[p++] = AMQP_FRAME_END;
    Amqp.n = p;
    Amqp.ok = PROTO_TRUE;
}

// A METHOD frame on ns->frame.channel: class-id, method-id, then the arguments (sec 4.2.4). The
// payload is written straight into ns->out.buf, no intermediate copy.
static void amqp_build_method(uint8_t *restrict work)
{
    (void)work;
    Amqp.n = 0;
    Amqp.ok = PROTO_FALSE;
    uint8_t *buf = Amqp.out.buf;
    const size_t args_len = Amqp.method.args_len;
    if (!buf || (args_len && !Amqp.method.args))
    {
        return;
    }
    size_t payload_len = 4 + args_len; // class-id(2) + method-id(2) + arguments
    size_t total = AMQP_FRAME_OVERHEAD + payload_len;
    if (total > Amqp.out.cap)
    {
        return;
    }
    size_t p = write_frame_header(buf, AMQP_FRAME_METHOD, Amqp.frame.channel, (uint32_t)payload_len);
    p += endian.wr16be(buf + p, Amqp.method.class_id);
    p += endian.wr16be(buf + p, Amqp.method.method_id);
    if (args_len)
    {
        mem.cpy(buf + p, Amqp.method.args, args_len);
        p += args_len;
    }
    buf[p++] = AMQP_FRAME_END;
    Amqp.n = p;
    Amqp.ok = PROTO_TRUE;
}

// A content HEADER frame on ns->frame.channel: class-id, weight, body size, property flags, then
// the property list (sec 4.2.6.1). The weight field is unused and written as zero.
static void amqp_build_content_header(uint8_t *restrict work)
{
    (void)work;
    Amqp.n = 0;
    Amqp.ok = PROTO_FALSE;
    uint8_t *buf = Amqp.out.buf;
    const size_t list_len = Amqp.content.property_list_len;
    if (!buf || (list_len && !Amqp.content.property_list))
    {
        return;
    }
    size_t payload_len = 2 + 2 + 8 + 2 + list_len; // class-id + weight + body-size + flags + list
    size_t total = AMQP_FRAME_OVERHEAD + payload_len;
    if (total > Amqp.out.cap)
    {
        return;
    }
    size_t p = write_frame_header(buf, AMQP_FRAME_HEADER, Amqp.frame.channel, (uint32_t)payload_len);
    p += endian.wr16be(buf + p, Amqp.content.class_id);
    p += endian.wr16be(buf + p, 0);
    p += endian.wr64be(buf + p, Amqp.content.body_size);
    p += endian.wr16be(buf + p, Amqp.content.property_flags);
    if (list_len)
    {
        mem.cpy(buf + p, Amqp.content.property_list, list_len);
        p += list_len;
    }
    buf[p++] = AMQP_FRAME_END;
    Amqp.n = p;
    Amqp.ok = PROTO_TRUE;
}

// A heartbeat: type 8, channel 0, size 0, frame-end (sec 4.2.1 grammar, sec 4.2.7). Reads ns->out
// alone and leaves ns->frame and ns->payload as the caller set them.
static void amqp_build_heartbeat(uint8_t *restrict work)
{
    (void)work;
    Amqp.n = 0;
    Amqp.ok = PROTO_FALSE;
    if (!Amqp.out.buf || Amqp.out.cap < AMQP_FRAME_OVERHEAD)
    {
        return;
    }
    size_t p = write_frame_header(Amqp.out.buf, AMQP_FRAME_HEARTBEAT, 0, 0);
    Amqp.out.buf[p++] = AMQP_FRAME_END;
    Amqp.n = p;
    Amqp.ok = PROTO_TRUE;
}

// One frame off the head of ns->in, the frame-end checked before anything is decoded (sec 4.2.3).
// ns->payload points into ns->in.buf; ns->consumed spans header, payload and frame-end.
static void amqp_parse_frame(uint8_t *restrict work)
{
    (void)work;
    Amqp.ok = PROTO_FALSE;
    Amqp.consumed = 0;
    const uint8_t *buf = Amqp.in.buf;
    const size_t len = Amqp.in.len;
    if (!buf || len < AMQP_FRAME_OVERHEAD)
    {
        return;
    }
    uint32_t size = endian.rd32be(buf + 3);
    // Compared against the remaining capacity without adding, so a 32-bit size_t cannot wrap
    // computing 8 + size and let a peer-controlled size past the bound.
    if (size > len - AMQP_FRAME_OVERHEAD)
    {
        return; // not fully buffered
    }
    if (buf[7 + size] != AMQP_FRAME_END)
    {
        return; // missing or corrupt frame-end
    }
    Amqp.frame.type = buf[0];
    Amqp.frame.channel = endian.rd16be(buf + 1);
    Amqp.payload.data = buf + 7;
    Amqp.payload.len = size;
    Amqp.consumed = AMQP_FRAME_OVERHEAD + (size_t)size;
    Amqp.ok = PROTO_TRUE;
}

// ns->payload split into class-id, method-id and the arguments behind them (sec 4.2.4).
static void amqp_parse_method(uint8_t *restrict work)
{
    (void)work;
    Amqp.ok = PROTO_FALSE;
    const uint8_t *payload = Amqp.payload.data;
    const size_t payload_len = Amqp.payload.len;
    if (!payload || payload_len < 4)
    {
        return;
    }
    Amqp.method.class_id = endian.rd16be(payload);
    Amqp.method.method_id = endian.rd16be(payload + 2);
    Amqp.method.args = payload + 4;
    Amqp.method.args_len = payload_len - 4;
    Amqp.ok = PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
AmqpNs Amqp = {.protocol_header = amqp_protocol_header,
               .build_frame = amqp_build_frame,
               .build_method = amqp_build_method,
               .build_content_header = amqp_build_content_header,
               .build_heartbeat = amqp_build_heartbeat,
               .parse_frame = amqp_parse_frame,
               .parse_method = amqp_parse_method};

#endif // PROTOCORE_ENABLE_AMQP
