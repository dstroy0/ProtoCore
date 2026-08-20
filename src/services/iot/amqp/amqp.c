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

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_AMQP

#include "services/iot/amqp/amqp.h"

#include "mmgr/endian/endian.h"     // endian.wr16be / rd32be: the network byte order of sec 4.2.5.1
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
void protocore_amqp_protocol_header(uint8_t *restrict work)
{
    (void)work;
    static const uint8_t hdr[8] = {'A', 'M', 'Q', 'P', 0, 0, 9, 1};
    AmqpV.n = 0;
    AmqpV.ok = PROTO_FALSE;
    if (!AmqpV.out.buf || AmqpV.out.cap < sizeof(hdr))
    {
        return;
    }
    mem.cpy(AmqpV.out.buf, hdr, sizeof(hdr));
    AmqpV.n = sizeof(hdr);
    AmqpV.ok = PROTO_TRUE;
}

// One frame: header, ns->payload, frame-end (sec 4.2.3). The size field is a long-uint, so a
// payload wider than 32 bits has no size to write.
void protocore_amqp_build_frame(uint8_t *restrict work)
{
    (void)work;
    AmqpV.n = 0;
    AmqpV.ok = PROTO_FALSE;
    uint8_t *buf = AmqpV.out.buf;
    const size_t payload_len = AmqpV.payload.len;
    if (!buf || (payload_len && !AmqpV.payload.data) || payload_len > 0xFFFFFFFFu)
    {
        return;
    }
    size_t total = AMQP_FRAME_OVERHEAD + payload_len;
    if (total > AmqpV.out.cap)
    {
        return;
    }
    size_t p = write_frame_header(buf, AmqpV.frame.type, AmqpV.frame.channel, (uint32_t)payload_len);
    if (payload_len)
    {
        mem.cpy(buf + p, AmqpV.payload.data, payload_len);
        p += payload_len;
    }
    buf[p++] = AMQP_FRAME_END;
    AmqpV.n = p;
    AmqpV.ok = PROTO_TRUE;
}

// A METHOD frame on ns->frame.channel: class-id, method-id, then the arguments (sec 4.2.4). The
// payload is written straight into ns->out.buf, no intermediate copy.
void protocore_amqp_build_method(uint8_t *restrict work)
{
    (void)work;
    AmqpV.n = 0;
    AmqpV.ok = PROTO_FALSE;
    uint8_t *buf = AmqpV.out.buf;
    const size_t args_len = AmqpV.method.args_len;
    if (!buf || (args_len && !AmqpV.method.args))
    {
        return;
    }
    size_t payload_len = 4 + args_len; // class-id(2) + method-id(2) + arguments
    size_t total = AMQP_FRAME_OVERHEAD + payload_len;
    if (total > AmqpV.out.cap)
    {
        return;
    }
    size_t p = write_frame_header(buf, AMQP_FRAME_METHOD, AmqpV.frame.channel, (uint32_t)payload_len);
    p += endian.wr16be(buf + p, AmqpV.method.class_id);
    p += endian.wr16be(buf + p, AmqpV.method.method_id);
    if (args_len)
    {
        mem.cpy(buf + p, AmqpV.method.args, args_len);
        p += args_len;
    }
    buf[p++] = AMQP_FRAME_END;
    AmqpV.n = p;
    AmqpV.ok = PROTO_TRUE;
}

// A content HEADER frame on ns->frame.channel: class-id, weight, body size, property flags, then
// the property list (sec 4.2.6.1). The weight field is unused and written as zero.
void protocore_amqp_build_content_header(uint8_t *restrict work)
{
    (void)work;
    AmqpV.n = 0;
    AmqpV.ok = PROTO_FALSE;
    uint8_t *buf = AmqpV.out.buf;
    const size_t list_len = AmqpV.content.property_list_len;
    if (!buf || (list_len && !AmqpV.content.property_list))
    {
        return;
    }
    size_t payload_len = 2 + 2 + 8 + 2 + list_len; // class-id + weight + body-size + flags + list
    size_t total = AMQP_FRAME_OVERHEAD + payload_len;
    if (total > AmqpV.out.cap)
    {
        return;
    }
    size_t p = write_frame_header(buf, AMQP_FRAME_HEADER, AmqpV.frame.channel, (uint32_t)payload_len);
    p += endian.wr16be(buf + p, AmqpV.content.class_id);
    p += endian.wr16be(buf + p, 0);
    p += endian.wr64be(buf + p, AmqpV.content.body_size);
    p += endian.wr16be(buf + p, AmqpV.content.property_flags);
    if (list_len)
    {
        mem.cpy(buf + p, AmqpV.content.property_list, list_len);
        p += list_len;
    }
    buf[p++] = AMQP_FRAME_END;
    AmqpV.n = p;
    AmqpV.ok = PROTO_TRUE;
}

// A heartbeat: type 8, channel 0, size 0, frame-end (sec 4.2.1 grammar, sec 4.2.7). Reads ns->out
// alone and leaves ns->frame and ns->payload as the caller set them.
void protocore_amqp_build_heartbeat(uint8_t *restrict work)
{
    (void)work;
    AmqpV.n = 0;
    AmqpV.ok = PROTO_FALSE;
    if (!AmqpV.out.buf || AmqpV.out.cap < AMQP_FRAME_OVERHEAD)
    {
        return;
    }
    size_t p = write_frame_header(AmqpV.out.buf, AMQP_FRAME_HEARTBEAT, 0, 0);
    AmqpV.out.buf[p++] = AMQP_FRAME_END;
    AmqpV.n = p;
    AmqpV.ok = PROTO_TRUE;
}

// One frame off the head of ns->in, the frame-end checked before anything is decoded (sec 4.2.3).
// ns->payload points into ns->in.buf; ns->consumed spans header, payload and frame-end.
void protocore_amqp_parse_frame(uint8_t *restrict work)
{
    (void)work;
    AmqpV.ok = PROTO_FALSE;
    AmqpV.consumed = 0;
    const uint8_t *buf = AmqpV.in.buf;
    const size_t len = AmqpV.in.len;
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
    AmqpV.frame.type = buf[0];
    AmqpV.frame.channel = endian.rd16be(buf + 1);
    AmqpV.payload.data = buf + 7;
    AmqpV.payload.len = size;
    AmqpV.consumed = AMQP_FRAME_OVERHEAD + (size_t)size;
    AmqpV.ok = PROTO_TRUE;
}

// ns->payload split into class-id, method-id and the arguments behind them (sec 4.2.4).
void protocore_amqp_parse_method(uint8_t *restrict work)
{
    (void)work;
    AmqpV.ok = PROTO_FALSE;
    const uint8_t *payload = AmqpV.payload.data;
    const size_t payload_len = AmqpV.payload.len;
    if (!payload || payload_len < 4)
    {
        return;
    }
    AmqpV.method.class_id = endian.rd16be(payload);
    AmqpV.method.method_id = endian.rd16be(payload + 2);
    AmqpV.method.args = payload + 4;
    AmqpV.method.args_len = payload_len - 4;
    AmqpV.ok = PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
AmqpVars AmqpV;

#endif // PROTOCORE_ENABLE_AMQP
