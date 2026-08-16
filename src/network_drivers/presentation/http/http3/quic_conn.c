// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_quic_conn.c
 * @brief Stateful QUIC v1 server connection engine (see protocore_quic_conn.h).
 */

#include "network_drivers/presentation/http/http3/quic_conn.h"
#include "mmgr/plaintext.h" // the streams carry HTTP/3; their bytes borrow from that arena
#include "mmgr/protomem.h"

#if PROTOCORE_ENABLE_HTTP3

#include "crypto/aead/aes128gcm.h" // PROTOCORE_AES128GCM_TAG_LEN
#include "mmgr/secure.h"            // the context span is key material
#include "network_drivers/presentation/http/http3/quic_crypto.h"
#include "network_drivers/presentation/http/http3/quic_frame.h"
#include "network_drivers/presentation/http/http3/quic_packet.h"
#include "network_drivers/presentation/http/http3/quic_tls.h"
#include "network_drivers/presentation/http/http3/quic_varint.h"

PROTOCORE_BEGIN_DECLS

// Per-connection stream state. The bytes it sends out of are the plaintext span at a fixed offset,
// so only what is not derivable lives here.
typedef struct
{
    uint64_t id;            ///< stream id (UINT64_MAX = free slot)
    uint64_t rx_off;        ///< next in-order byte offset expected
    uint64_t tx_off;        ///< next send offset
    proto_bool rx_fin;      ///< a FIN was received (final size known)
    proto_bool tx_fin;      ///< a FIN should be sent after the buffered tx bytes
    proto_bool tx_fin_sent; ///< the FIN has been sent
    uint8_t *tx;            ///< PROTOCORE_QUIC_STREAM_TX bytes of the connection's plaintext span
    size_t tx_have;         ///< bytes buffered to send
    size_t tx_sent;         ///< bytes of tx already put on the wire
} QuicStream;

// One packet-number space (Initial / Handshake / Application).
typedef struct
{
    uint64_t next_pn;            ///< next packet number to send in this space
    int64_t largest_acked;       ///< largest of our PNs the peer has acknowledged (-1 = none)
    int64_t last_ae_pn;          ///< PN of the last ack-eliciting packet we sent here (-1 = none)
    uint64_t largest_rx;         ///< largest PN received in this space
    proto_bool have_rx;          ///< at least one packet received
    proto_bool ack_eliciting_rx; ///< an ack-eliciting packet is unacknowledged (we owe an ACK)
    proto_bool discarded;        ///< this space's keys have been dropped
    uint64_t crypto_rx_off;      ///< in-order CRYPTO bytes already delivered to the TLS handshake
    uint8_t *crypto_rx;          ///< PROTOCORE_QUIC_CRYPTO_RX bytes of the connection's plaintext span
    size_t crypto_rx_have;       ///< contiguous CRYPTO bytes buffered at crypto_rx_off
    uint64_t crypto_tx_off;      ///< CRYPTO flight bytes already sent from this level
} QuicPnSpace;

// The one definition, private to this TU. It sits at QUIC_OFF_CTX in the caller's secure span, so
// its size never leaves this file and no consumer can name it.
typedef struct
{
    uint8_t *b; ///< the connection's plaintext span, bound once and held for its life

    uint8_t scid[QUIC_MAX_CID_LEN]; ///< our connection ID (peer's DCID toward us)
    uint8_t scid_len;
    uint8_t dcid[QUIC_MAX_CID_LEN]; ///< peer's connection ID (our DCID toward the peer)
    uint8_t dcid_len;
    uint8_t odcid[QUIC_MAX_CID_LEN]; ///< client's original DCID (Initial keys + transport param)
    uint8_t odcid_len;

    QuicInitialSecrets initial; ///< Initial keys derived from odcid
    QuicTls tls;                ///< the TLS 1.3 handshake

    QuicPnSpace space[3]; ///< indexed by QUIC_ENC_*

    QuicStream streams[PROTOCORE_QUIC_MAX_STREAMS];

    QuicConnCallbacks cb;

    proto_bool handshake_done_queued; ///< a HANDSHAKE_DONE frame still needs sending
    proto_bool handshake_done_sent;
    proto_bool closed;            ///< a CONNECTION_CLOSE has been sent or received
    proto_bool draining;          ///< peer closed; we only drain
    uint64_t recv_bytes;          ///< total bytes received (anti-amplification budget)
    uint64_t sent_bytes;          ///< total bytes sent before address validation
    proto_bool address_validated; ///< handshake-complete or received enough to lift the 3x limit

    proto_bool pto_armed;     ///< a Probe Timeout is running for the outstanding handshake flight
    uint8_t pto_count;        ///< consecutive PTO expirations (exponential backoff exponent)
    uint32_t pto_deadline_ms; ///< when the PTO fires (caller's monotonic ms; valid when pto_armed)

    proto_bool close_queued;   ///< a CONNECTION_CLOSE is owed to the peer (fatal error hit)
    proto_bool close_is_app;   ///< send the application variant (0x1d)
    proto_bool close_sent;     ///< the CONNECTION_CLOSE has been put on the wire
    uint64_t close_error;      ///< error code to report
    uint64_t close_frame_type; ///< frame type that triggered it (0 when not frame-specific)
    uint8_t close_level;       ///< encryption level to send the close at
} QuicConnCtx;

// The caller's two spans, split. The context takes the whole secure span; the plaintext span is
// grouped by field, so each region's stride is a power of two and stream i reaches its bytes with a
// shift rather than a multiply.
#define QUIC_OFF_CTX 0u
#define QUIC_OFF_TX 0u
#define QUIC_OFF_CRYPTO (QUIC_OFF_TX + (size_t)PROTOCORE_QUIC_MAX_STREAMS * PROTOCORE_QUIC_STREAM_TX)
static_assert(sizeof(QuicConnCtx) <= PROTOCORE_QUIC_CONN_CTX_BORROW,
              "PROTOCORE_QUIC_CONN_CTX_BORROW is short of the connection context - raise it in "
              "protocore_config.h, which derives PROTOCORE_SECURE_ARENA_SIZE from it");
static_assert(QUIC_OFF_CRYPTO + 3u * (size_t)PROTOCORE_QUIC_CRYPTO_RX <= PROTOCORE_QUIC_CONN_BORROW,
              "PROTOCORE_QUIC_CONN_BORROW is short of one outbound buffer per stream and one CRYPTO window "
              "per packet-number space - raise it in protocore_config.h, which derives "
              "PROTOCORE_PLAINTEXT_ARENA_SIZE from it");

// The handle a caller sets a call's members on, and the connection the bound span holds.
struct QuicConnInternal
{
    QuicConnCtx *c;  ///< the connection, resolved from the bound context span
    QuicConnNs *ns;  ///< the handle a caller sets a call's members on
};

// The regions, at their offsets in the caller's spans.
#define QUIC_CTX(w) ((QuicConnCtx *)(void *)((w) + QUIC_OFF_CTX))
// QUIC_OFF_CTX is 0, so a callback is handed back the span it was bound with.
#define QUIC_SPAN(c) ((uint8_t *)(void *)(c))
#define QUIC_TX(b, i) ((b) + QUIC_OFF_TX + (size_t)(i) * PROTOCORE_QUIC_STREAM_TX)
#define QUIC_CRYPTO(b, i) ((b) + QUIC_OFF_CRYPTO + (size_t)(i) * PROTOCORE_QUIC_CRYPTO_RX)

// A single STREAM frame's payload cannot exceed one datagram, so it can never overflow a stream's
// reassembly buffer - which is why that clamp in handle_stream carries a coverage exclusion. Both
// sizes are overridable macros (quic_conn.h), so pin the relationship here rather than let a jumbo
// PROTOCORE_QUIC_MAX_DATAGRAM or a shrunken PROTOCORE_QUIC_STREAM_RX silently make the excluded path reachable.
static_assert(
    PROTOCORE_QUIC_MAX_DATAGRAM < PROTOCORE_QUIC_STREAM_RX,
    "PROTOCORE_QUIC_STREAM_RX must exceed PROTOCORE_QUIC_MAX_DATAGRAM: one STREAM frame's payload is bounded by the "
    "datagram, and handle_stream relies on that to deliver it without clamping");

// The frame payload build_frames fills is one datagram; every builder it calls (ACK, CRYPTO header,
// HANDSHAKE_DONE, STREAM header) must fit with room to spare, which is why their failure guards
// carry coverage exclusions. RFC 9000 sec 14.1 already requires at least 1200 octets for Initial
// packets, so pin that floor: a smaller datagram would make those guards reachable.
static_assert(
    PROTOCORE_QUIC_MAX_DATAGRAM >= 1200,
    "PROTOCORE_QUIC_MAX_DATAGRAM must be at least the RFC 9000 sec 14.1 minimum of 1200 octets, which is also "
    "what lets build_frames assume every frame builder has room");

// The open (decrypt) keys for an encryption level: Initial keys come from the DCID, Handshake and
// 1-RTT keys from the TLS handshake. Returns NULL if that level's keys are not available yet.
static QuicPacketKeys *open_keys(QuicConnCtx *qc, int level)
{
    if (level == QUIC_ENC_INITIAL)
    {
        return &qc->initial.client;
    }
    return protocore_quic_tls_keys(&qc->tls, level, /*is_server=*/PROTO_FALSE);
}
static QuicPacketKeys *seal_keys(QuicConnCtx *qc, int level)
{
    if (level == QUIC_ENC_INITIAL)
    {
        return &qc->initial.server;
    }
    return protocore_quic_tls_keys(&qc->tls, level, /*is_server=*/PROTO_TRUE);
}

// The engine is one translation unit; these are defined below in dependency order.
static void quic_conn_close_transport(QuicConnCtx *qc, uint64_t error_code);
static void quic_conn_close_application(QuicConnCtx *qc, uint64_t error_code);
static size_t quic_conn_stream_put(QuicConnCtx *qc, uint64_t stream_id, const uint8_t *data, size_t len,
                                   proto_bool fin);
static proto_bool quic_conn_done(const QuicConnCtx *qc);
static proto_bool quic_conn_gone(const QuicConnCtx *qc);

// The connection's plaintext bytes, split by offset over its streams and packet-number spaces.
static proto_bool quic_conn_slot_storage(QuicConnCtx *qc)
{
    if (qc->b == NULL)
    {
        return PROTO_FALSE;
    }
    for (size_t i = 0; i < PROTOCORE_QUIC_MAX_STREAMS; i++)
    {
        qc->streams[i].tx = QUIC_TX(qc->b, i);
    }
    for (size_t i = 0; i < 3; i++)
    {
        qc->space[i].crypto_rx = QUIC_CRYPTO(qc->b, i);
    }
    return PROTO_TRUE;
}

// Find a stream slot by id, or allocate one; NULL if the table is full.

static QuicStream *stream_get(QuicConnCtx *qc, uint64_t id, proto_bool create)
{
    if (!quic_conn_slot_storage(qc))
    {
        return NULL; // the connection has no bytes; it holds no streams either
    }
    QuicStream *free_slot = NULL;
    for (size_t i = 0; i < PROTOCORE_QUIC_MAX_STREAMS; i++)
    {
        if (qc->streams[i].id == id && qc->streams[i].id != UINT64_MAX)
        {
            return &qc->streams[i];
        }
        if (!free_slot && qc->streams[i].id == UINT64_MAX)
        {
            free_slot = &qc->streams[i];
        }
    }
    if (!create || !free_slot)
    {
        return NULL; // QuicConnNs::stream_send), so the lookup-only arm is never taken
    }
    // The bytes are the connection's and outlive the stream that last held them.
    uint8_t *tx = free_slot->tx;
    mem.set(free_slot, 0, sizeof(*free_slot));
    free_slot->tx = tx;
    mem.set(tx, 0, PROTOCORE_QUIC_STREAM_TX);
    free_slot->id = id;
    return free_slot;
}

static void quic_conn_open(QuicConnCtx *qc, const QuicTlsConfig *cfg, const uint8_t *odcid, uint8_t odcid_len,
                              const uint8_t *peer_scid, uint8_t peer_scid_len, const uint8_t *our_scid,
                              uint8_t our_scid_len, const QuicConnCallbacks *cb)
{
    uint8_t *b = qc->b; // the plaintext span is the connection's, bound before this call
    mem.set(qc, 0, sizeof(*qc));
    qc->b = b;
    if (!quic_conn_slot_storage(qc))
    {
        qc->closed = PROTO_TRUE; // no bytes to run out of; the connection answers nothing
        return;
    }
    // The span carries the previous connection's stream and handshake bytes.
    mem.set(qc->b, 0, PROTOCORE_QUIC_CONN_BORROW);
    mem.cpy(qc->odcid, odcid, odcid_len);
    qc->odcid_len = odcid_len;
    mem.cpy(qc->dcid, peer_scid, peer_scid_len);
    qc->dcid_len = peer_scid_len;
    mem.cpy(qc->scid, our_scid, our_scid_len);
    qc->scid_len = our_scid_len;
    if (cb)
    {
        qc->cb = *cb;
    }

    protocore_quic_derive_initial_secrets(qc->tls.keys_work, odcid, odcid_len, &qc->initial);

    for (int i = 0; i < 3; i++)
    {
        qc->space[i].largest_acked = -1;
        qc->space[i].last_ae_pn = -1;
    }
    for (size_t i = 0; i < PROTOCORE_QUIC_MAX_STREAMS; i++)
    {
        qc->streams[i].id = UINT64_MAX;
    }

    // The server's transport parameters carry the connection IDs the handshake must echo.
    QuicTlsConfig c = *cfg;
    c.params.has_original_dcid = PROTO_TRUE;
    mem.cpy(c.params.original_dcid, odcid, odcid_len);
    c.params.original_dcid_len = odcid_len;
    c.params.has_initial_scid = PROTO_TRUE;
    mem.cpy(c.params.initial_scid, our_scid, our_scid_len);
    c.params.initial_scid_len = our_scid_len;
    protocore_quic_tls_server_init(&qc->tls, &c);
}

// --- Frame handling --------------------------------------------------------------------------
// Queue a transport CONNECTION_CLOSE for a fatal error at @p level; the first error wins (RFC 9000 sec
// 10.2.3). Sending at the level the error was seen on guarantees the peer holds keys to read it.
static void queue_close(QuicConnCtx *qc, uint64_t error_code, uint64_t frame_type, int level, proto_bool app)
{
    if (qc->close_queued || qc->closed)
    {
        return;
    }
    qc->close_queued = PROTO_TRUE;
    qc->close_is_app = app;
    qc->close_error = error_code;
    qc->close_frame_type = frame_type;
    qc->close_level = (uint8_t)level;
}

static void handle_crypto(QuicConnCtx *qc, int level, const QuicFrame *f)
{
    QuicPnSpace *s = &qc->space[level];
    uint64_t want = s->crypto_rx_off;
    if (f->crypto.offset > want)
    {
        return; // out-of-order beyond our window; the peer will retransmit
    }
    if (f->crypto.offset + f->crypto.length <= want)
    {
        return; // wholly duplicate
    }
    size_t skip = (size_t)(want - f->crypto.offset);
    const uint8_t *nd = f->crypto.data + skip;
    size_t nl = (size_t)(f->crypto.length - skip);
    if (s->crypto_rx_have + nl > PROTOCORE_QUIC_CRYPTO_RX)
    {
        nl = PROTOCORE_QUIC_CRYPTO_RX - s->crypto_rx_have; // clamp to the reassembly window
    }
    mem.cpy(s->crypto_rx + s->crypto_rx_have, nd, nl);
    s->crypto_rx_have += nl;
    s->crypto_rx_off += nl;

    size_t used = protocore_quic_tls_recv_crypto(&qc->tls, level, s->crypto_rx, s->crypto_rx_have);
    if (used)
    {
        mem.move(s->crypto_rx, s->crypto_rx + used, s->crypto_rx_have - used);
        s->crypto_rx_have -= used;
    }
    // A fatal TLS error (bad Finished, unsupported handshake) becomes a QUIC CRYPTO_ERROR: report it
    // to the client with the TLS alert in the low byte (RFC 9001 sec 4.8) instead of stalling.
    if (qc->tls.state == QTLS_FAILED)
    {
        queue_close(qc, QUIC_ERR_CRYPTO_BASE + qc->tls.alert, QUIC_FT_CRYPTO, level, PROTO_FALSE);
        return;
    }
    // Completing the handshake opens 1-RTT and lets us send HANDSHAKE_DONE. We keep the Handshake
    // space live so the same outbound datagram still ACKs the client's Finished; HANDSHAKE_DONE (at
    // 1-RTT) is what tells the client the handshake is confirmed (RFC 9001 sec 4.1.2).
    if (qc->tls.state == QTLS_DONE && !qc->handshake_done_sent && !qc->handshake_done_queued)
    {
        qc->handshake_done_queued = PROTO_TRUE;
        qc->address_validated = PROTO_TRUE;
        if (qc->cb.on_handshake_done)
        {
            qc->cb.on_handshake_done(qc->cb.app, QUIC_SPAN(qc));
        }
    }
}

static void handle_stream(QuicConnCtx *qc, const QuicFrame *f)
{
    QuicStream *st = stream_get(qc, f->stream.id, PROTO_TRUE);
    if (!st)
    {
        return;
    }
    uint64_t want = st->rx_off;
    if (f->stream.offset > want)
    {
        return; // out-of-order beyond window
    }
    if (f->stream.offset + f->stream.length > want)
    {
        size_t skip = (size_t)(want - f->stream.offset);
        const uint8_t *nd = f->stream.data + skip;
        size_t nl = (size_t)(f->stream.length - skip);
        if (nl > PROTOCORE_QUIC_STREAM_RX)
        {
            nl = PROTOCORE_QUIC_STREAM_RX;
        }
        // Deliver in place (we hand the callback the contiguous new bytes directly).
        st->rx_off += nl;
        if (f->stream.fin)
        {
            st->rx_fin = PROTO_TRUE;
        }
        if (qc->cb.on_stream_data)
        {
            qc->cb.on_stream_data(qc->cb.app, QUIC_SPAN(qc), st->id, nd, nl, st->rx_fin);
        }
        return;
    }
    if (f->stream.fin && f->stream.offset + f->stream.length == want && !st->rx_fin)
    {
        st->rx_fin = PROTO_TRUE;
        if (qc->cb.on_stream_data)
        {
            qc->cb.on_stream_data(qc->cb.app, QUIC_SPAN(qc), st->id, NULL, 0, PROTO_TRUE);
        }
    }
}

// Process the frames in one decrypted packet. Returns false on a fatal connection error.
static proto_bool process_frames(QuicConnCtx *qc, int level, const uint8_t *p, size_t len,
                                 proto_bool *ack_eliciting)
{
    size_t off = 0;
    while (off < len)
    {
        if (p[off] == QUIC_FT_PADDING)
        {
            off++;
            continue;
        }
        QuicFrame f;
        size_t n = protocore_quic_frame_parse(p + off, len - off, &f);
        if (!n)
        {
            // Undecodable frame: a transport FRAME_ENCODING_ERROR (RFC 9000 sec 20.1). Report it.
            queue_close(qc, QUIC_ERR_FRAME_ENCODING, 0, level, PROTO_FALSE);
            return PROTO_FALSE;
        }
        off += n;

        if (f.type != QUIC_FT_ACK && f.type != QUIC_FT_ACK_ECN && f.type != QUIC_FT_CONNECTION_CLOSE &&
            f.type != QUIC_FT_CONNECTION_CLOSE_APP)
        {
            *ack_eliciting = PROTO_TRUE;
        }

        switch (f.type)
        {
        case QUIC_FT_CRYPTO:
            handle_crypto(qc, level, &f);
            break;
        case QUIC_FT_ACK:
        case QUIC_FT_ACK_ECN:
            if ((int64_t)f.ack.largest > qc->space[level].largest_acked)
            {
                qc->space[level].largest_acked = (int64_t)f.ack.largest;
                // Forward progress: reset the PTO backoff and re-evaluate the timer (RFC 9002 sec 6.2).
                qc->pto_count = 0;
                qc->pto_armed = PROTO_FALSE;
            }
            break;
        case QUIC_FT_CONNECTION_CLOSE:
        case QUIC_FT_CONNECTION_CLOSE_APP:
            qc->draining = PROTO_TRUE;
            qc->closed = PROTO_TRUE;
            break;
        case QUIC_FT_HANDSHAKE_DONE:
            break; // server-only frame; ignore if a peer sends it
        case QUIC_FT_MAX_DATA:
        case QUIC_FT_PING:
            break; // no per-frame state to keep for a minimal server
        default:
            if (f.type >= QUIC_FT_STREAM && f.type <= QUIC_FT_STREAM + 7)
            {
                handle_stream(qc, &f);
            }
            break;
        }
    }
    return PROTO_TRUE;
}

// Skip an Initial packet's Token field (RFC 9000 sec 17.2.2), advancing *off. False if malformed.
static proto_bool skip_initial_token(const uint8_t *dg, size_t len, size_t *off)
{
    uint64_t tok_len = 0;
    size_t c = 0;
    if (!protocore_quic_varint_decode(dg + *off, len - *off, &tok_len, &c))
    {
        return PROTO_FALSE;
    }
    *off += c + (size_t)tok_len;
    return *off <= len;
}

// Parse one packet's (long or short) header, filling the fields needed to locate + unprotect it.
// Returns false on a malformed header or an unsupported type/version (drop the packet).
static proto_bool parse_packet_header(const QuicConnCtx *qc, const uint8_t *dg, size_t len, proto_bool is_long,
                                      int *level, size_t *pn_offset, size_t *pkt_len, uint64_t *payload_length)
{
    if (!is_long)
    {
        // Short header: DCID length is our locally chosen scid_len; the packet runs to datagram end.
        *level = QUIC_ENC_APP;
        *pn_offset = 1 + qc->scid_len;
        if (*pn_offset >= len)
        {
            return PROTO_FALSE;
        }
        *payload_length = len - *pn_offset;
        *pkt_len = len;
        return PROTO_TRUE;
    }

    QuicLongHeader h;
    if (!protocore_quic_parse_long_header(dg, len, &h))
    {
        return PROTO_FALSE;
    }
    if (h.version == 0 || h.version != QUIC_VERSION_1)
    {
        return PROTO_FALSE; // Version Negotiation is a client concern; unknown versions are dropped
    }
    if (h.type == QUIC_LP_INITIAL)
    {
        *level = QUIC_ENC_INITIAL;
    }
    else if (h.type == QUIC_LP_HANDSHAKE)
    {
        *level = QUIC_ENC_HANDSHAKE;
    }
    else
    {
        return PROTO_FALSE; // 0-RTT / Retry not supported
    }

    size_t off = h.hdr_len;
    if (*level == QUIC_ENC_INITIAL && !skip_initial_token(dg, len, &off))
    {
        return PROTO_FALSE;
    }
    size_t c = 0;
    if (!protocore_quic_varint_decode(dg + off, len - off, payload_length, &c))
    {
        return PROTO_FALSE;
    }
    off += c;
    *pn_offset = off;
    *pkt_len = *pn_offset + (size_t)*payload_length;
    return *pkt_len <= len;
}

// Decrypt and process one packet at datagram offset; returns bytes consumed (0 to stop the datagram).
static size_t recv_packet(QuicConnCtx *qc, const uint8_t *dg, size_t len)
{
    if (len < 1)
    {
        return 0;
    }
    proto_bool is_long = protocore_quic_is_long_header(dg[0]);

    int level = 0;
    size_t pn_offset = 0;
    size_t pkt_len = 0; // total on-wire bytes of this packet
    uint64_t payload_length = 0;
    if (!parse_packet_header(qc, dg, len, is_long, &level, &pn_offset, &pkt_len, &payload_length))
    {
        return 0;
    }

    QuicPacketKeys *const keys = open_keys(qc, level);
    if (!keys)
    {
        return 0; // keys for this level are not available yet
    }

    // Unprotect on a copy so a failed decrypt does not corrupt following coalesced packets. The
    // engine runs sequentially on one task, so the scratch is a shared static (not reentrant).
    static uint8_t work[PROTOCORE_QUIC_MAX_DATAGRAM];
    static uint8_t plain[PROTOCORE_QUIC_MAX_DATAGRAM];
    if (pkt_len > sizeof(work))
    {
        return 0;
    }
    mem.cpy(work, dg, pkt_len);
    uint64_t pn = 0;
    size_t pt = protocore_quic_packet_unprotect(work, pn_offset, (size_t)payload_length, qc->space[level].largest_rx,
                                                keys, is_long, plain, &pn);
    if (pt == (size_t)-1)
    {
        return is_long ? pkt_len : 0; // drop this packet, keep parsing later coalesced ones
    }

    // RFC 9000 sec 17.2 / 17.3.1: the Reserved Bits are zero. work[0] is the unprotected first
    // byte, and the packet is authenticated by here, so a non-zero value is the peer's doing and
    // not a bit flipped in flight. Long headers reserve 0x0c, short headers 0x18.
    uint8_t reserved_mask = 0x18;
    if (is_long)
    {
        reserved_mask = 0x0c;
    }
    if (work[0] & reserved_mask)
    {
        quic_conn_close_transport(qc, QUIC_ERR_PROTOCOL_VIOLATION);
        return 0;
    }

    if (!qc->space[level].have_rx || pn > qc->space[level].largest_rx)
    {
        qc->space[level].largest_rx = pn;
    }
    qc->space[level].have_rx = PROTO_TRUE;

    // Receiving a Handshake packet validates the client's address (lifts anti-amplification).
    if (level == QUIC_ENC_HANDSHAKE)
    {
        qc->address_validated = PROTO_TRUE;
        qc->space[QUIC_ENC_INITIAL].discarded = PROTO_TRUE;
    }

    proto_bool ack_eliciting = PROTO_FALSE;
    process_frames(qc, level, plain, pt, &ack_eliciting);
    if (ack_eliciting)
    {
        qc->space[level].ack_eliciting_rx = PROTO_TRUE;
    }

    return pkt_len;
}

static proto_bool quic_conn_take(QuicConnCtx *qc, const uint8_t *datagram, size_t len)
{
    if (qc->closed)
    {
        return PROTO_FALSE;
    }
    qc->recv_bytes += len;
    size_t off = 0;
    proto_bool any = PROTO_FALSE;
    while (off < len)
    {
        size_t n = recv_packet(qc, datagram + off, len - off);
        if (!n)
        {
            break;
        }
        any = PROTO_TRUE;
        off += n;
    }
    return any;
}

// --- Sending ---------------------------------------------------------------------------------
// Append the owed ACK frame for space @p s (RFC 9000 sec 13.2); returns bytes written.
static size_t build_ack_frame(QuicPnSpace *s, uint8_t *buf, size_t cap)
{
    if (!s->ack_eliciting_rx || !s->have_rx)
    {
        return 0;
    }
    size_t n = protocore_quic_build_ack(buf, cap, s->largest_rx, 0, s->largest_rx);
    if (n)
    {
        s->ack_eliciting_rx =
            PROTO_FALSE; // static_assert above), and one ACK frame is at most ~20 bytes: it always fits
    }
    return n;
}

// Append the CRYPTO flight for INITIAL/HANDSHAKE (ServerHello / EE..Finished); returns bytes written,
// sets *ae when it emits an ack-eliciting CRYPTO frame.
static size_t build_crypto_frame(const QuicConnCtx *qc, int level, QuicPnSpace *s, uint8_t *buf, size_t cap,
                                 proto_bool *ae)
{
    if (level != QUIC_ENC_INITIAL && level != QUIC_ENC_HANDSHAKE)
    {
        return 0;
    }
    size_t flen = 0;
    const uint8_t *flight = protocore_quic_tls_flight(&qc->tls, level, &flen);
    if (!flight || s->crypto_tx_off >= flen)
    {
        return 0; // other than INITIAL/HANDSHAKE, which the guard above already excluded
    }
    size_t remain = flen - (size_t)s->crypto_tx_off;
    // Leave room for the CRYPTO frame header (type + offset + length varints, <= 1+8+8).
    size_t room = (cap > 20) ? (cap - 20) : 0;
    size_t take = remain < room ? remain : room;
    if (!take)
    {
        return 0;
    }
    size_t n = protocore_quic_build_crypto(buf, cap, s->crypto_tx_off, flight + s->crypto_tx_off, take);
    if (n)
    {
        s->crypto_tx_off += take;
        *ae = PROTO_TRUE;
    }
    return n;
}

// Append 1-RTT extras (HANDSHAKE_DONE + stream data) at APP level; returns bytes written, sets *ae.
static size_t build_app_frames(QuicConnCtx *qc, int level, uint8_t *buf, size_t cap, proto_bool *ae)
{
    if (level != QUIC_ENC_APP)
    {
        return 0;
    }
    size_t p = 0;
    if (qc->handshake_done_queued)
    {
        size_t n = protocore_quic_build_handshake_done(buf + p, cap - p);
        if (n)
        { // ACK/CRYPTO, so the datagram-sized scratch always has room for it

            p += n;
            qc->handshake_done_queued = PROTO_FALSE;
            qc->handshake_done_sent = PROTO_TRUE;
            *ae = PROTO_TRUE;
        }
    }
    for (size_t i = 0; i < PROTOCORE_QUIC_MAX_STREAMS; i++)
    {
        QuicStream *st = &qc->streams[i];
        if (st->id == UINT64_MAX)
        {
            continue;
        }
        proto_bool more = st->tx_sent < st->tx_have;
        proto_bool fin_due = st->tx_fin && !st->tx_fin_sent && st->tx_sent == st->tx_have;
        if (!more && !fin_due)
        {
            continue;
        }
        size_t room = (cap - p > 24) ? (cap - p - 24) : 0;
        size_t remain = st->tx_have - st->tx_sent;
        size_t take = remain < room ? remain : room;
        proto_bool fin = st->tx_fin && (st->tx_sent + take == st->tx_have);
        size_t n = protocore_quic_build_stream(buf + p, cap - p, st->id, st->tx_off, st->tx + st->tx_sent, take, fin);
        if (n)
        {
            p += n;
            st->tx_off += take;
            st->tx_sent += take;
            st->tx_fin_sent = st->tx_fin_sent || fin;
            *ae = PROTO_TRUE;
        }
    }
    return p;
}

// Build the frame payload for one encryption level into buf; returns its length (0 = nothing to send).
// @p ae is set true if the payload carries an ack-eliciting frame (CRYPTO / STREAM / HANDSHAKE_DONE),
// which arms loss recovery for this space.
static size_t build_frames(QuicConnCtx *qc, int level, uint8_t *buf, size_t cap, proto_bool *ae)
{
    QuicPnSpace *s = &qc->space[level];
    size_t p = 0;
    *ae = PROTO_FALSE;

    // While closing, the only frame we send is the transport CONNECTION_CLOSE (RFC 9000 sec 10.2.3).
    // It is not ack-eliciting (*ae stays false), so no PTO is armed for it. QuicConnNs::send invokes
    // this for a single level when a close is queued, so it is emitted exactly once.
    if (qc->close_queued && !qc->close_sent)
    {
        return protocore_quic_build_connection_close(buf, cap, qc->close_is_app, qc->close_error, qc->close_frame_type,
                                                     NULL, 0);
    }

    p += build_ack_frame(s, buf + p, cap - p); // ACK first, if we owe one
    p += build_crypto_frame(qc, level, s, buf + p, cap - p, ae);
    p += build_app_frames(qc, level, buf + p, cap - p, ae);
    return p;
}

// Long-header packet type for an encryption level.
static uint8_t level_lp_type(int level)
{
    return level == QUIC_ENC_INITIAL ? QUIC_LP_INITIAL : QUIC_LP_HANDSHAKE;
}

// Bytes a protected packet needs on top of its payload: AEAD tag + packet number, plus the header.
static size_t packet_overhead(const QuicConnCtx *qc, proto_bool is_long, uint8_t pn_len)
{
    size_t overhead = (size_t)PROTOCORE_AES128GCM_TAG_LEN + pn_len;
    if (is_long)
    {
        // type(1) + version(4) + dcid_len(1) + dcid + scid_len(1) + scid, then the Initial token
        // varint and the Length varint (bounded generously - both are far smaller in practice).
        overhead += 7u + qc->dcid_len + qc->scid_len + 1u + 4u;
    }
    else
    {
        overhead += 1u + qc->dcid_len; // short header: first byte + DCID, no length on the wire
    }
    return overhead;
}

// Build one protected packet for a level into out; returns its length (0 = nothing to send).
static size_t build_packet(QuicConnCtx *qc, int level, uint8_t *out, size_t cap)
{
    QuicPnSpace *s = &qc->space[level];
    if (s->discarded)
    {
        return 0;
    }
    QuicPacketKeys *const keys = seal_keys(qc, level);
    if (!keys)
    {
        return 0;
    }

    uint64_t pn = s->next_pn;
    uint8_t pn_len = protocore_quic_pn_length(pn, s->largest_acked);
    proto_bool is_long = (level != QUIC_ENC_APP);

    // Reserve the framing BEFORE filling the payload. build_frames() advances the connection's send
    // offsets (crypto_tx_off, and each stream's tx_off / tx_sent) as it writes, so a payload that
    // turns out not to fit once the header, packet number and AEAD tag are added cannot simply be
    // rejected further down: those bytes would already be counted as sent and would never go out.
    // That is silent data loss, and it was reachable - build_frames was handed the whole
    // PROTOCORE_QUIC_MAX_DATAGRAM scratch while the packet needs header + pn + 16-byte tag on top of it,
    // so a stream with ~1326+ bytes pending, or a Handshake CRYPTO flight carrying a large
    // certificate, produced a payload the packet had no room for. Bound the payload instead.
    size_t overhead = packet_overhead(qc, is_long, pn_len);
    if (cap <= overhead)
    {
        return 0;
    }
    size_t budget = cap - overhead;

    uint8_t frames[PROTOCORE_QUIC_MAX_DATAGRAM];
    // Dead with every current caller - quic_server's datagram buffer is itself
    // PROTOCORE_QUIC_MAX_DATAGRAM, so `cap - overhead` is always below `frames` - but kept as the bound
    // on the memcpy target if a caller ever hands us a larger buffer.
    if (budget > sizeof(frames))
    {
        budget = sizeof(frames);
    }
    // Leave room for the PADDING the header-protection minimum may need (RFC 9001 sec 5.4.2).
    if (budget < 4)
    {
        return 0;
    }
    budget -= 4;
    proto_bool ae = PROTO_FALSE;
    size_t frame_len = build_frames(qc, level, frames, budget, &ae);
    if (frame_len == 0)
    {
        return 0;
    }

    // Header protection samples 16 bytes at (packet number + 4), so the packet number and payload
    // together must be at least 4 bytes (RFC 9001 sec 5.4.2). Pad tiny packets (e.g. a lone
    // HANDSHAKE_DONE or ACK) with PADDING frames (zero bytes) to reach that minimum.
    if ((size_t)pn_len + frame_len < 4)
    {
        size_t pad = 4 - pn_len - frame_len;
        mem.set(frames + frame_len, 0, pad);
        frame_len += pad;
    }

    size_t p = 0;
    if (is_long)
    {
        // Invariant header fields, then the type-specific token (Initial only) + Length + PN.
        size_t hn = protocore_quic_build_long_header(out, cap, level_lp_type(level), QUIC_VERSION_1, qc->dcid,
                                                     qc->dcid_len, qc->scid, qc->scid_len, pn_len);
        if (!hn)
        {
            return 0;
        }
        p = hn;
        if (level == QUIC_ENC_INITIAL)
        {
            size_t n = protocore_quic_varint_encode(out + p, cap - p, 0); // empty token
            if (!n)
            {
                return 0;
            }
            p += n;
        }
        uint64_t length = (uint64_t)pn_len + frame_len + PROTOCORE_AES128GCM_TAG_LEN;
        size_t n = protocore_quic_varint_encode(out + p, cap - p, length);
        if (!n)
        {
            return 0;
        }
        p += n;
    }
    else
    {
        // Short header: 0x40 fixed bit | pn_len-1; then the peer's DCID (no length on the wire).
        if (1 + qc->dcid_len > cap)
        {
            return 0;
        }
        out[0] = (uint8_t)(0x40 | (pn_len - 1));
        mem.cpy(out + 1, qc->dcid, qc->dcid_len);
        p = 1 + qc->dcid_len;
    }

    size_t pn_offset = p;
    // Bound the whole packet up front (p <= cap here): the header builders checked their own writes
    // but not the packet number + payload + tag that follow, so verify the remainder fits before
    // writing it (avoids a size_t addition wrap in the bounds check, cpp:S3519).
    // Redundant since the payload is now budgeted against `overhead` up front, which is exactly
    // what makes the offsets build_frames() advanced safe to keep. Retained as defense in depth.
    if (cap - p < (size_t)pn_len + frame_len + PROTOCORE_AES128GCM_TAG_LEN)
    {
        return 0;
    }
    // Write the (unprotected) truncated packet number.
    for (uint8_t i = 0; i < pn_len; i++)
    {
        out[pn_offset + i] = (uint8_t)(pn >> (8 * (pn_len - 1 - i)));
    }
    p += pn_len;

    mem.cpy(out + p, frames, frame_len);

    size_t total = protocore_quic_packet_protect(out, cap, pn_offset, pn_len, pn, frame_len, keys, is_long);
    if (!total)
    {
        return 0;
    }
    if (ae)
    {
        s->last_ae_pn = (int64_t)pn; // this space now has ack-eliciting data outstanding
    }
    s->next_pn++;
    return total;
}

// Highest encryption level (INITIAL..APP) we still hold seal keys for and haven't discarded - the
// level at which a CONNECTION_CLOSE can still be decrypted by the peer. Falls back to INITIAL.
static int protocore_quic_highest_sealed_level(QuicConnCtx *qc)
{
    for (int l = QUIC_ENC_APP; l >= QUIC_ENC_INITIAL; l--)
    {
        if (!qc->space[l].discarded && seal_keys(qc, l))
        {
            return l;
        }
    }
    return QUIC_ENC_INITIAL;
}

static size_t quic_conn_build(QuicConnCtx *qc, uint8_t *out, size_t cap)
{
    if (qc->closed && !qc->draining)
    {
        return 0;
    }
    // Anti-amplification (RFC 9000 sec 8.1): until the client's address is validated, send nothing
    // once we have already put 3x the received bytes on the wire. Checked BEFORE building the packets
    // so a blocked send never advances packet-number / CRYPTO / stream state (which would desync the
    // flight); a build then discard was the bug. Being at-most one datagram approximate here is fine.
    if (!qc->address_validated && qc->sent_bytes >= 3 * qc->recv_bytes)
    {
        return 0;
    }
    if (cap > PROTOCORE_QUIC_MAX_DATAGRAM)
    {
        cap = PROTOCORE_QUIC_MAX_DATAGRAM;
    }

    // A queued transport CONNECTION_CLOSE is sent once - at the highest encryption level we still hold
    // keys for (so the peer can decrypt it) - and then the connection is closed. It replaces the normal
    // frame build (a closing endpoint sends nothing else) and is bounded by the amplification limit above.
    if (qc->close_queued && !qc->close_sent)
    {
        // Send at the level the error was seen on (the peer holds those keys); if that space has since
        // been discarded, fall back to the highest level we still hold keys for.
        int level = qc->close_level;
        if (level < QUIC_ENC_INITIAL || level > QUIC_ENC_APP || qc->space[level].discarded || !seal_keys(qc, level))
        {
            level = protocore_quic_highest_sealed_level(qc);
        }
        size_t n = build_packet(qc, level, out, cap);
        if (n)
        {
            qc->close_sent = PROTO_TRUE;
            qc->closed = PROTO_TRUE;
            qc->sent_bytes += n;
        }
        return n;
    }

    size_t dg = 0;
    // Coalesce Initial, then Handshake, then 1-RTT into one datagram.
    for (int level = QUIC_ENC_INITIAL; level <= QUIC_ENC_APP; level++)
    {
        size_t n = build_packet(qc, level, out + dg, cap - dg);
        dg += n;
    }
    if (dg == 0)
    {
        return 0;
    }
    qc->sent_bytes += dg;
    return dg;
}

// PTO period with exponential backoff, capped so the shift cannot overflow (RFC 9002 sec 6.2.1).
static uint32_t pto_period(uint8_t count)
{
    uint32_t p = PROTOCORE_QUIC_PTO_MS;
    for (uint8_t i = 0; i < count && p < (1u << 30); i++)
    {
        p <<= 1;
    }
    return p;
}
// A space has unacknowledged ack-eliciting data outstanding: it sent an ack-eliciting packet the peer
// has not yet acknowledged, and its keys are still live.
static proto_bool space_outstanding(const QuicPnSpace *s)
{
    return !s->discarded && s->last_ae_pn >= 0 && s->largest_acked < s->last_ae_pn;
}

static void quic_conn_timeout(QuicConnCtx *qc, uint32_t now_ms)
{
    if (qc->closed)
    {
        return;
    }
    // Loss recovery (RFC 9002): retransmission is driven by a Probe Timeout, because a lost server
    // packet is not re-triggered by the peer (a duplicate ClientHello re-delivers no CRYPTO; a lost
    // 1-RTT response is never re-requested). Anything the peer has not acknowledged in a live space -
    // the handshake CRYPTO flight, HANDSHAKE_DONE, or the 1-RTT response - is outstanding.
    proto_bool outstanding = space_outstanding(&qc->space[QUIC_ENC_INITIAL]) ||
                             space_outstanding(&qc->space[QUIC_ENC_HANDSHAKE]) ||
                             space_outstanding(&qc->space[QUIC_ENC_APP]);
    if (!outstanding)
    {
        qc->pto_armed = PROTO_FALSE; // everything acknowledged: nothing to retransmit
        qc->pto_count = 0;
        return;
    }
    if (!qc->pto_armed)
    {
        qc->pto_armed = PROTO_TRUE;
        qc->pto_deadline_ms = now_ms + pto_period(qc->pto_count);
        return;
    }
    if ((int32_t)(now_ms - qc->pto_deadline_ms) < 0)
    {
        return; // not yet (wrap-safe compare)
    }

    // PTO fired: mark the unacknowledged data in each outstanding space for retransmission so the next
    // QuicConnNs::send re-sends it, then back the timer off.
    for (int level = QUIC_ENC_INITIAL; level <= QUIC_ENC_HANDSHAKE; level++)
    {
        if (space_outstanding(&qc->space[level]))
        {
            qc->space[level].crypto_tx_off = 0; // re-send the CRYPTO flight for this level
        }
    }
    if (space_outstanding(&qc->space[QUIC_ENC_APP]))
    {
        // Re-send 1-RTT data. The peer dedups STREAM data by offset, so rewinding each stream to 0
        // recovers a lost response and is a no-op for data already received.
        if (qc->handshake_done_sent)
        {
            qc->handshake_done_queued = PROTO_TRUE;
            qc->handshake_done_sent = PROTO_FALSE;
        }
        for (size_t i = 0; i < PROTOCORE_QUIC_MAX_STREAMS; i++)
        {
            QuicStream *st = &qc->streams[i];
            if (st->id == UINT64_MAX)
            {
                continue;
            }
            st->tx_off = 0;
            st->tx_sent = 0;
            st->tx_fin_sent = PROTO_FALSE;
        }
    }
    if (qc->pto_count < 8)
    {
        qc->pto_count++;
    }
    qc->pto_deadline_ms = now_ms + pto_period(qc->pto_count);
}

static size_t quic_conn_stream_put(QuicConnCtx *qc, uint64_t stream_id, const uint8_t *data, size_t len,
                                       proto_bool fin)
{
    QuicStream *st = stream_get(qc, stream_id, PROTO_TRUE);
    if (!st)
    {
        return 0;
    }
    size_t room = PROTOCORE_QUIC_STREAM_TX - st->tx_have;
    size_t take = len < room ? len : room;
    mem.cpy(st->tx + st->tx_have, data, take);
    st->tx_have += take;
    if (fin && take == len)
    {
        st->tx_fin = PROTO_TRUE;
    }
    return take;
}

static void quic_conn_close_transport(QuicConnCtx *qc, uint64_t error_code)
{
    // Application-initiated close: send at the highest level we still hold keys for.
    int level = protocore_quic_highest_sealed_level(qc);
    queue_close(qc, error_code, 0, level, PROTO_FALSE);
}

static void quic_conn_close_application(QuicConnCtx *qc, uint64_t error_code)
{
    // RFC 9000 sec 19.19 / 10.2.3: the application variant travels only in 1-RTT packets. Before
    // those keys exist the same intent goes as a transport close carrying APPLICATION_ERROR, whose
    // error code is the transport's, so @p error_code is dropped rather than reinterpreted.
    int level = protocore_quic_highest_sealed_level(qc);
    if (level != QUIC_ENC_APP)
    {
        queue_close(qc, QUIC_ERR_APPLICATION, 0, level, PROTO_FALSE);
        return;
    }
    queue_close(qc, error_code, 0, level, PROTO_TRUE);
}

static proto_bool quic_conn_done(const QuicConnCtx *qc)
{
    return qc->tls.state == QTLS_DONE;
}

static proto_bool quic_conn_gone(const QuicConnCtx *qc)
{
    return qc->closed || qc->draining;
}

// --- the entries -----------------------------------------------------------

// The bound context span, as this file's connection. Every entry starts here, so no entry reads the
// bind twice and none of them carries the span as a parameter.
static QuicConnCtx *qc_bound(struct QuicConnInternal *restrict ctx)
{
    if (!ctx || !ctx->ns->bind.ctx)
    {
        return NULL;
    }
    ctx->c = QUIC_CTX(ctx->ns->bind.ctx);
    return ctx->c;
}

static void quic_conn_init(struct QuicConnInternal *restrict ctx)
{
    QuicConnCtx *qc = qc_bound(ctx);
    QuicConn.ok = PROTO_FALSE;
    if (!qc || !QuicConn.bind.b || !QuicConn.init_args.cfg)
    {
        return;
    }
    qc->b = QuicConn.bind.b; // survives the wipe inside quic_conn_open
    quic_conn_open(qc, QuicConn.init_args.cfg, QuicConn.init_args.odcid, QuicConn.init_args.odcid_len,
                   QuicConn.init_args.peer_scid, QuicConn.init_args.peer_scid_len, QuicConn.init_args.our_scid,
                   QuicConn.init_args.our_scid_len, &QuicConn.cb);
    QuicConn.ok = !qc->closed;
}

static void quic_conn_callbacks(struct QuicConnInternal *restrict ctx)
{
    QuicConnCtx *qc = qc_bound(ctx);
    QuicConn.ok = PROTO_FALSE;
    if (!qc)
    {
        return;
    }
    qc->cb = QuicConn.cb;
    QuicConn.ok = PROTO_TRUE;
}

static void quic_conn_recv(struct QuicConnInternal *restrict ctx)
{
    QuicConnCtx *qc = qc_bound(ctx);
    QuicConn.ok = PROTO_FALSE;
    if (!qc || !QuicConn.recv_args.datagram)
    {
        return;
    }
    QuicConn.ok = quic_conn_take(qc, QuicConn.recv_args.datagram, QuicConn.recv_args.len);
}

static void quic_conn_send(struct QuicConnInternal *restrict ctx)
{
    QuicConnCtx *qc = qc_bound(ctx);
    QuicConn.ok = PROTO_FALSE;
    QuicConn.n = 0;
    if (!qc || !QuicConn.send_args.out)
    {
        return;
    }
    QuicConn.n = quic_conn_build(qc, QuicConn.send_args.out, QuicConn.send_args.cap);
    QuicConn.ok = PROTO_TRUE;
}

static void quic_conn_on_timeout(struct QuicConnInternal *restrict ctx)
{
    QuicConnCtx *qc = qc_bound(ctx);
    QuicConn.ok = PROTO_FALSE;
    if (!qc)
    {
        return;
    }
    quic_conn_timeout(qc, QuicConn.timeout_args.now_ms);
    QuicConn.ok = PROTO_TRUE;
}

static void quic_conn_stream_send(struct QuicConnInternal *restrict ctx)
{
    QuicConnCtx *qc = qc_bound(ctx);
    QuicConn.ok = PROTO_FALSE;
    QuicConn.n = 0;
    if (!qc)
    {
        return;
    }
    QuicConn.n = quic_conn_stream_put(qc, QuicConn.stream_send_args.stream_id, QuicConn.stream_send_args.data,
                                      QuicConn.stream_send_args.len, QuicConn.stream_send_args.fin);
    QuicConn.ok = PROTO_TRUE;
}

static void quic_conn_close(struct QuicConnInternal *restrict ctx)
{
    QuicConnCtx *qc = qc_bound(ctx);
    QuicConn.ok = PROTO_FALSE;
    if (!qc)
    {
        return;
    }
    quic_conn_close_transport(qc, QuicConn.close_args.error_code);
    QuicConn.ok = PROTO_TRUE;
}

static void quic_conn_close_app(struct QuicConnInternal *restrict ctx)
{
    QuicConnCtx *qc = qc_bound(ctx);
    QuicConn.ok = PROTO_FALSE;
    if (!qc)
    {
        return;
    }
    quic_conn_close_application(qc, QuicConn.close_args.error_code);
    QuicConn.ok = PROTO_TRUE;
}

static void quic_conn_is_established(struct QuicConnInternal *restrict ctx)
{
    QuicConnCtx *qc = qc_bound(ctx);
    QuicConn.established = PROTO_FALSE;
    QuicConn.ok = (qc != NULL);
    if (qc)
    {
        QuicConn.established = quic_conn_done(qc);
    }
}

static void quic_conn_is_closed(struct QuicConnInternal *restrict ctx)
{
    QuicConnCtx *qc = qc_bound(ctx);
    QuicConn.closed = PROTO_TRUE; // an unbound connection answers nothing, which is closed
    QuicConn.ok = (qc != NULL);
    if (qc)
    {
        QuicConn.closed = quic_conn_gone(qc);
    }
}

static struct QuicConnInternal s_qc = {.ns = &QuicConn};

QuicConnNs QuicConn = {.init = quic_conn_init,
                       .callbacks = quic_conn_callbacks,
                       .recv = quic_conn_recv,
                       .send = quic_conn_send,
                       .on_timeout = quic_conn_on_timeout,
                       .stream_send = quic_conn_stream_send,
                       .close = quic_conn_close,
                       .close_app = quic_conn_close_app,
                       .is_established = quic_conn_is_established,
                       .is_closed = quic_conn_is_closed,
                       .internal = &s_qc};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_HTTP3
