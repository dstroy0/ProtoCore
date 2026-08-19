// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_dtls_conn.c
 * @brief DTLS 1.3 server handshake state machine (RFC 9147 §5-6). See protocore_dtls_conn.h.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_DTLS

#include "crypto/ct_eq.h" // protocore_ct_eq: the Finished compare
#include "mmgr/protomem/protomem.h"
#include "mmgr/secure/secure.h" // protocore_secure_wipe
#include "network_drivers/presentation/security/dtls/dtls_conn/dtls_conn.h"

static uint8_t dtls_handshake_work[16]; // the borrow an entry takes; DtlsHandshake never reads it

static uint8_t dtls_record_work[16]; // the borrow an entry takes; DtlsRecord never reads it

#include "crypto/asymmetric/curve25519/curve25519.h"
#include "crypto/asymmetric/ed25519/ed25519.h" // protocore_ed25519_pubkey for the RFC 7250 RawPublicKey
#include "network_drivers/presentation/http/http3/tls13_msg/tls13_msg.h"
#include "network_drivers/presentation/http/http3/tls13_rpk/tls13_rpk.h" // the RFC 7250 RawPublicKey Certificate
#include "server/clock/clock.h" // protocore_millis() stamps / checks the HelloRetryRequest cookie freshness

static uint8_t tls13_rpk_work[16]; // the borrow an entry takes; Tls13Rpk never reads it

static uint8_t tls13_msg_work[16]; // the borrow an entry takes; Tls13Msg never reads it

PROTOCORE_BEGIN_DECLS

// TLS alert codes used here (RFC 8446 §6).
static const uint8_t ALERT_UNEXPECTED_MESSAGE = 10;
static const uint8_t ALERT_HANDSHAKE_FAILURE = 40;
static const uint8_t ALERT_UNSUPPORTED_CERTIFICATE = 43;
static const uint8_t ALERT_ILLEGAL_PARAMETER = 47;
static const uint8_t ALERT_DECODE_ERROR = 50;
static const uint8_t ALERT_DECRYPT_ERROR = 51;
static const uint8_t ALERT_PROTOCOL_VERSION = 70;
static const uint8_t ALERT_INTERNAL_ERROR = 80;

// HelloRetryRequest cookie freshness window: the client must echo the cookie within this many
// milliseconds of it being minted (RFC 9147 §5.1). protocore_millis() supplies both timestamps.
static const uint64_t DTLS_HRR_COOKIE_MAX_AGE_MS = 60000;

// The record-layer demux (RFC 9147 §4): a first byte 0b001xxxxx is a DTLSCiphertext unified header.
static proto_bool is_ciphertext(uint8_t b0)
{
    return (b0 & 0xE0) == 0x20;
}

// On-wire length of a DTLSCiphertext record from its (plaintext) header, so a datagram carrying more
// than one record can be walked. Mirrors the header flags DtlsRecord.protect writes. @p cid_len is
// our negotiated connection id length (the CID is not length-prefixed on the wire, RFC 9146), 0 if none.
static size_t ciphertext_record_len(const uint8_t *rec, size_t avail, size_t cid_len)
{
    if (avail < 1)
    {
        return 0;
    }
    uint8_t b0 = rec[0];
    size_t off = 1;
    if (b0 & 0x10) // C bit: connection id present, cid_len bytes (known only from negotiation)
    {
        off += cid_len;
    }
    off += (b0 & 0x08) ? 2 : 1; // S bit: 16- vs 8-bit sequence number
    if (b0 & 0x04)              // L bit: explicit length present
    {
        if (off + 2 > avail)
        {
            return 0;
        }
        size_t enc = ((size_t)rec[off] << 8) | rec[off + 1];
        off += 2;
        if (off + enc > avail)
        {
            return 0;
        }
        return off + enc;
    }
    return avail; // no length -> record runs to the end of the datagram
}

static int fail(DtlsConn *c, uint8_t alert)
{
    c->state = DTLS_CONN_STATE_FAILED;
    c->alert = alert;
    return -1;
}

// The Transcript-Hash runs under the suite's hash (RFC 8446 sec 4.4.1), so it goes through the key
// schedule that bound it rather than naming one here. The schedule a call acts on is named first,
// which is what tells the entry which hash it is.

// Start a Transcript-Hash in @p ctx.
static void transcript_start(DtlsConn *c, uint8_t *ctx)
{
    Tls13Ks.bind.ks = &c->ks;
    Tls13Ks.transcript_init(ctx);
}

// Fold @p len bytes of @p data into the transcript in @p ctx.
static void transcript_add(DtlsConn *c, uint8_t *ctx, const uint8_t *data, size_t len)
{
    Tls13Ks.bind.ks = &c->ks;
    Tls13Ks.transcript_args.data = data;
    Tls13Ks.transcript_args.len = len;
    Tls13Ks.transcript_update(ctx);
}

// The running transcript's hash so far (RFC 8446 intermediate hashes). Finalizing compresses the
// padded blocks into a copy of the state, so the context comes out untouched and keeps taking
// messages.
static void snapshot(DtlsConn *c, uint8_t *ctx, uint8_t *out)
{
    Tls13Ks.bind.ks = &c->ks;
    Tls13Ks.transcript_args.out = out;
    Tls13Ks.transcript_peek(ctx);
}

// Begin a new outbound flight (RFC 9147 §5.8): drop whatever was buffered for the previous one.
static void flight_reset(DtlsConn *c)
{
    c->flight_count = 0;
    c->flight_len = 0;
}

// Append one TLS handshake message (@p tls_msg, 4-byte TLS header + body) to the current flight: wrap
// it in a DTLS handshake header (message_seq from the running counter, so an optional HelloRetryRequest
// shifts every later message up by one) and buffer the fragment for (re)transmission. @p epoch is 0
// (DTLSPlaintext) or 2 (DTLSCiphertext). Records are not built here - that happens in flight_transmit,
// so a retransmission can use fresh record sequence numbers.
static proto_bool flight_add(DtlsConn *c, uint16_t epoch, const uint8_t *tls_msg, size_t tls_len)
{
    // tls_len < 4 is defensive and unreachable from any input the server accepts - every caller
    // passes a builder's output, and a TLS handshake message is never shorter than its own 4-byte
    // header - which is why it and every `if (!flight_add(...))` at the call sites carry coverage
    // exclusions.
    if (tls_len < 4)
    {
        return PROTO_FALSE;
    }
    // sec 4.3: what one fragment may carry is the path's datagram less the record around it and the
    // fragment's own header. A path too small to carry any body cannot carry a handshake.
    if (c->pmtu <= PROTOCORE_DTLS_REC_OVERHEAD_MAX + PROTOCORE_DTLS_HS_HDR_LEN)
    {
        return PROTO_FALSE;
    }
    const uint32_t per_frag = (uint32_t)(c->pmtu - PROTOCORE_DTLS_REC_OVERHEAD_MAX - PROTOCORE_DTLS_HS_HDR_LEN);

    const uint8_t msg_type = tls_msg[0];
    const uint32_t body_len = (uint32_t)(tls_len - 4);
    const uint16_t msg_seq = c->tx_msg_seq++;

    // A message longer than per_frag becomes several fragments, each its own entry and its own
    // record. An empty body still emits one, which is why the offset is tested after the append.
    uint32_t off = 0;
    do
    {
        if (c->flight_count >= PROTOCORE_DTLS_FLIGHT_MSGS)
        {
            return PROTO_FALSE;
        }
        uint32_t take = body_len - off;
        if (take > per_frag)
        {
            take = per_frag;
        }
        DtlsHandshake.frag_build_args.msg_type = msg_type;
        DtlsHandshake.frag_build_args.msg_seq = msg_seq;
        DtlsHandshake.frag_build_args.full_len = body_len;
        DtlsHandshake.frag_build_args.frag_offset = off;
        DtlsHandshake.frag_build_args.frag = tls_msg + 4 + off;
        DtlsHandshake.frag_build_args.frag_len = take;
        DtlsHandshake.frag_build_args.out = c->flight_buf + c->flight_len;
        DtlsHandshake.frag_build_args.out_cap = sizeof(c->flight_buf) - c->flight_len;
        DtlsHandshake.frag_build(dtls_handshake_work);
        const size_t flen = DtlsHandshake.n;
        if (!flen)
        {
            return PROTO_FALSE;
        }
        c->flight_msgs[c->flight_count].off = c->flight_len;
        c->flight_msgs[c->flight_count].len = (uint16_t)flen;
        c->flight_msgs[c->flight_count].epoch = (uint8_t)epoch;
        c->flight_count++;
        c->flight_len = (uint16_t)(c->flight_len + flen);
        off += take;
    } while (off < body_len);
    return PROTO_TRUE;
}

// Protect the buffered flight into @p out with FRESH record sequence numbers (RFC 9147 §5.8: a
// retransmission MUST use new sequence numbers - reusing one would repeat an AEAD nonce and be dropped
// by the peer's replay window). Records the record number of each message's transmission for ACK
// matching. Used for both the initial send and every retransmission.
static proto_bool flight_transmit(DtlsConn *c, uint8_t *out, size_t out_cap, size_t *out_len)
{
    for (uint8_t i = 0; i < c->flight_count; i++)
    {
        const uint8_t *frag = c->flight_buf + c->flight_msgs[i].off;
        size_t flen = c->flight_msgs[i].len;
        uint8_t epoch = c->flight_msgs[i].epoch;
        uint64_t seq;
        size_t rn;
        if (epoch == 0)
        {
            seq = c->tx_seq_ep0++;
            DtlsRecord.plaintext_build_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
            DtlsRecord.plaintext_build_args.epoch = 0;
            DtlsRecord.plaintext_build_args.seq = seq;
            DtlsRecord.plaintext_build_args.fragment = frag;
            DtlsRecord.plaintext_build_args.frag_len = flen;
            DtlsRecord.plaintext_build_args.out = out + *out_len;
            DtlsRecord.plaintext_build_args.out_cap = out_cap - *out_len;
            DtlsRecord.plaintext_build(dtls_record_work);
            rn = DtlsRecord.n;
        }
        else
        {
            seq = c->tx_seq_ep2++;
            DtlsRecord.protect_args.keys = &c->ep2_srv;
            DtlsRecord.protect_args.seq = seq;
            DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_HANDSHAKE;
            DtlsRecord.protect_args.plaintext = frag;
            DtlsRecord.protect_args.pt_len = flen;
            DtlsRecord.protect_args.out = out + *out_len;
            DtlsRecord.protect_args.out_cap = out_cap - *out_len;
            DtlsRecord.protect_args.cid = c->cid_negotiated ? c->peer_cid : NULL;
            DtlsRecord.protect_args.cid_len = c->cid_negotiated ? c->peer_cid_len : 0;
            DtlsRecord.protect(dtls_record_work);
            rn = DtlsRecord.n;
        }
        if (!rn)
        {
            return PROTO_FALSE;
        }
        *out_len += rn;
        c->flight_rec[i].epoch = epoch;
        c->flight_rec[i].seq = seq;
    }
    return PROTO_TRUE;
}

// Arm the retransmission timer after (re)sending a flight that expects a peer reply (RFC 9147 §5.8).
static void flight_arm(DtlsConn *c)
{
    c->awaiting_reply = PROTO_TRUE;
    c->retransmits = 0;
    c->pto_ms = PROTOCORE_DTLS_PTO_INITIAL_MS;
    c->flight_sent_ms = Clock.ms;
}

// Stop the retransmission timer: the expected reply arrived, or the flight was acknowledged.
static void flight_disarm(DtlsConn *c)
{
    c->awaiting_reply = PROTO_FALSE;
}

// Emit a HelloRetryRequest (RFC 9147 §5.1, RFC 8446 §4.1.4) asking the client to retry with an
// X25519 key_share, binding a stateless return-routability cookie to the peer address. Per RFC 8446
// §4.4.1 the transcript is restarted as the synthetic message_hash(ClientHello1) before the HRR is
// folded in, so the eventual transcript is message_hash || HRR || ClientHello2 || ServerHello || ...
static int send_hello_retry(DtlsConn *c, const Tls13ClientHello *ch, const uint8_t *ch1, size_t ch1_len, uint8_t *out,
                            size_t out_cap, size_t *out_len)
{
    uint8_t ch1_hash[TLS13_SECRET_MAX];
    uint8_t *h;
    h = c->hash_work2;
    transcript_start(c, h);
    transcript_add(c, h, ch1, ch1_len);
    snapshot(c, h, ch1_hash);

    c->transcript = c->hash_work;
    transcript_start(c, c->transcript); // restart: message_hash(Hash(CH1)) replaces ClientHello1
    Tls13Msg.build_message_hash_args.out = c->msgbuf;
    Tls13Msg.build_message_hash_args.cap = sizeof(c->msgbuf);
    Tls13Msg.build_message_hash_args.ch1_hash = ch1_hash;
    Tls13Msg.build_message_hash(tls13_msg_work);
    size_t n = Tls13Msg.n;
    if (!n)
    {
        return fail(c, ALERT_INTERNAL_ERROR);
    }
    transcript_add(c, c->transcript, c->msgbuf, n); // transcript only; message_hash is never sent

    // Stateless cookie with an empty payload: this connection keeps its own transcript across the
    // retry, so the cookie only has to prove return-routability and bind the client address.
    uint8_t cookie[PROTOCORE_DTLS_COOKIE_MAX];
    DtlsHandshake.cookie_make_args.mac_work = c->mac_work;
    DtlsHandshake.cookie_make_args.protocore_hmac_key = c->cfg.cookie_key;
    DtlsHandshake.cookie_make_args.timestamp = Clock.ms;
    DtlsHandshake.cookie_make_args.payload = NULL;
    DtlsHandshake.cookie_make_args.payload_len = 0;
    DtlsHandshake.cookie_make_args.client_addr = c->peer_addr;
    DtlsHandshake.cookie_make_args.addr_len = c->peer_addr_len;
    DtlsHandshake.cookie_make_args.out = cookie;
    DtlsHandshake.cookie_make_args.out_cap = sizeof(cookie);
    DtlsHandshake.cookie_make(dtls_handshake_work);
    size_t clen = DtlsHandshake.n;
    if (!clen)
    {
        return fail(c, ALERT_INTERNAL_ERROR);
    }

    Tls13Msg.build_hello_retry_request_args.out = c->msgbuf;
    Tls13Msg.build_hello_retry_request_args.cap = sizeof(c->msgbuf);
    Tls13Msg.build_hello_retry_request_args.session_id = ch->session_id;
    Tls13Msg.build_hello_retry_request_args.session_id_len = ch->session_id_len;
    Tls13Msg.build_hello_retry_request_args.selected_group = TLS_GROUP_X25519;
    Tls13Msg.build_hello_retry_request_args.suite = PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256;
    Tls13Msg.build_hello_retry_request_args.cookie = cookie;
    Tls13Msg.build_hello_retry_request_args.cookie_len = clen;
    Tls13Msg.build_hello_retry_request_args.dtls = /*dtls=*/PROTO_TRUE;
    Tls13Msg.build_hello_retry_request(tls13_msg_work);
    n = Tls13Msg.n;
    if (!n)
    {
        return fail(c, ALERT_INTERNAL_ERROR);
    }
    transcript_add(c, c->transcript, c->msgbuf, n);
    flight_reset(c);
    if (!flight_add(c, 0, c->msgbuf, n) || !flight_transmit(c, out, out_cap, out_len))
    {
        return fail(c, ALERT_INTERNAL_ERROR);
    }
    flight_arm(c); // await ClientHello2
    c->hrr_sent = PROTO_TRUE;
    return 0;
}

// After a HelloRetryRequest, the retry ClientHello must echo a valid cookie (proving the client's
// address) before we spend the handshake's asymmetric crypto (RFC 9147 §5.1). No HRR -> nothing to check.
static proto_bool protocore_dtls_hrr_cookie_ok(const DtlsConn *c, const Tls13ClientHello *ch)
{
    if (!c->hrr_sent)
    {
        return PROTO_TRUE;
    }
    if (!ch->cookie)
    {
        return PROTO_FALSE;
    }
    uint8_t payload[1];
    size_t plen = 0;
    DtlsHandshake.cookie_verify_args.mac_work = c->mac_work;
    DtlsHandshake.cookie_verify_args.protocore_hmac_key = c->cfg.cookie_key;
    DtlsHandshake.cookie_verify_args.now = Clock.ms;
    DtlsHandshake.cookie_verify_args.max_age = DTLS_HRR_COOKIE_MAX_AGE_MS;
    DtlsHandshake.cookie_verify_args.client_addr = c->peer_addr;
    DtlsHandshake.cookie_verify_args.addr_len = c->peer_addr_len;
    DtlsHandshake.cookie_verify_args.cookie = ch->cookie;
    DtlsHandshake.cookie_verify_args.cookie_len = ch->cookie_len;
    DtlsHandshake.cookie_verify_args.payload_out = payload;
    DtlsHandshake.cookie_verify_args.payload_cap = sizeof(payload);
    DtlsHandshake.cookie_verify_args.payload_len_out = &plen;
    DtlsHandshake.cookie_verify(dtls_handshake_work);
    return DtlsHandshake.ok;
}

// Connection-id negotiation (RFC 9146 / RFC 9147 §9): if the client offered a CID we can hold, store it
// (placed in records we send it) and choose our own CID from the fresh ServerHello random (unique per
// connection) for the records the client sends us.
static void protocore_dtls_negotiate_conn_id(DtlsConn *c, const Tls13ClientHello *ch)
{
    if (!ch->has_conn_id || ch->conn_id_len > PROTOCORE_DTLS_CID_MAX)
    {
        return;
    }
    c->cid_negotiated = PROTO_TRUE;
    c->peer_cid_len = (uint8_t)ch->conn_id_len;
    if (ch->conn_id_len)
    {
        mem.cpy(c->peer_cid, ch->conn_id, ch->conn_id_len);
    }
    c->local_cid_len = PROTOCORE_DTLS_CONN_LOCAL_CID_LEN;
    mem.cpy(c->local_cid, c->cfg.server_random, PROTOCORE_DTLS_CONN_LOCAL_CID_LEN);
}

// Consume a ClientHello and emit the whole server flight (ServerHello + the epoch-2 encrypted
// messages), installing handshake and application keys. Mirrors protocore_quic_tls process_client_hello. If the
// client did not offer an X25519 key_share, this instead sends a HelloRetryRequest and returns to wait
// for the client's second ClientHello (RFC 9147 §5.1).
static int handle_client_hello(DtlsConn *c, const uint8_t *msg, size_t msg_len, uint8_t *out, size_t out_cap,
                               size_t *out_len)
{
    Tls13ClientHello ch;
    Tls13Msg.parse_client_hello_args.msg = msg;
    Tls13Msg.parse_client_hello_args.len = msg_len;
    Tls13Msg.parse_client_hello_args.out = &ch;
    Tls13Msg.parse_client_hello_args.dtls = /*dtls=*/PROTO_TRUE;
    Tls13Msg.parse_client_hello(tls13_msg_work);
    if (!Tls13Msg.ok)
    {
        return fail(c, ALERT_DECODE_ERROR);
    }
    if (!ch.offers_tls13)
    {
        return fail(c, ALERT_PROTOCOL_VERSION);
    }
    // sec 4.1.3: the suite named in the ServerHello is one the client offered.
    if (!ch.offers_ed25519 || !ch.offers_x25519 || !ch.offers_aes128gcm_sha256)
    {
        return fail(c, ALERT_HANDSHAKE_FAILURE);
    }

    uint16_t ch_seq = c->reasm.msg_seq; // the message_seq this ClientHello arrived as

    // Group negotiation (RFC 8446 §4.1.4): the client offered X25519 but sent no X25519 key_share.
    // Answer with a HelloRetryRequest and await the retry - but only once (a retry that still lacks
    // the share is fatal, so a malicious client cannot loop us).
    if (!ch.has_key_share)
    {
        if (c->hrr_sent)
        {
            return fail(c, ALERT_HANDSHAKE_FAILURE);
        }
        if (send_hello_retry(c, &ch, msg, msg_len, out, out_cap, out_len) < 0)
        {
            return -1;
        }
        c->next_recv_msg_seq = (uint16_t)(ch_seq + 1);
        DtlsHandshake.reasm_init_args.r = &c->reasm;
        DtlsHandshake.reasm_init_args.msg_seq = c->next_recv_msg_seq;
        DtlsHandshake.reasm_init_args.buf = c->reasm_buf + 4;
        DtlsHandshake.reasm_init_args.buf_cap = PROTOCORE_DTLS_CONN_REASM_CAP;
        DtlsHandshake.reasm_init(dtls_handshake_work);
        return 0;
    }

    // A key_share is present. If it followed our HelloRetryRequest, the client must echo the cookie,
    // authenticating its address before we spend the handshake's asymmetric crypto (§5.1).
    if (!protocore_dtls_hrr_cookie_ok(c, &ch))
    {
        // RFC 9147 sec 5.1: "If a server receives a ClientHello with an invalid cookie, it MUST
        // terminate the handshake with an illegal_parameter alert", which tells the client to
        // restart without one rather than that the parameters could not be agreed.
        return fail(c, ALERT_ILLEGAL_PARAMETER);
    }

    protocore_dtls_negotiate_conn_id(c, &ch);

    // X25519 shared secret and the server's key_share.
    uint8_t ecdhe[32];
    uint8_t server_share[32];
    Curve25519.x25519_args.scalar = c->cfg.ephemeral_priv;
    Curve25519.x25519_args.point = ch.client_x25519;
    Curve25519.x25519_args.out = ecdhe;
    Curve25519.x25519(c->sign_work);
    if (!Curve25519.ok)
    {
        // RFC 8446 sec 7.4.2: the shared secret came out all-zero, so the peer's key share was a
        // point of small order and the secret is a constant it chose. Abort (RFC 7748 sec 6.1).
        return fail(c, ALERT_ILLEGAL_PARAMETER);
    }
    Curve25519.x25519_base_args.scalar = c->cfg.ephemeral_priv;
    Curve25519.x25519_base_args.out = server_share;
    Curve25519.x25519_base(c->sign_work);

    transcript_add(c, c->transcript, msg, msg_len); // transcript: ClientHello (CH2 when an HRR preceded it)

    flight_reset(c); // this ClientHello starts a fresh server flight (ServerHello..Finished)

    // ServerHello (epoch 0, plaintext).
    Tls13Msg.build_server_hello_args.out = c->msgbuf;
    Tls13Msg.build_server_hello_args.cap = sizeof(c->msgbuf);
    Tls13Msg.build_server_hello_args.random = c->cfg.server_random;
    Tls13Msg.build_server_hello_args.session_id = ch.session_id;
    Tls13Msg.build_server_hello_args.session_id_len = ch.session_id_len;
    Tls13Msg.build_server_hello_args.share = server_share;
    Tls13Msg.build_server_hello_args.share_len = 32;
    Tls13Msg.build_server_hello_args.group = TLS_GROUP_X25519;
    Tls13Msg.build_server_hello_args.suite = PROTOCORE_TLS_SUITE_AES_128_GCM_SHA256;
    Tls13Msg.build_server_hello_args.dtls = /*dtls=*/PROTO_TRUE;
    Tls13Msg.build_server_hello_args.conn_id = c->cid_negotiated ? c->local_cid : NULL;
    Tls13Msg.build_server_hello_args.conn_id_len = c->cid_negotiated ? c->local_cid_len : 0;
    Tls13Msg.build_server_hello(tls13_msg_work);
    size_t n = Tls13Msg.n;
    if (!n)
    {
        return fail(c, ALERT_INTERNAL_ERROR);
    }
    transcript_add(c, c->transcript, c->msgbuf, n);
    if (!flight_add(c, 0, c->msgbuf, n))
    {
        return fail(c, ALERT_INTERNAL_ERROR);
    }

    // Handshake-traffic keys from Transcript-Hash(..ServerHello).
    uint8_t hash[TLS13_SECRET_MAX];
    snapshot(c, c->transcript, hash);
    Tls13Ks.bind.kdf = &DTLS13_KDF;
    Tls13Ks.bind.ks = &c->ks;
    Tls13Ks.bind.s = c->ks_store;
    // This handshake offers TLS_AES_128_GCM_SHA256 only, so the schedule's hash is SHA-256. It is
    // stated rather than assumed because the schedule binds either.
    Tls13Ks.bind.is384 = PROTO_FALSE;
    Tls13Ks.early(NULL);
    Tls13Ks.bind.ks = &c->ks;
    Tls13Ks.step.ecdhe = ecdhe;
    Tls13Ks.step.ch_sh_hash = hash;
    Tls13Ks.step.ecdhe_len = 32;
    Tls13Ks.handshake(NULL);
    protocore_secure_wipe(ecdhe, sizeof(ecdhe)); // every epoch-2 and epoch-3 key derives from these 32 bytes
    DtlsRecord.keys_derive_args.out = &c->ep2_srv;
    DtlsRecord.keys_derive_args.cipher = DTLS_CIPHER_AES_128_GCM_SHA256;
    DtlsRecord.keys_derive_args.epoch = 2;
    DtlsRecord.keys_derive_args.secret = c->ks.s + TLS13_KS_SERVER_HS;
    DtlsRecord.keys_derive(dtls_record_work);
    DtlsRecord.keys_derive_args.out = &c->ep2_cli;
    DtlsRecord.keys_derive_args.cipher = DTLS_CIPHER_AES_128_GCM_SHA256;
    DtlsRecord.keys_derive_args.epoch = 2;
    DtlsRecord.keys_derive_args.secret = c->ks.s + TLS13_KS_CLIENT_HS;
    DtlsRecord.keys_derive(dtls_record_work);
    c->ep2_ready = PROTO_TRUE;

    // Raw Public Key negotiation (RFC 7250): if the client offered server_certificate_type = RawPublicKey,
    // answer with that type in EncryptedExtensions and send our Ed25519 SubjectPublicKeyInfo as the
    // Certificate instead of the X.509 chain. Additive - a client that does not ask still gets X.509.
    proto_bool rpk = PROTO_FALSE;
#if PROTOCORE_ENABLE_TLS_RPK
    rpk = ch.offers_rpk_server_cert;
#endif
    // RFC 7250 sec 4.2 outcome 2: a client that sent the extension named the only types it will
    // take. X.509 is what we answer with when it is among them; RawPublicKey only when this build
    // has it. Neither in common terminates the handshake rather than sending a type it refuses.
    if (ch.has_server_cert_type && !ch.offers_x509_server_cert && !rpk)
    {
        return fail(c, ALERT_UNSUPPORTED_CERTIFICATE);
    }

    // EncryptedExtensions.
    Tls13Msg.build_encrypted_extensions_empty_args.out = c->msgbuf;
    Tls13Msg.build_encrypted_extensions_empty_args.cap = sizeof(c->msgbuf);
    Tls13Msg.build_encrypted_extensions_empty_args.rpk_server_cert = rpk;
    Tls13Msg.build_encrypted_extensions_empty_args.alpn = NULL;
    Tls13Msg.build_encrypted_extensions_empty(tls13_msg_work);
    n = Tls13Msg.n;
    transcript_add(c, c->transcript, c->msgbuf, n);
    if (!flight_add(c, 2, c->msgbuf, n))
    {
        return fail(c, ALERT_INTERNAL_ERROR);
    }

    // Certificate (X.509 chain, or the RFC 7250 RawPublicKey when negotiated).
#if PROTOCORE_ENABLE_TLS_RPK
    if (rpk)
    {
        uint8_t ed_pub[PROTOCORE_ED25519_PUBKEY_LEN];
        Ed25519.pubkey_args.seed = c->cfg.ed25519_seed;
        Ed25519.pubkey_args.pub = ed_pub;
        Ed25519.pubkey(c->sign_work);
        Tls13Rpk.build_certificate_args.out = c->msgbuf;
        Tls13Rpk.build_certificate_args.cap = sizeof(c->msgbuf);
        Tls13Rpk.build_certificate_args.ed25519_pub = ed_pub;
        Tls13Rpk.build_certificate(tls13_rpk_work);
        n = Tls13Rpk.n;
    }
    else
#endif
    {
        Tls13Msg.build_certificate_args.out = c->msgbuf;
        Tls13Msg.build_certificate_args.cap = sizeof(c->msgbuf);
        Tls13Msg.build_certificate_args.cert_der = c->cfg.cert_der;
        Tls13Msg.build_certificate_args.cert_len = c->cfg.cert_len;
        Tls13Msg.build_certificate(tls13_msg_work);
        n = Tls13Msg.n;
    }
    if (!n)
    {
        return fail(c, ALERT_INTERNAL_ERROR);
    }
    transcript_add(c, c->transcript, c->msgbuf, n);
    if (!flight_add(c, 2, c->msgbuf, n))
    {
        return fail(c, ALERT_INTERNAL_ERROR);
    }

    // CertificateVerify signs Transcript-Hash(..Certificate).
    snapshot(c, c->transcript, hash);
    Tls13Msg.build_cert_verify_args.sign_work = c->sign_work;
    Tls13Msg.build_cert_verify_args.out = c->msgbuf;
    Tls13Msg.build_cert_verify_args.cap = sizeof(c->msgbuf);
    Tls13Msg.build_cert_verify_args.transcript_hash = hash;
    Tls13Msg.build_cert_verify_args.hash_len = c->ks.len;
    Tls13Msg.build_cert_verify_args.seed = c->cfg.ed25519_seed;
    Tls13Msg.build_cert_verify(tls13_msg_work);
    n = Tls13Msg.n;
    if (!n)
    {
        return fail(c, ALERT_INTERNAL_ERROR);
    }
    transcript_add(c, c->transcript, c->msgbuf, n);
    if (!flight_add(c, 2, c->msgbuf, n))
    {
        return fail(c, ALERT_INTERNAL_ERROR);
    }

    // Server Finished over Transcript-Hash(..CertificateVerify).
    snapshot(c, c->transcript, hash);
    uint8_t verify[TLS13_SECRET_MAX];
    Tls13Ks.bind.ks = &c->ks;
    Tls13Ks.finished_args.base_secret = c->ks.s + TLS13_KS_SERVER_HS;
    Tls13Ks.finished_args.transcript_hash = hash;
    Tls13Ks.finished_args.out = verify;
    Tls13Ks.finished_mac(NULL);
    Tls13Msg.build_finished_args.out = c->msgbuf;
    Tls13Msg.build_finished_args.cap = sizeof(c->msgbuf);
    Tls13Msg.build_finished_args.verify_data = verify;
    Tls13Msg.build_finished_args.verify_len = c->ks.len;
    Tls13Msg.build_finished(tls13_msg_work);
    n = Tls13Msg.n;
    transcript_add(c, c->transcript, c->msgbuf, n);
    if (!flight_add(c, 2, c->msgbuf, n))
    {
        return fail(c, ALERT_INTERNAL_ERROR);
    }

    // Application-traffic keys from Transcript-Hash(..server Finished); this hash also verifies the
    // client's Finished.
    snapshot(c, c->transcript, c->hs_finished_hash);
    Tls13Ks.bind.ks = &c->ks;
    Tls13Ks.step.ch_sfin_hash = c->hs_finished_hash;
    Tls13Ks.master(NULL);
    DtlsRecord.keys_derive_args.out = &c->ep3_srv;
    DtlsRecord.keys_derive_args.cipher = DTLS_CIPHER_AES_128_GCM_SHA256;
    DtlsRecord.keys_derive_args.epoch = 3;
    DtlsRecord.keys_derive_args.secret = c->ks.s + TLS13_KS_SERVER_AP;
    DtlsRecord.keys_derive(dtls_record_work);
    DtlsRecord.keys_derive_args.out = &c->ep3_cli;
    DtlsRecord.keys_derive_args.cipher = DTLS_CIPHER_AES_128_GCM_SHA256;
    DtlsRecord.keys_derive_args.epoch = 3;
    DtlsRecord.keys_derive_args.secret = c->ks.s + TLS13_KS_CLIENT_AP;
    DtlsRecord.keys_derive(dtls_record_work);
    c->ep3_ready = PROTO_TRUE;

    if (!flight_transmit(c, out, out_cap, out_len)) // protect the whole flight now that ep2 keys exist
    {
        return fail(c, ALERT_INTERNAL_ERROR);
    }
    flight_arm(c); // await the client Finished
    c->state = DTLS_CONN_STATE_WAIT_FINISHED;
    c->next_recv_msg_seq = (uint16_t)(ch_seq + 1);
    DtlsHandshake.reasm_init_args.r = &c->reasm;
    DtlsHandshake.reasm_init_args.msg_seq = c->next_recv_msg_seq;
    DtlsHandshake.reasm_init_args.buf = c->reasm_buf + 4;
    DtlsHandshake.reasm_init_args.buf_cap = PROTOCORE_DTLS_CONN_REASM_CAP;
    DtlsHandshake.reasm_init(dtls_handshake_work);
    return 0;
}

// Verify the client's Finished and complete the handshake.
static int handle_client_finished(DtlsConn *c, const uint8_t *msg, size_t msg_len)
{
    if (msg[0] != TLS_HS_FINISHED || msg_len != 4 + c->ks.len)
    {
        return fail(c, ALERT_DECODE_ERROR); // only routes a Finished here, so the type arm cannot be taken
    }
    Tls13Ks.bind.ks = &c->ks;
    Tls13Ks.finished_args.base_secret = c->ks.s + TLS13_KS_CLIENT_HS;
    Tls13Ks.finished_args.transcript_hash = c->hs_finished_hash;
    Tls13Ks.finished_args.out = c->ks.s + TLS13_KS_VERIFY;
    Tls13Ks.finished_mac(NULL);
    if (!protocore_ct_eq(c->ks.s + TLS13_KS_VERIFY, msg + 4, c->ks.len))
    {
        return fail(c, ALERT_DECRYPT_ERROR);
    }
    transcript_add(c, c->transcript, msg, msg_len);
    c->state = DTLS_CONN_STATE_DONE;
    flight_disarm(c); // the reply arrived; stop retransmitting the server flight
    // Re-arm the reassembler for the same message_seq so a retransmitted Finished (its ACK was lost)
    // completes again and we re-acknowledge it, instead of being rejected as unexpected (RFC 9147 §5.8.3).
    DtlsHandshake.reasm_init_args.r = &c->reasm;
    DtlsHandshake.reasm_init_args.msg_seq = c->next_recv_msg_seq;
    DtlsHandshake.reasm_init_args.buf = c->reasm_buf + 4;
    DtlsHandshake.reasm_init_args.buf_cap = PROTOCORE_DTLS_CONN_REASM_CAP;
    DtlsHandshake.reasm_init(dtls_handshake_work);
    return 0;
}

static int dispatch_message(DtlsConn *c, const uint8_t *tls_msg, size_t tls_len, uint8_t *out, size_t out_cap,
                            size_t *out_len)
{
    if (c->state == DTLS_CONN_STATE_START && tls_msg[0] == TLS_HS_CLIENT_HELLO)
    {
        return handle_client_hello(c, tls_msg, tls_len, out, out_cap, out_len);
    }
    if (c->state == DTLS_CONN_STATE_WAIT_FINISHED && tls_msg[0] == TLS_HS_FINISHED)
    {
        return handle_client_finished(c, tls_msg, tls_len);
    }
    if (c->state == DTLS_CONN_STATE_DONE && tls_msg[0] == TLS_HS_FINISHED)
    {
        c->hs_ack_sent = PROTO_FALSE; // a retransmitted client Finished (our ACK was lost): re-acknowledge it
        DtlsHandshake.reasm_init_args.r = &c->reasm;
        DtlsHandshake.reasm_init_args.msg_seq = c->next_recv_msg_seq;
        DtlsHandshake.reasm_init_args.buf = c->reasm_buf + 4;
        DtlsHandshake.reasm_init_args.buf_cap = PROTOCORE_DTLS_CONN_REASM_CAP;
        DtlsHandshake.reasm_init(dtls_handshake_work); // accept the next one too
        return 0;
    }
    return fail(c, ALERT_UNEXPECTED_MESSAGE);
}

// Parse and reassemble the DTLS handshake fragments carried in one record's payload, dispatching each
// complete TLS message.
static int drive_handshake(DtlsConn *c, const uint8_t *payload, size_t plen, uint8_t *out, size_t out_cap,
                           size_t *out_len)
{
    size_t p = 0;
    while (p < plen)
    {
        DtlsHsHeader hh;
        DtlsHandshake.header_parse_args.p = payload + p;
        DtlsHandshake.header_parse_args.len = plen - p;
        DtlsHandshake.header_parse_args.out = &hh;
        DtlsHandshake.header_parse(dtls_handshake_work);
        size_t used = DtlsHandshake.n;
        if (!used)
        {
            break;
        }
        p += used;
        DtlsHandshake.reasm_add_args.r = &c->reasm;
        DtlsHandshake.reasm_add_args.frag = &hh;
        DtlsHandshake.reasm_add(dtls_handshake_work);
        int r = DtlsHandshake.n; // ignores fragments for other message_seqs
        if (r < 0)
        {
            return fail(c, ALERT_DECODE_ERROR);
        }
        if (r == 1)
        {
            // Rebuild the TLS handshake structure (4-byte header + reassembled body) for the transcript.
            c->reasm_buf[0] = c->reasm.msg_type;
            c->reasm_buf[1] = (uint8_t)(c->reasm.length >> 16);
            c->reasm_buf[2] = (uint8_t)(c->reasm.length >> 8);
            c->reasm_buf[3] = (uint8_t)c->reasm.length;
            if (dispatch_message(c, c->reasm_buf, 4 + c->reasm.length, out, out_cap, out_len) < 0)
            {
                return -1;
            }
        }
    }
    return 0;
}

// A client ACK (RFC 9147 §7) for the outstanding flight: if it acknowledges every message of the last
// transmission, the peer has the whole flight, so stop retransmitting (§5.8.3). A partial ACK is
// ignored here - the timer simply retransmits the whole flight, which is always correct.
static void process_ack(DtlsConn *c, const uint8_t *body, size_t len)
{
    if (!c->awaiting_reply)
    {
        return;
    }
    DtlsRecordNumber acked[16];
    size_t count = 0;
    DtlsHandshake.ack_parse_args.body = body;
    DtlsHandshake.ack_parse_args.len = len;
    DtlsHandshake.ack_parse_args.out = acked;
    DtlsHandshake.ack_parse_args.out_cap = 16;
    DtlsHandshake.ack_parse_args.out_count = &count;
    DtlsHandshake.ack_parse(dtls_handshake_work);
    if (!DtlsHandshake.ok)
    {
        return;
    }
    for (uint8_t i = 0; i < c->flight_count; i++)
    {
        proto_bool found = PROTO_FALSE;
        for (size_t j = 0; j < count; j++)
        {
            if (acked[j].epoch == c->flight_rec[i].epoch && acked[j].seq == c->flight_rec[i].seq)
            {
                found = PROTO_TRUE;
                break;
            }
        }
        if (!found)
        {
            return; // a flight record is still unacknowledged; keep the timer running
        }
    }
    flight_disarm(c);
}

// One-record outcome for the protocore_dtls_conn_process datagram walk.
typedef enum PROTO_ENUM_PACKED
{
    DTLS_REC_STEP_NEXT,  // record consumed; keep walking the datagram
    DTLS_REC_STEP_STOP,  // malformed/short record; stop walking (leave the rest)
    DTLS_REC_STEP_FATAL, // fatal error already recorded via fail(); caller returns -1
} DtlsRecStep;

// Process one ciphertext (epoch-2) record at dgram[*off], advancing *off past a well-formed record.
static DtlsRecStep process_ciphertext_record(DtlsConn *c, const uint8_t *dgram, size_t len, size_t *off, uint8_t *out,
                                             size_t out_cap, size_t *out_len)
{
    size_t rlen = ciphertext_record_len(dgram + *off, len - *off, c->cid_negotiated ? c->local_cid_len : 0);
    if (!rlen)
    {
        return DTLS_REC_STEP_STOP; // malformed header; stop walking the datagram
    }
    // sec 4.5.2: "invalid records SHOULD be silently discarded, thus preserving the association";
    // an implementation that answers with a fatal alert instead is "extremely susceptible to DoS
    // attacks because UDP forgery is so easy". A record for an epoch whose keys do not exist yet is
    // one such record: it is skipped, not answered.
    if (!c->ep2_ready)
    {
        *off += rlen;
        return DTLS_REC_STEP_NEXT;
    }
    uint8_t inner[PROTOCORE_DTLS_CONN_REASM_CAP + PROTOCORE_DTLS_TAG_LEN];
    DtlsCiphertext info;
    uint64_t next = c->replay_ep2.seeded ? c->replay_ep2.highest + 1 : 0;
    DtlsRecord.unprotect_args.keys = &c->ep2_cli;
    DtlsRecord.unprotect_args.next_seq = next;
    DtlsRecord.unprotect_args.rec = dgram + *off;
    DtlsRecord.unprotect_args.rec_len = rlen;
    DtlsRecord.unprotect_args.out = inner;
    DtlsRecord.unprotect_args.out_cap = sizeof(inner);
    DtlsRecord.unprotect_args.info = &info;
    DtlsRecord.unprotect_args.expected_cid = c->cid_negotiated ? c->local_cid : NULL;
    DtlsRecord.unprotect_args.expected_cid_len = c->cid_negotiated ? c->local_cid_len : 0;
    DtlsRecord.unprotect(dtls_record_work);
    if (!DtlsRecord.ok)
    {
        // The same sec 4.5.2 rule: a record that fails its AEAD is discarded and the association
        // survives. One forged datagram must not end a live connection.
        *off += rlen;
        return DTLS_REC_STEP_NEXT;
    }
    *off += rlen;
    DtlsRecord.replay_check_args.w = &c->replay_ep2;
    DtlsRecord.replay_check_args.seq = info.seq;
    DtlsRecord.replay_check(dtls_record_work);
    if (!DtlsRecord.ok)
    {
        return DTLS_REC_STEP_NEXT; // replay: drop, but keep processing the datagram
    }
    DtlsRecord.replay_mark_args.w = &c->replay_ep2;
    DtlsRecord.replay_mark_args.seq = info.seq;
    DtlsRecord.replay_mark(dtls_record_work);
    proto_bool is_hs = (info.content_type == PROTOCORE_DTLS_CT_HANDSHAKE);
    if (is_hs)
    {
        c->rx_ep2_seq = info.seq; // the client Finished's record number, for the completion ACK
    }
    if (info.content_type == PROTOCORE_DTLS_CT_ACK)
    {
        process_ack(c, inner, info.pt_len); // the client acknowledged our flight
    }
    else if (is_hs && drive_handshake(c, inner, info.pt_len, out, out_cap, out_len) < 0)
    {
        return DTLS_REC_STEP_FATAL;
    }
    return DTLS_REC_STEP_NEXT;
}

// Process one plaintext (epoch-0) record at dgram[*off], advancing *off past a well-formed record.
static DtlsRecStep process_plaintext_record(DtlsConn *c, const uint8_t *dgram, size_t len, size_t *off, uint8_t *out,
                                            size_t out_cap, size_t *out_len)
{
    DtlsPlaintext pt;
    DtlsRecord.plaintext_parse_args.rec = dgram + *off;
    DtlsRecord.plaintext_parse_args.rec_len = len - *off;
    DtlsRecord.plaintext_parse_args.out = &pt;
    DtlsRecord.plaintext_parse(dtls_record_work);
    size_t rlen = DtlsRecord.n;
    if (!rlen)
    {
        return DTLS_REC_STEP_STOP;
    }
    *off += rlen;
    if (pt.content_type == PROTOCORE_DTLS_CT_HANDSHAKE &&
        drive_handshake(c, pt.fragment, pt.frag_len, out, out_cap, out_len) < 0)
    {
        return DTLS_REC_STEP_FATAL;
    }
    return DTLS_REC_STEP_NEXT;
}

// Called above its definition; the ACK helper below reads the handshake state through it.
static void dtls_server_established(uint8_t *restrict work);

// Once the client Finished completes the handshake, acknowledge it so the client stops retransmitting
// its final flight (RFC 9147 §5.8.3). The ACK is a content-type-26 record in the highest available epoch
// (3, application), covering the epoch-2 Finished record (§7). Sent at most once.
static void maybe_send_completion_ack(uint8_t *restrict work, DtlsConn *c, uint8_t *out, size_t out_cap,
                                      size_t *out_len)
{
    DtlsServer.established_args.c = c;
    dtls_server_established(work);
    if (!DtlsServer.ok || c->hs_ack_sent)
    {
        return;
    }
    DtlsRecordNumber rn = {2, c->rx_ep2_seq};
    uint8_t ack_body[2 + 16];
    DtlsHandshake.ack_build_args.nums = &rn;
    DtlsHandshake.ack_build_args.count = 1;
    DtlsHandshake.ack_build_args.out = ack_body;
    DtlsHandshake.ack_build_args.out_cap = sizeof(ack_body);
    DtlsHandshake.ack_build(dtls_handshake_work);
    size_t bl = DtlsHandshake.n;
    DtlsRecord.protect_args.keys = &c->ep3_srv;
    DtlsRecord.protect_args.seq = c->tx_seq_ep3++;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_ACK;
    DtlsRecord.protect_args.plaintext = ack_body;
    DtlsRecord.protect_args.pt_len = bl;
    DtlsRecord.protect_args.out = out + *out_len;
    DtlsRecord.protect_args.out_cap = out_cap - *out_len;
    DtlsRecord.protect_args.cid = c->cid_negotiated ? c->peer_cid : NULL;
    DtlsRecord.protect_args.cid_len = c->cid_negotiated ? c->peer_cid_len : 0;
    DtlsRecord.protect(dtls_record_work);
    size_t rec = DtlsRecord.n;
    if (rec)
    {
        *out_len += rec;
        c->hs_ack_sent = PROTO_TRUE;
    }
}

// --- the entries -----------------------------------------------------------

// No context and no borrow: every operand is the caller's. The borrow an entry takes is
// never read.

static void dtls_server_init(uint8_t *restrict work)
{
    (void)work;
    DtlsConn *c = DtlsServer.init_args.c;
    const DtlsServerConfig *cfg = DtlsServer.init_args.cfg;
    const uint8_t *peer_addr = DtlsServer.init_args.peer_addr;
    size_t peer_addr_len = DtlsServer.init_args.peer_addr_len;

    mem.zero(c, sizeof(*c));
    c->cfg = *cfg;
    c->state = DTLS_CONN_STATE_START;
    // sec 4.3: the path's datagram size bounds every fragment this connection emits. A caller that
    // knows its own path states it; one that does not gets the size no path may fall below.
    c->pmtu = cfg->pmtu;
    if (c->pmtu == 0)
    {
        c->pmtu = PROTOCORE_DTLS_PMTU_DEFAULT;
    }
    if (peer_addr && peer_addr_len)
    {
        if (peer_addr_len > PROTOCORE_DTLS_PEER_ADDR_MAX)
        {
            peer_addr_len = PROTOCORE_DTLS_PEER_ADDR_MAX;
        }
        mem.cpy(c->peer_addr, peer_addr, peer_addr_len);
        c->peer_addr_len = (uint8_t)peer_addr_len;
    }
    c->transcript = c->hash_work;
    transcript_start(c, c->transcript);
    DtlsRecord.replay_init_args.w = &c->replay_ep2;
    DtlsRecord.replay_init(dtls_record_work);
    DtlsRecord.replay_init_args.w = &c->replay_ep3;
    DtlsRecord.replay_init(dtls_record_work);
    c->next_recv_msg_seq = 0;
    DtlsHandshake.reasm_init_args.r = &c->reasm;
    DtlsHandshake.reasm_init_args.msg_seq = 0;
    DtlsHandshake.reasm_init_args.buf = c->reasm_buf + 4;
    DtlsHandshake.reasm_init_args.buf_cap = PROTOCORE_DTLS_CONN_REASM_CAP;
    DtlsHandshake.reasm_init(dtls_handshake_work);
}

static void dtls_server_process(uint8_t *restrict work)
{
    DtlsConn *c = DtlsServer.process_args.c;
    const uint8_t *dgram = DtlsServer.process_args.dgram;
    size_t len = DtlsServer.process_args.len;
    uint8_t *out = DtlsServer.process_args.out;
    size_t out_cap = DtlsServer.process_args.out_cap;

    if (c->state == DTLS_CONN_STATE_FAILED)
    {
        DtlsServer.n = -1;
        return;
    }
    size_t out_len = 0;
    size_t off = 0;
    while (off < len)
    {
        DtlsRecStep step = is_ciphertext(dgram[off])
                               ? process_ciphertext_record(c, dgram, len, &off, out, out_cap, &out_len)
                               : process_plaintext_record(c, dgram, len, &off, out, out_cap, &out_len);
        if (step == DTLS_REC_STEP_FATAL)
        {
            DtlsServer.n = -1;
            return;
        }
        if (step == DTLS_REC_STEP_STOP)
        {
            break;
        }
    }

    maybe_send_completion_ack(work, c, out, out_cap, &out_len);
    DtlsServer.n = (int)out_len;
}

static void dtls_server_timeout_ms(uint8_t *restrict work)
{
    (void)work;
    const DtlsConn *c = DtlsServer.timeout_ms_args.c;

    if (!c->awaiting_reply || c->state == DTLS_CONN_STATE_FAILED || c->state == DTLS_CONN_STATE_DONE)
    {
        DtlsServer.n = -1;
        return;
    }
    // Wrap-safe remaining time: (deadline - now) as a signed delta, clamped at 0 (already due).
    int32_t remaining = (int32_t)(c->flight_sent_ms + c->pto_ms - Clock.ms);
    DtlsServer.n = remaining > 0 ? remaining : 0;
}

static void dtls_server_on_timeout(uint8_t *restrict work)
{
    (void)work;
    DtlsConn *c = DtlsServer.on_timeout_args.c;
    uint8_t *out = DtlsServer.on_timeout_args.out;
    size_t out_cap = DtlsServer.on_timeout_args.out_cap;

    if (!c->awaiting_reply || c->state == DTLS_CONN_STATE_FAILED || c->state == DTLS_CONN_STATE_DONE)
    {
        DtlsServer.n = 0;
        return;
    }
    if ((int32_t)(Clock.ms - (c->flight_sent_ms + c->pto_ms)) < 0)
    {
        DtlsServer.n = 0; // not yet due (spurious / early wake-up)
        return;
    }
    if (c->retransmits >= PROTOCORE_DTLS_MAX_RETRANSMITS)
    {
        // Peer is gone; abandon the handshake. No alert - there is nobody to receive it.
        c->state = DTLS_CONN_STATE_FAILED;
        c->awaiting_reply = PROTO_FALSE;
        DtlsServer.n = -1;
        return;
    }
    size_t out_len = 0;
    if (!flight_transmit(c, out, out_cap, &out_len))
    {
        DtlsServer.n = -1;
        return;
    }
    c->retransmits++;
    c->pto_ms = c->pto_ms >= PROTOCORE_DTLS_PTO_MAX_MS / 2 ? PROTOCORE_DTLS_PTO_MAX_MS
                                                           : c->pto_ms * 2; // §5.8.1 backoff, capped
    c->flight_sent_ms = Clock.ms;
    DtlsServer.n = (int)out_len;
}

static void dtls_server_established(uint8_t *restrict work)
{
    (void)work;
    const DtlsConn *c = DtlsServer.established_args.c;

    DtlsServer.ok = c->state == DTLS_CONN_STATE_DONE && c->ep3_ready;
}

static void dtls_server_alert(uint8_t *restrict work)
{
    (void)work;
    const DtlsConn *c = DtlsServer.alert_args.c;

    DtlsServer.value = c->alert;
}

static void dtls_server_app_write_keys(uint8_t *restrict work)
{
    (void)work;
    DtlsConn *c = DtlsServer.app_write_keys_args.c;

    DtlsServer.ptr = c->ep3_ready ? &c->ep3_srv : NULL;
}

static void dtls_server_app_read_keys(uint8_t *restrict work)
{
    (void)work;
    DtlsConn *c = DtlsServer.app_read_keys_args.c;

    DtlsServer.ptr = c->ep3_ready ? &c->ep3_cli : NULL;
}

static void dtls_server_local_cid(uint8_t *restrict work)
{
    (void)work;
    const DtlsConn *c = DtlsServer.local_cid_args.c;
    uint8_t *out = DtlsServer.local_cid_args.out;

    if (!c->cid_negotiated || c->local_cid_len == 0)
    {
        DtlsServer.n = 0;
        return;
    }
    mem.cpy(out, c->local_cid, c->local_cid_len);
    DtlsServer.n = c->local_cid_len;
}

static void dtls_server_open_app(uint8_t *restrict work)
{
    DtlsConn *c = DtlsServer.open_app_args.c;
    const uint8_t *rec = DtlsServer.open_app_args.rec;
    size_t rec_len = DtlsServer.open_app_args.rec_len;
    uint8_t *out = DtlsServer.open_app_args.out;
    size_t out_cap = DtlsServer.open_app_args.out_cap;
    size_t *out_len = DtlsServer.open_app_args.out_len;

    DtlsServer.established_args.c = c;
    dtls_server_established(work);
    if (!DtlsServer.ok)
    {
        DtlsServer.ok = PROTO_FALSE;
        return;
    }
    DtlsCiphertext info;
    uint64_t next = c->replay_ep3.seeded ? c->replay_ep3.highest + 1 : 0;
    DtlsRecord.unprotect_args.keys = &c->ep3_cli;
    DtlsRecord.unprotect_args.next_seq = next;
    DtlsRecord.unprotect_args.rec = rec;
    DtlsRecord.unprotect_args.rec_len = rec_len;
    DtlsRecord.unprotect_args.out = out;
    DtlsRecord.unprotect_args.out_cap = out_cap;
    DtlsRecord.unprotect_args.info = &info;
    DtlsRecord.unprotect_args.expected_cid = c->cid_negotiated ? c->local_cid : NULL;
    DtlsRecord.unprotect_args.expected_cid_len = c->cid_negotiated ? c->local_cid_len : 0;
    DtlsRecord.unprotect(dtls_record_work);
    if (!DtlsRecord.ok)
    {
        DtlsServer.ok = PROTO_FALSE;
        return;
    }
    DtlsRecord.replay_check_args.w = &c->replay_ep3;
    DtlsRecord.replay_check_args.seq = info.seq;
    DtlsRecord.replay_check(dtls_record_work);
    if (!DtlsRecord.ok)
    {
        DtlsServer.ok = PROTO_FALSE; // replay or too old
        return;
    }
    DtlsRecord.replay_mark_args.w = &c->replay_ep3;
    DtlsRecord.replay_mark_args.seq = info.seq;
    DtlsRecord.replay_mark(dtls_record_work);
    if (info.content_type != PROTOCORE_DTLS_CT_APPLICATION_DATA)
    {
        DtlsServer.ok = PROTO_FALSE;
        return;
    }
    *out_len = info.pt_len;
    DtlsServer.ok = PROTO_TRUE;
}

static void dtls_server_seal_app(uint8_t *restrict work)
{
    DtlsConn *c = DtlsServer.seal_app_args.c;
    const uint8_t *data = DtlsServer.seal_app_args.data;
    size_t len = DtlsServer.seal_app_args.len;
    uint8_t *out = DtlsServer.seal_app_args.out;
    size_t out_cap = DtlsServer.seal_app_args.out_cap;

    DtlsServer.established_args.c = c;
    dtls_server_established(work);
    if (!DtlsServer.ok)
    {
        DtlsServer.n = 0;
        return;
    }
    // tx_seq_ep3 is shared with the completion ACK, so app records never reuse its sequence number.
    DtlsRecord.protect_args.keys = &c->ep3_srv;
    DtlsRecord.protect_args.seq = c->tx_seq_ep3++;
    DtlsRecord.protect_args.content_type = PROTOCORE_DTLS_CT_APPLICATION_DATA;
    DtlsRecord.protect_args.plaintext = data;
    DtlsRecord.protect_args.pt_len = len;
    DtlsRecord.protect_args.out = out;
    DtlsRecord.protect_args.out_cap = out_cap;
    DtlsRecord.protect_args.cid = c->cid_negotiated ? c->peer_cid : NULL;
    DtlsRecord.protect_args.cid_len = c->cid_negotiated ? c->peer_cid_len : 0;
    DtlsRecord.protect(dtls_record_work);
    DtlsServer.n = DtlsRecord.n;
}

DtlsConnNs DtlsServer = {
    .init = dtls_server_init,
    .process = dtls_server_process,
    .timeout_ms = dtls_server_timeout_ms,
    .on_timeout = dtls_server_on_timeout,
    .established = dtls_server_established,
    .alert = dtls_server_alert,
    .app_write_keys = dtls_server_app_write_keys,
    .app_read_keys = dtls_server_app_read_keys,
    .local_cid = dtls_server_local_cid,
    .open_app = dtls_server_open_app,
    .seal_app = dtls_server_seal_app,
};

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_DTLS
