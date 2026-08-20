// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ws_client.c
 * @brief The WebSocket Protocol (RFC 6455), client end: the opening handshake codec (sec 4), the
 *        base framing protocol (sec 5), and the one connection they run over.
 *
 * The codec halves hold nothing and work in the caller's buffer. The transport half owns one
 * connection: the outbound TCP client under it, the shared persistent client TLS session when
 * /secure/ is set, a receive ring, and the frame buffers. All of it static; no heap.
 */

#include "services/net/ws_client/ws_client.h"

static uint8_t base64_work[16]; // the borrow an entry takes; Base64 never reads it

#if PROTOCORE_ENABLE_WS_CLIENT

#include "crypto/hash/sha1/sha1.h"                            // Sha1: the accept computation
#include "crypto/rng/rng.h"                                   // Rng: the key and the Masking-key
#include "mmgr/membuild/membuild.h"                           // protocore_sb: the request-line and its field lines
#include "mmgr/protomem/protomem.h"                           // mem.cpy / mem.cmp / mem.chr
#include "mmgr/protostr/protostr.h"                           // str.len / str.starts
#include "mmgr/secure/secure.h"                               // the pool the digest borrow comes from
#include "mmgr/span/span.h"                                   // protocore_span, span.ok
#include "network_drivers/presentation/codec/base64/base64.h" // Base64.encode (RFC 4648)
#include "server/clock/clock.h"                               // protocore_millis, pcdelay

#if PROTOCORE_HAS_NET_STACK
#include "network_drivers/transport/tcp/client/client.h" // TcpClient: the outbound transport (L4)
#endif
#if PROTOCORE_HAS_VENDOR_TLS && PROTOCORE_ENABLE_WS_CLIENT_TLS
#include "network_drivers/tls/tls.h"
#endif
#ifdef PROTOCORE_WS_CLIENT_DEBUG
#include <stdio.h>
#endif

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

// RFC 6455 sec 1.3: the GUID concatenated with |Sec-WebSocket-Key| before the SHA-1.
static const char WS_ACCEPT_GUID[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

#define WSC_CONCAT_CAP 64    // |Sec-WebSocket-Key| plus the GUID, hashed in one buffer
#define WSC_FRAME_HDR_MAX 14 // 2 + an 8-octet extended length + a 4-octet Masking-key (RFC 6455 sec 5.2)
#define WSC_LEN16 126        // Payload len 126: the next 2 octets carry the length (RFC 6455 sec 5.2)
#define WSC_LEN64 127        // Payload len 127: the next 8 octets carry it (RFC 6455 sec 5.2)
#define WSC_STATUS_MIN 12    // "HTTP/1.1 101" is the shortest status-line worth reading (RFC 9112 sec 4)
#define WSC_RESP_CAP 512     // the server's opening handshake, up to the empty line ending it
#define WSC_CONNECT_TIMEOUT_MS 8000
#define WSC_POLL_MS 5
#define WSC_PUMP_CHUNK 256

#ifdef PROTOCORE_WS_CLIENT_DEBUG
#define WSC_DBG(...) printf(__VA_ARGS__)
#else
#define WSC_DBG(...) ((void)0)
#endif

// ---------------------------------------------------------------------------
// Typedefs
// ---------------------------------------------------------------------------

#if PROTOCORE_HAS_NET_STACK

#define WSC_RING_SIZE ((size_t)PROTOCORE_WS_CLIENT_BUF_SIZE)
#define WSC_RING_MASK (WSC_RING_SIZE - 1u)

_Static_assert((PROTOCORE_WS_CLIENT_BUF_SIZE & (PROTOCORE_WS_CLIENT_BUF_SIZE - 1)) == 0,
               "PROTOCORE_WS_CLIENT_BUF_SIZE must be a power of two: the receive ring wraps by mask");

/**
 * @brief The one connection's compile-time storage: its transport slot, its ring and its buffers.
 *
 * All of it BSS, so a connection costs no heap and nothing lands on a task stack.
 */
struct WsClientStorage
{
    WsClientMessageCb on_message; ///< where a reassembled Text or Binary message is delivered
    int cid;                      ///< the outbound transport slot, or < 0 when there is none
    volatile proto_bool closed;   ///< the peer closed or the transport failed
    proto_bool established;       ///< the opening handshake completed (RFC 6455 sec 4.1)
    proto_bool secure;            ///< /secure/: the frames ride a TLS session (RFC 6455 sec 3)

    uint8_t rx[WSC_RING_SIZE];  ///< inbound plaintext ring, filled by the pump
    volatile size_t rx_head;    ///< where the pump writes
    volatile size_t rx_tail;    ///< where the frame reader reads
    uint8_t pkt[WSC_RING_SIZE]; ///< one frame copied out of the ring to parse
    uint8_t tx[WSC_RING_SIZE];  ///< the handshake or the frame being sent

    uint8_t msg[WSC_RING_SIZE]; ///< fragments joined into one message (RFC 6455 sec 5.4)
    size_t msg_len;             ///< how much of it is filled
    uint8_t msg_op;             ///< the opcode the first fragment carried
};

#endif // PROTOCORE_HAS_NET_STACK

#if PROTOCORE_HAS_NET_STACK
// The caller's borrow, split: the context at its offset. One pointer arrives and every
// region is that pointer plus a compile-time offset, so the assert below proves the span
// covers them before anything runs.
#define WS_CLIENT_OFF_CTX 0u
static_assert(WS_CLIENT_OFF_CTX + sizeof(struct WsClientStorage) <= PROTOCORE_WS_CLIENT_BORROW,
              "PROTOCORE_WS_CLIENT_BORROW is short of the module context - raise it in protocore_config.h, which"
              " sums it into its arena");

// The region, at its offset in the caller's borrow.
#define WS_CLIENT_CTX(w) ((struct WsClientStorage *)(void *)((w) + WS_CLIENT_OFF_CTX))

// --- the program's shared state, beside the namespace not on it -------------

// The one owned instance, private to this TU: the pointer to the bytes this module took for
// itself. A caller that hands in its own borrow never reaches it.
typedef struct
{
    uint8_t *span; ///< PROTOCORE_WS_CLIENT_BORROW persistent bytes
} WsClientOwnCtx;
static WsClientOwnCtx s_own;

// Not an entry: an entry takes a borrow and this is where that borrow comes from.
uint8_t *protocore_ws_client_span(void)
{
    if (s_own.span == NULL)
    {
        s_own.span = protocore_secure_persist_span(PROTOCORE_WS_CLIENT_BORROW).buf;
        // A borrow arrives zeroed, and these do not start at zero.
        WS_CLIENT_CTX(s_own.span)->cid = -1;
    }
    return s_own.span;
}
#else
// No storage member: without a network stack there is no connection to own, and the codec below
// works entirely in the caller's buffers.
#endif

// ---------------------------------------------------------------------------
// The opening handshake (RFC 6455 sec 4), host-testable
// ---------------------------------------------------------------------------

// accept = base64(SHA-1(key || GUID)), the value the server's |Sec-WebSocket-Accept| must carry
// (RFC 6455 sec 1.3, sec 4.2.2 step 5).
static void ws_accept_for_key(uint8_t *restrict work)
{
    (void)work;
    char *accept = WsClient.handshake.accept;
    const size_t cap = WsClient.handshake.accept_cap;
    const char *key = WsClient.handshake.key;
    if (!accept || cap == 0)
    {
        return;
    }
    accept[0] = '\0';
    if (!key)
    {
        return;
    }
    char concat[WSC_CONCAT_CAP];
    const size_t klen = str.len(key, sizeof(concat));
    const size_t glen = sizeof(WS_ACCEPT_GUID) - 1;
    if (klen + glen >= sizeof(concat) || cap < PROTOCORE_WS_ACCEPT_CAP)
    {
        return;
    }
    mem.cpy(concat, key, klen);
    mem.cpy(concat + klen, WS_ACCEPT_GUID, glen);
    uint8_t digest[PROTOCORE_SHA1_DIGEST_LEN];
    const size_t mark = protocore_secure_mark();
    protocore_span w = protocore_secure_span(PROTOCORE_SHA1_BORROW, 8);
    if (!span.ok(w))
    {
        protocore_secure_release(mark);
        return;
    }
    Sha1V.hash_args.data = (const uint8_t *)concat;
    Sha1V.hash_args.len = klen + glen;
    Sha1V.hash_args.out = digest;
    Sha1.hash(w.buf);
    protocore_secure_release(mark);
    Base64V.encode_args.src = digest;
    Base64V.encode_args.src_len = PROTOCORE_SHA1_DIGEST_LEN;
    Base64V.encode_args.dst = accept;
    Base64.encode(base64_work);
}

// The client's opening handshake: a GET request-line (RFC 9112 sec 3) and the field lines RFC 6455
// sec 4.1 requires. A |Sec-WebSocket-Protocol| offer is emitted only when a subprotocol is named,
// and the server echoes the one it selected; null or empty omits the field line.
static void ws_build_opening_handshake(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = WsClient.buf.out;
    const size_t cap = WsClient.buf.cap;
    const char *host = WsClient.handshake.host;
    const char *resource_name = WsClient.handshake.resource_name;
    const char *key = WsClient.handshake.key;
    const char *subprotocol = WsClient.handshake.subprotocol;
    WsClient.n = 0;
    if (!out || !host || !resource_name || !key)
    {
        return;
    }
    protocore_sb sb = {(char *)out, cap, 0, PROTO_TRUE};
    Sb.put(&sb, "GET ");
    Sb.put(&sb, resource_name);
    Sb.put(&sb, " HTTP/1.1\r\nHost: ");
    Sb.put(&sb, host);
    Sb.put(&sb, "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: ");
    Sb.put(&sb, key);
    if (subprotocol && subprotocol[0])
    {
        Sb.put(&sb, "\r\nSec-WebSocket-Protocol: ");
        Sb.put(&sb, subprotocol);
    }
    Sb.put(&sb, "\r\nSec-WebSocket-Version: 13\r\n\r\n");
    const size_t n = Sb.finish(&sb);
    if (sb.ok)
    {
        WsClient.n = n;
    }
}

// The value of the field line named @p name within [buf, buf+len): past the colon and any optional
// whitespace, its length in *vlen, or NULL when no line names it. Field lines are
// "field-name ':' OWS field-value OWS" (RFC 9112 sec 5) and field names are case-insensitive
// (RFC 9110 sec 5.1).
static const char *field_value(const uint8_t *buf, size_t len, const char *name, size_t *vlen)
{
    const size_t nlen = str.len(name, len + 1);
    const uint8_t *p = buf;
    const uint8_t *end = buf + len;
    while (p + nlen + 1 < end)
    {
        if (str.starts((const char *)p, name, nlen + 1u, PROTO_TRUE) && p[nlen] == ':')
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

// The server's opening handshake: a 101 Switching Protocols status-line (RFC 9110 sec 15.2.2)
// carrying a |Sec-WebSocket-Accept| equal to the value the accept computation produced
// (RFC 6455 sec 4.1).
static void ws_check_server_handshake(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = WsClient.buf.in;
    const size_t len = WsClient.buf.avail;
    const char *accept = WsClient.handshake.accept;
    WsClient.ok = PROTO_FALSE;
    if (!buf || len < WSC_STATUS_MIN || !accept)
    {
        return;
    }
    const uint8_t *eol = (const uint8_t *)mem.chr(buf, len, '\n');
    if (!eol)
    {
        return;
    }
    proto_bool is_101 = PROTO_FALSE;
    for (const uint8_t *q = buf; q + 3 < eol; q++)
    {
        if (q[0] == '1' && q[1] == '0' && q[2] == '1')
        {
            is_101 = PROTO_TRUE;
            break;
        }
    }
    if (!is_101)
    {
        return;
    }
    size_t vlen = 0;
    const char *got = field_value(buf, len, "Sec-WebSocket-Accept", &vlen);
    if (!got)
    {
        return;
    }
    WsClient.ok = (vlen == str.len(accept, vlen + 1)) && mem.cmp(got, accept, vlen) == 0;
}

// ---------------------------------------------------------------------------
// The base framing protocol (RFC 6455 sec 5), host-testable
// ---------------------------------------------------------------------------

// One FIN frame: FIN and the 4-bit opcode, the Payload len in its short, 16-bit or 64-bit form, the
// 4-octet Masking-key, then Payload data XORed with octet i modulo 4 of that key (sec 5.2, sec 5.3).
static void ws_build_frame(uint8_t *restrict work)
{
    (void)work;
    uint8_t *out = WsClient.buf.out;
    const size_t cap = WsClient.buf.cap;
    const uint8_t opcode = WsClient.frame.opcode;
    const uint8_t *payload = WsClient.frame.payload;
    const size_t len = WsClient.frame.payload_len;
    const uint8_t *mask = WsClient.frame.masking_key;
    WsClient.n = 0;
    if (!out || !mask)
    {
        return;
    }
    size_t hdr = 2 + 4; // the two fixed octets and the Masking-key
    if (len >= WSC_LEN16 && len < 65536)
    {
        hdr += 2;
    }
    else if (len >= 65536)
    {
        hdr += 8;
    }
    if (hdr + len > cap)
    {
        return;
    }

    size_t i = 0;
    out[i++] = (uint8_t)(0x80u | (opcode & 0x0Fu));
    if (len < WSC_LEN16)
    {
        out[i++] = (uint8_t)(0x80u | len);
    }
    else if (len < 65536)
    {
        out[i++] = (uint8_t)(0x80u | WSC_LEN16);
        out[i++] = (uint8_t)(len >> 8);
        out[i++] = (uint8_t)(len & 0xFFu);
    }
    else
    {
        out[i++] = (uint8_t)(0x80u | WSC_LEN64);
        for (int s = 56; s >= 0; s -= 8)
        {
            out[i++] = (uint8_t)((uint64_t)len >> s);
        }
    }
    mem.cpy(out + i, mask, 4);
    i += 4;
    for (size_t j = 0; j < len; j++)
    {
        out[i + j] = (uint8_t)(payload[j] ^ mask[j & 3]);
    }
    WsClient.n = i + len;
}

// One inbound frame's header, read back into the frame members. False while fewer octets than the
// whole frame are present (RFC 6455 sec 5.2).
static void ws_parse_frame(uint8_t *restrict work)
{
    (void)work;
    const uint8_t *buf = WsClient.buf.in;
    const size_t avail = WsClient.buf.avail;
    WsClient.ok = PROTO_FALSE;
    if (!buf || avail < 2)
    {
        return;
    }
    const uint8_t b0 = buf[0];
    const uint8_t b1 = buf[1];
    const proto_bool masked = (b1 & 0x80) != 0;
    uint64_t len = b1 & 0x7F;
    size_t off = 2;
    if (len == WSC_LEN16)
    {
        if (avail < off + 2)
        {
            return;
        }
        len = ((uint64_t)buf[off] << 8) | buf[off + 1];
        off += 2;
    }
    else if (len == WSC_LEN64)
    {
        if (avail < off + 8)
        {
            return;
        }
        uint64_t v = 0;
        for (int s = 0; s < 8; s++)
        {
            v = (v << 8) | buf[off + s];
        }
        if (v > 0xFFFFFFFFu) // a Payload len no buffer on a constrained part can hold
        {
            return;
        }
        len = v;
        off += 8;
    }
    if (masked)
    {
        off += 4; // a server frame carries no Masking-key (sec 5.3), but stay aligned if one does
    }
    if (avail < off + (size_t)len)
    {
        return;
    }
    WsClient.frame.opcode = (uint8_t)(b0 & 0x0F);
    WsClient.frame.fin = (b0 & 0x80) != 0;
    WsClient.frame.payload_off = off;
    WsClient.frame.payload_len = (size_t)len;
    WsClient.frame.consumed = off + (size_t)len;
    WsClient.ok = PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// The connection: the outbound transport, and wss:// over the persistent client TLS session
// ---------------------------------------------------------------------------
#if PROTOCORE_HAS_NET_STACK

static size_t ring_avail(uint8_t *restrict work)
{
    return (WS_CLIENT_CTX(work)->rx_head + WSC_RING_SIZE - WS_CLIENT_CTX(work)->rx_tail) & WSC_RING_MASK;
}

static uint8_t ring_peek(uint8_t *restrict work, size_t i)
{
    return WS_CLIENT_CTX(work)->rx[(WS_CLIENT_CTX(work)->rx_tail + i) & WSC_RING_MASK];
}

static void ring_advance(uint8_t *restrict work, size_t n)
{
    WS_CLIENT_CTX(work)->rx_tail = (WS_CLIENT_CTX(work)->rx_tail + n) & WSC_RING_MASK;
}

static void ring_copy(uint8_t *restrict work, uint8_t *dst, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        dst[i] = WS_CLIENT_CTX(work)->rx[(WS_CLIENT_CTX(work)->rx_tail + i) & WSC_RING_MASK];
    }
}

static void ring_write(uint8_t *restrict work, const uint8_t *src, size_t n)
{
    for (size_t i = 0; i < n; i++)
    {
        WS_CLIENT_CTX(work)->rx[WS_CLIENT_CTX(work)->rx_head] = src[i];
        WS_CLIENT_CTX(work)->rx_head = (WS_CLIENT_CTX(work)->rx_head + 1) & WSC_RING_MASK;
    }
}

static proto_bool ws_tx_plain(uint8_t *restrict work, const uint8_t *data, size_t len)
{
    TcpClient.cid = WS_CLIENT_CTX(work)->cid;
    TcpClient.io.data = data;
    TcpClient.io.len = len;
    TcpClient.send(protocore_tcp_client_span());
    return TcpClient.ok;
}

// Drain plaintext octets from the transport slot into the receive ring.
static void ws_pump_plain(uint8_t *restrict work)
{
    uint8_t tmp[WSC_PUMP_CHUNK];
    for (;;)
    {
        const size_t room = (WSC_RING_SIZE - 1) - ring_avail(work);
        if (room == 0)
        {
            break;
        }
        TcpClient.cid = WS_CLIENT_CTX(work)->cid;
        TcpClient.io.buf = tmp;
        TcpClient.io.cap = room < sizeof(tmp) ? room : sizeof(tmp);
        TcpClient.read(protocore_tcp_client_span());
        const size_t n = TcpClient.n;
        if (n == 0)
        {
            TcpClient.cid = WS_CLIENT_CTX(work)->cid;
            TcpClient.is_closed(protocore_tcp_client_span());
            if (TcpClient.ok)
            {
                WS_CLIENT_CTX(work)->closed = PROTO_TRUE;
            }
            break;
        }
        ring_write(work, tmp, n);
    }
}

#if PROTOCORE_ENABLE_WS_CLIENT_TLS
// The TLS BIO over the transport slot: ciphertext out through the slot, ciphertext in by draining
// it. The signature is the TLS module's, so these two reach the module instance directly.
static int ws_tls_send(void *bio, const unsigned char *buf, size_t len)
{
    (void)bio;
    const size_t cap = len > 0xFFFF ? 0xFFFF : len;
    return ws_tx_plain(protocore_ws_client_span(), buf, cap) ? (int)cap : PROTOCORE_PLATFORM_TLS_WANT_WRITE;
}

static int ws_tls_recv(void *bio, unsigned char *buf, size_t len)
{
    (void)bio;
    TcpClient.cid = s_ws.store->cid;
    TcpClient.io.buf = buf;
    TcpClient.io.cap = len;
    TcpClient.read(protocore_tcp_client_span());
    const size_t n = TcpClient.n;
    if (n == 0)
    {
        TcpClient.cid = s_ws.store->cid;
        TcpClient.is_closed(protocore_tcp_client_span());
        return TcpClient.ok ? 0 : PROTOCORE_PLATFORM_TLS_WANT_READ;
    }
    return (int)n;
}

// Drain plaintext out of the TLS session into the receive ring.
static void ws_pump_tls(uint8_t *restrict work)
{
    uint8_t tmp[WSC_PUMP_CHUNK];
    for (;;)
    {
        const size_t room = (WSC_RING_SIZE - 1) - ring_avail(work);
        if (room == 0)
        {
            break;
        }
        const size_t want = room < sizeof(tmp) ? room : sizeof(tmp);
        const int n = protocore_tls_client_session_read(tmp, want);
        if (n <= 0)
        {
            if (n < 0)
            {
                WS_CLIENT_CTX(work)->closed = PROTO_TRUE;
            }
            break;
        }
        ring_write(work, tmp, (size_t)n);
    }
}
#endif // PROTOCORE_ENABLE_WS_CLIENT_TLS

static void ws_pump(uint8_t *restrict work)
{
#if PROTOCORE_ENABLE_WS_CLIENT_TLS
    if (WS_CLIENT_CTX(work)->secure)
    {
        ws_pump_tls(work);
        return;
    }
#endif
    ws_pump_plain(work);
}

// Send framed octets, through the TLS session when /secure/ is set.
static proto_bool ws_tx(uint8_t *restrict work, const uint8_t *data, size_t len)
{
#if PROTOCORE_ENABLE_WS_CLIENT_TLS
    if (WS_CLIENT_CTX(work)->secure)
    {
        return protocore_tls_client_session_write(data, len) == (int)len;
    }
#endif
    return ws_tx_plain(work, data, len);
}

// Frame and send with a Masking-key drawn fresh per frame from the CSPRNG (RFC 6455 sec 5.3,
// sec 10.3).
static proto_bool ws_emit_frame(uint8_t *restrict work, uint8_t opcode, const uint8_t *payload, size_t len)
{
    if (!WS_CLIENT_CTX(work)->established)
    {
        return PROTO_FALSE;
    }
    uint8_t mask[4];
    RngV.fill_args.out = mask;
    RngV.fill_args.len = sizeof(mask);
    Rng.fill(protocore_rng_span());
    WsClient.frame.opcode = opcode;
    WsClient.frame.payload = payload;
    WsClient.frame.payload_len = len;
    WsClient.frame.masking_key = mask;
    WsClient.buf.out = WS_CLIENT_CTX(work)->tx;
    WsClient.buf.cap = sizeof(WS_CLIENT_CTX(work)->tx);
    ws_build_frame(work);
    const size_t n = WsClient.n;
    return n != 0 && ws_tx(work, WS_CLIENT_CTX(work)->tx, n);
}

// RFC 6455 sec 7.1.1: close the WebSocket connection - end the TLS session, then the transport slot.
static void ws_close_transport(uint8_t *restrict work)
{
#if PROTOCORE_ENABLE_WS_CLIENT_TLS
    if (WS_CLIENT_CTX(work)->secure)
    {
        protocore_tls_client_session_end();
    }
#endif
    if (WS_CLIENT_CTX(work)->cid >= 0)
    {
        TcpClient.cid = WS_CLIENT_CTX(work)->cid;
        TcpClient.close(protocore_tcp_client_span());
    }
    WS_CLIENT_CTX(work)->cid = -1;
    WS_CLIENT_CTX(work)->established = PROTO_FALSE;
}

static void ws_deliver(uint8_t *restrict work, uint8_t opcode, const uint8_t *payload, size_t len)
{
    if (WS_CLIENT_CTX(work)->on_message && (opcode == (uint8_t)WSC_OP_TEXT || opcode == (uint8_t)WSC_OP_BINARY))
    {
        WS_CLIENT_CTX(work)->on_message(opcode, payload, len);
    }
}

// One parsed frame: join fragments (sec 5.4), answer Ping with Pong carrying the same Application
// data (sec 5.5.2, sec 5.5.3), and echo a Close (sec 5.5.1).
static void ws_handle_frame(uint8_t *restrict work, uint8_t opcode, proto_bool fin, const uint8_t *payload, size_t len)
{
    switch ((WsClientOpcode)opcode)
    {
    case WSC_OP_TEXT:
    case WSC_OP_BINARY:
        if (fin)
        {
            ws_deliver(work, opcode, payload, len); // an unfragmented message is one frame
        }
        else
        {
            WS_CLIENT_CTX(work)->msg_op = opcode; // the first fragment
            WS_CLIENT_CTX(work)->msg_len =
                len < sizeof(WS_CLIENT_CTX(work)->msg) ? len : sizeof(WS_CLIENT_CTX(work)->msg);
            mem.cpy(WS_CLIENT_CTX(work)->msg, payload, WS_CLIENT_CTX(work)->msg_len);
        }
        break;
    case WSC_OP_CONT:
        if (WS_CLIENT_CTX(work)->msg_len + len <= sizeof(WS_CLIENT_CTX(work)->msg))
        {
            mem.cpy(WS_CLIENT_CTX(work)->msg + WS_CLIENT_CTX(work)->msg_len, payload, len);
            WS_CLIENT_CTX(work)->msg_len += len;
        }
        if (fin)
        {
            ws_deliver(work, WS_CLIENT_CTX(work)->msg_op, WS_CLIENT_CTX(work)->msg, WS_CLIENT_CTX(work)->msg_len);
            WS_CLIENT_CTX(work)->msg_len = 0;
        }
        break;
    case WSC_OP_PING:
        ws_emit_frame(work, (uint8_t)WSC_OP_PONG, payload, len);
        break;
    case WSC_OP_CLOSE:
        ws_emit_frame(work, (uint8_t)WSC_OP_CLOSE, NULL, 0);
        WS_CLIENT_CTX(work)->closed = PROTO_TRUE;
        break;
    case WSC_OP_PONG:
    default:
        break;
    }
}

// RFC 6455 sec 6.2: read what arrived and process each complete frame in it.
static void ws_process_rx(uint8_t *restrict work)
{
    ws_pump(work);
    for (;;)
    {
        const size_t avail = ring_avail(work);
        if (avail < 2)
        {
            return;
        }
        // A frame header is at most WSC_FRAME_HDR_MAX octets, so peeking that many is enough to
        // parse one; the parse is handed the ring's real count to test the frame's completeness.
        uint8_t hdr[WSC_FRAME_HDR_MAX];
        const size_t hn = avail < sizeof(hdr) ? avail : sizeof(hdr);
        for (size_t i = 0; i < hn; i++)
        {
            hdr[i] = ring_peek(work, i);
        }
        WsClient.buf.in = hdr;
        WsClient.buf.avail = avail;
        ws_parse_frame(work);
        if (!WsClient.ok)
        {
            return; // the header or the frame it names has not fully arrived
        }
        const uint8_t opcode = WsClient.frame.opcode;
        const proto_bool fin = WsClient.frame.fin;
        const size_t off = WsClient.frame.payload_off;
        const size_t plen = WsClient.frame.payload_len;
        const size_t consumed = WsClient.frame.consumed;
        if (consumed > sizeof(WS_CLIENT_CTX(work)->pkt))
        {
            ring_advance(work, consumed); // a frame no buffer holds: drop it
            continue;
        }
        ring_copy(work, WS_CLIENT_CTX(work)->pkt, consumed);
        ring_advance(work, consumed);
        ws_handle_frame(work, opcode, fin, WS_CLIENT_CTX(work)->pkt + off, plen);
    }
}

static void ws_on_message(uint8_t *restrict work)
{
    WS_CLIENT_CTX(work)->on_message = WsClient.msg.on_message;
}

// RFC 6455 sec 4.1: dial /host/ and /port/, raise TLS when /secure/ is set, send the client's
// opening handshake and verify the server's. The connection is established when that verifies.
static void ws_connect(uint8_t *restrict work)
{
    const char *host = WsClient.handshake.host;
    const char *resource_name = WsClient.handshake.resource_name;
    const uint16_t port = WsClient.handshake.port;
    const proto_bool secure = WsClient.handshake.secure;
    WsClient.ok = PROTO_FALSE;
    if (!host || !resource_name)
    {
        return;
    }
#if !PROTOCORE_ENABLE_WS_CLIENT_TLS
    if (secure)
    {
        return;
    }
#endif
    WS_CLIENT_CTX(work)->rx_head = 0;
    WS_CLIENT_CTX(work)->rx_tail = 0;
    WS_CLIENT_CTX(work)->closed = PROTO_FALSE;
    WS_CLIENT_CTX(work)->established = PROTO_FALSE;
    WS_CLIENT_CTX(work)->msg_len = 0;
    WS_CLIENT_CTX(work)->secure = secure;

    const uint32_t deadline = Clock.ms + WSC_CONNECT_TIMEOUT_MS;

    TcpClient.dial.host = host;
    TcpClient.dial.port = port;
    TcpClient.dial.timeout_ms = WSC_CONNECT_TIMEOUT_MS;
    TcpClient.open(protocore_tcp_client_span());
    WS_CLIENT_CTX(work)->cid = TcpClient.i32;
    if (WS_CLIENT_CTX(work)->cid < 0)
    {
        WSC_DBG("[wsc] open failed (%d)\n", WS_CLIENT_CTX(work)->cid);
        return;
    }

    // The open returns before the connection exists: step it until the slot reports the handshake
    // complete, reports itself closed, or the deadline passes.
    proto_bool up = PROTO_FALSE;
    while ((int32_t)(deadline - Clock.ms) > 0)
    {
        TcpClient.cid = WS_CLIENT_CTX(work)->cid;
        TcpClient.connected(protocore_tcp_client_span());
        up = TcpClient.ok;
        if (up)
        {
            break;
        }
        TcpClient.cid = WS_CLIENT_CTX(work)->cid;
        TcpClient.is_closed(protocore_tcp_client_span());
        if (TcpClient.ok)
        {
            break;
        }
        pcdelay(WSC_POLL_MS);
    }
    if (!up)
    {
        WSC_DBG("[wsc] transport never came up\n");
        ws_close_transport(work);
        return;
    }

#if PROTOCORE_ENABLE_WS_CLIENT_TLS
    if (WS_CLIENT_CTX(work)->secure)
    {
        if (!protocore_tls_client_session_begin(host, ws_tls_send, ws_tls_recv))
        {
            WSC_DBG("[wsc] TLS session begin failed\n");
            ws_close_transport(work);
            return;
        }
        protocore_tls_state h = PROTOCORE_TLS_BUSY;
        while ((h = protocore_tls_client_session_handshake()) == PROTOCORE_TLS_BUSY && !WS_CLIENT_CTX(work)->closed &&
               (int32_t)(deadline - Clock.ms) > 0)
        {
            pcdelay(WSC_POLL_MS);
        }
        if (h != PROTOCORE_TLS_READY)
        {
            WSC_DBG("[wsc] TLS handshake h=%d closed=%d\n", (int)h, (int)WS_CLIENT_CTX(work)->closed);
            ws_close_transport(work);
            return;
        }
        WSC_DBG("[wsc] TLS handshake ok\n");
    }
#endif

    // |Sec-WebSocket-Key| is 16 fresh random octets, base64-encoded (RFC 6455 sec 4.1); the accept
    // it implies is computed now and compared against the field the server sends back.
    uint8_t key_raw[16];
    RngV.fill_args.out = key_raw;
    RngV.fill_args.len = sizeof(key_raw);
    Rng.fill(protocore_rng_span());
    char key_b64[PROTOCORE_WS_KEY_CAP];
    Base64V.encode_args.src = key_raw;
    Base64V.encode_args.src_len = sizeof(key_raw);
    Base64V.encode_args.dst = key_b64;
    Base64.encode(base64_work);
    char accept[PROTOCORE_WS_ACCEPT_CAP];
    WsClient.handshake.key = key_b64;
    WsClient.handshake.accept = accept;
    WsClient.handshake.accept_cap = sizeof(accept);
    ws_accept_for_key(work);

    WsClient.buf.out = WS_CLIENT_CTX(work)->tx;
    WsClient.buf.cap = sizeof(WS_CLIENT_CTX(work)->tx);
    ws_build_opening_handshake(work);
    const size_t n = WsClient.n;
    if (n == 0 || !ws_tx(work, WS_CLIENT_CTX(work)->tx, n))
    {
        ws_close_transport(work);
        return;
    }

    // The server's opening handshake ends at the empty line that closes its field section
    // (RFC 9112 sec 2.1).
    uint8_t resp[WSC_RESP_CAP];
    size_t rlen = 0;
    proto_bool done = PROTO_FALSE;
    while (!done && !WS_CLIENT_CTX(work)->closed && (int32_t)(deadline - Clock.ms) > 0)
    {
        ws_pump(work);
        while (ring_avail(work) > 0 && rlen < sizeof(resp))
        {
            resp[rlen++] = ring_peek(work, 0);
            ring_advance(work, 1);
            if (rlen >= 4 && resp[rlen - 4] == '\r' && resp[rlen - 3] == '\n' && resp[rlen - 2] == '\r' &&
                resp[rlen - 1] == '\n')
            {
                done = PROTO_TRUE;
                break;
            }
        }
        if (!done)
        {
            pcdelay(WSC_POLL_MS);
        }
    }
    WsClient.ok = PROTO_FALSE;
    if (done)
    {
        WsClient.buf.in = resp;
        WsClient.buf.avail = rlen;
        ws_check_server_handshake(work);
    }
    if (!WsClient.ok)
    {
        WSC_DBG("[wsc] handshake fail done=%d rlen=%u resp:\n%.*s\n", (int)done, (unsigned)rlen, (int)rlen,
                (const char *)resp);
        ws_close_transport(work);
        return;
    }
    WS_CLIENT_CTX(work)->established = PROTO_TRUE;
    WsClient.ok = PROTO_TRUE;
}

static void ws_send_text(uint8_t *restrict work)
{
    const char *text = WsClient.msg.text;
    const size_t len = text ? str.len(text, PROTOCORE_WS_CLIENT_BUF_SIZE) : 0;
    WsClient.ok = ws_emit_frame(work, (uint8_t)WSC_OP_TEXT, (const uint8_t *)text, len);
}

static void ws_send_binary(uint8_t *restrict work)
{
    WsClient.ok = ws_emit_frame(work, (uint8_t)WSC_OP_BINARY, WsClient.msg.data, WsClient.msg.len);
}

static void ws_loop(uint8_t *restrict work)
{
    WsClient.ok = PROTO_FALSE;
    if (!WS_CLIENT_CTX(work)->established)
    {
        return;
    }
    ws_process_rx(work);
    if (WS_CLIENT_CTX(work)->closed)
    {
        ws_close_transport(work);
        return;
    }
    WsClient.ok = PROTO_TRUE;
}

static void ws_connected(uint8_t *restrict work)
{
    WsClient.ok = WS_CLIENT_CTX(work)->established;
}

// RFC 6455 sec 5.5.1 then sec 7.1.1: send a Close frame, then close the WebSocket connection.
static void ws_close(uint8_t *restrict work)
{
    if (WS_CLIENT_CTX(work)->established)
    {
        ws_emit_frame(work, (uint8_t)WSC_OP_CLOSE, NULL, 0);
    }
    ws_close_transport(work);
}

#else // no network stack: the codec stands alone and the connection calls answer no

static void ws_on_message(uint8_t *restrict work)
{
    (void)work;
}

static void ws_connect(uint8_t *restrict work)
{
    (void)work;
    WsClient.ok = PROTO_FALSE;
}

static void ws_send_text(uint8_t *restrict work)
{
    (void)work;
    WsClient.ok = PROTO_FALSE;
}

static void ws_send_binary(uint8_t *restrict work)
{
    (void)work;
    WsClient.ok = PROTO_FALSE;
}

static void ws_loop(uint8_t *restrict work)
{
    (void)work;
    WsClient.ok = PROTO_FALSE;
}

static void ws_connected(uint8_t *restrict work)
{
    (void)work;
    WsClient.ok = PROTO_FALSE;
}

static void ws_close(uint8_t *restrict work)
{
    (void)work;
}

#endif // PROTOCORE_HAS_NET_STACK

// Designated, so a member's position in the struct does not decide what it binds to.
WsClientNs WsClient = {.accept_for_key = ws_accept_for_key,
                       .build_opening_handshake = ws_build_opening_handshake,
                       .check_server_handshake = ws_check_server_handshake,
                       .build_frame = ws_build_frame,
                       .parse_frame = ws_parse_frame,
                       .on_message = ws_on_message,
                       .connect = ws_connect,
                       .send_text = ws_send_text,
                       .send_binary = ws_send_binary,
                       .loop = ws_loop,
                       .connected = ws_connected,
                       .close = ws_close};

#endif // PROTOCORE_ENABLE_WS_CLIENT
