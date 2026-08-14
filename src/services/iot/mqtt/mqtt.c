// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mqtt.c
 * @brief The MQTT Client: the Control Packet codec (OASIS MQTT Version 3.1.1 sec 2, sec 3) and the
 *        one Network Connection it runs over (sec 4.2).
 *
 * The codec half holds nothing and works in the caller's buffers. The transport half owns one
 * Network Connection: the outbound TCP client under it, the shared persistent client TLS session for
 * `mqtts://`, the receive reassembly, the wire buffer, and the QoS 1 and QoS 2 in-flight window. All
 * of it static; no heap.
 */

#include "services/iot/mqtt/mqtt.h"

#if PROTOCORE_ENABLE_MQTT

#include "mmgr/protomem.h"    // mem.cpy / mem.chr / mem.move / mem.set: the spans a packet is built from
#include "mmgr/protostr.h"    // str.len: the bounded field lengths
#include "shared/utf8/utf8.h" // Utf8.valid: a Topic Name is a UTF-8 encoded string (sec 1.5.3)

#if PROTOCORE_HAS_NET_STACK
#include "mmgr/secure.h"                                 // secure.persist_span: this module's storage
#include "mmgr/span.h"                                   // span.ok: the borrow landed
#include "network_drivers/transport/tcp/client/client.h" // TcpClient: the outbound transport (L4)
#include "server/clock/clock.h"                          // protocore_millis: the link timer and Keep Alive
#if PROTOCORE_ENABLE_MQTT_TLS
#include "network_drivers/tls/tls.h" // the persistent client TLS session
#endif
#endif

#ifdef PROTOCORE_MQTT_DEBUG
#include <stdio.h>
#endif

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

#define MQ_REMLEN_BITS 7       // data bits per Remaining Length octet (sec 2.2.3)
#define MQ_REMLEN_DATA 0x7F    // those bits as a mask
#define MQ_REMLEN_MORE 0x80    // the continuation bit of one Remaining Length octet
#define MQ_REMLEN_OCTETS_MAX 4 // the field is at most four octets (sec 2.2.3)

#define MQ_TYPE_SHIFT 4    // the Control Packet type sits in byte 1 bits 7-4 (sec 2.2.1)
#define MQ_FLAGS_MASK 0x0F // the type-specific flags are byte 1 bits 3-0 (sec 2.2.2)

// The PUBLISH fixed-header flags (sec 3.3.1).
#define MQ_PUB_RETAIN 0x01   // RETAIN, bit 0 (sec 3.3.1.3)
#define MQ_PUB_QOS_SHIFT 1   // QoS level, bits 2-1 (sec 3.3.1.2)
#define MQ_PUB_QOS_MASK 0x03 // both QoS bits set is malformed (MQTT-3.3.1-4)
#define MQ_PUB_DUP 0x08      // DUP, bit 3 (sec 3.3.1.1)

// The reserved fixed-header flags PUBREL, SUBSCRIBE and UNSUBSCRIBE must carry: 0,0,1,0
// (sec 3.6.1, sec 3.8.1, sec 3.10.1).
#define MQ_FLAGS_RESERVED_0010 0x02

// The Connect Flags byte (sec 3.1.2.3).
#define MQ_CONNECT_CLEAN_SESSION 0x02 // Clean Session, bit 1 (sec 3.1.2.4)
#define MQ_CONNECT_WILL_FLAG 0x04     // Will Flag, bit 2 (sec 3.1.2.5)
#define MQ_CONNECT_WILL_QOS_SHIFT 3   // Will QoS, bits 4-3 (sec 3.1.2.6)
#define MQ_CONNECT_WILL_RETAIN 0x20   // Will Retain, bit 5 (sec 3.1.2.7)
#define MQ_CONNECT_PASSWORD 0x40      // Password Flag, bit 6 (sec 3.1.2.9)
#define MQ_CONNECT_USER_NAME 0x80     // User Name Flag, bit 7 (sec 3.1.2.8)

#define MQ_CONNACK_SESSION_PRESENT 0x01 // Session Present, byte 1 bit 0 (sec 3.2.2.2)

#define MQ_ACK_REMAINING_LENGTH 2 // PUBACK / PUBREC / PUBREL / PUBCOMP carry a Packet Identifier only
#define MQ_ACK_PACKET_LEN 4       // and so are four octets whole (sec 3.4 - sec 3.7)
#define MQ_EMPTY_PACKET_LEN 2     // PINGREQ and DISCONNECT have no variable header (sec 3.12, sec 3.14)

#define MQ_FIELD_PREFIX 2 // every UTF-8 encoded string and binary field is length prefixed (sec 1.5.3)

#ifdef PROTOCORE_MQTT_DEBUG
#define MQ_DBG(...) printf(__VA_ARGS__)
#else
#define MQ_DBG(...) ((void)0)
#endif

#if PROTOCORE_HAS_NET_STACK && PROTOCORE_ENABLE_MQTT_TLS
#if !defined(PROTOCORE_PLATFORM_TLS_WANT_READ) || !defined(PROTOCORE_PLATFORM_TLS_WANT_WRITE)
#error                                                                                                                 \
    "ProtoCore: the platform must state PROTOCORE_PLATFORM_TLS_WANT_READ and PROTOCORE_PLATFORM_TLS_WANT_WRITE - the value a BIO returns to the TLS engine when no octet moved and the call should be retried."
#endif
#endif

// ---------------------------------------------------------------------------
// Typedefs
// ---------------------------------------------------------------------------

#if PROTOCORE_HAS_NET_STACK

/** @brief Where an outbound QoS 1 or QoS 2 exchange has got to (MQTT 3.1.1 sec 4.3.2, sec 4.3.3). */
typedef enum PROTO_ENUM_PACKED
{
    MQTT_INFLIGHT_FREE = 0, ///< the slot holds no exchange
    MQTT_INFLIGHT_ACK,      ///< sent, awaiting PUBACK (QoS 1) or PUBREC (QoS 2)
    MQTT_INFLIGHT_COMP,     ///< PUBREC seen, awaiting PUBCOMP (QoS 2)
} MqttInflightState;

// One outbound QoS 1 or QoS 2 exchange. The PUBLISH stays in tx - a re-delivery rewinds the worker
// to the start of it - so this records what identifies and times the exchange, not the octets.
typedef struct
{
    uint16_t packet_id; ///< the Packet Identifier the exchange runs under (sec 2.3.1)
    MqttInflightState state;
    uint32_t sent_ms;
    size_t len; ///< the framed PUBLISH length, bounded by PROTOCORE_MQTT_BUF_SIZE
} MqttInflight;

/** @brief How far the Network Connection has come up. Anything but IDLE means a connect is in flight. */
typedef enum PROTO_ENUM_PACKED
{
    MQTT_LINK_IDLE = 0, ///< no connect in flight
    MQTT_LINK_TCP,      ///< the transport slot is coming up
    MQTT_LINK_TLS,      ///< the TLS handshake advances one flight per step
    MQTT_LINK_CONNACK,  ///< CONNECT is on the wire, its CONNACK has not arrived (sec 3.2)
} MqttLinkState;

/**
 * @brief The Client's compile-time storage: the one Network Connection and everything it holds.
 *
 * All of it static, so a session costs no heap. The receive and wire buffers are one borrow from the
 * secure pool's persistent end, split at a stated offset - reclaiming per packet would wipe them each
 * time, and mark/release is a bump discipline whose release here would reclaim another module's
 * borrow.
 */
struct MqttStorage
{
    int cid;           ///< the outbound transport slot, or < 0 when none is held
    proto_bool closed; ///< the peer closed or errored, as the pump saw it

    MqttLinkState link;      ///< how far the Network Connection has come up
    uint32_t timer;          ///< when the connect started
    uint32_t link_budget_ms; ///< what the whole connect is given

    uint8_t *rx;         ///< receive reassembly, and the scratch a build assembles a body in
    size_t rx_len;       ///< octets currently in rx
    uint8_t *tx;         ///< the wire buffer holding one framed Control Packet
    size_t tx_len;       ///< octets of the framed packet
    size_t tx_off;       ///< octets already put on the wire
    proto_bool tx_ready; ///< a packet is framed and waiting for a worker

    proto_bool use_tls;    ///< the Network Connection runs over TLS
    proto_bool session_up; ///< the Server accepted the CONNECT (sec 3.2.2.3)
    int connack_code;      ///< the Connect Return code the CONNACK carried, -1 until it arrives

    uint16_t keep_alive;     ///< the Keep Alive the CONNECT stated (sec 3.1.2.10)
    uint32_t last_tx_ms;     ///< when the last Control Packet finished going out
    proto_bool ping_pending; ///< a PINGREQ is awaiting its PINGRESP (sec 3.12, sec 3.13)
    uint32_t ping_sent_ms;   ///< when that PINGREQ went out
    uint16_t next_packet_id; ///< the next Packet Identifier to hand out (sec 2.3.1)

    MqttMessageCb on_message;                            ///< where an inbound Application Message goes
    char topic[PROTOCORE_MQTT_MAX_TOPIC];                ///< the Topic Name a parse copies out of an inbound PUBLISH
    MqttInflight inflight[PROTOCORE_MQTT_MAX_INFLIGHT];  ///< the outbound QoS 1 and QoS 2 window
    uint16_t rx_packet_id[PROTOCORE_MQTT_RX_QOS2_SLOTS]; ///< inbound QoS 2 ids held from PUBREC to PUBCOMP
};

#endif // PROTOCORE_HAS_NET_STACK

/**
 * @brief The Client's state and the calls that reach it - what MqttNs points at.
 *
 * @var MqttInternal::store  the Network Connection, its buffers and its in-flight window
 * @var MqttInternal::ns     the handle a caller sets a call's members on
 *
 * No storage member on a build with no network stack: the codec works in the caller's buffers and
 * there is no Network Connection to hold.
 */
struct MqttInternal
{
#if PROTOCORE_HAS_NET_STACK
    struct MqttStorage *store;
#endif
    MqttNs *ns;
};

#if PROTOCORE_HAS_NET_STACK
static struct MqttStorage s_store = {.cid = -1, .next_packet_id = 1};
static struct MqttInternal s_mqtt = {.store = &s_store, .ns = &Mqtt};
#else
static struct MqttInternal s_mqtt = {.ns = &Mqtt};
#endif

// ---------------------------------------------------------------------------
// Codec: the octet moves every build and parse is made of (pure, host-testable)
// ---------------------------------------------------------------------------

// Write v as the two-octet big-endian integer the wire uses (sec 1.5.2).
static inline void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}

// Read that same two-octet big-endian integer.
static inline uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

// Write a two-octet length followed by len octets; returns 2 + len (sec 1.5.3).
static size_t put_field(uint8_t *p, const uint8_t *data, size_t len)
{
    put_u16(p, (uint16_t)len);
    if (len)
    {
        mem.cpy(p + MQ_FIELD_PREFIX, data, len);
    }
    return MQ_FIELD_PREFIX + len;
}

// put_field for a NUL-terminated UTF-8 encoded string, bounded by the body scratch.
static inline size_t put_str(uint8_t *p, const char *s)
{
    return put_field(p, (const uint8_t *)s, s ? str.len(s, PROTOCORE_MQTT_BUF_SIZE) : 0);
}

// Encode len as a Remaining Length field of 1 to 4 octets, seven data bits and a continuation bit
// each, least significant first. Returns the octets written, 0 above the four-octet maximum
// (sec 2.2.3).
static size_t encode_remlen(uint8_t *out, uint32_t len)
{
    if (len > PROTOCORE_MQTT_REMAINING_LENGTH_MAX)
    {
        return 0;
    }
    size_t n = 0;
    do
    {
        uint8_t octet = (uint8_t)(len & MQ_REMLEN_DATA);
        len >>= MQ_REMLEN_BITS;
        if (len > 0)
        {
            octet |= MQ_REMLEN_MORE;
        }
        out[n++] = octet;
    } while (len > 0);
    return n;
}

// Decode a Remaining Length field, reporting the value and the octets it took. False when the field
// is not all present or a fifth continuation octet follows (sec 2.2.3).
static proto_bool decode_remlen(const uint8_t *buf, size_t avail, uint32_t *value, size_t *used)
{
    uint32_t v = 0;
    uint32_t mult = 1;
    for (size_t i = 0; i < MQ_REMLEN_OCTETS_MAX; i++)
    {
        if (i >= avail)
        {
            return PROTO_FALSE;
        }
        uint8_t b = buf[i];
        v += (uint32_t)(b & MQ_REMLEN_DATA) * mult;
        if ((b & MQ_REMLEN_MORE) == 0)
        {
            *value = v;
            *used = i + 1;
            return PROTO_TRUE;
        }
        mult <<= MQ_REMLEN_BITS;
    }
    return PROTO_FALSE;
}

// Compose byte 1, the Remaining Length over blen, and the body already assembled elsewhere. Returns
// the whole Control Packet length, 0 when it does not fit cap (sec 2.2).
static size_t compose(uint8_t *out, size_t cap, uint8_t byte1, const uint8_t *body, size_t blen)
{
    uint8_t rl[MQ_REMLEN_OCTETS_MAX];
    size_t rln = encode_remlen(rl, (uint32_t)blen);
    if (rln == 0)
    {
        return 0;
    }
    size_t total = 1 + rln + blen;
    if (total > cap)
    {
        return 0;
    }
    out[0] = byte1;
    mem.cpy(out + 1, rl, rln);
    if (blen)
    {
        mem.cpy(out + 1 + rln, body, blen);
    }
    return total;
}

// ---------------------------------------------------------------------------
// Codec: the calls
// ---------------------------------------------------------------------------

static void mqtt_encode_remaining_length(struct MqttInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->buf.out)
    {
        return;
    }
    uint8_t rl[MQ_REMLEN_OCTETS_MAX];
    size_t used = encode_remlen(rl, ctx->ns->packet.remaining_length);
    if (used == 0 || used > ctx->ns->buf.cap)
    {
        return;
    }
    mem.cpy(ctx->ns->buf.out, rl, used);
    ctx->ns->n = used;
    ctx->ns->ok = PROTO_TRUE;
}

static void mqtt_decode_remaining_length(struct MqttInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->buf.in)
    {
        return;
    }
    uint32_t value = 0;
    size_t used = 0;
    if (!decode_remlen(ctx->ns->buf.in, ctx->ns->buf.avail, &value, &used))
    {
        return;
    }
    ctx->ns->packet.remaining_length = value;
    ctx->ns->n = used;
    ctx->ns->ok = PROTO_TRUE;
}

// CONNECT: Protocol Name, Protocol Level, Connect Flags, Keep Alive, then the payload's Client
// Identifier, Will Topic, Will Message, User Name and Password, in that order (sec 3.1).
static void mqtt_build_connect(struct MqttInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    uint8_t *body = ctx->ns->buf.body;
    if (!ctx->ns->buf.out || !body || !ctx->ns->session.client_id)
    {
        return;
    }

    size_t n = 0;
    n += put_str(body + n, "MQTT");
    body[n++] = PROTOCORE_MQTT_PROTOCOL_LEVEL;

    uint8_t flags = 0;
    if (ctx->ns->session.clean_session)
    {
        flags |= MQ_CONNECT_CLEAN_SESSION;
    }
    if (ctx->ns->will.topic)
    {
        flags |= MQ_CONNECT_WILL_FLAG;
        flags |= (uint8_t)((ctx->ns->will.qos & MQ_PUB_QOS_MASK) << MQ_CONNECT_WILL_QOS_SHIFT);
        if (ctx->ns->will.retain)
        {
            flags |= MQ_CONNECT_WILL_RETAIN;
        }
    }
    if (ctx->ns->session.user_name)
    {
        flags |= MQ_CONNECT_USER_NAME;
    }
    if (ctx->ns->session.password)
    {
        flags |= MQ_CONNECT_PASSWORD;
    }
    body[n++] = flags;
    put_u16(body + n, ctx->ns->session.keep_alive);
    n += MQ_FIELD_PREFIX;

    // Every payload field the flags called for, measured against the body scratch before any of it
    // is written.
    size_t need = MQ_FIELD_PREFIX + str.len(ctx->ns->session.client_id, PROTOCORE_MQTT_BUF_SIZE);
    if (ctx->ns->will.topic)
    {
        need += MQ_FIELD_PREFIX + str.len(ctx->ns->will.topic, PROTOCORE_MQTT_BUF_SIZE) + MQ_FIELD_PREFIX +
                ctx->ns->will.message_len;
    }
    if (ctx->ns->session.user_name)
    {
        need += MQ_FIELD_PREFIX + str.len(ctx->ns->session.user_name, PROTOCORE_MQTT_BUF_SIZE);
    }
    if (ctx->ns->session.password)
    {
        need += MQ_FIELD_PREFIX + str.len(ctx->ns->session.password, PROTOCORE_MQTT_BUF_SIZE);
    }
    if (n + need > ctx->ns->buf.body_cap)
    {
        return;
    }

    n += put_str(body + n, ctx->ns->session.client_id);
    if (ctx->ns->will.topic)
    {
        n += put_str(body + n, ctx->ns->will.topic);
        n += put_field(body + n, ctx->ns->will.message, ctx->ns->will.message_len);
    }
    if (ctx->ns->session.user_name)
    {
        n += put_str(body + n, ctx->ns->session.user_name);
    }
    if (ctx->ns->session.password)
    {
        n += put_str(body + n, ctx->ns->session.password);
    }

    ctx->ns->n =
        compose(ctx->ns->buf.out, ctx->ns->buf.cap, (uint8_t)((uint8_t)MQTT_CONNECT << MQ_TYPE_SHIFT), body, n);
    ctx->ns->ok = ctx->ns->n != 0;
}

// PUBLISH: Topic Name, the Packet Identifier when QoS is above 0, then the Payload (sec 3.3).
static void mqtt_build_publish(struct MqttInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    uint8_t *body = ctx->ns->buf.body;
    const char *topic = ctx->ns->message.topic_name;
    if (!ctx->ns->buf.out || !body || !topic || ctx->ns->message.qos > 2)
    {
        return;
    }
    // MQTT-3.3.2-2: the Topic Name in a PUBLISH MUST NOT contain wildcard characters. A subscribe
    // Topic Filter may, so this is publish-only.
    for (const char *t = topic; *t; t++)
    {
        if (*t == '+' || *t == '#')
        {
            return;
        }
    }
    size_t tlen = str.len(topic, PROTOCORE_MQTT_BUF_SIZE);
    size_t blen =
        MQ_FIELD_PREFIX + tlen + (ctx->ns->message.qos > 0 ? MQ_FIELD_PREFIX : 0) + ctx->ns->message.payload_len;
    if (blen > ctx->ns->buf.body_cap)
    {
        return;
    }
    size_t n = 0;
    n += put_field(body + n, (const uint8_t *)topic, tlen);
    if (ctx->ns->message.qos > 0)
    {
        put_u16(body + n, ctx->ns->packet.packet_id);
        n += MQ_FIELD_PREFIX;
    }
    if (ctx->ns->message.payload_len)
    {
        mem.cpy(body + n, ctx->ns->message.payload, ctx->ns->message.payload_len);
    }
    n += ctx->ns->message.payload_len;

    uint8_t f = (uint8_t)((ctx->ns->message.qos & MQ_PUB_QOS_MASK) << MQ_PUB_QOS_SHIFT);
    if (ctx->ns->message.retain)
    {
        f |= MQ_PUB_RETAIN;
    }
    if (ctx->ns->message.dup)
    {
        f |= MQ_PUB_DUP;
    }
    ctx->ns->n =
        compose(ctx->ns->buf.out, ctx->ns->buf.cap, (uint8_t)(((uint8_t)MQTT_PUBLISH << MQ_TYPE_SHIFT) | f), body, n);
    ctx->ns->ok = ctx->ns->n != 0;
}

// SUBSCRIBE: the Packet Identifier, then one Topic Filter and its Requested QoS (sec 3.8).
static void mqtt_build_subscribe(struct MqttInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    uint8_t *body = ctx->ns->buf.body;
    const char *topic = ctx->ns->filter.topic_filter;
    if (!ctx->ns->buf.out || !body || !topic || ctx->ns->filter.qos > 2)
    {
        return;
    }
    size_t tlen = str.len(topic, PROTOCORE_MQTT_BUF_SIZE);
    if (MQ_FIELD_PREFIX + MQ_FIELD_PREFIX + tlen + 1 > ctx->ns->buf.body_cap)
    {
        return;
    }
    size_t n = 0;
    put_u16(body + n, ctx->ns->packet.packet_id);
    n += MQ_FIELD_PREFIX;
    n += put_field(body + n, (const uint8_t *)topic, tlen);
    body[n++] = (uint8_t)(ctx->ns->filter.qos & MQ_PUB_QOS_MASK);
    ctx->ns->n = compose(ctx->ns->buf.out, ctx->ns->buf.cap,
                         (uint8_t)(((uint8_t)MQTT_SUBSCRIBE << MQ_TYPE_SHIFT) | MQ_FLAGS_RESERVED_0010), body, n);
    ctx->ns->ok = ctx->ns->n != 0;
}

// UNSUBSCRIBE: the Packet Identifier, then one Topic Filter (sec 3.10).
static void mqtt_build_unsubscribe(struct MqttInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    uint8_t *body = ctx->ns->buf.body;
    const char *topic = ctx->ns->filter.topic_filter;
    if (!ctx->ns->buf.out || !body || !topic)
    {
        return;
    }
    size_t tlen = str.len(topic, PROTOCORE_MQTT_BUF_SIZE);
    if (MQ_FIELD_PREFIX + MQ_FIELD_PREFIX + tlen > ctx->ns->buf.body_cap)
    {
        return;
    }
    size_t n = 0;
    put_u16(body + n, ctx->ns->packet.packet_id);
    n += MQ_FIELD_PREFIX;
    n += put_field(body + n, (const uint8_t *)topic, tlen);
    ctx->ns->n = compose(ctx->ns->buf.out, ctx->ns->buf.cap,
                         (uint8_t)(((uint8_t)MQTT_UNSUBSCRIBE << MQ_TYPE_SHIFT) | MQ_FLAGS_RESERVED_0010), body, n);
    ctx->ns->ok = ctx->ns->n != 0;
}

// PUBACK, PUBREC, PUBREL or PUBCOMP: a Packet Identifier and nothing else (sec 3.4 - sec 3.7).
static void mqtt_build_ack(struct MqttInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->buf.out || ctx->ns->buf.cap < MQ_ACK_PACKET_LEN)
    {
        return;
    }
    uint8_t f = (ctx->ns->packet.type == MQTT_PUBREL) ? MQ_FLAGS_RESERVED_0010 : 0;
    ctx->ns->buf.out[0] = (uint8_t)(((uint8_t)ctx->ns->packet.type << MQ_TYPE_SHIFT) | f);
    ctx->ns->buf.out[1] = MQ_ACK_REMAINING_LENGTH;
    put_u16(ctx->ns->buf.out + MQ_FIELD_PREFIX, ctx->ns->packet.packet_id);
    ctx->ns->n = MQ_ACK_PACKET_LEN;
    ctx->ns->ok = PROTO_TRUE;
}

static void mqtt_build_pingreq(struct MqttInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->buf.out || ctx->ns->buf.cap < MQ_EMPTY_PACKET_LEN)
    {
        return;
    }
    ctx->ns->buf.out[0] = (uint8_t)((uint8_t)MQTT_PINGREQ << MQ_TYPE_SHIFT);
    ctx->ns->buf.out[1] = 0x00;
    ctx->ns->n = MQ_EMPTY_PACKET_LEN;
    ctx->ns->ok = PROTO_TRUE;
}

static void mqtt_build_disconnect(struct MqttInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->buf.out || ctx->ns->buf.cap < MQ_EMPTY_PACKET_LEN)
    {
        return;
    }
    ctx->ns->buf.out[0] = (uint8_t)((uint8_t)MQTT_DISCONNECT << MQ_TYPE_SHIFT);
    ctx->ns->buf.out[1] = 0x00;
    ctx->ns->n = MQ_EMPTY_PACKET_LEN;
    ctx->ns->ok = PROTO_TRUE;
}

// The fixed header: the type and flags of byte 1, then the Remaining Length behind it (sec 2.2).
static void mqtt_parse_fixed_header(struct MqttInternal *restrict ctx)
{
    ctx->ns->n = 0;
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->buf.in || ctx->ns->buf.avail < MQ_EMPTY_PACKET_LEN)
    {
        return;
    }
    uint32_t rl = 0;
    size_t used = 0;
    if (!decode_remlen(ctx->ns->buf.in + 1, ctx->ns->buf.avail - 1, &rl, &used))
    {
        return;
    }
    ctx->ns->packet.type = (MqttType)(ctx->ns->buf.in[0] >> MQ_TYPE_SHIFT);
    ctx->ns->packet.flags = (uint8_t)(ctx->ns->buf.in[0] & MQ_FLAGS_MASK);
    ctx->ns->packet.remaining_length = rl;
    ctx->ns->n = 1 + used;
    ctx->ns->ok = PROTO_TRUE;
}

// A PUBLISH variable header and payload: the Topic Name, the Packet Identifier when QoS is above 0,
// and the Payload that fills what the Remaining Length leaves (sec 3.3).
static void mqtt_parse_publish(struct MqttInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    const uint8_t *buf = ctx->ns->buf.in;
    const uint32_t rl = ctx->ns->packet.remaining_length;
    if (!buf || rl < MQ_FIELD_PREFIX || !ctx->ns->message.topic_out)
    {
        return;
    }
    uint16_t tlen = get_u16(buf);
    size_t off = MQ_FIELD_PREFIX;
    if ((uint32_t)off + tlen > rl)
    {
        return;
    }
    if ((size_t)tlen + 1 > ctx->ns->message.topic_cap)
    {
        return; // the Topic Name and its NUL must fit
    }
    // sec 1.5.3: a UTF-8 encoded string must be well-formed (MQTT-1.5.3-1) and must not encode
    // U+0000 (MQTT-1.5.3-2).
    Utf8.args.s = buf + off;
    Utf8.args.n = tlen;
    Utf8.valid(Utf8.internal);
    if (!Utf8.ok || mem.chr(buf + off, tlen, 0x00))
    {
        return;
    }
    mem.cpy(ctx->ns->message.topic_out, buf + off, tlen);
    ctx->ns->message.topic_out[tlen] = '\0';
    ctx->ns->message.topic_len = tlen;
    off += tlen;

    uint8_t qos = (uint8_t)((ctx->ns->packet.flags >> MQ_PUB_QOS_SHIFT) & MQ_PUB_QOS_MASK);
    if (qos == 3)
    {
        return; // MQTT-3.3.1-4: a PUBLISH MUST NOT have both QoS bits set
    }
    ctx->ns->message.qos = qos;
    ctx->ns->message.retain = (ctx->ns->packet.flags & MQ_PUB_RETAIN) != 0;
    ctx->ns->message.dup = (ctx->ns->packet.flags & MQ_PUB_DUP) != 0;
    ctx->ns->packet.packet_id = 0;
    if (qos > 0)
    {
        if ((uint32_t)off + MQ_FIELD_PREFIX > rl)
        {
            return;
        }
        ctx->ns->packet.packet_id = get_u16(buf + off);
        off += MQ_FIELD_PREFIX;
    }
    ctx->ns->message.payload = buf + off;
    ctx->ns->message.payload_len = rl - off;
    ctx->ns->ok = PROTO_TRUE;
}

// The Packet Identifier a PUBACK, PUBREC, PUBREL, PUBCOMP or UNSUBACK body carries (sec 2.3.1).
static void mqtt_parse_ack(struct MqttInternal *restrict ctx)
{
    ctx->ns->packet.packet_id = 0;
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->buf.in || ctx->ns->packet.remaining_length < MQ_ACK_REMAINING_LENGTH)
    {
        return;
    }
    ctx->ns->packet.packet_id = get_u16(ctx->ns->buf.in);
    ctx->ns->ok = PROTO_TRUE;
}

// A CONNACK body: the Connect Acknowledge Flags then the Connect Return code (sec 3.2.2).
static void mqtt_parse_connack(struct MqttInternal *restrict ctx)
{
    ctx->ns->i32 = -1;
    ctx->ns->session_present = PROTO_FALSE;
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->buf.in || ctx->ns->packet.remaining_length < MQ_ACK_REMAINING_LENGTH)
    {
        return;
    }
    ctx->ns->session_present = (ctx->ns->buf.in[0] & MQ_CONNACK_SESSION_PRESENT) != 0;
    ctx->ns->i32 = ctx->ns->buf.in[1];
    ctx->ns->ok = PROTO_TRUE;
}

// A SUBACK body: the Packet Identifier then the payload's return-code list (sec 3.9.2, sec 3.9.3).
static void mqtt_parse_suback(struct MqttInternal *restrict ctx)
{
    ctx->ns->u8 = PROTOCORE_MQTT_SUBACK_FAILURE;
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->buf.in || ctx->ns->packet.remaining_length < MQ_ACK_REMAINING_LENGTH + 1)
    {
        return;
    }
    ctx->ns->packet.packet_id = get_u16(ctx->ns->buf.in);
    ctx->ns->u8 = ctx->ns->buf.in[MQ_ACK_REMAINING_LENGTH];
    ctx->ns->ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Transport: one Network Connection to one Server (MQTT 3.1.1 sec 4.2)
// ---------------------------------------------------------------------------
#if PROTOCORE_HAS_NET_STACK

// The next Packet Identifier, skipping 0 because a real one is never 0 (sec 2.3.1).
static uint16_t next_packet_id(struct MqttInternal *restrict ctx)
{
    uint16_t p = ctx->store->next_packet_id++;
    if (ctx->store->next_packet_id == 0)
    {
        ctx->store->next_packet_id = 1;
    }
    return p;
}

// Point the codec's buffers at this Client's own storage: the body assembles in rx, the whole
// Control Packet lands in tx.
static void bind_codec_buffers(struct MqttInternal *restrict ctx)
{
    ctx->ns->buf.out = ctx->store->tx;
    ctx->ns->buf.cap = PROTOCORE_MQTT_BUF_SIZE;
    ctx->ns->buf.body = ctx->store->rx;
    ctx->ns->buf.body_cap = PROTOCORE_MQTT_BUF_SIZE;
}

// Take this module's storage on first use and hold it: one borrow from the secure pool's persistent
// end - the end no mark walks - reused for every packet. One borrow and not two, because each
// carries a block header rounded up to the arena alignment; the region is split at a stated offset,
// rx first and tx behind it.
static proto_bool mem_bind(struct MqttInternal *restrict ctx)
{
    if (ctx->store->rx != NULL)
    {
        return PROTO_TRUE;
    }
    protocore_span region = secure.persist_span(2u * PROTOCORE_MQTT_BUF_SIZE);
    if (!span.ok(region))
    {
        return PROTO_FALSE;
    }
    ctx->store->rx = region.buf;
    ctx->store->tx = region.buf + PROTOCORE_MQTT_BUF_SIZE;
    return PROTO_TRUE;
}

// Send plaintext octets to the Server.
static proto_bool tx_plain(struct MqttInternal *restrict ctx, const uint8_t *data, size_t len)
{
    TcpClient.cid = ctx->store->cid;
    TcpClient.io.data = data;
    TcpClient.io.len = len;
    TcpClient.send(TcpClient.internal);
    return TcpClient.ok;
}

// Append what the transport holds to the reassembly buffer. One read: the transport already knows
// how much it has and the worker is calling across passes, so a loop here would only ask a socket
// that has nothing. A full buffer stops draining, which is the backpressure the peer sees.
static void fill_plain(struct MqttInternal *restrict ctx)
{
    size_t room = PROTOCORE_MQTT_BUF_SIZE - ctx->store->rx_len;
    if (room == 0)
    {
        return;
    }
    TcpClient.cid = ctx->store->cid;
    TcpClient.io.buf = ctx->store->rx + ctx->store->rx_len;
    TcpClient.io.cap = room;
    TcpClient.read(TcpClient.internal);
    size_t n = TcpClient.n;
    if (n == 0)
    {
        TcpClient.cid = ctx->store->cid;
        TcpClient.is_closed(TcpClient.internal);
        if (TcpClient.ok)
        {
            ctx->store->closed = PROTO_TRUE;
        }
        return;
    }
    ctx->store->rx_len += n;
}

#if PROTOCORE_ENABLE_MQTT_TLS
// The TLS BIO over the outbound transport: ciphertext out through the slot, ciphertext in by
// draining what the slot buffered. The retry sentinels are the platform's.
static int tls_send(void *bio, const unsigned char *buf, size_t len)
{
    (void)bio;
    size_t cap = len > 0xFFFF ? 0xFFFF : len;
    return tx_plain(&s_mqtt, buf, cap) ? (int)cap : PROTOCORE_PLATFORM_TLS_WANT_WRITE;
}

static int tls_recv(void *bio, unsigned char *buf, size_t len)
{
    (void)bio;
    TcpClient.cid = s_mqtt.store->cid;
    TcpClient.io.buf = buf;
    TcpClient.io.cap = len;
    TcpClient.read(TcpClient.internal);
    size_t n = TcpClient.n;
    if (n == 0)
    {
        TcpClient.cid = s_mqtt.store->cid;
        TcpClient.is_closed(TcpClient.internal);
        return TcpClient.ok ? 0 : PROTOCORE_PLATFORM_TLS_WANT_READ;
    }
    return (int)n;
}

// Append what the session has decrypted to the reassembly buffer. Same shape as fill_plain.
static void fill_tls(struct MqttInternal *restrict ctx)
{
    size_t room = PROTOCORE_MQTT_BUF_SIZE - ctx->store->rx_len;
    if (room == 0)
    {
        return;
    }
    int n = protocore_tls_client_session_read(ctx->store->rx + ctx->store->rx_len, room);
    if (n <= 0)
    {
        if (n < 0)
        {
            ctx->store->closed = PROTO_TRUE;
        }
        return;
    }
    ctx->store->rx_len += (size_t)n;
}
#endif // PROTOCORE_ENABLE_MQTT_TLS

// Raise the flag over the Control Packet the codec just framed into tx. A packet offered while one
// is still going out is refused rather than overwriting it. This layer never reaches the wire.
static proto_bool tx_arm(struct MqttInternal *restrict ctx, size_t len)
{
    if (ctx->store->tx_ready || len == 0)
    {
        return PROTO_FALSE;
    }
    ctx->store->tx_len = len;
    ctx->store->tx_off = 0;
    ctx->store->tx_ready = PROTO_TRUE;
    return PROTO_TRUE;
}

// Put the flagged packet on the wire. The worker owns this connection and the pool the packet sits
// in, so it moves the octets itself: what the transport takes now, the rest on a later pass, and the
// flag drops once the last octet is out.
static void tx_drain(struct MqttInternal *restrict ctx)
{
    if (!ctx->store->tx_ready)
    {
        return;
    }
    size_t n = ctx->store->tx_len - ctx->store->tx_off;
#if PROTOCORE_ENABLE_MQTT_TLS
    if (ctx->store->use_tls)
    {
        int w = protocore_tls_client_session_write(ctx->store->tx + ctx->store->tx_off, n);
        if (w > 0)
        {
            ctx->store->tx_off += (size_t)w;
        }
    }
    else
#endif
        if (tx_plain(ctx, ctx->store->tx + ctx->store->tx_off, n))
    {
        ctx->store->tx_off += n;
    }
    if (ctx->store->tx_off >= ctx->store->tx_len)
    {
        ctx->store->last_tx_ms = protocore_millis();
        ctx->store->tx_ready = PROTO_FALSE;
    }
}

// Close the Network Connection (sec 4.2). A link still coming up is given up with the slot.
static void link_close(struct MqttInternal *restrict ctx)
{
#if PROTOCORE_ENABLE_MQTT_TLS
    if (ctx->store->use_tls)
    {
        protocore_tls_client_session_end();
    }
#endif
    if (ctx->store->cid >= 0)
    {
        TcpClient.cid = ctx->store->cid;
        TcpClient.close(TcpClient.internal);
    }
    ctx->store->cid = -1;
    ctx->store->session_up = PROTO_FALSE;
    ctx->store->link = MQTT_LINK_IDLE;
}

// The in-flight slot running the exchange for this Packet Identifier, or -1.
static int inflight_find(struct MqttInternal *restrict ctx, uint16_t packet_id)
{
    for (int i = 0; i < PROTOCORE_MQTT_MAX_INFLIGHT; i++)
    {
        if (ctx->store->inflight[i].state != MQTT_INFLIGHT_FREE && ctx->store->inflight[i].packet_id == packet_id)
        {
            return i;
        }
    }
    return -1;
}

// Hold an inbound QoS 2 Packet Identifier from PUBREC until PUBCOMP, so a repeat of the same
// PUBLISH delivers the Application Message once (sec 4.3.3).
static void rx_id_add(struct MqttInternal *restrict ctx, uint16_t packet_id)
{
    for (int i = 0; i < PROTOCORE_MQTT_RX_QOS2_SLOTS; i++)
    {
        if (ctx->store->rx_packet_id[i] == 0)
        {
            ctx->store->rx_packet_id[i] = packet_id;
            return;
        }
    }
}

static proto_bool rx_id_has(struct MqttInternal *restrict ctx, uint16_t packet_id)
{
    for (int i = 0; i < PROTOCORE_MQTT_RX_QOS2_SLOTS; i++)
    {
        if (ctx->store->rx_packet_id[i] == packet_id)
        {
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

static void rx_id_del(struct MqttInternal *restrict ctx, uint16_t packet_id)
{
    for (int i = 0; i < PROTOCORE_MQTT_RX_QOS2_SLOTS; i++)
    {
        if (ctx->store->rx_packet_id[i] == packet_id)
        {
            ctx->store->rx_packet_id[i] = 0;
        }
    }
}

// Frame one acknowledgement into tx and flag it for the worker (sec 3.4 - sec 3.7). False when a
// packet is already going out and this one was refused rather than overwriting it.
static proto_bool send_ack(struct MqttInternal *restrict ctx, MqttType type, uint16_t packet_id)
{
    bind_codec_buffers(ctx);
    ctx->ns->packet.type = type;
    ctx->ns->packet.packet_id = packet_id;
    mqtt_build_ack(ctx);
    return tx_arm(ctx, ctx->ns->n);
}

// Act on one whole Control Packet sitting where the worker put it.
static void handle_packet(struct MqttInternal *restrict ctx, const uint8_t *body, MqttType type, uint8_t flags,
                          uint32_t rl)
{
    ctx->ns->buf.in = body;
    ctx->ns->packet.flags = flags;
    ctx->ns->packet.remaining_length = rl;

    switch (type)
    {
    case MQTT_CONNACK:
        mqtt_parse_connack(ctx);
        ctx->store->connack_code = ctx->ns->i32;
        if (ctx->store->connack_code == 0)
        {
            ctx->store->session_up = PROTO_TRUE;
        }
        MQ_DBG("[mqtt] CONNACK return code=%d\n", ctx->store->connack_code);
        break;

    case MQTT_PUBLISH: {
        ctx->ns->message.topic_out = ctx->store->topic;
        ctx->ns->message.topic_cap = sizeof(ctx->store->topic);
        mqtt_parse_publish(ctx);
        if (!ctx->ns->ok)
        {
            // MQTT-4.8.0-1: a protocol violation MUST close the Network Connection the offending
            // Control Packet arrived on. Both QoS bits set is one (MQTT-3.3.1-4).
            link_close(ctx);
            break;
        }
        uint16_t packet_id = ctx->ns->packet.packet_id;
        const uint8_t *payload = ctx->ns->message.payload;
        size_t payload_len = ctx->ns->message.payload_len;
        uint8_t qos = ctx->ns->message.qos;
        if (qos < 2)
        {
            if (ctx->store->on_message)
            {
                ctx->store->on_message(ctx->store->topic, payload, payload_len);
            }
            if (qos == 1)
            {
                (void)send_ack(ctx, MQTT_PUBACK, packet_id); // sec 4.3.2
            }
        }
        else // sec 4.3.3: deliver once, and hold the identifier until PUBREL completes
        {
            if (!rx_id_has(ctx, packet_id))
            {
                if (ctx->store->on_message)
                {
                    ctx->store->on_message(ctx->store->topic, payload, payload_len);
                }
                rx_id_add(ctx, packet_id);
            }
            (void)send_ack(ctx, MQTT_PUBREC, packet_id);
        }
        break;
    }

    case MQTT_PUBACK:  // our QoS 1 PUBLISH acknowledged (sec 4.3.2)
    case MQTT_PUBCOMP: // our QoS 2 PUBLISH complete (sec 4.3.3)
    {
        mqtt_parse_ack(ctx);
        int slot = inflight_find(ctx, ctx->ns->packet.packet_id);
        if (slot >= 0)
        {
            ctx->store->inflight[slot].state = MQTT_INFLIGHT_FREE;
        }
        break;
    }

    case MQTT_PUBREC: // our QoS 2 PUBLISH received: answer PUBREL, await PUBCOMP (sec 4.3.3)
    {
        mqtt_parse_ack(ctx);
        uint16_t packet_id = ctx->ns->packet.packet_id;
        int slot = inflight_find(ctx, packet_id);
        if (slot >= 0)
        {
            ctx->store->inflight[slot].state = MQTT_INFLIGHT_COMP;
            ctx->store->inflight[slot].sent_ms = protocore_millis();
        }
        (void)send_ack(ctx, MQTT_PUBREL, packet_id);
        break;
    }

    case MQTT_PUBREL: // the Server releasing an inbound QoS 2 message: answer PUBCOMP (sec 4.3.3)
    {
        mqtt_parse_ack(ctx);
        uint16_t packet_id = ctx->ns->packet.packet_id;
        rx_id_del(ctx, packet_id);
        (void)send_ack(ctx, MQTT_PUBCOMP, packet_id);
        break;
    }

    case MQTT_PINGRESP: // sec 3.13
        ctx->store->ping_pending = PROTO_FALSE;
        break;

    case MQTT_SUBACK:
        mqtt_parse_suback(ctx);
        break;

    case MQTT_CONNECT:
    case MQTT_SUBSCRIBE:
    case MQTT_UNSUBSCRIBE:
    case MQTT_UNSUBACK:
    case MQTT_PINGREQ:
    case MQTT_DISCONNECT:
    default:
        break; // nothing this end acts on
    }
}

// Fill from the transport, then act on every whole Control Packet the buffer now holds. The loop is
// bounded by the octets already received and never waits: it ends on the first packet that is not
// all here, and what is left of it shifts down to wait for the next fill. Each packet is handled
// where it landed, so nothing is copied out to parse it.
static void process_rx(struct MqttInternal *restrict ctx)
{
#if PROTOCORE_ENABLE_MQTT_TLS
    if (ctx->store->use_tls)
    {
        fill_tls(ctx);
    }
    else
#endif
        fill_plain(ctx);

    size_t off = 0;
    for (;;)
    {
        ctx->ns->buf.in = ctx->store->rx + off;
        ctx->ns->buf.avail = ctx->store->rx_len - off;
        mqtt_parse_fixed_header(ctx);
        if (!ctx->ns->ok)
        {
            break; // the fixed header is not all here yet
        }
        size_t header_len = ctx->ns->n;
        MqttType type = ctx->ns->packet.type;
        uint8_t flags = ctx->ns->packet.flags;
        uint32_t rl = ctx->ns->packet.remaining_length;
        size_t total = header_len + rl;
        if (ctx->store->rx_len - off < total)
        {
            break; // the body is not all here yet
        }
        handle_packet(ctx, ctx->store->rx + off + header_len, type, flags, rl);
        off += total;
    }
    if (off != 0)
    {
        ctx->store->rx_len -= off;
        if (ctx->store->rx_len != 0)
        {
            // The tail overlaps what it moves onto, so this is the overlapping move.
            mem.move(ctx->store->rx, ctx->store->rx + off, ctx->store->rx_len);
        }
    }
}

// Step the Network Connection one stage per call. Nothing here waits: the transport slot, the
// handshake and the CONNACK each report where they are and the caller comes back on its own tick.
static void link_step(struct MqttInternal *restrict ctx)
{
    if (ctx->store->closed || (protocore_millis() - ctx->store->timer) >= ctx->store->link_budget_ms)
    {
        link_close(ctx);
        return;
    }
    switch (ctx->store->link)
    {
    case MQTT_LINK_TCP:
        TcpClient.cid = ctx->store->cid;
        TcpClient.connected(TcpClient.internal);
        if (!TcpClient.ok)
        {
            return;
        }
#if PROTOCORE_ENABLE_MQTT_TLS
        if (ctx->store->use_tls)
        {
            ctx->store->link = MQTT_LINK_TLS;
            return;
        }
#endif
        ctx->store->link = MQTT_LINK_CONNACK;
        if (!tx_arm(ctx, ctx->store->tx_len)) // CONNECT is already framed in tx; this flags it
        {
            link_close(ctx);
        }
        return;

    case MQTT_LINK_TLS:
#if PROTOCORE_ENABLE_MQTT_TLS
    {
        protocore_tls_state hs = protocore_tls_client_session_handshake();
        if (hs == PROTOCORE_TLS_BUSY)
        {
            return; // another flight is owed; the next step carries it
        }
        if (hs != PROTOCORE_TLS_READY)
        {
            MQ_DBG("[mqtt] TLS handshake failed (%d)\n", (int)hs);
            link_close(ctx);
            return;
        }
        ctx->store->link = MQTT_LINK_CONNACK;
        if (!tx_arm(ctx, ctx->store->tx_len)) // CONNECT is already framed in tx; this flags it
        {
            link_close(ctx);
        }
    }
#endif
        return;

    case MQTT_LINK_CONNACK:
        process_rx(ctx);
        if (ctx->store->session_up)
        {
            ctx->store->link = MQTT_LINK_IDLE;
            ctx->store->last_tx_ms = protocore_millis();
        }
        else if (ctx->store->connack_code >= 0)
        {
            link_close(ctx); // the Server answered and refused (sec 3.2.2.3)
        }
        return;

    case MQTT_LINK_IDLE:
        return;
    }
}

// ---------------------------------------------------------------------------
// Transport: the calls
// ---------------------------------------------------------------------------

static void mqtt_on_message(struct MqttInternal *restrict ctx)
{
    ctx->store->on_message = ctx->ns->delivery.on_message;
    ctx->ns->ok = PROTO_TRUE;
}

static void mqtt_connect(struct MqttInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->ns->server.host || !ctx->ns->session.client_id)
    {
        return;
    }
#if !PROTOCORE_ENABLE_MQTT_TLS
    if (ctx->ns->server.use_tls)
    {
        return; // built without mqtts:// support
    }
#endif
    if (!mem_bind(ctx))
    {
        return; // no storage: fail closed rather than build into nothing
    }

    // Every session-scoped field starts over.
    mem.set(ctx->store->inflight, 0, sizeof(ctx->store->inflight));
    mem.set(ctx->store->rx_packet_id, 0, sizeof(ctx->store->rx_packet_id));
    ctx->store->rx_len = 0;
    ctx->store->tx_len = 0;
    ctx->store->tx_off = 0;
    ctx->store->tx_ready = PROTO_FALSE;
    ctx->store->closed = PROTO_FALSE;
    ctx->store->session_up = PROTO_FALSE;
    ctx->store->ping_pending = PROTO_FALSE;
    ctx->store->connack_code = -1;
    ctx->store->keep_alive = ctx->ns->session.keep_alive;
    ctx->store->use_tls = ctx->ns->server.use_tls;

    // Frame CONNECT now, while the caller's session and will members still hold: the payload
    // assembles in rx and the whole packet lands in tx, where it waits for the link.
    bind_codec_buffers(ctx);
    mqtt_build_connect(ctx);
    if (ctx->ns->n == 0)
    {
        return;
    }
    ctx->store->tx_len = ctx->ns->n;

    ctx->store->link_budget_ms = PROTOCORE_MQTT_CONNECT_MS;
    TcpClient.dial.host = ctx->ns->server.host;
    TcpClient.dial.port = ctx->ns->server.port;
    TcpClient.dial.timeout_ms = ctx->store->link_budget_ms;
    TcpClient.open(TcpClient.internal);
    ctx->store->cid = TcpClient.i32;
    if (ctx->store->cid < 0)
    {
        return;
    }

#if PROTOCORE_ENABLE_MQTT_TLS
    // Bind the session here, while host is still in scope. Its BIO reads the transport slot, so
    // nothing moves until the loop starts stepping the handshake.
    if (ctx->store->use_tls && !protocore_tls_client_session_begin(ctx->ns->server.host, tls_send, tls_recv))
    {
        link_close(ctx);
        return;
    }
#endif

    ctx->store->link = MQTT_LINK_TCP;
    ctx->store->timer = protocore_millis();
    ctx->ns->ok = PROTO_TRUE; // started, not connected: step the loop and read connected()
}

static void mqtt_publish(struct MqttInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->store->session_up || ctx->ns->message.qos > 2)
    {
        return;
    }
    bind_codec_buffers(ctx);
    ctx->ns->message.dup = PROTO_FALSE;

    if (ctx->ns->message.qos == 0)
    {
        ctx->ns->packet.packet_id = 0;
        mqtt_build_publish(ctx);
        ctx->ns->ok = ctx->ns->n != 0 && tx_arm(ctx, ctx->ns->n);
        return;
    }

    // QoS 1 and QoS 2 take an in-flight slot. The PUBLISH itself stays in tx - a re-delivery rewinds
    // the worker to the start of it - so the slot records only what identifies and times the
    // exchange (sec 4.3.2, sec 4.3.3).
    int slot = -1;
    for (int i = 0; i < PROTOCORE_MQTT_MAX_INFLIGHT; i++)
    {
        if (ctx->store->inflight[i].state == MQTT_INFLIGHT_FREE)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
    {
        return; // the in-flight window is full
    }
    uint16_t packet_id = next_packet_id(ctx);
    ctx->ns->packet.packet_id = packet_id;
    mqtt_build_publish(ctx);
    if (ctx->ns->n == 0)
    {
        return;
    }
    ctx->store->inflight[slot].packet_id = packet_id;
    ctx->store->inflight[slot].state = MQTT_INFLIGHT_ACK;
    ctx->store->inflight[slot].len = ctx->ns->n;
    ctx->store->inflight[slot].sent_ms = protocore_millis();
    ctx->ns->ok = tx_arm(ctx, ctx->ns->n);
}

static void mqtt_subscribe(struct MqttInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->store->session_up)
    {
        return;
    }
    bind_codec_buffers(ctx);
    ctx->ns->packet.packet_id = next_packet_id(ctx);
    mqtt_build_subscribe(ctx);
    ctx->ns->ok = ctx->ns->n != 0 && tx_arm(ctx, ctx->ns->n);
}

static void mqtt_unsubscribe(struct MqttInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->store->session_up)
    {
        return;
    }
    bind_codec_buffers(ctx);
    ctx->ns->packet.packet_id = next_packet_id(ctx);
    mqtt_build_unsubscribe(ctx);
    ctx->ns->ok = ctx->ns->n != 0 && tx_arm(ctx, ctx->ns->n);
}

static void mqtt_loop(struct MqttInternal *restrict ctx)
{
    // A connect still coming up takes one step per call, and nothing below it runs until the Server
    // has answered: this is the tick the connect hands the link to.
    if (ctx->store->link != MQTT_LINK_IDLE)
    {
        tx_drain(ctx); // CONNECT is framed and flagged from the first step; carry it out
        link_step(ctx);
        ctx->ns->ok = ctx->store->session_up;
        return;
    }
    ctx->ns->ok = PROTO_FALSE;
    if (!ctx->store->session_up)
    {
        return;
    }
    tx_drain(ctx); // whatever the codec flagged last pass goes out before more arrives
    process_rx(ctx);
    if (ctx->store->closed)
    {
        link_close(ctx);
        return;
    }
    if (!ctx->store->session_up)
    {
        return; // a protocol violation in the batch above already closed it (MQTT-4.8.0-1)
    }

    uint32_t now = protocore_millis();

    // Keep Alive (sec 3.1.2.10): send PINGREQ once the interval has passed with nothing sent
    // (sec 3.12), and close the Network Connection when no PINGRESP comes back within another
    // interval, which sec 3.1.2.10 says a Client SHOULD do after a reasonable time (sec 3.13).
    if (ctx->store->keep_alive)
    {
        uint32_t ka = (uint32_t)ctx->store->keep_alive * 1000u;
        if (ctx->store->ping_pending && (now - ctx->store->ping_sent_ms) > ka)
        {
            link_close(ctx);
            return;
        }
        if (!ctx->store->ping_pending && (now - ctx->store->last_tx_ms) >= ka)
        {
            bind_codec_buffers(ctx);
            mqtt_build_pingreq(ctx);
            if (tx_arm(ctx, ctx->ns->n))
            {
                ctx->store->ping_pending = PROTO_TRUE;
                ctx->store->ping_sent_ms = now;
            }
        }
    }

    // Re-deliver unacknowledged in-flight QoS 1 and QoS 2 messages (sec 4.3.2, sec 4.3.3). The
    // PUBLISH is still in tx, so a resend sets DUP where it lies (MQTT-3.3.1-1) and rewinds the
    // worker to the start of it; a QoS 2 exchange past PUBREC resends PUBREL instead
    // (MQTT-4.3.3-1 forbids resending the PUBLISH once PUBREL has gone).
    for (int i = 0; i < PROTOCORE_MQTT_MAX_INFLIGHT; i++)
    {
        if (ctx->store->inflight[i].state == MQTT_INFLIGHT_FREE)
        {
            continue;
        }
        if ((now - ctx->store->inflight[i].sent_ms) < PROTOCORE_MQTT_RETRANSMIT_MS)
        {
            continue;
        }
        if (ctx->store->inflight[i].state == MQTT_INFLIGHT_ACK)
        {
            // One packet is in flight at a time, so tx holds that PUBLISH.
            ctx->store->tx[0] |= MQ_PUB_DUP;
            if (!tx_arm(ctx, ctx->store->inflight[i].len))
            {
                continue; // still going out; the next pass retries
            }
        }
        else if (!send_ack(ctx, MQTT_PUBREL, ctx->store->inflight[i].packet_id))
        {
            continue; // still going out; the next pass retries
        }
        ctx->store->inflight[i].sent_ms = now;
    }
    ctx->ns->ok = PROTO_TRUE;
}

static void mqtt_connected(struct MqttInternal *restrict ctx)
{
    ctx->ns->ok = ctx->store->session_up;
}

static void mqtt_disconnect(struct MqttInternal *restrict ctx)
{
    if (ctx->store->cid >= 0 && ctx->store->session_up)
    {
        bind_codec_buffers(ctx);
        mqtt_build_disconnect(ctx);
        (void)tx_arm(ctx, ctx->ns->n);
    }
    link_close(ctx);
    ctx->ns->ok = PROTO_TRUE;
}

#else // no network stack: the codec still builds, the transport refuses

static void mqtt_on_message(struct MqttInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
}
static void mqtt_connect(struct MqttInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
}
static void mqtt_publish(struct MqttInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
}
static void mqtt_subscribe(struct MqttInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
}
static void mqtt_unsubscribe(struct MqttInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
}
static void mqtt_loop(struct MqttInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
}
static void mqtt_connected(struct MqttInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
}
static void mqtt_disconnect(struct MqttInternal *restrict ctx)
{
    ctx->ns->ok = PROTO_FALSE;
}

#endif // PROTOCORE_HAS_NET_STACK

// Designated, so a member's position in the struct does not decide what it binds to.
MqttNs Mqtt = {.encode_remaining_length = mqtt_encode_remaining_length,
               .decode_remaining_length = mqtt_decode_remaining_length,
               .build_connect = mqtt_build_connect,
               .build_publish = mqtt_build_publish,
               .build_subscribe = mqtt_build_subscribe,
               .build_unsubscribe = mqtt_build_unsubscribe,
               .build_ack = mqtt_build_ack,
               .build_pingreq = mqtt_build_pingreq,
               .build_disconnect = mqtt_build_disconnect,
               .parse_fixed_header = mqtt_parse_fixed_header,
               .parse_publish = mqtt_parse_publish,
               .parse_ack = mqtt_parse_ack,
               .parse_connack = mqtt_parse_connack,
               .parse_suback = mqtt_parse_suback,
               .on_message = mqtt_on_message,
               .connect = mqtt_connect,
               .publish = mqtt_publish,
               .subscribe = mqtt_subscribe,
               .unsubscribe = mqtt_unsubscribe,
               .loop = mqtt_loop,
               .connected = mqtt_connected,
               .disconnect = mqtt_disconnect,
               .internal = &s_mqtt};

#endif // PROTOCORE_ENABLE_MQTT
