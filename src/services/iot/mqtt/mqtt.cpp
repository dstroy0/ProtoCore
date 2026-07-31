// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file mqtt.cpp
 * @brief MQTT 3.1.1 packet codec (host-testable) + the raw-lwIP / mbedTLS
 *        persistent client transport (ESP32 only).
 */

#include "services/iot/mqtt/mqtt.h"
#include "services/system/clock.h" // pcdelay

#if PC_ENABLE_MQTT

#include "shared_primitives/utf8.h"
#include <string.h>

// ---------------------------------------------------------------------------
// Pure codec (host-testable)
// ---------------------------------------------------------------------------

// Big-endian 16-bit helpers and a length-prefixed UTF-8 string writer.
#if defined(ARDUINO)
#include "network_drivers/transport/client.h" // shared outbound TCP client (L4)
#include <Arduino.h>
#endif
#if defined(ARDUINO) && PC_ENABLE_MQTT_TLS
#include "network_drivers/tls/tls.h" // persistent client TLS session (csess)
#include <mbedtls/ssl.h>             // MBEDTLS_ERR_SSL_WANT_* for the BIO callbacks
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
        memcpy(p + 2, data, len);
    }
    return 2 + len;
}
static inline size_t put_str(uint8_t *p, const char *s)
{
    return put_field(p, (const uint8_t *)s, // GCOVR_EXCL_BR_LINE  s is never null: every call site in this TU (the
                     s ? strnlen(s, PC_MQTT_BUF_SIZE) // "MQTT" literal, or client_id/will_topic/user/pass already
                       : 0); // guarded non-null by pc_mqtt_build_connect before calling) is non-null
}

size_t pc_mqtt_encode_remlen(uint8_t *out, uint32_t len)
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

bool pc_mqtt_decode_remlen(const uint8_t *buf, size_t avail, uint32_t *value, size_t *used)
{
    uint32_t v = 0;
    uint32_t mult = 1;
    size_t i = 0;
    for (; i < 4; i++)
    {
        if (i >= avail)
        {
            return false; // incomplete
        }
        uint8_t b = buf[i];
        v += (uint32_t)(b & 0x7F) * mult;
        if ((b & 0x80) == 0)
        {
            *value = v;
            *used = i + 1;
            return true;
        }
        mult *= 128;
    }
    return false; // malformed (5th continuation byte)
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
    // Every caller pre-builds body in body[PC_MQTT_BUF_SIZE] (1024), so blen is bounded far below
    // the 2^28 remaining-length limit and pc_mqtt_encode_remlen never rejects here; the len > 256MB reject
    // is covered directly on the public pc_mqtt_encode_remlen.
    size_t rln = pc_mqtt_encode_remlen(rl, (uint32_t)blen);
    if (rln == 0) // GCOVR_EXCL_BR_LINE  the true branch is unreachable: blen <= PC_MQTT_BUF_SIZE << 2^28 via compose's
                  // bounded callers
    {
        return 0; // GCOVR_EXCL_LINE  unreachable: blen <= PC_MQTT_BUF_SIZE << 2^28 via compose's bounded callers
    }
    size_t total = 1 + rln + blen;
    if (total > cap)
    {
        return 0;
    }
    out[0] = byte0;
    memcpy(out + 1, rl, rln);
    if (blen) // GCOVR_EXCL_BR_LINE  blen is never 0: every compose() caller
              // (build_connect/publish/subscribe/unsubscribe) always writes at least a 2-byte length-prefixed field
    {
        memcpy(out + 1 + rln, body, blen);
    }
    return total;
}

size_t pc_mqtt_build_connect(uint8_t *out, size_t cap, const MqttConnectOpts *opts)
{
    if (!out || !opts || !opts->client_id)
    {
        return 0;
    }
    uint8_t body[PC_MQTT_BUF_SIZE];
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
    size_t need = 2 + strnlen(opts->client_id, PC_MQTT_BUF_SIZE);
    if (opts->will_topic)
    {
        need += 2 + strnlen(opts->will_topic, PC_MQTT_BUF_SIZE) + 2 + opts->will_len;
    }
    if (opts->user)
    {
        need += 2 + strnlen(opts->user, PC_MQTT_BUF_SIZE);
    }
    if (opts->pass)
    {
        need += 2 + strnlen(opts->pass, PC_MQTT_BUF_SIZE);
    }
    if (n + need > sizeof(body))
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

    return compose(out, cap, (uint8_t)((uint8_t)MqttType::MQTT_CONNECT << 4), body, n);
}

size_t pc_mqtt_build_publish(uint8_t *out, size_t cap, const char *topic, const uint8_t *payload, size_t payload_len,
                             uint8_t qos, uint16_t packet_id, bool retain, bool dup)
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
    size_t tlen = strnlen(topic, PC_MQTT_BUF_SIZE);
    size_t blen = 2 + tlen + (qos > 0 ? 2 : 0) + payload_len;
    uint8_t body[PC_MQTT_BUF_SIZE];
    if (blen > sizeof(body))
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
        memcpy(body + n, payload, payload_len);
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
    return compose(out, cap, (uint8_t)(((uint8_t)MqttType::MQTT_PUBLISH << 4) | f), body, n);
}

size_t pc_mqtt_build_subscribe(uint8_t *out, size_t cap, uint16_t packet_id, const char *topic, uint8_t qos)
{
    if (!out || !topic || qos > 2)
    {
        return 0;
    }
    size_t tlen = strnlen(topic, PC_MQTT_BUF_SIZE);
    uint8_t body[PC_MQTT_BUF_SIZE];
    size_t blen = 2 + 2 + tlen + 1;
    if (blen > sizeof(body))
    {
        return 0;
    }
    size_t n = 0;
    put_u16(body + n, packet_id);
    n += 2;
    n += put_field(body + n, (const uint8_t *)topic, tlen);
    body[n++] = (uint8_t)(qos & 0x03);
    return compose(out, cap, (uint8_t)(((uint8_t)MqttType::MQTT_SUBSCRIBE << 4) | 0x02), body,
                   n); // SUBSCRIBE flags = 0010
}

size_t pc_mqtt_build_unsubscribe(uint8_t *out, size_t cap, uint16_t packet_id, const char *topic)
{
    if (!out || !topic)
    {
        return 0;
    }
    size_t tlen = strnlen(topic, PC_MQTT_BUF_SIZE);
    uint8_t body[PC_MQTT_BUF_SIZE];
    size_t blen = 2 + 2 + tlen;
    if (blen > sizeof(body))
    {
        return 0;
    }
    size_t n = 0;
    put_u16(body + n, packet_id);
    n += 2;
    n += put_field(body + n, (const uint8_t *)topic, tlen);
    return compose(out, cap, (uint8_t)(((uint8_t)MqttType::MQTT_UNSUBSCRIBE << 4) | 0x02), body,
                   n); // UNSUBSCRIBE flags = 0010
}

size_t pc_mqtt_build_ack(uint8_t *out, size_t cap, MqttType type, uint16_t packet_id)
{
    if (!out || cap < 4)
    {
        return 0;
    }
    uint8_t f = (type == MqttType::MQTT_PUBREL) ? 0x02 : 0x00; // PUBREL requires flags 0010
    out[0] = (uint8_t)(((uint8_t)type << 4) | f);
    out[1] = 0x02;
    put_u16(out + 2, packet_id);
    return 4;
}

size_t pc_mqtt_build_pingreq(uint8_t *out, size_t cap)
{
    if (!out || cap < 2)
    {
        return 0;
    }
    out[0] = (uint8_t)((uint8_t)MqttType::MQTT_PINGREQ << 4);
    out[1] = 0x00;
    return 2;
}

size_t pc_mqtt_build_disconnect(uint8_t *out, size_t cap)
{
    if (!out || cap < 2)
    {
        return 0;
    }
    out[0] = (uint8_t)((uint8_t)MqttType::MQTT_DISCONNECT << 4);
    out[1] = 0x00;
    return 2;
}

bool pc_mqtt_parse_fixed_header(const uint8_t *buf, size_t avail, uint8_t *type, uint8_t *flags,
                                uint32_t *remaining_len, size_t *header_len)
{
    if (avail < 2)
    {
        return false;
    }
    uint32_t rl;
    size_t used;
    if (!pc_mqtt_decode_remlen(buf + 1, avail - 1, &rl, &used))
    {
        return false;
    }
    *type = (uint8_t)(buf[0] >> 4);
    *flags = (uint8_t)(buf[0] & 0x0F);
    *remaining_len = rl;
    *header_len = 1 + used;
    return true;
}

bool pc_mqtt_parse_publish(const uint8_t *buf, uint32_t remaining_len, uint8_t flags, char *topic_out, size_t topic_cap,
                           size_t *topic_len, const uint8_t **payload, size_t *payload_len, uint16_t *packet_id)
{
    if (!buf || remaining_len < 2)
    {
        return false;
    }
    uint16_t tlen = get_u16(buf);
    size_t off = 2;
    if ((uint32_t)off + tlen > remaining_len)
    {
        return false;
    }
    if ((size_t)tlen + 1 > topic_cap)
    {
        return false; // topic + NUL must fit
    }
    // MQTT 1.5.3: a UTF-8 string must be well-formed and must not contain U+0000.
    if (!pc_utf8_valid(buf + off, tlen) || memchr(buf + off, 0x00, tlen))
    {
        return false;
    }
    memcpy(topic_out, buf + off, tlen);
    topic_out[tlen] = '\0';
    *topic_len = tlen;
    off += tlen;

    uint8_t qos = (uint8_t)((flags >> 1) & 0x03);
    if (qos == 3)
    {
        return false; // MQTT-3.3.1-4: a PUBLISH MUST NOT have both QoS bits set (malformed)
    }
    *packet_id = 0;
    if (qos > 0)
    {
        if ((uint32_t)off + 2 > remaining_len)
        {
            return false;
        }
        *packet_id = get_u16(buf + off);
        off += 2;
    }
    *payload = buf + off;
    *payload_len = remaining_len - off;
    return true;
}

uint16_t pc_mqtt_parse_ack(const uint8_t *buf, uint32_t remaining_len)
{
    if (!buf || remaining_len < 2)
    {
        return 0;
    }
    return get_u16(buf);
}

int pc_mqtt_parse_connack(const uint8_t *buf, uint32_t remaining_len, bool *session_present)
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

bool pc_mqtt_parse_suback(const uint8_t *buf, uint32_t remaining_len, uint16_t *packet_id, uint8_t *return_code)
{
    if (!buf || remaining_len < 3)
    {
        return false;
    }
    if (packet_id)
    {
        *packet_id = get_u16(buf);
    }
    if (return_code)
    {
        *return_code = buf[2];
    }
    return true;
}

// ---------------------------------------------------------------------------
// Transport (ESP32 only): persistent raw-lwIP TCP client + QoS state machine,
// with mqtts:// over a persistent client TLS session (pc_tls csess).
// ---------------------------------------------------------------------------
#if defined(ARDUINO)

#ifdef PC_MQTT_DEBUG
#define MQ_DBG(...) printf(__VA_ARGS__)
#else
#define MQ_DBG(...) ((void)0)
#endif

// Outbound QoS 1/2 in-flight (held for DUP retransmit until acknowledged).
struct MqttInflight
{
    uint16_t pid;
    uint8_t state; // 0 free, 1 awaiting PUBACK(qos1)/PUBREC(qos2), 2 awaiting PUBCOMP(qos2)
    uint32_t sent_ms;
    uint16_t len;
    uint8_t pkt[PC_MQTT_INFLIGHT_BUF];
};

// All MQTT connection state, owned by one instance (internal linkage): one broker at a time,
// all static / no heap. Grouped so it is one named owner, unreachable from any other TU.
struct MqttCtx
{
    MqttMessageCb cb;
    int cid = -1;         // outbound connection id (pc_client pool)
    volatile bool closed; // peer closed / error (set when the pump sees it)

    // Inbound plaintext byte ring (consumer = process_rx). It is fed by a pump in
    // process_rx: for plain TCP from pc_client_read, for MQTTS from the TLS session
    // (pc_tls_client_session_read), whose BIO in turn reads ciphertext from pc_client.
    uint8_t rx[PC_MQTT_BUF_SIZE];
    volatile size_t rx_head;
    volatile size_t rx_tail;

    uint8_t pkt[PC_MQTT_BUF_SIZE]; // contiguous scratch a packet is copied into to parse
    uint8_t tx[PC_MQTT_BUF_SIZE];  // outgoing packet scratch
    bool use_tls;                  // mqtts:// mode

    bool mqtt_up;
    uint16_t keepalive_s;
    uint32_t last_tx_ms;
    bool ping_pending;
    uint32_t ping_sent_ms;
    uint16_t next_pid = 1;
    int connack_code; // set by process_rx during the connect handshake

    MqttInflight inflight[PC_MQTT_MAX_INFLIGHT];
    // Inbound QoS 2 packet ids that have been PUBREC'd and await PUBREL (0 = empty).
    uint16_t rx_qos2[PC_MQTT_RX_QOS2_SLOTS];
};
static MqttCtx s_mqtt;

static uint16_t next_pid()
{
    uint16_t p = s_mqtt.next_pid++;
    if (s_mqtt.next_pid == 0)
    {
        s_mqtt.next_pid = 1;
    }
    return p;
}

// --- ring helpers (single-producer/single-consumer) ---
static inline size_t ring_avail()
{
    return (s_mqtt.rx_head + sizeof(s_mqtt.rx) - s_mqtt.rx_tail) % sizeof(s_mqtt.rx);
}
static inline uint8_t ring_peek(size_t i)
{
    return s_mqtt.rx[(s_mqtt.rx_tail + i) % sizeof(s_mqtt.rx)];
}
static void ring_copy(uint8_t *dst, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        dst[i] = s_mqtt.rx[(s_mqtt.rx_tail + i) % sizeof(s_mqtt.rx)];
    }
}
static inline void ring_advance(size_t n)
{
    s_mqtt.rx_tail = (s_mqtt.rx_tail + n) % sizeof(s_mqtt.rx);
}

// --- transport over the shared outbound client (pc_client) ---

// Send raw plaintext bytes to the broker.
static bool mq_tx_plain(const uint8_t *data, size_t len)
{
    return pc_client_send(s_mqtt.cid, data, len);
}

// Drain plaintext wire bytes from the client into the s_mqtt.rx ring (plain TCP).
// pc_client's own ring applies lossless backpressure to the peer when s_mqtt.rx is
// full and we stop draining.
static void mq_pump_plain()
{
    uint8_t tmp[256];
    for (;;)
    {
        size_t freey = (sizeof(s_mqtt.rx) - 1) - ring_avail();
        if (freey == 0)
        {
            break;
        }
        size_t want = freey < sizeof(tmp) ? freey : sizeof(tmp);
        size_t n = pc_client_read(s_mqtt.cid, tmp, want);
        if (n == 0)
        {
            if (pc_client_is_closed(s_mqtt.cid))
            {
                s_mqtt.closed = true;
            }
            break;
        }
        for (size_t i = 0; i < n; i++)
        {
            s_mqtt.rx[s_mqtt.rx_head] = tmp[i];
            s_mqtt.rx_head = (s_mqtt.rx_head + 1) % sizeof(s_mqtt.rx);
        }
    }
}

#if PC_ENABLE_MQTT_TLS
// TLS BIO over the shared client: write ciphertext through the pool, read
// ciphertext by draining the client's wire ring.
static int mq_tls_send(void *ctx, const unsigned char *buf, size_t len)
{
    (void)ctx;
    size_t cap = len > 0xFFFF ? 0xFFFF : len;
    return pc_client_send(s_mqtt.cid, buf, cap) ? (int)cap : MBEDTLS_ERR_SSL_WANT_WRITE;
}
static int mq_tls_recv(void *ctx, unsigned char *buf, size_t len)
{
    (void)ctx;
    size_t n = pc_client_read(s_mqtt.cid, buf, len);
    if (n == 0)
    {
        return pc_client_is_closed(s_mqtt.cid) ? 0 : MBEDTLS_ERR_SSL_WANT_READ;
    }
    return (int)n;
}
// Drain decrypted plaintext from the TLS session into the s_mqtt.rx ring (main loop).
static void mq_pump_tls()
{
    uint8_t tmp[256];
    for (;;)
    {
        size_t freey = (sizeof(s_mqtt.rx) - 1) - ring_avail();
        if (freey == 0)
        {
            break;
        }
        size_t want = freey < sizeof(tmp) ? freey : sizeof(tmp);
        int n = pc_tls_client_session_read(tmp, want);
        if (n <= 0)
        {
            if (n < 0)
            {
                s_mqtt.closed = true;
            }
            break;
        }
        for (int i = 0; i < n; i++)
        {
            s_mqtt.rx[s_mqtt.rx_head] = tmp[i];
            s_mqtt.rx_head = (s_mqtt.rx_head + 1) % sizeof(s_mqtt.rx);
        }
    }
}
#endif // PC_ENABLE_MQTT_TLS

// Send a complete MQTT packet (plaintext or TLS-encrypted per the mode).
static bool mq_tx(const uint8_t *data, size_t len)
{
    bool ok;
#if PC_ENABLE_MQTT_TLS
    if (s_mqtt.use_tls)
    {
        ok = pc_tls_client_session_write(data, len) == (int)len;
    }
    else
#endif
        ok = mq_tx_plain(data, len);
    if (ok)
    {
        s_mqtt.last_tx_ms = millis();
    }
    return ok;
}

static void mq_close()
{
#if PC_ENABLE_MQTT_TLS
    if (s_mqtt.use_tls)
    {
        pc_tls_client_session_end();
    }
#endif
    if (s_mqtt.cid >= 0)
    {
        pc_client_close(s_mqtt.cid);
    }
    s_mqtt.cid = -1;
    s_mqtt.mqtt_up = false;
}

static int inflight_find(uint16_t pid)
{
    for (int i = 0; i < PC_MQTT_MAX_INFLIGHT; i++)
    {
        if (s_mqtt.inflight[i].state != 0 && s_mqtt.inflight[i].pid == pid)
        {
            return i;
        }
    }
    return -1;
}

static void rxqos2_add(uint16_t pid)
{
    for (int i = 0; i < PC_MQTT_RX_QOS2_SLOTS; i++)
    {
        if (s_mqtt.rx_qos2[i] == 0)
        {
            s_mqtt.rx_qos2[i] = pid;
            return;
        }
    }
}
static bool rxqos2_has(uint16_t pid)
{
    for (int i = 0; i < PC_MQTT_RX_QOS2_SLOTS; i++)
    {
        if (s_mqtt.rx_qos2[i] == pid)
        {
            return true;
        }
    }
    return false;
}
static void rxqos2_del(uint16_t pid)
{
    for (int i = 0; i < PC_MQTT_RX_QOS2_SLOTS; i++)
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
    case MqttType::MQTT_CONNACK:
        s_mqtt.connack_code = pc_mqtt_parse_connack(body, rl, nullptr);
        if (s_mqtt.connack_code == 0)
        {
            s_mqtt.mqtt_up = true;
        }
        MQ_DBG("[mqtt] CONNACK code=%d\n", s_mqtt.connack_code);
        break;
    case MqttType::MQTT_PUBLISH: {
        char topic[PC_MQTT_MAX_TOPIC];
        size_t tlen;
        size_t plen;
        const uint8_t *payload;
        uint16_t pid;
        if (!pc_mqtt_parse_publish(body, rl, flags, topic, sizeof(topic), &tlen, &payload, &plen, &pid))
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
                size_t n = pc_mqtt_build_ack(s_mqtt.tx, sizeof(s_mqtt.tx), MqttType::MQTT_PUBACK, pid);
                mq_tx(s_mqtt.tx, n);
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
            size_t n = pc_mqtt_build_ack(s_mqtt.tx, sizeof(s_mqtt.tx), MqttType::MQTT_PUBREC, pid);
            mq_tx(s_mqtt.tx, n);
        }
        break;
    }
    case MqttType::MQTT_PUBACK:  // our QoS 1 publish acknowledged
    case MqttType::MQTT_PUBCOMP: // our QoS 2 publish completed
    {
        int s = inflight_find(pc_mqtt_parse_ack(body, rl));
        if (s >= 0)
        {
            s_mqtt.inflight[s].state = 0;
        }
        break;
    }
    case MqttType::MQTT_PUBREC: // our QoS 2 publish: reply PUBREL, await PUBCOMP
    {
        uint16_t pid = pc_mqtt_parse_ack(body, rl);
        int s = inflight_find(pid);
        if (s >= 0)
        {
            s_mqtt.inflight[s].state = 2;
            s_mqtt.inflight[s].sent_ms = millis();
        }
        size_t n = pc_mqtt_build_ack(s_mqtt.tx, sizeof(s_mqtt.tx), MqttType::MQTT_PUBREL, pid);
        mq_tx(s_mqtt.tx, n);
        break;
    }
    case MqttType::MQTT_PUBREL: // broker releasing an inbound QoS 2 message: reply PUBCOMP
    {
        uint16_t pid = pc_mqtt_parse_ack(body, rl);
        rxqos2_del(pid);
        size_t n = pc_mqtt_build_ack(s_mqtt.tx, sizeof(s_mqtt.tx), MqttType::MQTT_PUBCOMP, pid);
        mq_tx(s_mqtt.tx, n);
        break;
    }
    case MqttType::MQTT_PINGRESP:
        s_mqtt.ping_pending = false;
        break;
    case MqttType::MQTT_SUBACK:
    case MqttType::MQTT_UNSUBACK:
    default:
        break; // acknowledgements we do not need to act on
    }
}

// Drain complete packets from the rx ring (copies each into s_mqtt.pkt to parse).
static void process_rx()
{
#if PC_ENABLE_MQTT_TLS
    if (s_mqtt.use_tls)
    {
        mq_pump_tls(); // decrypt ciphertext into the plaintext ring first
    }
    else
#endif
        mq_pump_plain(); // drain plaintext wire bytes into the ring
    for (;;)
    {
        size_t avail = ring_avail();
        if (avail < 2)
        {
            return;
        }
        // Peek the fixed header (byte0 + 1-4 remlen bytes) without advancing.
        uint8_t hdr[5];
        size_t hn = avail < 5 ? avail : 5;
        for (size_t i = 0; i < hn; i++)
        {
            hdr[i] = ring_peek(i);
        }
        uint8_t type;
        uint8_t flags;
        uint32_t rl;
        size_t hl;
        if (!pc_mqtt_parse_fixed_header(hdr, hn, &type, &flags, &rl, &hl))
        {
            return; // incomplete header
        }
        size_t total = hl + rl;
        if (avail < total)
        {
            return; // packet not fully arrived yet
        }
        if (total > sizeof(s_mqtt.pkt))
        {
            ring_advance(total); // oversized: drop it
            continue;
        }
        ring_copy(s_mqtt.pkt, total);
        ring_advance(total);
        handle_packet(type, flags, s_mqtt.pkt + hl, rl);
    }
}

void pc_mqtt_set_message_cb(MqttMessageCb cb)
{
    s_mqtt.cb = cb;
}

bool pc_mqtt_connect(const char *host, uint16_t port, bool use_tls, const MqttConnectOpts *opts)
{
    if (!host || !opts)
    {
        return false;
    }
#if !PC_ENABLE_MQTT_TLS
    if (use_tls)
    {
        return false; // built without MQTTS support
    }
#endif

    // Reset all session state.
    memset(s_mqtt.inflight, 0, sizeof(s_mqtt.inflight));
    memset(s_mqtt.rx_qos2, 0, sizeof(s_mqtt.rx_qos2));
    s_mqtt.rx_head = s_mqtt.rx_tail = 0;
    s_mqtt.closed = s_mqtt.mqtt_up = s_mqtt.ping_pending = false;
    s_mqtt.connack_code = -1;
    s_mqtt.keepalive_s = opts->keepalive_s;
    s_mqtt.use_tls = use_tls;

    uint32_t deadline = millis() + 8000;

    // Open the TCP connection (DNS + connect) via the shared client transport.
    s_mqtt.cid = pc_client_open(host, port, 8000);
    if (s_mqtt.cid < 0)
    {
        return false;
    }

#if PC_ENABLE_MQTT_TLS
    if (s_mqtt.use_tls)
    {
        if (!pc_tls_client_session_begin(host, mq_tls_send, mq_tls_recv))
        {
            mq_close();
            return false;
        }
        int h;
        while ((h = pc_tls_client_session_handshake()) == 0 && !s_mqtt.closed && (int32_t)(deadline - millis()) > 0)
        {
            pcdelay(5);
        }
        if (h != 1)
        {
            MQ_DBG("[mqtt] TLS handshake failed (%d)\n", h);
            mq_close();
            return false;
        }
    }
#endif

    size_t n = pc_mqtt_build_connect(s_mqtt.tx, sizeof(s_mqtt.tx), opts);
    if (n == 0 || !mq_tx(s_mqtt.tx, n))
    {
        mq_close();
        return false;
    }

    // Wait for CONNACK.
    while (!s_mqtt.mqtt_up && s_mqtt.connack_code < 0 && !s_mqtt.closed && (int32_t)(deadline - millis()) > 0)
    {
        process_rx();
        pcdelay(5);
    }
    if (!s_mqtt.mqtt_up)
    {
        mq_close();
        return false;
    }
    s_mqtt.last_tx_ms = millis();
    return true;
}

bool pc_mqtt_publish(const char *topic, const uint8_t *payload, size_t len, uint8_t qos, bool retain)
{
    if (!s_mqtt.mqtt_up || qos > 2)
    {
        return false;
    }
    if (qos == 0)
    {
        size_t n = pc_mqtt_build_publish(s_mqtt.tx, sizeof(s_mqtt.tx), topic, payload, len, 0, 0, retain, false);
        return n && mq_tx(s_mqtt.tx, n);
    }
    // QoS 1/2: take an in-flight slot, store the serialized packet for retransmit.
    int slot = inflight_find(0);
    if (slot < 0)
    {
        for (int i = 0; i < PC_MQTT_MAX_INFLIGHT; i++)
        {
            if (s_mqtt.inflight[i].state == 0)
            {
                slot = i;
                break;
            }
        }
    }
    if (slot < 0)
    {
        return false; // in-flight window full
    }
    uint16_t pid = next_pid();
    size_t n = pc_mqtt_build_publish(s_mqtt.inflight[slot].pkt, sizeof(s_mqtt.inflight[slot].pkt), topic, payload, len,
                                     qos, pid, retain, false);
    if (n == 0)
    {
        return false; // too large for an in-flight slot
    }
    s_mqtt.inflight[slot].pid = pid;
    s_mqtt.inflight[slot].state = 1;
    s_mqtt.inflight[slot].len = (uint16_t)n;
    s_mqtt.inflight[slot].sent_ms = millis();
    return mq_tx(s_mqtt.inflight[slot].pkt, n);
}

bool pc_mqtt_subscribe(const char *topic, uint8_t qos)
{
    if (!s_mqtt.mqtt_up)
    {
        return false;
    }
    size_t n = pc_mqtt_build_subscribe(s_mqtt.tx, sizeof(s_mqtt.tx), next_pid(), topic, qos);
    return n && mq_tx(s_mqtt.tx, n);
}

bool pc_mqtt_unsubscribe(const char *topic)
{
    if (!s_mqtt.mqtt_up)
    {
        return false;
    }
    size_t n = pc_mqtt_build_unsubscribe(s_mqtt.tx, sizeof(s_mqtt.tx), next_pid(), topic);
    return n && mq_tx(s_mqtt.tx, n);
}

bool pc_mqtt_loop()
{
    if (!s_mqtt.mqtt_up)
    {
        return false;
    }
    process_rx();
    if (s_mqtt.closed)
    {
        mq_close();
        return false;
    }

    uint32_t now = millis();

    // Keep-alive: send PINGREQ when idle; drop the link if no PINGRESP comes back.
    if (s_mqtt.keepalive_s)
    {
        uint32_t ka = (uint32_t)s_mqtt.keepalive_s * 1000u;
        if (s_mqtt.ping_pending && (now - s_mqtt.ping_sent_ms) > ka)
        {
            mq_close();
            return false;
        }
        if (!s_mqtt.ping_pending && (now - s_mqtt.last_tx_ms) >= ka)
        {
            size_t n = pc_mqtt_build_pingreq(s_mqtt.tx, sizeof(s_mqtt.tx));
            if (mq_tx(s_mqtt.tx, n))
            {
                s_mqtt.ping_pending = true;
                s_mqtt.ping_sent_ms = now;
            }
        }
    }

    // Retransmit unacked in-flight QoS 1/2 messages.
    for (int i = 0; i < PC_MQTT_MAX_INFLIGHT; i++)
    {
        if (s_mqtt.inflight[i].state == 0)
        {
            continue;
        }
        if ((now - s_mqtt.inflight[i].sent_ms) < PC_MQTT_RETRANSMIT_MS)
        {
            continue;
        }
        if (s_mqtt.inflight[i].state == 1)
        {
            s_mqtt.inflight[i].pkt[0] |= 0x08; // set DUP on the stored PUBLISH
            mq_tx(s_mqtt.inflight[i].pkt, s_mqtt.inflight[i].len);
        }
        else // state 2: re-send PUBREL
        {
            size_t n = pc_mqtt_build_ack(s_mqtt.tx, sizeof(s_mqtt.tx), MqttType::MQTT_PUBREL, s_mqtt.inflight[i].pid);
            mq_tx(s_mqtt.tx, n);
        }
        s_mqtt.inflight[i].sent_ms = now;
    }
    return true;
}

bool pc_mqtt_connected()
{
    return s_mqtt.mqtt_up;
}

void pc_mqtt_disconnect()
{
    if (s_mqtt.cid >= 0 && s_mqtt.mqtt_up)
    {
        size_t n = pc_mqtt_build_disconnect(s_mqtt.tx, sizeof(s_mqtt.tx));
        mq_tx(s_mqtt.tx, n);
    }
    mq_close();
}

#else // host build: transport is a stub

void pc_mqtt_set_message_cb(MqttMessageCb)
{
}
bool pc_mqtt_connect(const char *, uint16_t, bool, const MqttConnectOpts *)
{
    return false;
}
bool pc_mqtt_publish(const char *, const uint8_t *, size_t, uint8_t, bool)
{
    return false;
}
bool pc_mqtt_subscribe(const char *, uint8_t)
{
    return false;
}
bool pc_mqtt_unsubscribe(const char *)
{
    return false;
}
bool pc_mqtt_loop()
{
    return false;
}
bool pc_mqtt_connected()
{
    return false;
}
void pc_mqtt_disconnect()
{
}

#endif // ARDUINO

#endif // PC_ENABLE_MQTT
