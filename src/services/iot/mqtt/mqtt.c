// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mqtt.c
 * @brief MQTT 3.1.1 packet codec (host-testable) + the raw-lwIP / mbedTLS
 *        persistent client transport (ESP32 only).
 */

#include "services/iot/mqtt/mqtt.h"
#include "mmgr/protomem.h"
#include "mmgr/rawmemcpy.h"     // raw.read: a partial packet shifts to the front of rx
#include "mmgr/secure.h"        // protocore_secure_persist_span: this module's storage
#include "server/clock/clock.h" // protocore_millis: the link timer and the keep-alive read it

#if PROTOCORE_ENABLE_MQTT

#include "shared/utf8/utf8.h"

// ---------------------------------------------------------------------------
// Pure codec (host-testable)
// ---------------------------------------------------------------------------

// Big-endian 16-bit helpers and a length-prefixed UTF-8 string writer.
#if PROTOCORE_HAS_NET_STACK
#include "network_drivers/transport/tcp/tcp.h" // shared outbound TCP client (L4)
#endif
#if PROTOCORE_HAS_VENDOR_TLS && PROTOCORE_ENABLE_MQTT_TLS
#include "network_drivers/tls/tls.h" // persistent client TLS session (csess)
#endif
static inline void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)(v & 0xFF);
}
static inline uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}
// Write a 2-byte length + the bytes; returns total bytes written (2 + len).
static size_t put_field(uint8_t *p, const uint8_t *data, size_t len)
{
    put_u16(p, (uint16_t)len);
    if (len)
    {
        mem.cpy(p + 2, data, len);
    }
    return 2 + len;
}
static inline size_t put_str(uint8_t *p, const char *s)
{
    return put_field(
        p, (const uint8_t *)s,
        s ? strnlen(s, PROTOCORE_MQTT_BUF_SIZE) // "MQTT" literal, or client_id/will_topic/user/pass already
          : 0); // guarded non-null by protocore_mqtt_build_connect before calling) is non-null
}

size_t protocore_mqtt_encode_remlen(uint8_t *out, uint32_t len)
{
    if (len > 268435455u) // 4 * 7 bits
    {
        return 0;
    }
    size_t n = 0;
    do
    {
        uint8_t byte = (uint8_t)(len % 128);
        len /= 128;
        if (len > 0)
        {
            byte |= 0x80;
        }
        out[n++] = byte;
    } while (len > 0);
    return n;
}

proto_bool protocore_mqtt_decode_remlen(const uint8_t *buf, size_t avail, uint32_t *value, size_t *used)
{
    uint32_t v = 0;
    uint32_t mult = 1;
    size_t i = 0;
    for (; i < 4; i++)
    {
        if (i >= avail)
        {
            return PROTO_FALSE; // incomplete
        }
        uint8_t b = buf[i];
        v += (uint32_t)(b & 0x7F) * mult;
        if ((b & 0x80) == 0)
        {
            *value = v;
            *used = i + 1;
            return PROTO_TRUE;
        }
        mult *= 128;
    }
    return PROTO_FALSE; // malformed (5th continuation byte)
}

// Assemble a packet: fixed header byte0 + remaining-length, then a pre-built
// variable-header+payload body of vlen bytes already placed at out + (header).
// Returns total length or 0 if it will not fit cap. The body must be written by
// the caller into a scratch first; here we shift it after the header is known.
// To avoid a second buffer we build the body at a fixed offset (max 4-byte
// remlen) and memmove it tight - simpler: build body in `body`, then compose.
static size_t compose(uint8_t *out, size_t cap, uint8_t byte0, const uint8_t *body, size_t blen)
{
    uint8_t rl[4];
    // Every caller pre-builds body in body[PROTOCORE_MQTT_BUF_SIZE] (1024), so blen is bounded far below
    // the 2^28 remaining-length limit and protocore_mqtt_encode_remlen never rejects here; the len > 256MB reject
    // is covered directly on the public protocore_mqtt_encode_remlen.
    size_t rln = protocore_mqtt_encode_remlen(rl, (uint32_t)blen);
    if (rln == 0)
    // bounded callers
    {
        return 0;
    }
    size_t total = 1 + rln + blen;
    if (total > cap)
    {
        return 0;
    }
    out[0] = byte0;
    mem.cpy(out + 1, rl, rln);
    if (blen)
    // (build_connect/publish/subscribe/unsubscribe) always writes at least a 2-byte length-prefixed field
    {
        mem.cpy(out + 1 + rln, body, blen);
    }
    return total;
}

size_t protocore_mqtt_build_connect(uint8_t *out, size_t cap, const MqttConnectOpts *opts, uint8_t *body,
                                    size_t body_cap)
{
    if (!out || !opts || !opts->client_id)
    {
        return 0;
    }
    size_t n = 0;
    // Variable header: protocol name + level + flags + keep-alive.
    n += put_str(body + n, "MQTT");
    body[n++] = 0x04; // protocol level 4 (MQTT 3.1.1)

    uint8_t flags = 0;
    if (opts->clean_session)
    {
        flags |= 0x02;
    }
    if (opts->will_topic)
    {
        flags |= 0x04;                                    // will flag
        flags |= (uint8_t)((opts->will_qos & 0x03) << 3); // will QoS
        if (opts->will_retain)
        {
            flags |= 0x20;
        }
    }
    if (opts->user)
    {
        flags |= 0x80;
    }
    if (opts->pass)
    {
        flags |= 0x40;
    }
    body[n++] = flags;
    put_u16(body + n, opts->keepalive_s);
    n += 2;

    // Payload: client id, [will topic, will msg], [user], [pass]. Bounds-check
    // each field against the body scratch as we go.
    size_t need = 2 + strnlen(opts->client_id, PROTOCORE_MQTT_BUF_SIZE);
    if (opts->will_topic)
    {
        need += 2 + strnlen(opts->will_topic, PROTOCORE_MQTT_BUF_SIZE) + 2 + opts->will_len;
    }
    if (opts->user)
    {
        need += 2 + strnlen(opts->user, PROTOCORE_MQTT_BUF_SIZE);
    }
    if (opts->pass)
    {
        need += 2 + strnlen(opts->pass, PROTOCORE_MQTT_BUF_SIZE);
    }
    if (n + need > body_cap)
    {
        return 0;
    }

    n += put_str(body + n, opts->client_id);
    if (opts->will_topic)
    {
        n += put_str(body + n, opts->will_topic);
        n += put_field(body + n, opts->will_msg, opts->will_len);
    }
    if (opts->user)
    {
        n += put_str(body + n, opts->user);
    }
    if (opts->pass)
    {
        n += put_str(body + n, opts->pass);
    }

    return compose(out, cap, (uint8_t)((uint8_t)MQTT_CONNECT << 4), body, n);
}

size_t protocore_mqtt_build_publish(uint8_t *out, size_t cap, const char *topic, const uint8_t *payload,
                                    size_t payload_len, uint8_t qos, uint16_t packet_id, proto_bool retain,
                                    proto_bool dup, uint8_t *body, size_t body_cap)
{
    if (!out || !topic || qos > 2)
    {
        return 0;
    }
    // MQTT-3.3.2-2: a PUBLISH Topic Name MUST NOT contain wildcard characters
    // (subscribe topic *filters* may, so this check is publish-only).
    for (const char *t = topic; *t; t++)
    {
        if (*t == '+' || *t == '#')
        {
            return 0;
        }
    }
    size_t tlen = strnlen(topic, PROTOCORE_MQTT_BUF_SIZE);
    size_t blen = 2 + tlen + (qos > 0 ? 2 : 0) + payload_len;
    if (blen > body_cap)
    {
        return 0;
    }
    size_t n = 0;
    n += put_field(body + n, (const uint8_t *)topic, tlen);
    if (qos > 0)
    {
        put_u16(body + n, packet_id);
        n += 2;
    }
    if (payload_len)
    {
        mem.cpy(body + n, payload, payload_len);
    }
    n += payload_len;

    uint8_t f = (uint8_t)((qos & 0x03) << 1);
    if (retain)
    {
        f |= 0x01;
    }
    if (dup)
    {
        f |= 0x08;
    }
    return compose(out, cap, (uint8_t)(((uint8_t)MQTT_PUBLISH << 4) | f), body, n);
}

size_t protocore_mqtt_build_subscribe(uint8_t *out, size_t cap, uint16_t packet_id, const char *topic, uint8_t qos,
                                      uint8_t *body, size_t body_cap)
{
    if (!out || !topic || qos > 2)
    {
        return 0;
    }
    size_t tlen = strnlen(topic, PROTOCORE_MQTT_BUF_SIZE);
    size_t blen = 2 + 2 + tlen + 1;
    if (blen > body_cap)
    {
        return 0;
    }
    size_t n = 0;
    put_u16(body + n, packet_id);
    n += 2;
    n += put_field(body + n, (const uint8_t *)topic, tlen);
    body[n++] = (uint8_t)(qos & 0x03);
    return compose(out, cap, (uint8_t)(((uint8_t)MQTT_SUBSCRIBE << 4) | 0x02), body,
                   n); // SUBSCRIBE flags = 0010
}

size_t protocore_mqtt_build_unsubscribe(uint8_t *out, size_t cap, uint16_t packet_id, const char *topic, uint8_t *body,
                                        size_t body_cap)
{
    if (!out || !topic)
    {
        return 0;
    }
    size_t tlen = strnlen(topic, PROTOCORE_MQTT_BUF_SIZE);
    size_t blen = 2 + 2 + tlen;
    if (blen > body_cap)
    {
        return 0;
    }
    size_t n = 0;
    put_u16(body + n, packet_id);
    n += 2;
    n += put_field(body + n, (const uint8_t *)topic, tlen);
    return compose(out, cap, (uint8_t)(((uint8_t)MQTT_UNSUBSCRIBE << 4) | 0x02), body,
                   n); // UNSUBSCRIBE flags = 0010
}

size_t protocore_mqtt_build_ack(uint8_t *out, size_t cap, MqttType type, uint16_t packet_id)
{
    if (!out || cap < 4)
    {
        return 0;
    }
    uint8_t f = (type == MQTT_PUBREL) ? 0x02 : 0x00; // PUBREL requires flags 0010
    out[0] = (uint8_t)(((uint8_t)type << 4) | f);
    out[1] = 0x02;
    put_u16(out + 2, packet_id);
    return 4;
}

size_t protocore_mqtt_build_pingreq(uint8_t *out, size_t cap)
{
    if (!out || cap < 2)
    {
        return 0;
    }
    out[0] = (uint8_t)((uint8_t)MQTT_PINGREQ << 4);
    out[1] = 0x00;
    return 2;
}

size_t protocore_mqtt_build_disconnect(uint8_t *out, size_t cap)
{
    if (!out || cap < 2)
    {
        return 0;
    }
    out[0] = (uint8_t)((uint8_t)MQTT_DISCONNECT << 4);
    out[1] = 0x00;
    return 2;
}

proto_bool protocore_mqtt_parse_fixed_header(const uint8_t *buf, size_t avail, uint8_t *type, uint8_t *flags,
                                             uint32_t *remaining_len, size_t *header_len)
{
    if (avail < 2)
    {
        return PROTO_FALSE;
    }
    uint32_t rl;
    size_t used;
    if (!protocore_mqtt_decode_remlen(buf + 1, avail - 1, &rl, &used))
    {
        return PROTO_FALSE;
    }
    *type = (uint8_t)(buf[0] >> 4);
    *flags = (uint8_t)(buf[0] & 0x0F);
    *remaining_len = rl;
    *header_len = 1 + used;
    return PROTO_TRUE;
}

proto_bool protocore_mqtt_parse_publish(const uint8_t *buf, uint32_t remaining_len, uint8_t flags, char *topic_out,
                                        size_t topic_cap, size_t *topic_len, const uint8_t **payload,
                                        size_t *payload_len, uint16_t *packet_id)
{
    if (!buf || remaining_len < 2)
    {
        return PROTO_FALSE;
    }
    uint16_t tlen = get_u16(buf);
    size_t off = 2;
    if ((uint32_t)off + tlen > remaining_len)
    {
        return PROTO_FALSE;
    }
    if ((size_t)tlen + 1 > topic_cap)
    {
        return PROTO_FALSE; // topic + NUL must fit
    }
    // MQTT 1.5.3: a UTF-8 string must be well-formed and must not contain U+0000.
    if (!protocore_utf8_valid(buf + off, tlen) || memchr(buf + off, 0x00, tlen))
    {
        return PROTO_FALSE;
    }
    mem.cpy(topic_out, buf + off, tlen);
    topic_out[tlen] = '\0';
    *topic_len = tlen;
    off += tlen;

    uint8_t qos = (uint8_t)((flags >> 1) & 0x03);
    if (qos == 3)
    {
        return PROTO_FALSE; // MQTT-3.3.1-4: a PUBLISH MUST NOT have both QoS bits set (malformed)
    }
    *packet_id = 0;
    if (qos > 0)
    {
        if ((uint32_t)off + 2 > remaining_len)
        {
            return PROTO_FALSE;
        }
        *packet_id = get_u16(buf + off);
        off += 2;
    }
    *payload = buf + off;
    *payload_len = remaining_len - off;
    return PROTO_TRUE;
}

uint16_t protocore_mqtt_parse_ack(const uint8_t *buf, uint32_t remaining_len)
{
    if (!buf || remaining_len < 2)
    {
        return 0;
    }
    return get_u16(buf);
}

int protocore_mqtt_parse_connack(const uint8_t *buf, uint32_t remaining_len, proto_bool *session_present)
{
    if (!buf || remaining_len < 2)
    {
        return -1;
    }
    if (session_present)
    {
        *session_present = (buf[0] & 0x01) != 0;
    }
    return buf[1];
}

proto_bool protocore_mqtt_parse_suback(const uint8_t *buf, uint32_t remaining_len, uint16_t *packet_id,
                                       uint8_t *return_code)
{
    if (!buf || remaining_len < 3)
    {
        return PROTO_FALSE;
    }
    if (packet_id)
    {
        *packet_id = get_u16(buf);
    }
    if (return_code)
    {
        *return_code = buf[2];
    }
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Transport (ESP32 only): persistent raw-lwIP TCP client + QoS state machine,
// with mqtts:// over a persistent client TLS session (protocore_tls csess).
// ---------------------------------------------------------------------------
#if PROTOCORE_HAS_NET_STACK

#ifdef PROTOCORE_MQTT_DEBUG
#define MQ_DBG(...) printf(__VA_ARGS__)
#else
#define MQ_DBG(...) ((void)0)
#endif

/** @brief Where an outbound QoS 1/2 exchange has got to. */
typedef enum PROTO_ENUM_PACKED
{
    MQ_INFLIGHT_FREE = 0, ///< the slot holds no exchange
    MQ_INFLIGHT_ACK,      ///< sent, awaiting PUBACK (QoS 1) or PUBREC (QoS 2)
    MQ_INFLIGHT_COMP,     ///< PUBREC seen, awaiting PUBCOMP (QoS 2)
} MqInflightState;

// One outbound QoS 1/2 exchange. The packet stays in tx - a retransmit rewinds the worker to the
// start of it - so this records what identifies and times the exchange, not the bytes.
typedef struct
{
    uint16_t pid; // MQTT packet identifier: the wire field is two bytes
    MqInflightState state;
    uint32_t sent_ms;
    size_t len; // bounded by PROTOCORE_MQTT_BUF_SIZE, which a build can raise past a 16-bit field
} MqttInflight;

/** @brief How far the link to the broker has come up. Anything but IDLE means a connect is in flight. */
typedef enum PROTO_ENUM_PACKED
{
    MQ_LINK_IDLE = 0, ///< no connect in flight
    MQ_LINK_TCP,      ///< the transport slot is coming up
    MQ_LINK_TLS,      ///< the TLS handshake advances one flight per step
    MQ_LINK_CONNACK,  ///< CONNECT is on the wire, its answer has not arrived
} MqLinkState;

// All MQTT connection state, owned by one instance (internal linkage): one broker at a time,
// all static / no heap. Grouped so it is one named owner, unreachable from any other TU.
typedef struct
{
    MqttMessageCb cb;
    int cid;           // outbound connection id (protocore_client pool)
    proto_bool closed; // peer closed / error (set when the pump sees it)

    // The module's one timer and where the connect has got to. protocore_mqtt_connect() sets these and
    // returns; protocore_mqtt_loop() steps the link and gives it up once the timer passes link_budget_ms.
    MqLinkState link;
    uint32_t timer;
    uint32_t link_budget_ms;

    // Receive reassembly: a packet may arrive across TCP segments.
    uint8_t *rx;   ///< Receive buffer. Null until the first packet.
    size_t rx_len; ///< Bytes currently in rx.

    // One finished packet waiting for a worker to put it on the wire. The codec frames into tx and
    // raises tx_ready; the worker sends from tx_off and lowers the flag when the last byte is out.
    // The codec never reaches the wire itself.
    //
    // Both buffers are taken once from the pool's persistent end - the end no mark walks - and
    // reused for every packet. Releasing per packet would wipe them each time, and the mark end
    // cannot hold them anyway: mark/release is a bump discipline, so this module's release would
    // reclaim another borrow. Secure for mqtts, whose bytes are session plaintext and whose reclaim
    // wipes; plaintext for mqtt.
    uint8_t *tx;         ///< The wire buffer. Null until the first packet.
    size_t tx_len;       ///< Bytes of the framed packet.
    size_t tx_off;       ///< Bytes already put on the wire.
    proto_bool tx_ready; ///< A packet is framed and waiting for a worker.

    proto_bool use_tls; // mqtts:// mode

    proto_bool mqtt_up;
    uint16_t keepalive_s;
    uint32_t last_tx_ms;
    proto_bool ping_pending;
    uint32_t ping_sent_ms;
    uint16_t next_pid;
    int connack_code; // set by process_rx during the connect handshake

    MqttInflight inflight[PROTOCORE_MQTT_MAX_INFLIGHT];
    // Inbound QoS 2 packet ids that have been PUBREC'd and await PUBREL (0 = empty).
    uint16_t rx_qos2[PROTOCORE_MQTT_RX_QOS2_SLOTS];
} MqttCtx;
static MqttCtx s_mqtt = {.cid = -1, .next_pid = 1};

static uint16_t next_pid()
{
    uint16_t p = s_mqtt.next_pid++;
    if (s_mqtt.next_pid == 0)
    {
        s_mqtt.next_pid = 1;
    }
    return p;
}

// The packet lands at rx_off and stays there until it is whole, so there is no wrap to compute and
// nothing to copy out of a ring: handle_packet reads it where the worker put it.

// --- transport over the shared outbound client (protocore_client) ---

// Send raw plaintext bytes to the broker.
static proto_bool mq_tx_plain(const uint8_t *data, size_t len)
{
    return Tcp.client->send(s_mqtt.cid, data, len);
}

// Append what the transport has to the reassembly buffer. One read: the transport already knows how
// much it holds, and the worker is calling across passes, so a loop here would only ask a socket
// that has nothing. A full buffer stops draining, which is the backpressure the peer sees.
static void mq_fill_plain(void)
{
    size_t room = PROTOCORE_MQTT_BUF_SIZE - s_mqtt.rx_len;
    if (room == 0)
    {
        return;
    }
    size_t n = Tcp.client->read(s_mqtt.cid, s_mqtt.rx + s_mqtt.rx_len, room);
    if (n == 0)
    {
        if (Tcp.client->is_closed(s_mqtt.cid))
        {
            s_mqtt.closed = PROTO_TRUE;
        }
        return;
    }
    s_mqtt.rx_len += n;
}

#if PROTOCORE_ENABLE_MQTT_TLS
// TLS BIO over the shared client: write ciphertext through the pool, read
// ciphertext by draining the client's wire ring.
static int mq_tls_send(void *ctx, const unsigned char *buf, size_t len)
{
    (void)ctx;
    size_t cap = len > 0xFFFF ? 0xFFFF : len;
    return Tcp.client->send(s_mqtt.cid, buf, cap) ? (int)cap : PROTOCORE_PLATFORM_TLS_WANT_WRITE;
}
static int mq_tls_recv(void *ctx, unsigned char *buf, size_t len)
{
    (void)ctx;
    size_t n = Tcp.client->read(s_mqtt.cid, buf, len);
    if (n == 0)
    {
        return Tcp.client->is_closed(s_mqtt.cid) ? 0 : PROTOCORE_PLATFORM_TLS_WANT_READ;
    }
    return (int)n;
}
// Append what the session has decrypted to the reassembly buffer. Same shape as mq_fill_plain.
static void mq_fill_tls(void)
{
    size_t room = PROTOCORE_MQTT_BUF_SIZE - s_mqtt.rx_len;
    if (room == 0)
    {
        return;
    }
    int n = protocore_tls_client_session_read(s_mqtt.rx + s_mqtt.rx_len, room);
    if (n <= 0)
    {
        if (n < 0)
        {
            s_mqtt.closed = PROTO_TRUE;
        }
        return;
    }
    s_mqtt.rx_len += (size_t)n;
}
#endif // PROTOCORE_ENABLE_MQTT_TLS

// Take this module's storage on first use and hold it: one borrow from the secure pool's persistent
// end - the end no mark walks - reused for every packet, because releasing per packet would wipe it
// each time and mark/release is a bump discipline that would reclaim another module's borrow.
//
// One borrow, not two: each carries a block header rounded up to the arena alignment, and the two
// buffers are one region split at a stated offset. rx is the payload the codec assembles into and
// the receive reassembly; tx is the wire.
static proto_bool mq_mem_bind(void)
{
    if (s_mqtt.rx != NULL)
    {
        return PROTO_TRUE;
    }
    protocore_span mem = protocore_secure_persist_span(2u * PROTOCORE_MQTT_BUF_SIZE);
    if (!span.ok(mem))
    {
        return PROTO_FALSE;
    }
    s_mqtt.rx = mem.buf;
    s_mqtt.tx = mem.buf + PROTOCORE_MQTT_BUF_SIZE;
    return PROTO_TRUE;
}

// Raise the flag over the packet the codec just framed into tx. A packet offered while one is
// still going out is refused rather than overwriting it. This layer never reaches the wire.
static proto_bool mq_tx(size_t len)
{
    if (s_mqtt.tx_ready || len == 0)
    {
        return PROTO_FALSE;
    }
    s_mqtt.tx_len = len;
    s_mqtt.tx_off = 0;
    s_mqtt.tx_ready = PROTO_TRUE;
    return PROTO_TRUE;
}

// Put the packet the codec left flagged on the wire. The worker owns this connection and the pool
// the packet sits in, so it moves the bytes itself: what the transport takes now, the rest on a
// later pass, and the flag drops once the last byte is out.
static void mq_tx_drain(void)
{
    if (!s_mqtt.tx_ready)
    {
        return;
    }
    size_t n = s_mqtt.tx_len - s_mqtt.tx_off;
#if PROTOCORE_ENABLE_MQTT_TLS
    if (s_mqtt.use_tls)
    {
        int w = protocore_tls_client_session_write(s_mqtt.tx + s_mqtt.tx_off, n);
        if (w > 0)
        {
            s_mqtt.tx_off += (size_t)w;
        }
    }
    else
#endif
        if (mq_tx_plain(s_mqtt.tx + s_mqtt.tx_off, n))
    {
        s_mqtt.tx_off += n;
    }
    if (s_mqtt.tx_off >= s_mqtt.tx_len)
    {
        s_mqtt.last_tx_ms = protocore_millis();
        s_mqtt.tx_ready = PROTO_FALSE;
    }
}

static void mq_close()
{
#if PROTOCORE_ENABLE_MQTT_TLS
    if (s_mqtt.use_tls)
    {
        protocore_tls_client_session_end();
    }
#endif
    if (s_mqtt.cid >= 0)
    {
        Tcp.client->close(s_mqtt.cid);
    }
    s_mqtt.cid = -1;
    s_mqtt.mqtt_up = PROTO_FALSE;
    s_mqtt.link = MQ_LINK_IDLE; // a link that was still coming up is given up with the socket
}

static int inflight_find(uint16_t pid)
{
    for (int i = 0; i < PROTOCORE_MQTT_MAX_INFLIGHT; i++)
    {
        if (s_mqtt.inflight[i].state != MQ_INFLIGHT_FREE && s_mqtt.inflight[i].pid == pid)
        {
            return i;
        }
    }
    return -1;
}

static void rxqos2_add(uint16_t pid)
{
    for (int i = 0; i < PROTOCORE_MQTT_RX_QOS2_SLOTS; i++)
    {
        if (s_mqtt.rx_qos2[i] == 0)
        {
            s_mqtt.rx_qos2[i] = pid;
            return;
        }
    }
}
static proto_bool rxqos2_has(uint16_t pid)
{
    for (int i = 0; i < PROTOCORE_MQTT_RX_QOS2_SLOTS; i++)
    {
        if (s_mqtt.rx_qos2[i] == pid)
        {
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}
static void rxqos2_del(uint16_t pid)
{
    for (int i = 0; i < PROTOCORE_MQTT_RX_QOS2_SLOTS; i++)
    {
        if (s_mqtt.rx_qos2[i] == pid)
        {
            s_mqtt.rx_qos2[i] = 0;
        }
    }
}

// Handle one fully-received packet sitting in s_mqtt.pkt (length plen).
static void handle_packet(uint8_t type, uint8_t flags, const uint8_t *body, uint32_t rl)
{
    switch ((MqttType)type) // wire byte -> typed control-packet dispatch
    {
    case MQTT_CONNACK:
        s_mqtt.connack_code = protocore_mqtt_parse_connack(body, rl, NULL);
        if (s_mqtt.connack_code == 0)
        {
            s_mqtt.mqtt_up = PROTO_TRUE;
        }
        MQ_DBG("[mqtt] CONNACK code=%d\n", s_mqtt.connack_code);
        break;
    case MQTT_PUBLISH: {
        char topic[PROTOCORE_MQTT_MAX_TOPIC];
        size_t tlen;
        size_t plen;
        const uint8_t *payload;
        uint16_t pid;
        if (!protocore_mqtt_parse_publish(body, rl, flags, topic, sizeof(topic), &tlen, &payload, &plen, &pid))
        {
            mq_close(); // MQTT-4.8.0-1: a malformed PUBLISH (incl. QoS=3) MUST close the connection
            break;
        }
        uint8_t qos = (uint8_t)((flags >> 1) & 0x03);
        if (qos < 2)
        {
            if (s_mqtt.cb)
            {
                s_mqtt.cb(topic, payload, plen);
            }
            if (qos == 1)
            {
                size_t n = protocore_mqtt_build_ack(s_mqtt.tx, PROTOCORE_MQTT_BUF_SIZE, MQTT_PUBACK, pid);
                mq_tx(n);
            }
        }
        else // QoS 2: dispatch once, dedup by id until PUBREL completes
        {
            if (!rxqos2_has(pid))
            {
                if (s_mqtt.cb)
                {
                    s_mqtt.cb(topic, payload, plen);
                }
                rxqos2_add(pid);
            }
            size_t n = protocore_mqtt_build_ack(s_mqtt.tx, PROTOCORE_MQTT_BUF_SIZE, MQTT_PUBREC, pid);
            mq_tx(n);
        }
        break;
    }
    case MQTT_PUBACK:  // our QoS 1 publish acknowledged
    case MQTT_PUBCOMP: // our QoS 2 publish completed
    {
        int s = inflight_find(protocore_mqtt_parse_ack(body, rl));
        if (s >= 0)
        {
            s_mqtt.inflight[s].state = MQ_INFLIGHT_FREE;
        }
        break;
    }
    case MQTT_PUBREC: // our QoS 2 publish: reply PUBREL, await PUBCOMP
    {
        uint16_t pid = protocore_mqtt_parse_ack(body, rl);
        int s = inflight_find(pid);
        if (s >= 0)
        {
            s_mqtt.inflight[s].state = MQ_INFLIGHT_COMP;
            s_mqtt.inflight[s].sent_ms = protocore_millis();
        }
        size_t n = protocore_mqtt_build_ack(s_mqtt.tx, PROTOCORE_MQTT_BUF_SIZE, MQTT_PUBREL, pid);
        mq_tx(n);
        break;
    }
    case MQTT_PUBREL: // broker releasing an inbound QoS 2 message: reply PUBCOMP
    {
        uint16_t pid = protocore_mqtt_parse_ack(body, rl);
        rxqos2_del(pid);
        size_t n = protocore_mqtt_build_ack(s_mqtt.tx, PROTOCORE_MQTT_BUF_SIZE, MQTT_PUBCOMP, pid);
        mq_tx(n);
        break;
    }
    case MQTT_PINGRESP:
        s_mqtt.ping_pending = PROTO_FALSE;
        break;
    case MQTT_SUBACK:
    case MQTT_UNSUBACK:
    default:
        break; // acknowledgements we do not need to act on
    }
}

// Fill from the transport, then dispatch every whole packet the buffer now holds. The loop is
// bounded by the bytes already received and never waits: it ends on the first packet that is not
// all here yet, and what is left of it shifts down to wait for the next fill. Each packet is
// handled where it landed, so nothing is copied out to parse it.
static void process_rx()
{
#if PROTOCORE_ENABLE_MQTT_TLS
    if (s_mqtt.use_tls)
    {
        mq_fill_tls();
    }
    else
#endif
        mq_fill_plain();

    size_t off = 0;
    for (;;)
    {
        uint8_t type;
        uint8_t flags;
        uint32_t rl;
        size_t hl;
        size_t have = s_mqtt.rx_len - off;
        if (!protocore_mqtt_parse_fixed_header(s_mqtt.rx + off, have, &type, &flags, &rl, &hl))
        {
            break; // header not all here yet
        }
        size_t total = hl + rl;
        if (have < total)
        {
            break; // body not all here yet
        }
        handle_packet(type, flags, s_mqtt.rx + off + hl, rl);
        off += total;
    }
    if (off != 0)
    {
        s_mqtt.rx_len -= off;
        if (s_mqtt.rx_len != 0)
        {
            raw.read(s_mqtt.rx, s_mqtt.rx + off, s_mqtt.rx_len); // the partial waits at the front
        }
    }
}

void protocore_mqtt_set_message_cb(MqttMessageCb cb)
{
    s_mqtt.cb = cb;
}

proto_bool protocore_mqtt_connect(const char *host, uint16_t port, proto_bool use_tls, const MqttConnectOpts *opts)
{
    if (!host || !opts)
    {
        return PROTO_FALSE;
    }
#if !PROTOCORE_ENABLE_MQTT_TLS
    if (use_tls)
    {
        return PROTO_FALSE; // built without MQTTS support
    }
#endif

    if (!mq_mem_bind())
    {
        return PROTO_FALSE; // no storage: fail closed rather than build into nothing
    }

    // Reset all session state.
    mem.set(s_mqtt.inflight, 0, sizeof(s_mqtt.inflight));
    mem.set(s_mqtt.rx_qos2, 0, sizeof(s_mqtt.rx_qos2));
    s_mqtt.rx_len = 0;
    s_mqtt.tx_len = s_mqtt.tx_off = 0;
    s_mqtt.tx_ready = PROTO_FALSE;
    s_mqtt.closed = s_mqtt.mqtt_up = s_mqtt.ping_pending = PROTO_FALSE;
    s_mqtt.connack_code = -1;
    s_mqtt.keepalive_s = opts->keepalive_s;
    s_mqtt.use_tls = use_tls;

    // Frame CONNECT now, while opts is still the caller's to read: the payload assembles in rx and
    // the packet lands in tx, where it waits for the link. Nothing else touches either until then.
    size_t n =
        protocore_mqtt_build_connect(s_mqtt.tx, PROTOCORE_MQTT_BUF_SIZE, opts, s_mqtt.rx, PROTOCORE_MQTT_BUF_SIZE);
    if (n == 0)
    {
        return PROTO_FALSE;
    }
    s_mqtt.tx_len = n;

    s_mqtt.link_budget_ms = PROTOCORE_MQTT_CONNECT_MS;
    s_mqtt.cid = Tcp.client->open(host, port, s_mqtt.link_budget_ms);
    if (s_mqtt.cid < 0)
    {
        return PROTO_FALSE;
    }

#if PROTOCORE_ENABLE_MQTT_TLS
    // Bind the session here, while host is still in scope. Its BIO reads the transport's wire ring,
    // so nothing moves until protocore_mqtt_loop() starts stepping the handshake.
    if (s_mqtt.use_tls && !protocore_tls_client_session_begin(host, mq_tls_send, mq_tls_recv))
    {
        mq_close();
        return PROTO_FALSE;
    }
#endif

    s_mqtt.link = MQ_LINK_TCP;
    s_mqtt.timer = protocore_millis();
    return PROTO_TRUE; // started, not connected: poll protocore_mqtt_loop() and read protocore_mqtt_connected()
}

// Step the link one stage per call. Nothing here waits: the transport slot, the handshake and the
// CONNACK each report where they are and the caller comes back on its own tick.
static void mq_link_step(void)
{
    if (s_mqtt.closed || (protocore_millis() - s_mqtt.timer) >= s_mqtt.link_budget_ms)
    {
        mq_close();
        return;
    }
    switch (s_mqtt.link)
    {
    case MQ_LINK_TCP:
        if (!Tcp.client->connected(s_mqtt.cid))
        {
            return;
        }
#if PROTOCORE_ENABLE_MQTT_TLS
        if (s_mqtt.use_tls)
        {
            s_mqtt.link = MQ_LINK_TLS;
            return;
        }
#endif
        s_mqtt.link = MQ_LINK_CONNACK;
        if (!mq_tx(s_mqtt.tx_len)) // CONNECT is already framed in tx; this raises the flag over it
        {
            mq_close();
        }
        return;

    case MQ_LINK_TLS:
#if PROTOCORE_ENABLE_MQTT_TLS
    {
        int h = protocore_tls_client_session_handshake();
        if (h == 0)
        {
            return; // another flight is owed; the next step carries it
        }
        if (h != 1)
        {
            MQ_DBG("[mqtt] TLS handshake failed (%d)\n", h);
            mq_close();
            return;
        }
        s_mqtt.link = MQ_LINK_CONNACK;
        if (!mq_tx(s_mqtt.tx_len)) // CONNECT is already framed in tx; this raises the flag over it
        {
            mq_close();
        }
    }
#endif
        return;

    case MQ_LINK_CONNACK:
        process_rx();
        if (s_mqtt.mqtt_up)
        {
            s_mqtt.link = MQ_LINK_IDLE;
            s_mqtt.last_tx_ms = protocore_millis();
        }
        else if (s_mqtt.connack_code >= 0)
        {
            mq_close(); // the broker answered and refused
        }
        return;

    case MQ_LINK_IDLE:
        return;
    }
}

proto_bool protocore_mqtt_publish(const char *topic, const uint8_t *payload, size_t len, uint8_t qos, proto_bool retain)
{
    if (!s_mqtt.mqtt_up || qos > 2)
    {
        return PROTO_FALSE;
    }
    if (qos == 0)
    {
        size_t n = protocore_mqtt_build_publish(s_mqtt.tx, PROTOCORE_MQTT_BUF_SIZE, topic, payload, len, 0, 0, retain,
                                                PROTO_FALSE, s_mqtt.rx, PROTOCORE_MQTT_BUF_SIZE);
        return n && mq_tx(n);
    }
    // QoS 1/2: take an in-flight slot. The packet itself stays in tx - a retransmit rewinds the
    // worker to the start of it - so the slot records only what identifies and times the exchange.
    int slot = inflight_find(0);
    if (slot < 0)
    {
        for (int i = 0; i < PROTOCORE_MQTT_MAX_INFLIGHT; i++)
        {
            if (s_mqtt.inflight[i].state == MQ_INFLIGHT_FREE)
            {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0)
    {
        return PROTO_FALSE; // in-flight window full
    }
    uint16_t pid = next_pid();
    size_t n = protocore_mqtt_build_publish(s_mqtt.tx, PROTOCORE_MQTT_BUF_SIZE, topic, payload, len, qos, pid, retain,
                                            PROTO_FALSE, s_mqtt.rx, PROTOCORE_MQTT_BUF_SIZE);
    if (n == 0)
    {
        return PROTO_FALSE;
    }
    s_mqtt.inflight[slot].pid = pid;
    s_mqtt.inflight[slot].state = MQ_INFLIGHT_ACK;
    s_mqtt.inflight[slot].len = n;
    s_mqtt.inflight[slot].sent_ms = protocore_millis();
    return mq_tx(n);
}

proto_bool protocore_mqtt_subscribe(const char *topic, uint8_t qos)
{
    if (!s_mqtt.mqtt_up)
    {
        return PROTO_FALSE;
    }
    size_t n = protocore_mqtt_build_subscribe(s_mqtt.tx, PROTOCORE_MQTT_BUF_SIZE, next_pid(), topic, qos, s_mqtt.rx,
                                              PROTOCORE_MQTT_BUF_SIZE);
    return n && mq_tx(n);
}

proto_bool protocore_mqtt_unsubscribe(const char *topic)
{
    if (!s_mqtt.mqtt_up)
    {
        return PROTO_FALSE;
    }
    size_t n = protocore_mqtt_build_unsubscribe(s_mqtt.tx, PROTOCORE_MQTT_BUF_SIZE, next_pid(), topic, s_mqtt.rx,
                                                PROTOCORE_MQTT_BUF_SIZE);
    return n && mq_tx(n);
}

proto_bool protocore_mqtt_loop()
{
    // A connect that is still coming up takes one step per call, and nothing below it runs until
    // the broker has answered: this is the tick protocore_mqtt_connect() hands the link to.
    if (s_mqtt.link != MQ_LINK_IDLE)
    {
        mq_tx_drain(); // CONNECT is framed and flagged from the first step; carry it out
        mq_link_step();
        return s_mqtt.mqtt_up;
    }
    if (!s_mqtt.mqtt_up)
    {
        return PROTO_FALSE;
    }
    mq_tx_drain(); // whatever the codec flagged last pass goes out before more arrives
    process_rx();
    if (s_mqtt.closed)
    {
        mq_close();
        return PROTO_FALSE;
    }

    uint32_t now = protocore_millis();

    // Keep-alive: send PINGREQ when idle; drop the link if no PINGRESP comes back.
    if (s_mqtt.keepalive_s)
    {
        uint32_t ka = (uint32_t)s_mqtt.keepalive_s * 1000u;
        if (s_mqtt.ping_pending && (now - s_mqtt.ping_sent_ms) > ka)
        {
            mq_close();
            return PROTO_FALSE;
        }
        if (!s_mqtt.ping_pending && (now - s_mqtt.last_tx_ms) >= ka)
        {
            size_t n = protocore_mqtt_build_pingreq(s_mqtt.tx, PROTOCORE_MQTT_BUF_SIZE);
            if (mq_tx(n))
            {
                s_mqtt.ping_pending = PROTO_TRUE;
                s_mqtt.ping_sent_ms = now;
            }
        }
    }

    // Retransmit unacked in-flight QoS 1/2 messages. The PUBLISH is still in tx, so a resend marks
    // DUP where it lies and rewinds the worker to the start of it.
    for (int i = 0; i < PROTOCORE_MQTT_MAX_INFLIGHT; i++)
    {
        if (s_mqtt.inflight[i].state == MQ_INFLIGHT_FREE)
        {
            continue;
        }
        if ((now - s_mqtt.inflight[i].sent_ms) < PROTOCORE_MQTT_RETRANSMIT_MS)
        {
            continue;
        }
        if (s_mqtt.inflight[i].state == MQ_INFLIGHT_ACK)
        {
            // The PUBLISH is still where the codec framed it: mark DUP in place and reset the
            // worker to the start of it. One packet is in flight at a time, so tx is that packet.
            s_mqtt.tx[0] |= 0x08;
            if (!mq_tx(s_mqtt.inflight[i].len))
            {
                continue; // still going out; the next pass retries
            }
        }
        else
        {
            size_t n =
                protocore_mqtt_build_ack(s_mqtt.tx, PROTOCORE_MQTT_BUF_SIZE, MQTT_PUBREL, s_mqtt.inflight[i].pid);
            if (n == 0 || !mq_tx(n))
            {
                continue;
            }
        }
        s_mqtt.inflight[i].sent_ms = now;
    }
    return PROTO_TRUE;
}

proto_bool protocore_mqtt_connected()
{
    return s_mqtt.mqtt_up;
}

void protocore_mqtt_disconnect()
{
    if (s_mqtt.cid >= 0 && s_mqtt.mqtt_up)
    {
        size_t n = protocore_mqtt_build_disconnect(s_mqtt.tx, PROTOCORE_MQTT_BUF_SIZE);
        mq_tx(n);
    }
    mq_close();
}

#else // host build: transport is a stub

void protocore_mqtt_set_message_cb(MqttMessageCb cb)
{
    (void)cb;
}
proto_bool protocore_mqtt_connect(const char *host, uint16_t port, proto_bool use_tls, const MqttConnectOpts *opts)
{
    (void)host;
    (void)port;
    (void)use_tls;
    (void)opts;
    return PROTO_FALSE;
}
proto_bool protocore_mqtt_publish(const char *topic, const uint8_t *payload, size_t len, uint8_t qos, proto_bool retain)
{
    (void)topic;
    (void)payload;
    (void)len;
    (void)qos;
    (void)retain;
    return PROTO_FALSE;
}
proto_bool protocore_mqtt_subscribe(const char *topic, uint8_t qos)
{
    (void)topic;
    (void)qos;
    return PROTO_FALSE;
}
proto_bool protocore_mqtt_unsubscribe(const char *topic)
{
    (void)topic;
    return PROTO_FALSE;
}
proto_bool protocore_mqtt_loop()
{
    return PROTO_FALSE;
}
proto_bool protocore_mqtt_connected()
{
    return PROTO_FALSE;
}
void protocore_mqtt_disconnect()
{
}

#endif // PROTOCORE_HAS_NET_STACK

#endif // PROTOCORE_ENABLE_MQTT
