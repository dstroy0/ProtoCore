// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_quic_conn.c
 * @brief Stateful QUIC v1 server connection engine (see pc_quic_conn.h).
 */

#include "network_drivers/presentation/http/http3/quic_conn.h"

#if PC_ENABLE_HTTP3

#include "crypto/aead/aes128gcm.h" // PC_AES128GCM_TAG_LEN
#include "network_drivers/presentation/http/http3/quic_frame.h"
#include "network_drivers/presentation/http/http3/quic_packet.h"
#include "network_drivers/presentation/http/http3/quic_varint.h"
#include <string.h>

// A single STREAM frame's payload cannot exceed one datagram, so it can never overflow a stream's
// reassembly buffer - which is why that clamp in handle_stream carries a coverage exclusion. Both
// sizes are overridable macros (quic_conn.h), so pin the relationship here rather than let a jumbo
// PC_QUIC_MAX_DATAGRAM or a shrunken PC_QUIC_STREAM_RX silently make the excluded path reachable.
static_assert(PC_QUIC_MAX_DATAGRAM < PC_QUIC_STREAM_RX,
              "PC_QUIC_STREAM_RX must exceed PC_QUIC_MAX_DATAGRAM: one STREAM frame's payload is bounded by the "
              "datagram, and handle_stream relies on that to deliver it without clamping");

// The frame payload build_frames fills is one datagram; every builder it calls (ACK, CRYPTO header,
// HANDSHAKE_DONE, STREAM header) must fit with room to spare, which is why their failure guards
// carry coverage exclusions. RFC 9000 sec 14.1 already requires at least 1200 octets for Initial
// packets, so pin that floor: a smaller datagram would make those guards reachable.
static_assert(PC_QUIC_MAX_DATAGRAM >= 1200,
              "PC_QUIC_MAX_DATAGRAM must be at least the RFC 9000 sec 14.1 minimum of 1200 octets, which is also "
              "what lets build_frames assume every frame builder has room");

// The open (decrypt) keys for an encryption level: Initial keys come from the DCID, Handshake and
// 1-RTT keys from the TLS handshake. Returns NULL if that level's keys are not available yet.
static QuicPacketKeys *open_keys(struct QuicConn *qc, int level)
{
    if (level == QUIC_ENC_INITIAL)
    {
        return &qc->initial.client;
    }
    return pc_quic_tls_keys(&qc->tls, level, /*is_server=*/PROTO_FALSE);
}
static QuicPacketKeys *seal_keys(struct QuicConn *qc, int level)
{
    if (level == QUIC_ENC_INITIAL)
    {
        return &qc->initial.server;
    }
    return pc_quic_tls_keys(&qc->tls, level, /*is_server=*/PROTO_TRUE);
}

// Find a stream slot by id, or allocate one; NULL if the table is full.
static QuicStream *stream_get(struct QuicConn *qc, uint64_t id, proto_bool create)
{
    QuicStream *free_slot = NULL;
    for (size_t i = 0; i < PC_QUIC_MAX_STREAMS; i++)
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
        return NULL; // pc_quic_conn_stream_send), so the lookup-only arm is never taken
    }
    memset(free_slot, 0, sizeof(*free_slot));
    free_slot->id = id;
    return free_slot;
}

void pc_quic_conn_init(struct QuicConn *qc, const QuicTlsConfig *cfg, const uint8_t *odcid, uint8_t odcid_len,
                       const uint8_t *peer_scid, uint8_t peer_scid_len, const uint8_t *our_scid, uint8_t our_scid_len,
                       const QuicConnCallbacks *cb)
{
    memset(qc, 0, sizeof(*qc));
    memcpy(qc->odcid, odcid, odcid_len);
    qc->odcid_len = odcid_len;
    memcpy(qc->dcid, peer_scid, peer_scid_len);
    qc->dcid_len = peer_scid_len;
    memcpy(qc->scid, our_scid, our_scid_len);
    qc->scid_len = our_scid_len;
    if (cb)
    {
        qc->cb = *cb;
    }

    pc_quic_derive_initial_secrets(odcid, odcid_len, &qc->initial);

    for (int i = 0; i < 3; i++)
    {
        qc->space[i].largest_acked = -1;
        qc->space[i].last_ae_pn = -1;
    }
    for (size_t i = 0; i < PC_QUIC_MAX_STREAMS; i++)
    {
        qc->streams[i].id = UINT64_MAX;
    }

    // The server's transport parameters carry the connection IDs the handshake must echo.
    QuicTlsConfig c = *cfg;
    c.params.has_original_dcid = PROTO_TRUE;
    memcpy(c.params.original_dcid, odcid, odcid_len);
    c.params.original_dcid_len = odcid_len;
    c.params.has_initial_scid = PROTO_TRUE;
    memcpy(c.params.initial_scid, our_scid, our_scid_len);
    c.params.initial_scid_len = our_scid_len;
    pc_quic_tls_server_init(&qc->tls, &c);
}

// --- Frame handling --------------------------------------------------------------------------
// Queue a transport CONNECTION_CLOSE for a fatal error at @p level; the first error wins (RFC 9000 sec
// 10.2.3). Sending at the level the error was seen on guarantees the peer holds keys to read it.
static void queue_close(struct QuicConn *qc, uint64_t error_code, uint64_t frame_type, int level)
{
    if (qc->close_queued || qc->closed)
    {
        return;
    }
    qc->close_queued = PROTO_TRUE;
    qc->close_error = error_code;
    qc->close_frame_type = frame_type;
    qc->close_level = (uint8_t)level;
}

static void handle_crypto(struct QuicConn *qc, int level, const QuicFrame *f)
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
    if (s->crypto_rx_have + nl > sizeof(s->crypto_rx))
    {
        nl = sizeof(s->crypto_rx) - s->crypto_rx_have; // clamp to the reassembly window
    }
    memcpy(s->crypto_rx + s->crypto_rx_have, nd, nl);
    s->crypto_rx_have += nl;
    s->crypto_rx_off += nl;

    size_t used = pc_quic_tls_recv_crypto(&qc->tls, level, s->crypto_rx, s->crypto_rx_have);
    if (used)
    {
        memmove(s->crypto_rx, s->crypto_rx + used, s->crypto_rx_have - used);
        s->crypto_rx_have -= used;
    }
    // A fatal TLS error (bad Finished, unsupported handshake) becomes a QUIC CRYPTO_ERROR: report it
    // to the client with the TLS alert in the low byte (RFC 9001 sec 4.8) instead of stalling.
    if (qc->tls.state == QTLS_FAILED)
    {
        queue_close(qc, QUIC_ERR_CRYPTO_BASE + qc->tls.alert, QUIC_FT_CRYPTO, level);
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
            qc->cb.on_handshake_done(qc->cb.app, qc);
        }
    }
}

static void handle_stream(struct QuicConn *qc, const QuicFrame *f)
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
        if (nl > sizeof(st->rx))
        {
            nl = sizeof(st->rx);
        }
        // Deliver in place (we hand the callback the contiguous new bytes directly).
        st->rx_off += nl;
        if (f->stream.fin)
        {
            st->rx_fin = PROTO_TRUE;
        }
        if (qc->cb.on_stream_data)
        {
            qc->cb.on_stream_data(qc->cb.app, qc, st->id, nd, nl, st->rx_fin);
        }
        return;
    }
    if (f->stream.fin && f->stream.offset + f->stream.length == want && !st->rx_fin)
    {
        st->rx_fin = PROTO_TRUE;
        if (qc->cb.on_stream_data)
        {
            qc->cb.on_stream_data(qc->cb.app, qc, st->id, NULL, 0, PROTO_TRUE);
        }
    }
}

// Process the frames in one decrypted packet. Returns false on a fatal connection error.
static proto_bool process_frames(struct QuicConn *qc, int level, const uint8_t *p, size_t len,
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
        size_t n = pc_quic_frame_parse(p + off, len - off, &f);
        if (!n)
        {
            // Undecodable frame: a transport FRAME_ENCODING_ERROR (RFC 9000 sec 20.1). Report it.
            queue_close(qc, QUIC_ERR_FRAME_ENCODING, 0, level);
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
    if (!pc_quic_varint_decode(dg + *off, len - *off, &tok_len, &c))
    {
        return PROTO_FALSE;
    }
    *off += c + (size_t)tok_len;
    return *off <= len;
}

// Parse one packet's (long or short) header, filling the fields needed to locate + unprotect it.
// Returns false on a malformed header or an unsupported type/version (drop the packet).
static proto_bool parse_packet_header(const struct QuicConn *qc, const uint8_t *dg, size_t len, proto_bool is_long,
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
    if (!pc_quic_parse_long_header(dg, len, &h))
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
    if (!pc_quic_varint_decode(dg + off, len - off, payload_length, &c))
    {
        return PROTO_FALSE;
    }
    off += c;
    *pn_offset = off;
    *pkt_len = *pn_offset + (size_t)*payload_length;
    return *pkt_len <= len;
}

// Decrypt and process one packet at datagram offset; returns bytes consumed (0 to stop the datagram).
static size_t recv_packet(struct QuicConn *qc, const uint8_t *dg, size_t len)
{
    if (len < 1)
    {
        return 0;
    }
    proto_bool is_long = pc_quic_is_long_header(dg[0]);

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
    static uint8_t work[PC_QUIC_MAX_DATAGRAM];
    static uint8_t plain[PC_QUIC_MAX_DATAGRAM];
    if (pkt_len > sizeof(work))
    {
        return 0;
    }
    memcpy(work, dg, pkt_len);
    uint64_t pn = 0;
    size_t pt = pc_quic_packet_unprotect(work, pn_offset, (size_t)payload_length, qc->space[level].largest_rx, keys,
                                         is_long, plain, &pn);
    if (pt == (size_t)-1)
    {
        return is_long ? pkt_len : 0; // drop this packet, keep parsing later coalesced ones
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

proto_bool pc_quic_conn_recv(struct QuicConn *qc, const uint8_t *datagram, size_t len)
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
    size_t n = pc_quic_build_ack(buf, cap, s->largest_rx, 0, s->largest_rx);
    if (n)
    {
        s->ack_eliciting_rx =
            PROTO_FALSE; // static_assert above), and one ACK frame is at most ~20 bytes: it always fits
    }
    return n;
}

// Append the CRYPTO flight for INITIAL/HANDSHAKE (ServerHello / EE..Finished); returns bytes written,
// sets *ae when it emits an ack-eliciting CRYPTO frame.
static size_t build_crypto_frame(const struct QuicConn *qc, int level, QuicPnSpace *s, uint8_t *buf, size_t cap,
                                 proto_bool *ae)
{
    if (level != QUIC_ENC_INITIAL && level != QUIC_ENC_HANDSHAKE)
    {
        return 0;
    }
    size_t flen = 0;
    const uint8_t *flight = pc_quic_tls_flight(&qc->tls, level, &flen);
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
    size_t n = pc_quic_build_crypto(buf, cap, s->crypto_tx_off, flight + s->crypto_tx_off, take);
    if (n)
    {
        s->crypto_tx_off += take;
        *ae = PROTO_TRUE;
    }
    return n;
}

// Append 1-RTT extras (HANDSHAKE_DONE + stream data) at APP level; returns bytes written, sets *ae.
static size_t build_app_frames(struct QuicConn *qc, int level, uint8_t *buf, size_t cap, proto_bool *ae)
{
    if (level != QUIC_ENC_APP)
    {
        return 0;
    }
    size_t p = 0;
    if (qc->handshake_done_queued)
    {
        size_t n = pc_quic_build_handshake_done(buf + p, cap - p);
        if (n)
        { // ACK/CRYPTO, so the datagram-sized scratch always has room for it

            p += n;
            qc->handshake_done_queued = PROTO_FALSE;
            qc->handshake_done_sent = PROTO_TRUE;
            *ae = PROTO_TRUE;
        }
    }
    for (size_t i = 0; i < PC_QUIC_MAX_STREAMS; i++)
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
        size_t n = pc_quic_build_stream(buf + p, cap - p, st->id, st->tx_off, st->tx + st->tx_sent, take, fin);
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
static size_t build_frames(struct QuicConn *qc, int level, uint8_t *buf, size_t cap, proto_bool *ae)
{
    QuicPnSpace *s = &qc->space[level];
    size_t p = 0;
    *ae = PROTO_FALSE;

    // While closing, the only frame we send is the transport CONNECTION_CLOSE (RFC 9000 sec 10.2.3).
    // It is not ack-eliciting (*ae stays false), so no PTO is armed for it. pc_quic_conn_send() invokes
    // this for a single level when a close is queued, so it is emitted exactly once.
    if (qc->close_queued && !qc->close_sent)
    {
        return pc_quic_build_connection_close(buf, cap, qc->close_error, qc->close_frame_type, NULL, 0);
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
static size_t packet_overhead(const struct QuicConn *qc, proto_bool is_long, uint8_t pn_len)
{
    size_t overhead = (size_t)PC_AES128GCM_TAG_LEN + pn_len;
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
static size_t build_packet(struct QuicConn *qc, int level, uint8_t *out, size_t cap)
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
    uint8_t pn_len = pc_quic_pn_length(pn, s->largest_acked);
    proto_bool is_long = (level != QUIC_ENC_APP);

    // Reserve the framing BEFORE filling the payload. build_frames() advances the connection's send
    // offsets (crypto_tx_off, and each stream's tx_off / tx_sent) as it writes, so a payload that
    // turns out not to fit once the header, packet number and AEAD tag are added cannot simply be
    // rejected further down: those bytes would already be counted as sent and would never go out.
    // That is silent data loss, and it was reachable - build_frames was handed the whole
    // PC_QUIC_MAX_DATAGRAM scratch while the packet needs header + pn + 16-byte tag on top of it,
    // so a stream with ~1326+ bytes pending, or a Handshake CRYPTO flight carrying a large
    // certificate, produced a payload the packet had no room for. Bound the payload instead.
    size_t overhead = packet_overhead(qc, is_long, pn_len);
    if (cap <= overhead)
    {
        return 0;
    }
    size_t budget = cap - overhead;

    uint8_t frames[PC_QUIC_MAX_DATAGRAM];
    // Dead with every current caller - quic_server's datagram buffer is itself
    // PC_QUIC_MAX_DATAGRAM, so `cap - overhead` is always below `frames` - but kept as the bound
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
        memset(frames + frame_len, 0, pad);
        frame_len += pad;
    }

    size_t p = 0;
    if (is_long)
    {
        // Invariant header fields, then the type-specific token (Initial only) + Length + PN.
        size_t hn = pc_quic_build_long_header(out, cap, level_lp_type(level), QUIC_VERSION_1, qc->dcid, qc->dcid_len,
                                              qc->scid, qc->scid_len, pn_len);
        if (!hn)
        {
            return 0;
        }
        p = hn;
        if (level == QUIC_ENC_INITIAL)
        {
            size_t n = pc_quic_varint_encode(out + p, cap - p, 0); // empty token
            if (!n)
            {
                return 0;
            }
            p += n;
        }
        uint64_t length = (uint64_t)pn_len + frame_len + PC_AES128GCM_TAG_LEN;
        size_t n = pc_quic_varint_encode(out + p, cap - p, length);
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
        memcpy(out + 1, qc->dcid, qc->dcid_len);
        p = 1 + qc->dcid_len;
    }

    size_t pn_offset = p;
    // Bound the whole packet up front (p <= cap here): the header builders checked their own writes
    // but not the packet number + payload + tag that follow, so verify the remainder fits before
    // writing it (avoids a size_t addition wrap in the bounds check, cpp:S3519).
    // Redundant since the payload is now budgeted against `overhead` up front, which is exactly
    // what makes the offsets build_frames() advanced safe to keep. Retained as defense in depth.
    if (cap - p < (size_t)pn_len + frame_len + PC_AES128GCM_TAG_LEN)
    {
        return 0;
    }
    // Write the (unprotected) truncated packet number.
    for (uint8_t i = 0; i < pn_len; i++)
    {
        out[pn_offset + i] = (uint8_t)(pn >> (8 * (pn_len - 1 - i)));
    }
    p += pn_len;

    memcpy(out + p, frames, frame_len);

    size_t total = pc_quic_packet_protect(out, cap, pn_offset, pn_len, pn, frame_len, keys, is_long);
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
static int pc_quic_highest_sealed_level(struct QuicConn *qc)
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

size_t pc_quic_conn_send(struct QuicConn *qc, uint8_t *out, size_t cap)
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
    if (cap > PC_QUIC_MAX_DATAGRAM)
    {
        cap = PC_QUIC_MAX_DATAGRAM;
    }

    // A queued transport CONNECTION_CLOSE is sent once - at the highest encryption level we still hold
    // keys for (so the peer can decrypt it) - and then the connection is closed. It replaces the normal
    // frame build (a closing endpoint sends nothing else) and is bounded by the amplification limit above.
    if (qc->close_queued && !qc->close_sent)
    {
        // Send at the level the error was seen on (the peer holds those keys); if that space has since
        // been discarded, fall back to the highest level we still hold keys for.
        int level = qc->close_level;
        // arm of the range check below can never be taken; the other three arms are all tested.
        if (level < QUIC_ENC_INITIAL || level > QUIC_ENC_APP || qc->space[level].discarded || !seal_keys(qc, level))
        {
            level = pc_quic_highest_sealed_level(qc);
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
    uint32_t p = PC_QUIC_PTO_MS;
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

void pc_quic_conn_on_timeout(struct QuicConn *qc, uint32_t now_ms)
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
    // pc_quic_conn_send() re-sends it, then back the timer off.
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
        for (size_t i = 0; i < PC_QUIC_MAX_STREAMS; i++)
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

size_t pc_quic_conn_stream_send(struct QuicConn *qc, uint64_t stream_id, const uint8_t *data, size_t len,
                                proto_bool fin)
{
    QuicStream *st = stream_get(qc, stream_id, PROTO_TRUE);
    if (!st)
    {
        return 0;
    }
    size_t room = sizeof(st->tx) - st->tx_have;
    size_t take = len < room ? len : room;
    memcpy(st->tx + st->tx_have, data, take);
    st->tx_have += take;
    if (fin && take == len)
    {
        st->tx_fin = PROTO_TRUE;
    }
    return take;
}

void pc_quic_conn_close(struct QuicConn *qc, uint64_t error_code)
{
    // Application-initiated close: send at the highest level we still hold keys for.
    int level = pc_quic_highest_sealed_level(qc);
    queue_close(qc, error_code, 0, level);
}

proto_bool pc_quic_conn_established(const struct QuicConn *qc)
{
    return qc->tls.state == QTLS_DONE;
}

proto_bool pc_quic_conn_is_closed(const struct QuicConn *qc)
{
    return qc->closed || qc->draining;
}

#endif // PC_ENABLE_HTTP3
