// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ws_client.c
 * @brief WebSocket (RFC 6455) client codec (host-testable) + the raw-lwIP /
 *        mbedTLS persistent transport (ESP32 only).
 */

#include "services/net/ws_client/ws_client.h"
#include "mmgr/membuild.h"      // pc_sb frame builder
#include "server/clock/clock.h" // pcdelay

#if PC_ENABLE_WS_CLIENT

#include "crypto/hash/sha1.h"
#include "network_drivers/presentation/codec/base64/base64.h"
#include <stdio.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Pure codec (host-testable)
// ---------------------------------------------------------------------------

#if PROTOCORE_HOT
#include "network_drivers/transport/client.h" // shared outbound TCP client (L4)
#include <Arduino.h>
#endif
#if PROTOCORE_HOT && PC_ENABLE_WS_CLIENT_TLS
#include "network_drivers/tls/tls.h"
#include <mbedtls/ssl.h>
#endif
static const char WS_MAGIC[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

void ws_client_accept_for_key(const char *key_b64, char *out, size_t out_cap)
{
    if (!out || out_cap == 0)
    {
        return;
    }
    out[0] = '\0';
    if (!key_b64)
    {
        return;
    }
    char concat[64];
    size_t klen = strnlen(key_b64, sizeof(concat));
    size_t mlen = sizeof(WS_MAGIC) - 1;
    if (klen + mlen >= sizeof(concat))
    {
        return;
    }
    memcpy(concat, key_b64, klen);
    memcpy(concat + klen, WS_MAGIC, mlen);
    uint8_t digest[PC_SHA1_DIGEST_LEN];
    pc_sha1((const uint8_t *)concat, klen + mlen, digest);
    if (out_cap < 29) // 28 base64 chars + NUL
    {
        return;
    }
    pc_base64_encode(digest, PC_SHA1_DIGEST_LEN, out);
}

size_t ws_client_build_handshake(uint8_t *out, size_t cap, const char *host, const char *path, const char *key_b64,
                                 const char *subprotocol)
{
    if (!out || !host || !path || !key_b64)
    {
        return 0;
    }
    // A Sec-WebSocket-Protocol offer is emitted only when a subprotocol is requested (e.g. "wamp.2.json" for
    // WAMP-over-WebSocket); the server echoes the one it selected. Null/empty omits the header entirely.
    // The two forms differ only by the optional Protocol header, so one builder emits both rather
    // than two near-identical copies of the same handshake.
    pc_sb sb = {(char *)out, cap, 0, PROTO_TRUE};
    pc_sb_put(&sb, "GET ");
    pc_sb_put(&sb, path);
    pc_sb_put(&sb, " HTTP/1.1\r\nHost: ");
    pc_sb_put(&sb, host);
    pc_sb_put(&sb, "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ");
    pc_sb_put(&sb, key_b64);
    if (subprotocol && subprotocol[0])
    {
        pc_sb_put(&sb, "\r\nSec-WebSocket-Protocol: ");
        pc_sb_put(&sb, subprotocol);
    }
    pc_sb_put(&sb, "\r\nSec-WebSocket-Version: 13\r\n\r\n");
    size_t n = pc_sb_finish(&sb);
    if (!sb.ok)
    {
        return 0;
    }
    return n;
}

// Case-insensitive header-value lookup within [buf, buf+len); returns a pointer
// to the value (past "name:" and OWS) and its length via *vlen, or nullptr.
static const char *find_header(const uint8_t *buf, size_t len, const char *name, size_t *vlen)
{
    size_t nlen = strnlen(name, len + 1);
    const uint8_t *p = buf;
    const uint8_t *end = buf + len;
    while (p + nlen + 1 < end)
    {
        if (strncasecmp((const char *)p, name, nlen) == 0 && p[nlen] == ':')
        {
            const uint8_t *v = p + nlen + 1;
            while (v < end && (*v == ' ' || *v == '\t'))
            {
                v++;
            }
            const uint8_t *e = v;
            while (e < end && *e != '\r' && *e != '\n')
            {
                e++;
            }
            *vlen = (size_t)(e - v);
            return (const char *)v;
        }
        while (p < end && *p != '\n')
        {
            p++;
        }
        if (p < end)
        {
            p++;
        }
    }
    return NULL;
}

proto_bool ws_client_check_response(const uint8_t *buf, size_t len, const char *expected_accept)
{
    if (!buf || len < 12 || !expected_accept)
    {
        return PROTO_FALSE;
    }
    // Status line must be an HTTP 101.
    const uint8_t *eol = (const uint8_t *)memchr(buf, '\n', len);
    if (!eol)
    {
        return PROTO_FALSE;
    }
    proto_bool ok101 = PROTO_FALSE;
    for (const uint8_t *q = buf; q + 3 < eol; q++)
    {
        if (q[0] == '1' && q[1] == '0' && q[2] == '1')
        {
            ok101 = PROTO_TRUE;
            break;
        }
    }
    if (!ok101)
    {
        return PROTO_FALSE;
    }
    size_t vlen = 0;
    const char *acc = find_header(buf, len, "Sec-WebSocket-Accept", &vlen);
    if (!acc)
    {
        return PROTO_FALSE;
    }
    return vlen == strnlen(expected_accept, vlen + 1) && memcmp(acc, expected_accept, vlen) == 0;
}

size_t ws_client_build_frame(uint8_t *out, size_t cap, WsClientOpcode opcode, const uint8_t *payload, size_t len,
                             const uint8_t mask[4])
{
    if (!out || !mask)
    {
        return 0;
    }
    size_t hdr = 2 + 4; // byte0 + len-byte + 4-byte mask (short form)
    if (len >= 126 && len < 65536)
    {
        hdr += 2;
    }
    else if (len >= 65536)
    {
        hdr += 8;
    }
    if (hdr + len > cap)
    {
        return 0;
    }

    size_t i = 0;
    out[i++] = (uint8_t)(0x80 | (uint8_t)(opcode)); // FIN + opcode (all opcodes <= 0x0A)
    if (len < 126)
    {
        out[i++] = (uint8_t)(0x80 | len);
    }
    else if (len < 65536)
    {
        out[i++] = 0x80 | 126;
        out[i++] = (uint8_t)(len >> 8);
        out[i++] = (uint8_t)(len & 0xFF);
    }
    else
    {
        out[i++] = 0x80 | 127;
        for (int s = 56; s >= 0; s -= 8)
        {
            out[i++] = (uint8_t)((uint64_t)len >> s);
        }
    }
    memcpy(out + i, mask, 4);
    i += 4;
    for (size_t j = 0; j < len; j++)
    {
        out[i + j] = (uint8_t)(payload[j] ^ mask[j & 3]);
    }
    return i + len;
}

proto_bool ws_client_parse_frame(const uint8_t *buf, size_t avail, uint8_t *opcode, proto_bool *fin,
                                 size_t *payload_off, size_t *payload_len, size_t *consumed)
{
    if (!buf || avail < 2)
    {
        return PROTO_FALSE;
    }
    uint8_t b0 = buf[0];
    uint8_t b1 = buf[1];
    proto_bool masked = (b1 & 0x80) != 0;
    uint64_t len = b1 & 0x7F;
    size_t off = 2;
    if (len == 126)
    {
        if (avail < off + 2)
        {
            return PROTO_FALSE;
        }
        len = ((uint64_t)buf[off] << 8) | buf[off + 1];
        off += 2;
    }
    else if (len == 127)
    {
        if (avail < off + 8)
        {
            return PROTO_FALSE;
        }
        uint64_t v = 0;
        for (int s = 0; s < 8; s++)
        {
            v = (v << 8) | buf[off + s];
        }
        if (v > 0xFFFFFFFFu) // absurd frame length on a constrained device
        {
            return PROTO_FALSE;
        }
        len = v;
        off += 8;
    }
    if (masked)
    {
        off += 4; // server frames should not be masked, but stay aligned if they are
    }
    if (avail < off + (size_t)len)
    {
        return PROTO_FALSE;
    }
    *opcode = (uint8_t)(b0 & 0x0F);
    *fin = (b0 & 0x80) != 0;
    *payload_off = off;
    *payload_len = (size_t)len;
    *consumed = off + (size_t)len;
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Transport (ESP32 only): persistent raw-lwIP client + RFC 6455 framing,
// with wss:// over a persistent client TLS session (pc_tls csess).
// ---------------------------------------------------------------------------
#if PROTOCORE_HOT

#ifdef PC_WS_CLIENT_DEBUG
#define WSC_DBG(...) printf(__VA_ARGS__)
#else
#define WSC_DBG(...) ((void)0)
#endif

// All WebSocket-client connection state, owned by one instance (internal linkage): one server
// at a time, all static / no heap. Grouped so it is one named owner, unreachable cross-TU.
typedef struct
{
    WsClientMessageCb cb;
    int cid;                    // outbound connection id (pc_client pool)
    volatile proto_bool closed; // peer closed / error (set when the pump sees it)
    proto_bool ws_up;
    proto_bool use_tls;

    // Inbound plaintext ring, fed by a pump in the loop: from pc_client_read for
    // plain ws, from the TLS session (pc_tls_client_session_read) for wss.
    uint8_t rx[PC_WS_CLIENT_BUF_SIZE];
    volatile size_t rx_head;
    volatile size_t rx_tail;
    uint8_t pkt[PC_WS_CLIENT_BUF_SIZE]; // a frame copied out to parse
    uint8_t tx[PC_WS_CLIENT_BUF_SIZE];  // outgoing frame scratch

    // Fragmented-message reassembly (continuation frames -> one delivered message).
    uint8_t msg[PC_WS_CLIENT_BUF_SIZE];
    size_t msg_len;
    uint8_t msg_op;
} WsClientCtx;
static WsClientCtx s_wsc = {.cid = -1};

static inline size_t ring_avail()
{
    return (s_wsc.rx_head + sizeof(s_wsc.rx) - s_wsc.rx_tail) % sizeof(s_wsc.rx);
}
static inline uint8_t ring_peek(size_t i)
{
    return s_wsc.rx[(s_wsc.rx_tail + i) % sizeof(s_wsc.rx)];
}
static inline void ring_advance(size_t n)
{
    s_wsc.rx_tail = (s_wsc.rx_tail + n) % sizeof(s_wsc.rx);
}
static void ring_copy(uint8_t *dst, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        dst[i] = s_wsc.rx[(s_wsc.rx_tail + i) % sizeof(s_wsc.rx)];
    }
}

// --- transport over the shared outbound client (pc_client) ---

static proto_bool ws_tx_plain(const uint8_t *data, size_t len)
{
    return pc_client_send(s_wsc.cid, data, len);
}

// Drain plaintext wire bytes from the client into the s_wsc.rx ring (plain ws).
static void ws_pump_plain()
{
    uint8_t tmp[256];
    for (;;)
    {
        size_t freey = (sizeof(s_wsc.rx) - 1) - ring_avail();
        if (freey == 0)
        {
            break;
        }
        size_t want = freey < sizeof(tmp) ? freey : sizeof(tmp);
        size_t n = pc_client_read(s_wsc.cid, tmp, want);
        if (n == 0)
        {
            if (pc_client_is_closed(s_wsc.cid))
            {
                s_wsc.closed = PROTO_TRUE;
            }
            break;
        }
        for (size_t i = 0; i < n; i++)
        {
            s_wsc.rx[s_wsc.rx_head] = tmp[i];
            s_wsc.rx_head = (s_wsc.rx_head + 1) % sizeof(s_wsc.rx);
        }
    }
}

#if PC_ENABLE_WS_CLIENT_TLS
// TLS BIO over the shared client: write ciphertext through the pool, read
// ciphertext by draining the client's wire ring.
static int ws_tls_send(void *ctx, const unsigned char *buf, size_t len)
{
    (void)ctx;
    size_t cap = len > 0xFFFF ? 0xFFFF : len;
    return pc_client_send(s_wsc.cid, buf, cap) ? (int)cap : MBEDTLS_ERR_SSL_WANT_WRITE;
}
static int ws_tls_recv(void *ctx, unsigned char *buf, size_t len)
{
    (void)ctx;
    size_t n = pc_client_read(s_wsc.cid, buf, len);
    if (n == 0)
    {
        return pc_client_is_closed(s_wsc.cid) ? 0 : MBEDTLS_ERR_SSL_WANT_READ;
    }
    return (int)n;
}
static void ws_pump_tls()
{
    uint8_t tmp[256];
    for (;;)
    {
        size_t freey = (sizeof(s_wsc.rx) - 1) - ring_avail();
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
                s_wsc.closed = PROTO_TRUE;
            }
            break;
        }
        for (int i = 0; i < n; i++)
        {
            s_wsc.rx[s_wsc.rx_head] = tmp[i];
            s_wsc.rx_head = (s_wsc.rx_head + 1) % sizeof(s_wsc.rx);
        }
    }
}
#endif // PC_ENABLE_WS_CLIENT_TLS

// Send already-framed bytes (plaintext or TLS-encrypted per the mode).
static proto_bool ws_tx(const uint8_t *data, size_t len)
{
#if PC_ENABLE_WS_CLIENT_TLS
    if (s_wsc.use_tls)
    {
        return pc_tls_client_session_write(data, len) == (int)len;
    }
#endif
    return ws_tx_plain(data, len);
}

// Frame and send a message with a fresh random masking key (RFC 6455 client rule).
static proto_bool ws_send_frame(WsClientOpcode opcode, const uint8_t *payload, size_t len)
{
    if (!s_wsc.ws_up)
    {
        return PROTO_FALSE;
    }
    uint8_t mask[4];
    pc_platform_rand_fill(mask, 4);
    size_t n = ws_client_build_frame(s_wsc.tx, sizeof(s_wsc.tx), opcode, payload, len, mask);
    return n && ws_tx(s_wsc.tx, n);
}

static void ws_close_tcp()
{
#if PC_ENABLE_WS_CLIENT_TLS
    if (s_wsc.use_tls)
    {
        pc_tls_client_session_end();
    }
#endif
    if (s_wsc.cid >= 0)
    {
        pc_client_close(s_wsc.cid);
    }
    s_wsc.cid = -1;
    s_wsc.ws_up = PROTO_FALSE;
}

static void deliver(uint8_t op, const uint8_t *payload, size_t len)
{
    if (s_wsc.cb && (op == (uint8_t)WSC_OP_TEXT || op == (uint8_t)WSC_OP_BINARY))
    {
        s_wsc.cb(op, payload, len);
    }
}

// Dispatch one parsed frame (handles fragmentation, ping/pong, close).
static void handle_frame(uint8_t op, proto_bool fin, const uint8_t *payload, size_t len)
{
    switch ((WsClientOpcode)op)
    {
    case WSC_OP_TEXT:
    case WSC_OP_BINARY:
        if (fin)
        {
            deliver(op, payload, len); // common case: unfragmented
        }
        else
        {
            s_wsc.msg_op = op; // first fragment
            s_wsc.msg_len = len < sizeof(s_wsc.msg) ? len : sizeof(s_wsc.msg);
            memcpy(s_wsc.msg, payload, s_wsc.msg_len);
        }
        break;
    case WSC_OP_CONT:
        if (s_wsc.msg_len + len <= sizeof(s_wsc.msg))
        {
            memcpy(s_wsc.msg + s_wsc.msg_len, payload, len);
            s_wsc.msg_len += len;
        }
        if (fin)
        {
            deliver(s_wsc.msg_op, s_wsc.msg, s_wsc.msg_len);
            s_wsc.msg_len = 0;
        }
        break;
    case WSC_OP_PING:
        ws_send_frame(WSC_OP_PONG, payload, len); // echo the application data
        break;
    case WSC_OP_CLOSE:
        ws_send_frame(WSC_OP_CLOSE, NULL, 0);
        s_wsc.closed = PROTO_TRUE;
        break;
    case WSC_OP_PONG:
    default:
        break;
    }
}

static void process_rx()
{
#if PC_ENABLE_WS_CLIENT_TLS
    if (s_wsc.use_tls)
    {
        ws_pump_tls();
    }
    else
#endif
        ws_pump_plain();
    for (;;)
    {
        size_t avail = ring_avail();
        if (avail < 2)
        {
            return;
        }
        // Peek the header bytes (a frame header is at most 14 bytes); parse_frame
        // reads only the header and uses the real ring count to test completeness.
        uint8_t hdr[14];
        size_t hn = avail < sizeof(hdr) ? avail : sizeof(hdr);
        for (size_t i = 0; i < hn; i++)
        {
            hdr[i] = ring_peek(i);
        }
        uint8_t op;
        proto_bool fin;
        size_t off;
        size_t plen;
        size_t consumed;
        if (!ws_client_parse_frame(hdr, avail, &op, &fin, &off, &plen, &consumed))
        {
            return; // header incomplete or full frame not yet arrived
        }
        if (consumed > sizeof(s_wsc.pkt))
        {
            ring_advance(consumed); // oversized frame: drop it
            continue;
        }
        ring_copy(s_wsc.pkt, consumed);
        ring_advance(consumed);
        handle_frame(op, fin, s_wsc.pkt + off, plen);
    }
}

void ws_client_on_message(WsClientMessageCb cb)
{
    s_wsc.cb = cb;
}

proto_bool ws_client_connect(const char *host, uint16_t port, proto_bool use_tls, const char *path)
{
    if (!host || !path)
    {
        return PROTO_FALSE;
    }
#if !PC_ENABLE_WS_CLIENT_TLS
    if (use_tls)
    {
        return PROTO_FALSE;
    }
#endif
    s_wsc.rx_head = s_wsc.rx_tail = 0;
    s_wsc.closed = s_wsc.ws_up = PROTO_FALSE;
    s_wsc.msg_len = 0;
    s_wsc.use_tls = use_tls;

    uint32_t deadline = millis() + 8000;

    // Open the TCP connection (DNS + connect) via the shared client transport.
    s_wsc.cid = pc_client_open(host, port, 8000);
    if (s_wsc.cid < 0)
    {
        WSC_DBG("[wsc] pc_client_open failed (%d)\n", s_wsc.cid);
        return PROTO_FALSE;
    }

#if PC_ENABLE_WS_CLIENT_TLS
    if (s_wsc.use_tls)
    {
        if (!pc_tls_client_session_begin(host, ws_tls_send, ws_tls_recv))
        {
            WSC_DBG("[wsc] csess_begin failed\n");
            ws_close_tcp();
            return PROTO_FALSE;
        }
        int h;
        while ((h = pc_tls_client_session_handshake()) == 0 && !s_wsc.closed && (int32_t)(deadline - millis()) > 0)
        {
            pcdelay(5);
        }
        if (h != 1)
        {
            WSC_DBG("[wsc] TLS handshake h=%d closed=%d\n", h, (int)s_wsc.closed);
            ws_close_tcp();
            return PROTO_FALSE;
        }
        WSC_DBG("[wsc] TLS handshake ok\n");
    }
#endif

    // Generate a random 16-byte key, base64 it, send the opening handshake.
    uint8_t keyraw[16];
    pc_platform_rand_fill(keyraw, sizeof(keyraw));
    char key_b64[25];
    pc_base64_encode(keyraw, sizeof(keyraw), key_b64);
    char expect[32];
    ws_client_accept_for_key(key_b64, expect, sizeof(expect));

    size_t n = ws_client_build_handshake(s_wsc.tx, sizeof(s_wsc.tx), host, path, key_b64, NULL);
    if (n == 0 || !ws_tx(s_wsc.tx, n))
    {
        ws_close_tcp();
        return PROTO_FALSE;
    }

    // Read the response header (up to "\r\n\r\n") out of the rx ring.
    uint8_t hs[512];
    size_t hl = 0;
    proto_bool done = PROTO_FALSE;
    while (!done && !s_wsc.closed && (int32_t)(deadline - millis()) > 0)
    {
#if PC_ENABLE_WS_CLIENT_TLS
        if (s_wsc.use_tls)
        {
            ws_pump_tls();
        }
        else
#endif
            ws_pump_plain();
        while (ring_avail() > 0 && hl < sizeof(hs))
        {
            hs[hl++] = ring_peek(0);
            ring_advance(1);
            if (hl >= 4 && hs[hl - 4] == '\r' && hs[hl - 3] == '\n' && hs[hl - 2] == '\r' && hs[hl - 1] == '\n')
            {
                done = PROTO_TRUE;
                break;
            }
        }
        if (!done)
        {
            pcdelay(5);
        }
    }
    if (!done || !ws_client_check_response(hs, hl, expect))
    {
        WSC_DBG("[wsc] handshake fail done=%d hl=%u resp:\n%.*s\n", (int)done, (unsigned)hl, (int)hl, (const char *)hs);
        ws_close_tcp();
        return PROTO_FALSE;
    }
    s_wsc.ws_up = PROTO_TRUE;
    return PROTO_TRUE;
}

proto_bool ws_client_send_text(const char *text)
{
    return ws_send_frame(WSC_OP_TEXT, (const uint8_t *)text, text ? strnlen(text, PC_WS_CLIENT_BUF_SIZE) : 0);
}
proto_bool ws_client_send_binary(const uint8_t *data, size_t len)
{
    return ws_send_frame(WSC_OP_BINARY, data, len);
}

proto_bool ws_client_loop()
{
    if (!s_wsc.ws_up)
    {
        return PROTO_FALSE;
    }
    process_rx();
    if (s_wsc.closed)
    {
        ws_close_tcp();
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

proto_bool ws_client_connected()
{
    return s_wsc.ws_up;
}

void ws_client_close()
{
    if (s_wsc.ws_up)
    {
        ws_send_frame(WSC_OP_CLOSE, NULL, 0);
    }
    ws_close_tcp();
}

#else // host build: transport is a stub

void ws_client_on_message(WsClientMessageCb cb)
{
    (void)cb;
}
proto_bool ws_client_connect(const char *host, uint16_t port, proto_bool use_tls, const char *path)
{
    (void)host;
    (void)port;
    (void)use_tls;
    (void)path;
    return PROTO_FALSE;
}
proto_bool ws_client_send_text(const char *text)
{
    (void)text;
    return PROTO_FALSE;
}
proto_bool ws_client_send_binary(const uint8_t *data, size_t len)
{
    (void)data;
    (void)len;
    return PROTO_FALSE;
}
proto_bool ws_client_loop()
{
    return PROTO_FALSE;
}
proto_bool ws_client_connected()
{
    return PROTO_FALSE;
}
void ws_client_close()
{
}

#endif // PROTOCORE_HOT

#endif // PC_ENABLE_WS_CLIENT
