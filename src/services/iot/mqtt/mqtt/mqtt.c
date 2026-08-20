// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "services/iot/mqtt/mqtt/mqtt.h"

#if PROTOCORE_ENABLE_MQTT

#include "mmgr/protomem/protomem.h" // mem.cpy / mem.chr / mem.move / mem.set: the spans a packet is built from
#include "mmgr/protostr/protostr.h" // str.len: the bounded field lengths
#include "shared/utf8/utf8.h"       // Utf8.valid: a Topic Name is a UTF-8 encoded string (sec 1.5.3)

#if PROTOCORE_HAS_NET_STACK
#include "mmgr/secure/secure.h"                          // secure.persist_span: this module's storage
#include "mmgr/span/span.h"                              // span.ok: the borrow landed
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

#if PROTOCORE_HAS_NET_STACK
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define MQTT_OFF_CTX 0u
static_assert(MQTT_OFF_CTX + sizeof(struct MqttStorage) <= PROTOCORE_MQTT_BORROW,
              "PROTOCORE_MQTT_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define MQTT_CTX(w) ((struct MqttStorage *)(void *)((w) + MQTT_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_MQTT_BORROW persistent bytes
} MqttOwnCtx;
static MqttOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_mqtt_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_MQTT_BORROW).buf;
        // A borrow arrives zeroed, and these do not start at zero.
        MQTT_CTX(s_own.span)->cid = -1;
        MQTT_CTX(s_own.span)->next_packet_id = 1;
    }
    return s_own.span;
}
#endif // PROTOCORE_HAS_NET_STACK

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

void protocore_mqtt_encode_remaining_length(uint8_t *restrict work)
{
    (void)work;
    MqttV.n = 0;
    MqttV.ok = PROTO_FALSE;
    if (!MqttV.buf.out)
    {
        return;
    }
    uint8_t rl[MQ_REMLEN_OCTETS_MAX];
    size_t used = encode_remlen(rl, MqttV.packet.remaining_length);
    if (used == 0 || used > MqttV.buf.cap)
    {
        return;
    }
    mem.cpy(MqttV.buf.out, rl, used);
    MqttV.n = used;
    MqttV.ok = PROTO_TRUE;
}

void protocore_mqtt_decode_remaining_length(uint8_t *restrict work)
{
    (void)work;
    MqttV.n = 0;
    MqttV.ok = PROTO_FALSE;
    if (!MqttV.buf.in)
    {
        return;
    }
    uint32_t value = 0;
    size_t used = 0;
    if (!decode_remlen(MqttV.buf.in, MqttV.buf.avail, &value, &used))
    {
        return;
    }
    MqttV.packet.remaining_length = value;
    MqttV.n = used;
    MqttV.ok = PROTO_TRUE;
}

// CONNECT: Protocol Name, Protocol Level, Connect Flags, Keep Alive, then the payload's Client
// Identifier, Will Topic, Will Message, User Name and Password, in that order (sec 3.1).
void protocore_mqtt_build_connect(uint8_t *restrict work)
{
    (void)work;
    MqttV.n = 0;
    MqttV.ok = PROTO_FALSE;
    uint8_t *body = MqttV.buf.body;
    if (!MqttV.buf.out || !body || !MqttV.session.client_id)
    {
        return;
    }

    size_t n = 0;
    n += put_str(body + n, "MQTT");
    body[n++] = PROTOCORE_MQTT_PROTOCOL_LEVEL;

    uint8_t flags = 0;
    if (MqttV.session.clean_session)
    {
        flags |= MQ_CONNECT_CLEAN_SESSION;
    }
    if (MqttV.will.topic)
    {
        flags |= MQ_CONNECT_WILL_FLAG;
        flags |= (uint8_t)((MqttV.will.qos & MQ_PUB_QOS_MASK) << MQ_CONNECT_WILL_QOS_SHIFT);
        if (MqttV.will.retain)
        {
            flags |= MQ_CONNECT_WILL_RETAIN;
        }
    }
    if (MqttV.session.user_name)
    {
        flags |= MQ_CONNECT_USER_NAME;
    }
    if (MqttV.session.password)
    {
        flags |= MQ_CONNECT_PASSWORD;
    }
    body[n++] = flags;
    put_u16(body + n, MqttV.session.keep_alive);
    n += MQ_FIELD_PREFIX;

    // Every payload field the flags called for, measured against the body scratch before any of it
    // is written.
    size_t need = MQ_FIELD_PREFIX + str.len(MqttV.session.client_id, PROTOCORE_MQTT_BUF_SIZE);
    if (MqttV.will.topic)
    {
        need += MQ_FIELD_PREFIX + str.len(MqttV.will.topic, PROTOCORE_MQTT_BUF_SIZE) + MQ_FIELD_PREFIX +
                MqttV.will.message_len;
    }
    if (MqttV.session.user_name)
    {
        need += MQ_FIELD_PREFIX + str.len(MqttV.session.user_name, PROTOCORE_MQTT_BUF_SIZE);
    }
    if (MqttV.session.password)
    {
        need += MQ_FIELD_PREFIX + str.len(MqttV.session.password, PROTOCORE_MQTT_BUF_SIZE);
    }
    if (n + need > MqttV.buf.body_cap)
    {
        return;
    }

    n += put_str(body + n, MqttV.session.client_id);
    if (MqttV.will.topic)
    {
        n += put_str(body + n, MqttV.will.topic);
        n += put_field(body + n, MqttV.will.message, MqttV.will.message_len);
    }
    if (MqttV.session.user_name)
    {
        n += put_str(body + n, MqttV.session.user_name);
    }
    if (MqttV.session.password)
    {
        n += put_str(body + n, MqttV.session.password);
    }

    MqttV.n = compose(MqttV.buf.out, MqttV.buf.cap, (uint8_t)((uint8_t)MQTT_CONNECT << MQ_TYPE_SHIFT), body, n);
    MqttV.ok = MqttV.n != 0;
}

// PUBLISH: Topic Name, the Packet Identifier when QoS is above 0, then the Payload (sec 3.3).
void protocore_mqtt_build_publish(uint8_t *restrict work)
{
    (void)work;
    MqttV.n = 0;
    MqttV.ok = PROTO_FALSE;
    uint8_t *body = MqttV.buf.body;
    const char *topic = MqttV.message.topic_name;
    if (!MqttV.buf.out || !body || !topic || MqttV.message.qos > 2)
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
    size_t blen = MQ_FIELD_PREFIX + tlen + (MqttV.message.qos > 0 ? MQ_FIELD_PREFIX : 0) + MqttV.message.payload_len;
    if (blen > MqttV.buf.body_cap)
    {
        return;
    }
    size_t n = 0;
    n += put_field(body + n, (const uint8_t *)topic, tlen);
    if (MqttV.message.qos > 0)
    {
        put_u16(body + n, MqttV.packet.packet_id);
        n += MQ_FIELD_PREFIX;
    }
    if (MqttV.message.payload_len)
    {
        mem.cpy(body + n, MqttV.message.payload, MqttV.message.payload_len);
    }
    n += MqttV.message.payload_len;

    uint8_t f = (uint8_t)((MqttV.message.qos & MQ_PUB_QOS_MASK) << MQ_PUB_QOS_SHIFT);
    if (MqttV.message.retain)
    {
        f |= MQ_PUB_RETAIN;
    }
    if (MqttV.message.dup)
    {
        f |= MQ_PUB_DUP;
    }
    MqttV.n = compose(MqttV.buf.out, MqttV.buf.cap, (uint8_t)(((uint8_t)MQTT_PUBLISH << MQ_TYPE_SHIFT) | f), body, n);
    MqttV.ok = MqttV.n != 0;
}

// SUBSCRIBE: the Packet Identifier, then one Topic Filter and its Requested QoS (sec 3.8).
void protocore_mqtt_build_subscribe(uint8_t *restrict work)
{
    (void)work;
    MqttV.n = 0;
    MqttV.ok = PROTO_FALSE;
    uint8_t *body = MqttV.buf.body;
    const char *topic = MqttV.filter.topic_filter;
    if (!MqttV.buf.out || !body || !topic || MqttV.filter.qos > 2)
    {
        return;
    }
    size_t tlen = str.len(topic, PROTOCORE_MQTT_BUF_SIZE);
    if (MQ_FIELD_PREFIX + MQ_FIELD_PREFIX + tlen + 1 > MqttV.buf.body_cap)
    {
        return;
    }
    size_t n = 0;
    put_u16(body + n, MqttV.packet.packet_id);
    n += MQ_FIELD_PREFIX;
    n += put_field(body + n, (const uint8_t *)topic, tlen);
    body[n++] = (uint8_t)(MqttV.filter.qos & MQ_PUB_QOS_MASK);
    MqttV.n = compose(MqttV.buf.out, MqttV.buf.cap,
                      (uint8_t)(((uint8_t)MQTT_SUBSCRIBE << MQ_TYPE_SHIFT) | MQ_FLAGS_RESERVED_0010), body, n);
    MqttV.ok = MqttV.n != 0;
}

// UNSUBSCRIBE: the Packet Identifier, then one Topic Filter (sec 3.10).
void protocore_mqtt_build_unsubscribe(uint8_t *restrict work)
{
    (void)work;
    MqttV.n = 0;
    MqttV.ok = PROTO_FALSE;
    uint8_t *body = MqttV.buf.body;
    const char *topic = MqttV.filter.topic_filter;
    if (!MqttV.buf.out || !body || !topic)
    {
        return;
    }
    size_t tlen = str.len(topic, PROTOCORE_MQTT_BUF_SIZE);
    if (MQ_FIELD_PREFIX + MQ_FIELD_PREFIX + tlen > MqttV.buf.body_cap)
    {
        return;
    }
    size_t n = 0;
    put_u16(body + n, MqttV.packet.packet_id);
    n += MQ_FIELD_PREFIX;
    n += put_field(body + n, (const uint8_t *)topic, tlen);
    MqttV.n = compose(MqttV.buf.out, MqttV.buf.cap,
                      (uint8_t)(((uint8_t)MQTT_UNSUBSCRIBE << MQ_TYPE_SHIFT) | MQ_FLAGS_RESERVED_0010), body, n);
    MqttV.ok = MqttV.n != 0;
}

// PUBACK, PUBREC, PUBREL or PUBCOMP: a Packet Identifier and nothing else (sec 3.4 - sec 3.7).
void protocore_mqtt_build_ack(uint8_t *restrict work)
{
    (void)work;
    MqttV.n = 0;
    MqttV.ok = PROTO_FALSE;
    if (!MqttV.buf.out || MqttV.buf.cap < MQ_ACK_PACKET_LEN)
    {
        return;
    }
    uint8_t f = (MqttV.packet.type == MQTT_PUBREL) ? MQ_FLAGS_RESERVED_0010 : 0;
    MqttV.buf.out[0] = (uint8_t)(((uint8_t)MqttV.packet.type << MQ_TYPE_SHIFT) | f);
    MqttV.buf.out[1] = MQ_ACK_REMAINING_LENGTH;
    put_u16(MqttV.buf.out + MQ_FIELD_PREFIX, MqttV.packet.packet_id);
    MqttV.n = MQ_ACK_PACKET_LEN;
    MqttV.ok = PROTO_TRUE;
}

void protocore_mqtt_build_pingreq(uint8_t *restrict work)
{
    (void)work;
    MqttV.n = 0;
    MqttV.ok = PROTO_FALSE;
    if (!MqttV.buf.out || MqttV.buf.cap < MQ_EMPTY_PACKET_LEN)
    {
        return;
    }
    MqttV.buf.out[0] = (uint8_t)((uint8_t)MQTT_PINGREQ << MQ_TYPE_SHIFT);
    MqttV.buf.out[1] = 0x00;
    MqttV.n = MQ_EMPTY_PACKET_LEN;
    MqttV.ok = PROTO_TRUE;
}

void protocore_mqtt_build_disconnect(uint8_t *restrict work)
{
    (void)work;
    MqttV.n = 0;
    MqttV.ok = PROTO_FALSE;
    if (!MqttV.buf.out || MqttV.buf.cap < MQ_EMPTY_PACKET_LEN)
    {
        return;
    }
    MqttV.buf.out[0] = (uint8_t)((uint8_t)MQTT_DISCONNECT << MQ_TYPE_SHIFT);
    MqttV.buf.out[1] = 0x00;
    MqttV.n = MQ_EMPTY_PACKET_LEN;
    MqttV.ok = PROTO_TRUE;
}

// The fixed header: the type and flags of byte 1, then the Remaining Length behind it (sec 2.2).
void protocore_mqtt_parse_fixed_header(uint8_t *restrict work)
{
    (void)work;
    MqttV.n = 0;
    MqttV.ok = PROTO_FALSE;
    if (!MqttV.buf.in || MqttV.buf.avail < MQ_EMPTY_PACKET_LEN)
    {
        return;
    }
    uint32_t rl = 0;
    size_t used = 0;
    if (!decode_remlen(MqttV.buf.in + 1, MqttV.buf.avail - 1, &rl, &used))
    {
        return;
    }
    MqttV.packet.type = (MqttType)(MqttV.buf.in[0] >> MQ_TYPE_SHIFT);
    MqttV.packet.flags = (uint8_t)(MqttV.buf.in[0] & MQ_FLAGS_MASK);
    MqttV.packet.remaining_length = rl;
    MqttV.n = 1 + used;
    MqttV.ok = PROTO_TRUE;
}

// A PUBLISH variable header and payload: the Topic Name, the Packet Identifier when QoS is above 0,
// and the Payload that fills what the Remaining Length leaves (sec 3.3).
void protocore_mqtt_parse_publish(uint8_t *restrict work)
{
    (void)work;
    MqttV.ok = PROTO_FALSE;
    const uint8_t *buf = MqttV.buf.in;
    const uint32_t rl = MqttV.packet.remaining_length;
    if (!buf || rl < MQ_FIELD_PREFIX || !MqttV.message.topic_out)
    {
        return;
    }
    uint16_t tlen = get_u16(buf);
    size_t off = MQ_FIELD_PREFIX;
    if ((uint32_t)off + tlen > rl)
    {
        return;
    }
    if ((size_t)tlen + 1 > MqttV.message.topic_cap)
    {
        return; // the Topic Name and its NUL must fit
    }
    // sec 1.5.3: a UTF-8 encoded string must be well-formed (MQTT-1.5.3-1) and must not encode
    // U+0000 (MQTT-1.5.3-2).
    Utf8V.args.s = buf + off;
    Utf8V.args.n = tlen;
    Utf8.valid(work);
    if (!Utf8V.ok || mem.chr(buf + off, tlen, 0x00))
    {
        return;
    }
    mem.cpy(MqttV.message.topic_out, buf + off, tlen);
    MqttV.message.topic_out[tlen] = '\0';
    MqttV.message.topic_len = tlen;
    off += tlen;

    uint8_t qos = (uint8_t)((MqttV.packet.flags >> MQ_PUB_QOS_SHIFT) & MQ_PUB_QOS_MASK);
    if (qos == 3)
    {
        return; // MQTT-3.3.1-4: a PUBLISH MUST NOT have both QoS bits set
    }
    MqttV.message.qos = qos;
    MqttV.message.retain = (MqttV.packet.flags & MQ_PUB_RETAIN) != 0;
    MqttV.message.dup = (MqttV.packet.flags & MQ_PUB_DUP) != 0;
    MqttV.packet.packet_id = 0;
    if (qos > 0)
    {
        if ((uint32_t)off + MQ_FIELD_PREFIX > rl)
        {
            return;
        }
        MqttV.packet.packet_id = get_u16(buf + off);
        off += MQ_FIELD_PREFIX;
    }
    MqttV.message.payload = buf + off;
    MqttV.message.payload_len = rl - off;
    MqttV.ok = PROTO_TRUE;
}

// The Packet Identifier a PUBACK, PUBREC, PUBREL, PUBCOMP or UNSUBACK body carries (sec 2.3.1).
void protocore_mqtt_parse_ack(uint8_t *restrict work)
{
    (void)work;
    MqttV.packet.packet_id = 0;
    MqttV.ok = PROTO_FALSE;
    if (!MqttV.buf.in || MqttV.packet.remaining_length < MQ_ACK_REMAINING_LENGTH)
    {
        return;
    }
    MqttV.packet.packet_id = get_u16(MqttV.buf.in);
    MqttV.ok = PROTO_TRUE;
}

// A CONNACK body: the Connect Acknowledge Flags then the Connect Return code (sec 3.2.2).
void protocore_mqtt_parse_connack(uint8_t *restrict work)
{
    (void)work;
    MqttV.i32 = -1;
    MqttV.session_present = PROTO_FALSE;
    MqttV.ok = PROTO_FALSE;
    if (!MqttV.buf.in || MqttV.packet.remaining_length < MQ_ACK_REMAINING_LENGTH)
    {
        return;
    }
    MqttV.session_present = (MqttV.buf.in[0] & MQ_CONNACK_SESSION_PRESENT) != 0;
    MqttV.i32 = MqttV.buf.in[1];
    MqttV.ok = PROTO_TRUE;
}

// A SUBACK body: the Packet Identifier then the payload's return-code list (sec 3.9.2, sec 3.9.3).
void protocore_mqtt_parse_suback(uint8_t *restrict work)
{
    (void)work;
    MqttV.u8 = PROTOCORE_MQTT_SUBACK_FAILURE;
    MqttV.ok = PROTO_FALSE;
    if (!MqttV.buf.in || MqttV.packet.remaining_length < MQ_ACK_REMAINING_LENGTH + 1)
    {
        return;
    }
    MqttV.packet.packet_id = get_u16(MqttV.buf.in);
    MqttV.u8 = MqttV.buf.in[MQ_ACK_REMAINING_LENGTH];
    MqttV.ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Transport: one Network Connection to one Server (MQTT 3.1.1 sec 4.2)
// ---------------------------------------------------------------------------
#if PROTOCORE_HAS_NET_STACK

// The next Packet Identifier, skipping 0 because a real one is never 0 (sec 2.3.1).
static uint16_t next_packet_id(uint8_t *restrict work)
{
    uint16_t p = MQTT_CTX(work)->next_packet_id++;
    if (MQTT_CTX(work)->next_packet_id == 0)
    {
        MQTT_CTX(work)->next_packet_id = 1;
    }
    return p;
}

// Point the codec's buffers at this Client's own storage: the body assembles in rx, the whole
// Control Packet lands in tx.
static void bind_codec_buffers(uint8_t *restrict work)
{
    MqttV.buf.out = MQTT_CTX(work)->tx;
    MqttV.buf.cap = PROTOCORE_MQTT_BUF_SIZE;
    MqttV.buf.body = MQTT_CTX(work)->rx;
    MqttV.buf.body_cap = PROTOCORE_MQTT_BUF_SIZE;
}

// Take this module's storage on first use and hold it: one borrow from the secure pool's persistent
// end - the end no mark walks - reused for every packet. One borrow and not two, because each
// carries a block header rounded up to the arena alignment; the region is split at a stated offset,
// rx first and tx behind it.
static proto_bool mem_bind(uint8_t *restrict work)
{
    if (MQTT_CTX(work)->rx != NULL)
    {
        return PROTO_TRUE;
    }
    protocore_span region = secure.persist_span(2u * PROTOCORE_MQTT_BUF_SIZE);
    if (!span.ok(region))
    {
        return PROTO_FALSE;
    }
    MQTT_CTX(work)->rx = region.buf;
    MQTT_CTX(work)->tx = region.buf + PROTOCORE_MQTT_BUF_SIZE;
    return PROTO_TRUE;
}

// Send plaintext octets to the Server.
static proto_bool tx_plain(uint8_t *restrict work, const uint8_t *data, size_t len)
{
    TcpClientV.cid = MQTT_CTX(work)->cid;
    TcpClientV.io.data = data;
    TcpClientV.io.len = len;
    TcpClient.send(protocore_tcp_client_span());
    return TcpClientV.ok;
}

// Append what the transport holds to the reassembly buffer. One read: the transport already knows
// how much it has and the worker is calling across passes, so a loop here would only ask a socket
// that has nothing. A full buffer stops draining, which is the backpressure the peer sees.
static void fill_plain(uint8_t *restrict work)
{
    size_t room = PROTOCORE_MQTT_BUF_SIZE - MQTT_CTX(work)->rx_len;
    if (room == 0)
    {
        return;
    }
    TcpClientV.cid = MQTT_CTX(work)->cid;
    TcpClientV.io.buf = MQTT_CTX(work)->rx + MQTT_CTX(work)->rx_len;
    TcpClientV.io.cap = room;
    TcpClient.read(protocore_tcp_client_span());
    size_t n = TcpClientV.n;
    if (n == 0)
    {
        TcpClientV.cid = MQTT_CTX(work)->cid;
        TcpClient.is_closed(protocore_tcp_client_span());
        if (TcpClientV.ok)
        {
            MQTT_CTX(work)->closed = PROTO_TRUE;
        }
        return;
    }
    MQTT_CTX(work)->rx_len += n;
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
    TcpClientV.cid = s_mqtt.store->cid;
    TcpClientV.io.buf = buf;
    TcpClientV.io.cap = len;
    TcpClient.read(protocore_tcp_client_span());
    size_t n = TcpClientV.n;
    if (n == 0)
    {
        TcpClientV.cid = s_mqtt.store->cid;
        TcpClient.is_closed(protocore_tcp_client_span());
        return TcpClientV.ok ? 0 : PROTOCORE_PLATFORM_TLS_WANT_READ;
    }
    return (int)n;
}

// Append what the session has decrypted to the reassembly buffer. Same shape as fill_plain.
static void fill_tls(uint8_t *restrict work)
{
    size_t room = PROTOCORE_MQTT_BUF_SIZE - MQTT_CTX(work)->rx_len;
    if (room == 0)
    {
        return;
    }
    int n = protocore_tls_client_session_read(MQTT_CTX(work)->rx + MQTT_CTX(work)->rx_len, room);
    if (n <= 0)
    {
        if (n < 0)
        {
            MQTT_CTX(work)->closed = PROTO_TRUE;
        }
        return;
    }
    MQTT_CTX(work)->rx_len += (size_t)n;
}
#endif // PROTOCORE_ENABLE_MQTT_TLS

// Raise the flag over the Control Packet the codec just framed into tx. A packet offered while one
// is still going out is refused rather than overwriting it. This layer never reaches the wire.
static proto_bool tx_arm(uint8_t *restrict work, size_t len)
{
    if (MQTT_CTX(work)->tx_ready || len == 0)
    {
        return PROTO_FALSE;
    }
    MQTT_CTX(work)->tx_len = len;
    MQTT_CTX(work)->tx_off = 0;
    MQTT_CTX(work)->tx_ready = PROTO_TRUE;
    return PROTO_TRUE;
}

// Put the flagged packet on the wire. The worker owns this connection and the pool the packet sits
// in, so it moves the octets itself: what the transport takes now, the rest on a later pass, and the
// flag drops once the last octet is out.
static void tx_drain(uint8_t *restrict work)
{
    if (!MQTT_CTX(work)->tx_ready)
    {
        return;
    }
    size_t n = MQTT_CTX(work)->tx_len - MQTT_CTX(work)->tx_off;
#if PROTOCORE_ENABLE_MQTT_TLS
    if (MQTT_CTX(work)->use_tls)
    {
        int w = protocore_tls_client_session_write(MQTT_CTX(work)->tx + MQTT_CTX(work)->tx_off, n);
        if (w > 0)
        {
            MQTT_CTX(work)->tx_off += (size_t)w;
        }
    }
    else
#endif
        if (tx_plain(work, MQTT_CTX(work)->tx + MQTT_CTX(work)->tx_off, n))
    {
        MQTT_CTX(work)->tx_off += n;
    }
    if (MQTT_CTX(work)->tx_off >= MQTT_CTX(work)->tx_len)
    {
        MQTT_CTX(work)->last_tx_ms = Clock.ms;
        MQTT_CTX(work)->tx_ready = PROTO_FALSE;
    }
}

// Close the Network Connection (sec 4.2). A link still coming up is given up with the slot.
static void link_close(uint8_t *restrict work)
{
#if PROTOCORE_ENABLE_MQTT_TLS
    if (MQTT_CTX(work)->use_tls)
    {
        protocore_tls_client_session_end();
    }
#endif
    if (MQTT_CTX(work)->cid >= 0)
    {
        TcpClientV.cid = MQTT_CTX(work)->cid;
        TcpClient.close(protocore_tcp_client_span());
    }
    MQTT_CTX(work)->cid = -1;
    MQTT_CTX(work)->session_up = PROTO_FALSE;
    MQTT_CTX(work)->link = MQTT_LINK_IDLE;
}

// The in-flight slot running the exchange for this Packet Identifier, or -1.
static int inflight_find(uint8_t *restrict work, uint16_t packet_id)
{
    for (int i = 0; i < PROTOCORE_MQTT_MAX_INFLIGHT; i++)
    {
        if (MQTT_CTX(work)->inflight[i].state != MQTT_INFLIGHT_FREE &&
            MQTT_CTX(work)->inflight[i].packet_id == packet_id)
        {
            return i;
        }
    }
    return -1;
}

// Hold an inbound QoS 2 Packet Identifier from PUBREC until PUBCOMP, so a repeat of the same
// PUBLISH delivers the Application Message once (sec 4.3.3).
static void rx_id_add(uint8_t *restrict work, uint16_t packet_id)
{
    for (int i = 0; i < PROTOCORE_MQTT_RX_QOS2_SLOTS; i++)
    {
        if (MQTT_CTX(work)->rx_packet_id[i] == 0)
        {
            MQTT_CTX(work)->rx_packet_id[i] = packet_id;
            return;
        }
    }
}

static proto_bool rx_id_has(uint8_t *restrict work, uint16_t packet_id)
{
    for (int i = 0; i < PROTOCORE_MQTT_RX_QOS2_SLOTS; i++)
    {
        if (MQTT_CTX(work)->rx_packet_id[i] == packet_id)
        {
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

static void rx_id_del(uint8_t *restrict work, uint16_t packet_id)
{
    for (int i = 0; i < PROTOCORE_MQTT_RX_QOS2_SLOTS; i++)
    {
        if (MQTT_CTX(work)->rx_packet_id[i] == packet_id)
        {
            MQTT_CTX(work)->rx_packet_id[i] = 0;
        }
    }
}

// Frame one acknowledgement into tx and flag it for the worker (sec 3.4 - sec 3.7). False when a
// packet is already going out and this one was refused rather than overwriting it.
static proto_bool send_ack(uint8_t *restrict work, MqttType type, uint16_t packet_id)
{
    bind_codec_buffers(work);
    MqttV.packet.type = type;
    MqttV.packet.packet_id = packet_id;
    protocore_mqtt_build_ack(work);
    return tx_arm(work, MqttV.n);
}

// Act on one whole Control Packet sitting where the worker put it.
static void handle_packet(uint8_t *restrict work, const uint8_t *body, MqttType type, uint8_t flags, uint32_t rl)
{
    MqttV.buf.in = body;
    MqttV.packet.flags = flags;
    MqttV.packet.remaining_length = rl;

    switch (type)
    {
    case MQTT_CONNACK:
        protocore_mqtt_parse_connack(work);
        MQTT_CTX(work)->connack_code = MqttV.i32;
        if (MQTT_CTX(work)->connack_code == 0)
        {
            MQTT_CTX(work)->session_up = PROTO_TRUE;
        }
        MQ_DBG("[mqtt] CONNACK return code=%d\n", MQTT_CTX(work)->connack_code);
        break;

    case MQTT_PUBLISH: {
        MqttV.message.topic_out = MQTT_CTX(work)->topic;
        MqttV.message.topic_cap = sizeof(MQTT_CTX(work)->topic);
        protocore_mqtt_parse_publish(work);
        if (!MqttV.ok)
        {
            // MQTT-4.8.0-1: a protocol violation MUST close the Network Connection the offending
            // Control Packet arrived on. Both QoS bits set is one (MQTT-3.3.1-4).
            link_close(work);
            break;
        }
        uint16_t packet_id = MqttV.packet.packet_id;
        const uint8_t *payload = MqttV.message.payload;
        size_t payload_len = MqttV.message.payload_len;
        uint8_t qos = MqttV.message.qos;
        if (qos < 2)
        {
            if (MQTT_CTX(work)->on_message)
            {
                MQTT_CTX(work)->on_message(MQTT_CTX(work)->topic, payload, payload_len);
            }
            if (qos == 1)
            {
                (void)send_ack(work, MQTT_PUBACK, packet_id); // sec 4.3.2
            }
        }
        else // sec 4.3.3: deliver once, and hold the identifier until PUBREL completes
        {
            if (!rx_id_has(work, packet_id))
            {
                if (MQTT_CTX(work)->on_message)
                {
                    MQTT_CTX(work)->on_message(MQTT_CTX(work)->topic, payload, payload_len);
                }
                rx_id_add(work, packet_id);
            }
            (void)send_ack(work, MQTT_PUBREC, packet_id);
        }
        break;
    }

    case MQTT_PUBACK:  // our QoS 1 PUBLISH acknowledged (sec 4.3.2)
    case MQTT_PUBCOMP: // our QoS 2 PUBLISH complete (sec 4.3.3)
    {
        protocore_mqtt_parse_ack(work);
        int slot = inflight_find(work, MqttV.packet.packet_id);
        if (slot >= 0)
        {
            MQTT_CTX(work)->inflight[slot].state = MQTT_INFLIGHT_FREE;
        }
        break;
    }

    case MQTT_PUBREC: // our QoS 2 PUBLISH received: answer PUBREL, await PUBCOMP (sec 4.3.3)
    {
        protocore_mqtt_parse_ack(work);
        uint16_t packet_id = MqttV.packet.packet_id;
        int slot = inflight_find(work, packet_id);
        if (slot >= 0)
        {
            MQTT_CTX(work)->inflight[slot].state = MQTT_INFLIGHT_COMP;
            MQTT_CTX(work)->inflight[slot].sent_ms = Clock.ms;
        }
        (void)send_ack(work, MQTT_PUBREL, packet_id);
        break;
    }

    case MQTT_PUBREL: // the Server releasing an inbound QoS 2 message: answer PUBCOMP (sec 4.3.3)
    {
        protocore_mqtt_parse_ack(work);
        uint16_t packet_id = MqttV.packet.packet_id;
        rx_id_del(work, packet_id);
        (void)send_ack(work, MQTT_PUBCOMP, packet_id);
        break;
    }

    case MQTT_PINGRESP: // sec 3.13
        MQTT_CTX(work)->ping_pending = PROTO_FALSE;
        break;

    case MQTT_SUBACK:
        protocore_mqtt_parse_suback(work);
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
static void process_rx(uint8_t *restrict work)
{
#if PROTOCORE_ENABLE_MQTT_TLS
    if (MQTT_CTX(work)->use_tls)
    {
        fill_tls(work);
    }
    else
#endif
        fill_plain(work);

    size_t off = 0;
    for (;;)
    {
        MqttV.buf.in = MQTT_CTX(work)->rx + off;
        MqttV.buf.avail = MQTT_CTX(work)->rx_len - off;
        protocore_mqtt_parse_fixed_header(work);
        if (!MqttV.ok)
        {
            break; // the fixed header is not all here yet
        }
        size_t header_len = MqttV.n;
        MqttType type = MqttV.packet.type;
        uint8_t flags = MqttV.packet.flags;
        uint32_t rl = MqttV.packet.remaining_length;
        size_t total = header_len + rl;
        if (MQTT_CTX(work)->rx_len - off < total)
        {
            break; // the body is not all here yet
        }
        handle_packet(work, MQTT_CTX(work)->rx + off + header_len, type, flags, rl);
        off += total;
    }
    if (off != 0)
    {
        MQTT_CTX(work)->rx_len -= off;
        if (MQTT_CTX(work)->rx_len != 0)
        {
            // The tail overlaps what it moves onto, so this is the overlapping move.
            mem.move(MQTT_CTX(work)->rx, MQTT_CTX(work)->rx + off, MQTT_CTX(work)->rx_len);
        }
    }
}

// Step the Network Connection one stage per call. Nothing here waits: the transport slot, the
// handshake and the CONNACK each report where they are and the caller comes back on its own tick.
static void link_step(uint8_t *restrict work)
{
    if (MQTT_CTX(work)->closed || (Clock.ms - MQTT_CTX(work)->timer) >= MQTT_CTX(work)->link_budget_ms)
    {
        link_close(work);
        return;
    }
    switch (MQTT_CTX(work)->link)
    {
    case MQTT_LINK_TCP:
        TcpClientV.cid = MQTT_CTX(work)->cid;
        TcpClient.connected(protocore_tcp_client_span());
        if (!TcpClientV.ok)
        {
            return;
        }
#if PROTOCORE_ENABLE_MQTT_TLS
        if (MQTT_CTX(work)->use_tls)
        {
            MQTT_CTX(work)->link = MQTT_LINK_TLS;
            return;
        }
#endif
        MQTT_CTX(work)->link = MQTT_LINK_CONNACK;
        if (!tx_arm(work, MQTT_CTX(work)->tx_len)) // CONNECT is already framed in tx; this flags it
        {
            link_close(work);
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
            link_close(work);
            return;
        }
        MQTT_CTX(work)->link = MQTT_LINK_CONNACK;
        if (!tx_arm(work, MQTT_CTX(work)->tx_len)) // CONNECT is already framed in tx; this flags it
        {
            link_close(work);
        }
    }
#endif
        return;

    case MQTT_LINK_CONNACK:
        process_rx(work);
        if (MQTT_CTX(work)->session_up)
        {
            MQTT_CTX(work)->link = MQTT_LINK_IDLE;
            MQTT_CTX(work)->last_tx_ms = Clock.ms;
        }
        else if (MQTT_CTX(work)->connack_code >= 0)
        {
            link_close(work); // the Server answered and refused (sec 3.2.2.3)
        }
        return;

    case MQTT_LINK_IDLE:
        return;
    }
}

// ---------------------------------------------------------------------------
// Transport: the calls
// ---------------------------------------------------------------------------

void protocore_mqtt_on_message(uint8_t *restrict work)
{
    MQTT_CTX(work)->on_message = MqttV.delivery.on_message;
    MqttV.ok = PROTO_TRUE;
}

void protocore_mqtt_connect(uint8_t *restrict work)
{
    MqttV.ok = PROTO_FALSE;
    if (!MqttV.server.host || !MqttV.session.client_id)
    {
        return;
    }
#if !PROTOCORE_ENABLE_MQTT_TLS
    if (MqttV.server.use_tls)
    {
        return; // built without mqtts:// support
    }
#endif
    if (!mem_bind(work))
    {
        return; // no storage: fail closed rather than build into nothing
    }

    // Every session-scoped field starts over.
    mem.set(MQTT_CTX(work)->inflight, 0, sizeof(MQTT_CTX(work)->inflight));
    mem.set(MQTT_CTX(work)->rx_packet_id, 0, sizeof(MQTT_CTX(work)->rx_packet_id));
    MQTT_CTX(work)->rx_len = 0;
    MQTT_CTX(work)->tx_len = 0;
    MQTT_CTX(work)->tx_off = 0;
    MQTT_CTX(work)->tx_ready = PROTO_FALSE;
    MQTT_CTX(work)->closed = PROTO_FALSE;
    MQTT_CTX(work)->session_up = PROTO_FALSE;
    MQTT_CTX(work)->ping_pending = PROTO_FALSE;
    MQTT_CTX(work)->connack_code = -1;
    MQTT_CTX(work)->keep_alive = MqttV.session.keep_alive;
    MQTT_CTX(work)->use_tls = MqttV.server.use_tls;

    // Frame CONNECT now, while the caller's session and will members still hold: the payload
    // assembles in rx and the whole packet lands in tx, where it waits for the link.
    bind_codec_buffers(work);
    protocore_mqtt_build_connect(work);
    if (MqttV.n == 0)
    {
        return;
    }
    MQTT_CTX(work)->tx_len = MqttV.n;

    MQTT_CTX(work)->link_budget_ms = PROTOCORE_MQTT_CONNECT_MS;
    TcpClientV.dial.host = MqttV.server.host;
    TcpClientV.dial.port = MqttV.server.port;
    TcpClientV.dial.timeout_ms = MQTT_CTX(work)->link_budget_ms;
    TcpClient.open(protocore_tcp_client_span());
    MQTT_CTX(work)->cid = TcpClientV.i32;
    if (MQTT_CTX(work)->cid < 0)
    {
        return;
    }

#if PROTOCORE_ENABLE_MQTT_TLS
    // Bind the session here, while host is still in scope. Its BIO reads the transport slot, so
    // nothing moves until the loop starts stepping the handshake.
    if (MQTT_CTX(work)->use_tls && !protocore_tls_client_session_begin(MqttV.server.host, tls_send, tls_recv))
    {
        link_close(work);
        return;
    }
#endif

    MQTT_CTX(work)->link = MQTT_LINK_TCP;
    MQTT_CTX(work)->timer = Clock.ms;
    MqttV.ok = PROTO_TRUE; // started, not connected: step the loop and read connected()
}

void protocore_mqtt_publish(uint8_t *restrict work)
{
    MqttV.ok = PROTO_FALSE;
    if (!MQTT_CTX(work)->session_up || MqttV.message.qos > 2)
    {
        return;
    }
    bind_codec_buffers(work);
    MqttV.message.dup = PROTO_FALSE;

    if (MqttV.message.qos == 0)
    {
        MqttV.packet.packet_id = 0;
        protocore_mqtt_build_publish(work);
        MqttV.ok = MqttV.n != 0 && tx_arm(work, MqttV.n);
        return;
    }

    // QoS 1 and QoS 2 take an in-flight slot. The PUBLISH itself stays in tx - a re-delivery rewinds
    // the worker to the start of it - so the slot records only what identifies and times the
    // exchange (sec 4.3.2, sec 4.3.3).
    int slot = -1;
    for (int i = 0; i < PROTOCORE_MQTT_MAX_INFLIGHT; i++)
    {
        if (MQTT_CTX(work)->inflight[i].state == MQTT_INFLIGHT_FREE)
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
    {
        return; // the in-flight window is full
    }
    uint16_t packet_id = next_packet_id(work);
    MqttV.packet.packet_id = packet_id;
    protocore_mqtt_build_publish(work);
    if (MqttV.n == 0)
    {
        return;
    }
    MQTT_CTX(work)->inflight[slot].packet_id = packet_id;
    MQTT_CTX(work)->inflight[slot].state = MQTT_INFLIGHT_ACK;
    MQTT_CTX(work)->inflight[slot].len = MqttV.n;
    MQTT_CTX(work)->inflight[slot].sent_ms = Clock.ms;
    MqttV.ok = tx_arm(work, MqttV.n);
}

void protocore_mqtt_subscribe(uint8_t *restrict work)
{
    MqttV.ok = PROTO_FALSE;
    if (!MQTT_CTX(work)->session_up)
    {
        return;
    }
    bind_codec_buffers(work);
    MqttV.packet.packet_id = next_packet_id(work);
    protocore_mqtt_build_subscribe(work);
    MqttV.ok = MqttV.n != 0 && tx_arm(work, MqttV.n);
}

void protocore_mqtt_unsubscribe(uint8_t *restrict work)
{
    MqttV.ok = PROTO_FALSE;
    if (!MQTT_CTX(work)->session_up)
    {
        return;
    }
    bind_codec_buffers(work);
    MqttV.packet.packet_id = next_packet_id(work);
    protocore_mqtt_build_unsubscribe(work);
    MqttV.ok = MqttV.n != 0 && tx_arm(work, MqttV.n);
}

void protocore_mqtt_loop(uint8_t *restrict work)
{
    // A connect still coming up takes one step per call, and nothing below it runs until the Server
    // has answered: this is the tick the connect hands the link to.
    if (MQTT_CTX(work)->link != MQTT_LINK_IDLE)
    {
        tx_drain(work); // CONNECT is framed and flagged from the first step; carry it out
        link_step(work);
        MqttV.ok = MQTT_CTX(work)->session_up;
        return;
    }
    MqttV.ok = PROTO_FALSE;
    if (!MQTT_CTX(work)->session_up)
    {
        return;
    }
    tx_drain(work); // whatever the codec flagged last pass goes out before more arrives
    process_rx(work);
    if (MQTT_CTX(work)->closed)
    {
        link_close(work);
        return;
    }
    if (!MQTT_CTX(work)->session_up)
    {
        return; // a protocol violation in the batch above already closed it (MQTT-4.8.0-1)
    }

    uint32_t now = Clock.ms;

    // Keep Alive (sec 3.1.2.10): send PINGREQ once the interval has passed with nothing sent
    // (sec 3.12), and close the Network Connection when no PINGRESP comes back within another
    // interval, which sec 3.1.2.10 says a Client SHOULD do after a reasonable time (sec 3.13).
    if (MQTT_CTX(work)->keep_alive)
    {
        uint32_t ka = (uint32_t)MQTT_CTX(work)->keep_alive * 1000u;
        if (MQTT_CTX(work)->ping_pending && (now - MQTT_CTX(work)->ping_sent_ms) > ka)
        {
            link_close(work);
            return;
        }
        if (!MQTT_CTX(work)->ping_pending && (now - MQTT_CTX(work)->last_tx_ms) >= ka)
        {
            bind_codec_buffers(work);
            protocore_mqtt_build_pingreq(work);
            if (tx_arm(work, MqttV.n))
            {
                MQTT_CTX(work)->ping_pending = PROTO_TRUE;
                MQTT_CTX(work)->ping_sent_ms = now;
            }
        }
    }

    // Re-deliver unacknowledged in-flight QoS 1 and QoS 2 messages (sec 4.3.2, sec 4.3.3). The
    // PUBLISH is still in tx, so a resend sets DUP where it lies (MQTT-3.3.1-1) and rewinds the
    // worker to the start of it; a QoS 2 exchange past PUBREC resends PUBREL instead
    // (MQTT-4.3.3-1 forbids resending the PUBLISH once PUBREL has gone).
    for (int i = 0; i < PROTOCORE_MQTT_MAX_INFLIGHT; i++)
    {
        if (MQTT_CTX(work)->inflight[i].state == MQTT_INFLIGHT_FREE)
        {
            continue;
        }
        if ((now - MQTT_CTX(work)->inflight[i].sent_ms) < PROTOCORE_MQTT_RETRANSMIT_MS)
        {
            continue;
        }
        if (MQTT_CTX(work)->inflight[i].state == MQTT_INFLIGHT_ACK)
        {
            // One packet is in flight at a time, so tx holds that PUBLISH.
            MQTT_CTX(work)->tx[0] |= MQ_PUB_DUP;
            if (!tx_arm(work, MQTT_CTX(work)->inflight[i].len))
            {
                continue; // still going out; the next pass retries
            }
        }
        else if (!send_ack(work, MQTT_PUBREL, MQTT_CTX(work)->inflight[i].packet_id))
        {
            continue; // still going out; the next pass retries
        }
        MQTT_CTX(work)->inflight[i].sent_ms = now;
    }
    MqttV.ok = PROTO_TRUE;
}

void protocore_mqtt_connected(uint8_t *restrict work)
{
    MqttV.ok = MQTT_CTX(work)->session_up;
}

void protocore_mqtt_disconnect(uint8_t *restrict work)
{
    if (MQTT_CTX(work)->cid >= 0 && MQTT_CTX(work)->session_up)
    {
        bind_codec_buffers(work);
        protocore_mqtt_build_disconnect(work);
        (void)tx_arm(work, MqttV.n);
    }
    link_close(work);
    MqttV.ok = PROTO_TRUE;
}

#else // no network stack: the codec still builds, the transport refuses

void protocore_mqtt_on_message(uint8_t *restrict work)
{
    (void)work;
    MqttV.ok = PROTO_FALSE;
}
void protocore_mqtt_connect(uint8_t *restrict work)
{
    (void)work;
    MqttV.ok = PROTO_FALSE;
}
void protocore_mqtt_publish(uint8_t *restrict work)
{
    (void)work;
    MqttV.ok = PROTO_FALSE;
}
void protocore_mqtt_subscribe(uint8_t *restrict work)
{
    (void)work;
    MqttV.ok = PROTO_FALSE;
}
void protocore_mqtt_unsubscribe(uint8_t *restrict work)
{
    (void)work;
    MqttV.ok = PROTO_FALSE;
}
void protocore_mqtt_loop(uint8_t *restrict work)
{
    (void)work;
    MqttV.ok = PROTO_FALSE;
}
void protocore_mqtt_connected(uint8_t *restrict work)
{
    (void)work;
    MqttV.ok = PROTO_FALSE;
}
void protocore_mqtt_disconnect(uint8_t *restrict work)
{
    (void)work;
    MqttV.ok = PROTO_FALSE;
}

#endif // PROTOCORE_HAS_NET_STACK

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
MqttVars MqttV;

#endif // PROTOCORE_ENABLE_MQTT
