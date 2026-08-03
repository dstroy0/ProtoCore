// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_dtls_conn.c
 * @brief DTLS 1.3 server handshake state machine (RFC 9147 §5-6). See pc_dtls_conn.h.
 */

#include "network_drivers/presentation/security/dtls/dtls_conn.h"

#if PC_ENABLE_DTLS

#include "crypto/asymmetric/curve25519.h"
#include "crypto/asymmetric/ed25519.h" // pc_ed25519_pubkey for the RFC 7250 RawPublicKey
#include "network_drivers/presentation/http/http3/tls13_msg.h"
#include "server/clock/clock.h" // pc_millis() stamps / checks the HelloRetryRequest cookie freshness
#include <string.h>

// TLS alert codes used here (RFC 8446 §6).
static const uint8_t ALERT_UNEXPECTED_MESSAGE = 10;
static const uint8_t ALERT_HANDSHAKE_FAILURE = 40;
static const uint8_t ALERT_DECODE_ERROR = 50;
static const uint8_t ALERT_DECRYPT_ERROR = 51;
static const uint8_t ALERT_PROTOCOL_VERSION = 70;
static const uint8_t ALERT_INTERNAL_ERROR = 80;

// HelloRetryRequest cookie freshness window: the client must echo the cookie within this many
// milliseconds of it being minted (RFC 9147 §5.1). pc_millis() supplies both timestamps.
static const uint64_t DTLS_HRR_COOKIE_MAX_AGE_MS = 60000;

// The record-layer demux (RFC 9147 §4): a first byte 0b001xxxxx is a DTLSCiphertext unified header.
static proto_bool is_ciphertext(uint8_t b0)
{
    return (b0 & 0xE0) == 0x20;
}

// On-wire length of a DTLSCiphertext record from its (plaintext) header, so a datagram carrying more
// than one record can be walked. Mirrors the header flags pc_dtls_ciphertext_protect writes. @p cid_len is
// our negotiated connection id length (the CID is not length-prefixed on the wire, RFC 9146), 0 if none.
static size_t ciphertext_record_len(const uint8_t *rec, size_t avail, size_t cid_len)
{
    if (avail < 1) // GCOVR_EXCL_LINE  the only caller (process_ciphertext_record) passes len - *off with
    {
        return 0; // GCOVR_EXCL_LINE  *off < len, so avail is always at least 1
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

// Finalize a copy of the running transcript without disturbing it (RFC 8446 intermediate hashes).
static void snapshot(const pc_sha256_ctx *ctx, uint8_t out[PC_SHA256_DIGEST_LEN])
{
    pc_sha256_ctx copy = *ctx;
    pc_sha256_final(&copy, out);
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
    // The three guards below are defensive and unreachable from any input the server accepts, which is
    // why they (and every `if (!flight_add(...))` at the call sites) carry coverage exclusions:
    //  - tls_len < 4: every caller passes a builder's output, and a TLS handshake message is never
    //    shorter than its own 4-byte header;
    //  - flight_count >= PC_DTLS_FLIGHT_MSGS: a flight is at most 5 messages (ServerHello, EncryptedExtensions,
    //    Certificate, CertificateVerify, Finished) against PC_DTLS_FLIGHT_MSGS = 6, and a HelloRetryRequest
    //    flight is a single message after flight_reset;
    //  - !flen: only Certificate can approach PC_DTLS_CONN_MSG_CAP, and PC_DTLS_FLIGHT_CAP is defined as
    //    PC_DTLS_CONN_MSG_CAP + 512 - more than the ~300 bytes the other four fragments occupy.
    if (tls_len < 4 || c->flight_count >= PC_DTLS_FLIGHT_MSGS) // GCOVR_EXCL_LINE
    {
        return PROTO_FALSE; // GCOVR_EXCL_LINE
    }
    uint8_t msg_type = tls_msg[0];
    uint32_t body_len = (uint32_t)(tls_len - 4);
    uint16_t msg_seq = c->tx_msg_seq++;
    size_t flen = pc_dtls_hs_frag_build(msg_type, msg_seq, body_len, 0, tls_msg + 4, body_len,
                                        c->flight_buf + c->flight_len, sizeof(c->flight_buf) - c->flight_len);
    if (!flen) // GCOVR_EXCL_LINE
    {
        return PROTO_FALSE; // GCOVR_EXCL_LINE
    }
    c->flight_msgs[c->flight_count].off = c->flight_len;
    c->flight_msgs[c->flight_count].len = (uint16_t)flen;
    c->flight_msgs[c->flight_count].epoch = (uint8_t)epoch;
    c->flight_count++;
    c->flight_len = (uint16_t)(c->flight_len + flen);
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
            rn = pc_dtls_plaintext_build(PC_DTLS_CT_HANDSHAKE, 0, seq, frag, flen, out + *out_len, out_cap - *out_len);
        }
        else
        {
            seq = c->tx_seq_ep2++;
            rn = pc_dtls_ciphertext_protect(&c->ep2_srv, seq, PC_DTLS_CT_HANDSHAKE, frag, flen, out + *out_len,
                                            out_cap - *out_len, c->cid_negotiated ? c->peer_cid : NULL,
                                            c->cid_negotiated ? c->peer_cid_len : 0);
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
    c->pto_ms = PC_DTLS_PTO_INITIAL_MS;
    c->flight_sent_ms = pc_millis();
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
    uint8_t ch1_hash[PC_SHA256_DIGEST_LEN];
    pc_sha256_ctx h;
    pc_sha256_init(&h);
    pc_sha256_update(&h, ch1, ch1_len);
    pc_sha256_final(&h, ch1_hash);

    pc_sha256_init(&c->transcript); // restart: message_hash(Hash(CH1)) replaces ClientHello1
    size_t n = pc_tls13_build_message_hash(c->msgbuf, sizeof(c->msgbuf), ch1_hash);
    if (!n) // GCOVR_EXCL_LINE  a message_hash is 36 bytes and msgbuf is
    {
        return fail(c, ALERT_INTERNAL_ERROR); // GCOVR_EXCL_LINE  PC_DTLS_CONN_MSG_CAP (1024): it always fits
    }
    pc_sha256_update(&c->transcript, c->msgbuf, n); // transcript only; message_hash is never sent

    // Stateless cookie with an empty payload: this connection keeps its own transcript across the
    // retry, so the cookie only has to prove return-routability and bind the client address.
    uint8_t cookie[PC_DTLS_COOKIE_MAX];
    size_t clen = pc_dtls_cookie_make(c->cfg.cookie_key, pc_millis(), NULL, 0, c->peer_addr, c->peer_addr_len, cookie,
                                      sizeof(cookie));
    if (!clen) // GCOVR_EXCL_LINE  an empty-payload cookie is 43 bytes against
    {
        return fail(c, ALERT_INTERNAL_ERROR); // GCOVR_EXCL_LINE  cookie[PC_DTLS_COOKIE_MAX] (128): it always fits
    }

    n = pc_tls13_build_hello_retry_request(c->msgbuf, sizeof(c->msgbuf), ch->session_id, ch->session_id_len,
                                           TLS_GROUP_X25519, cookie, clen, /*dtls=*/PROTO_TRUE);
    if (!n) // GCOVR_EXCL_LINE  an HRR with a 43-byte cookie is ~140 bytes
    {
        return fail(c, ALERT_INTERNAL_ERROR); // GCOVR_EXCL_LINE  against msgbuf's 1024: it always fits
    }
    pc_sha256_update(&c->transcript, c->msgbuf, n);
    flight_reset(c);
    // GCOVR_EXCL_LINE below: flight_add cannot fail here (see its guards) - only the flight_transmit arm
    // is reachable, and it is covered by a too-small output buffer.
    if (!flight_add(c, 0, c->msgbuf, n) || !flight_transmit(c, out, out_cap, out_len)) // GCOVR_EXCL_LINE
    {
        return fail(c, ALERT_INTERNAL_ERROR);
    }
    flight_arm(c); // await ClientHello2
    c->hrr_sent = PROTO_TRUE;
    return 0;
}

// After a HelloRetryRequest, the retry ClientHello must echo a valid cookie (proving the client's
// address) before we spend the handshake's asymmetric crypto (RFC 9147 §5.1). No HRR -> nothing to check.
static proto_bool pc_dtls_hrr_cookie_ok(const DtlsConn *c, const Tls13ClientHello *ch)
{
    if (!c->hrr_sent)
    {
        return PROTO_TRUE;
    }
    uint8_t payload[1];
    size_t plen = 0;
    return ch->cookie &&
           pc_dtls_cookie_verify(c->cfg.cookie_key, pc_millis(), DTLS_HRR_COOKIE_MAX_AGE_MS, c->peer_addr,
                                 c->peer_addr_len, ch->cookie, ch->cookie_len, payload, sizeof(payload), &plen);
}

// Connection-id negotiation (RFC 9146 / RFC 9147 §9): if the client offered a CID we can hold, store it
// (placed in records we send it) and choose our own CID from the fresh ServerHello random (unique per
// connection) for the records the client sends us.
static void pc_dtls_negotiate_conn_id(DtlsConn *c, const Tls13ClientHello *ch)
{
    if (!ch->has_conn_id || ch->conn_id_len > PC_DTLS_CID_MAX)
    {
        return;
    }
    c->cid_negotiated = PROTO_TRUE;
    c->peer_cid_len = (uint8_t)ch->conn_id_len;
    if (ch->conn_id_len)
    {
        memcpy(c->peer_cid, ch->conn_id, ch->conn_id_len);
    }
    c->local_cid_len = PC_DTLS_CONN_LOCAL_CID_LEN;
    memcpy(c->local_cid, c->cfg.server_random, PC_DTLS_CONN_LOCAL_CID_LEN);
}

// Consume a ClientHello and emit the whole server flight (ServerHello + the epoch-2 encrypted
// messages), installing handshake and application keys. Mirrors pc_quic_tls process_client_hello. If the
// client did not offer an X25519 key_share, this instead sends a HelloRetryRequest and returns to wait
// for the client's second ClientHello (RFC 9147 §5.1).
static int handle_client_hello(DtlsConn *c, const uint8_t *msg, size_t msg_len, uint8_t *out, size_t out_cap,
                               size_t *out_len)
{
    Tls13ClientHello ch;
    if (!pc_tls13_parse_client_hello(msg, msg_len, &ch, /*dtls=*/PROTO_TRUE))
    {
        return fail(c, ALERT_DECODE_ERROR);
    }
    if (!ch.offers_tls13)
    {
        return fail(c, ALERT_PROTOCOL_VERSION);
    }
    if (!ch.offers_ed25519 || !ch.offers_x25519)
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
        pc_dtls_hs_reasm_init(&c->reasm, c->next_recv_msg_seq, c->reasm_buf + 4, PC_DTLS_CONN_REASM_CAP);
        return 0;
    }

    // A key_share is present. If it followed our HelloRetryRequest, the client must echo the cookie,
    // authenticating its address before we spend the handshake's asymmetric crypto (§5.1).
    if (!pc_dtls_hrr_cookie_ok(c, &ch))
    {
        return fail(c, ALERT_HANDSHAKE_FAILURE);
    }

    pc_dtls_negotiate_conn_id(c, &ch);

    // X25519 shared secret and the server's key_share.
    uint8_t ecdhe[32];
    uint8_t server_share[32];
    pc_x25519(ecdhe, c->cfg.ephemeral_priv, ch.client_x25519);
    pc_x25519_base(server_share, c->cfg.ephemeral_priv);

    pc_sha256_update(&c->transcript, msg, msg_len); // transcript: ClientHello (CH2 when an HRR preceded it)

    flight_reset(c); // this ClientHello starts a fresh server flight (ServerHello..Finished)

    // ServerHello (epoch 0, plaintext).
    size_t n =
        pc_tls13_build_server_hello(c->msgbuf, sizeof(c->msgbuf), c->cfg.server_random, ch.session_id,
                                    ch.session_id_len, server_share, 32, TLS_GROUP_X25519, /*dtls=*/PROTO_TRUE,
                                    c->cid_negotiated ? c->local_cid : NULL, c->cid_negotiated ? c->local_cid_len : 0);
    if (!n) // GCOVR_EXCL_LINE  a ServerHello is ~130 bytes against msgbuf's
    {
        return fail(c, ALERT_INTERNAL_ERROR); // GCOVR_EXCL_LINE  1024: it always fits
    }
    pc_sha256_update(&c->transcript, c->msgbuf, n);
    if (!flight_add(c, 0, c->msgbuf, n)) // GCOVR_EXCL_LINE  flight_add cannot fail (see its guards):
    {
        return fail(c, ALERT_INTERNAL_ERROR); // GCOVR_EXCL_LINE  this is the first message of a reset flight
    }

    // Handshake-traffic keys from Transcript-Hash(..ServerHello).
    uint8_t hash[PC_SHA256_DIGEST_LEN];
    snapshot(&c->transcript, hash);
    pc_tls13_ks_early(&DTLS13_KDF, &c->ks);
    pc_tls13_ks_handshake(&c->ks, ecdhe, hash, 32);
    pc_dtls_record_keys_derive(&c->ep2_srv, DTLS_CIPHER_AES_128_GCM_SHA256, 2, c->ks.server_hs_traffic);
    pc_dtls_record_keys_derive(&c->ep2_cli, DTLS_CIPHER_AES_128_GCM_SHA256, 2, c->ks.client_hs_traffic);
    c->ep2_ready = PROTO_TRUE;

    // Raw Public Key negotiation (RFC 7250): if the client offered server_certificate_type = RawPublicKey,
    // answer with that type in EncryptedExtensions and send our Ed25519 SubjectPublicKeyInfo as the
    // Certificate instead of the X.509 chain. Additive - a client that does not ask still gets X.509.
    proto_bool rpk = PROTO_FALSE;
#if PC_ENABLE_TLS_RPK
    rpk = ch.offers_rpk_server_cert;
#endif

    // EncryptedExtensions.
    n = pc_tls13_build_encrypted_extensions_empty(c->msgbuf, sizeof(c->msgbuf), rpk);
    pc_sha256_update(&c->transcript, c->msgbuf, n);
    if (!flight_add(c, 2, c->msgbuf, n)) // GCOVR_EXCL_LINE  flight_add cannot fail (see its guards):
    {
        return fail(c, ALERT_INTERNAL_ERROR); // GCOVR_EXCL_LINE  the flight never fills PC_DTLS_FLIGHT_CAP
    }

    // Certificate (X.509 chain, or the RFC 7250 RawPublicKey when negotiated).
#if PC_ENABLE_TLS_RPK
    if (rpk)
    {
        uint8_t ed_pub[PC_ED25519_PUBKEY_LEN];
        pc_ed25519_pubkey(ed_pub, c->cfg.ed25519_seed);
        n = pc_tls13_build_certificate_rpk(c->msgbuf, sizeof(c->msgbuf), ed_pub);
    }
    else
#endif
        n = pc_tls13_build_certificate(c->msgbuf, sizeof(c->msgbuf), c->cfg.cert_der, c->cfg.cert_len);
    if (!n)
    {
        return fail(c, ALERT_INTERNAL_ERROR);
    }
    pc_sha256_update(&c->transcript, c->msgbuf, n);
    if (!flight_add(c, 2, c->msgbuf, n)) // GCOVR_EXCL_LINE  flight_add cannot fail (see its guards): the
    {
        return fail(c, ALERT_INTERNAL_ERROR); // GCOVR_EXCL_LINE  Certificate is capped by msgbuf, which fits
    }

    // CertificateVerify signs Transcript-Hash(..Certificate).
    snapshot(&c->transcript, hash);
    n = pc_tls13_build_cert_verify(c->msgbuf, sizeof(c->msgbuf), hash, c->cfg.ed25519_seed);
    if (!n) // GCOVR_EXCL_LINE  a CertificateVerify is 72 bytes against
    {
        return fail(c, ALERT_INTERNAL_ERROR); // GCOVR_EXCL_LINE  msgbuf's 1024: it always fits
    }
    pc_sha256_update(&c->transcript, c->msgbuf, n);
    if (!flight_add(c, 2, c->msgbuf, n)) // GCOVR_EXCL_LINE  flight_add cannot fail (see its guards):
    {
        return fail(c, ALERT_INTERNAL_ERROR); // GCOVR_EXCL_LINE  the flight never fills PC_DTLS_FLIGHT_CAP
    }

    // Server Finished over Transcript-Hash(..CertificateVerify).
    snapshot(&c->transcript, hash);
    uint8_t verify[PC_SHA256_DIGEST_LEN];
    pc_tls13_finished_mac(&DTLS13_KDF, c->ks.server_hs_traffic, hash, verify);
    n = pc_tls13_build_finished(c->msgbuf, sizeof(c->msgbuf), verify);
    pc_sha256_update(&c->transcript, c->msgbuf, n);
    if (!flight_add(c, 2, c->msgbuf, n)) // GCOVR_EXCL_LINE  flight_add cannot fail (see its guards):
    {
        return fail(c, ALERT_INTERNAL_ERROR); // GCOVR_EXCL_LINE  the flight never fills PC_DTLS_FLIGHT_CAP
    }

    // Application-traffic keys from Transcript-Hash(..server Finished); this hash also verifies the
    // client's Finished.
    snapshot(&c->transcript, c->hs_finished_hash);
    pc_tls13_ks_master(&c->ks, c->hs_finished_hash);
    pc_dtls_record_keys_derive(&c->ep3_srv, DTLS_CIPHER_AES_128_GCM_SHA256, 3, c->ks.server_ap_traffic);
    pc_dtls_record_keys_derive(&c->ep3_cli, DTLS_CIPHER_AES_128_GCM_SHA256, 3, c->ks.client_ap_traffic);
    c->ep3_ready = PROTO_TRUE;

    if (!flight_transmit(c, out, out_cap, out_len)) // protect the whole flight now that ep2 keys exist
    {
        return fail(c, ALERT_INTERNAL_ERROR);
    }
    flight_arm(c); // await the client Finished
    c->state = DTLS_CONN_STATE_WAIT_FINISHED;
    c->next_recv_msg_seq = (uint16_t)(ch_seq + 1);
    pc_dtls_hs_reasm_init(&c->reasm, c->next_recv_msg_seq, c->reasm_buf + 4, PC_DTLS_CONN_REASM_CAP);
    return 0;
}

// Verify the client's Finished and complete the handshake.
static int handle_client_finished(DtlsConn *c, const uint8_t *msg, size_t msg_len)
{
    if (msg[0] != TLS_HS_FINISHED || msg_len != 4 + PC_SHA256_DIGEST_LEN) // GCOVR_EXCL_LINE  dispatch_message
    {
        return fail(c, ALERT_DECODE_ERROR); // only routes a Finished here, so the type arm cannot be taken
    }
    uint8_t expected[PC_SHA256_DIGEST_LEN];
    pc_tls13_finished_mac(&DTLS13_KDF, c->ks.client_hs_traffic, c->hs_finished_hash, expected);
    uint8_t diff = 0;
    for (int i = 0; i < PC_SHA256_DIGEST_LEN; i++)
    {
        diff |= (uint8_t)(expected[i] ^ msg[4 + i]);
    }
    if (diff)
    {
        return fail(c, ALERT_DECRYPT_ERROR);
    }
    pc_sha256_update(&c->transcript, msg, msg_len);
    c->state = DTLS_CONN_STATE_DONE;
    flight_disarm(c); // the reply arrived; stop retransmitting the server flight
    // Re-arm the reassembler for the same message_seq so a retransmitted Finished (its ACK was lost)
    // completes again and we re-acknowledge it, instead of being rejected as unexpected (RFC 9147 §5.8.3).
    pc_dtls_hs_reasm_init(&c->reasm, c->next_recv_msg_seq, c->reasm_buf + 4, PC_DTLS_CONN_REASM_CAP);
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
        pc_dtls_hs_reasm_init(&c->reasm, c->next_recv_msg_seq, c->reasm_buf + 4,
                              PC_DTLS_CONN_REASM_CAP); // accept the next one too
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
        size_t used = pc_dtls_hs_header_parse(payload + p, plen - p, &hh);
        if (!used)
        {
            break;
        }
        p += used;
        int r = pc_dtls_hs_reasm_add(&c->reasm, &hh); // ignores fragments for other message_seqs
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
    if (!pc_dtls_ack_parse(body, len, acked, 16, &count))
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

// One-record outcome for the pc_dtls_conn_process datagram walk.
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
    if (!c->ep2_ready)
    {
        fail(c, ALERT_UNEXPECTED_MESSAGE);
        return DTLS_REC_STEP_FATAL;
    }
    uint8_t inner[PC_DTLS_CONN_REASM_CAP + PC_DTLS_TAG_LEN];
    DtlsCiphertext info;
    uint64_t next = c->replay_ep2.seeded ? c->replay_ep2.highest + 1 : 0;
    if (!pc_dtls_ciphertext_unprotect(&c->ep2_cli, next, dgram + *off, rlen, inner, sizeof(inner), &info,
                                      c->cid_negotiated ? c->local_cid : NULL,
                                      c->cid_negotiated ? c->local_cid_len : 0))
    {
        fail(c, ALERT_DECRYPT_ERROR);
        return DTLS_REC_STEP_FATAL;
    }
    *off += rlen;
    if (!pc_dtls_replay_check(&c->replay_ep2, info.seq))
    {
        return DTLS_REC_STEP_NEXT; // replay: drop, but keep processing the datagram
    }
    pc_dtls_replay_mark(&c->replay_ep2, info.seq);
    proto_bool is_hs = (info.content_type == PC_DTLS_CT_HANDSHAKE);
    if (is_hs)
    {
        c->rx_ep2_seq = info.seq; // the client Finished's record number, for the completion ACK
    }
    if (info.content_type == PC_DTLS_CT_ACK)
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
    size_t rlen = pc_dtls_plaintext_parse(dgram + *off, len - *off, &pt);
    if (!rlen)
    {
        return DTLS_REC_STEP_STOP;
    }
    *off += rlen;
    if (pt.content_type == PC_DTLS_CT_HANDSHAKE &&
        drive_handshake(c, pt.fragment, pt.frag_len, out, out_cap, out_len) < 0)
    {
        return DTLS_REC_STEP_FATAL;
    }
    return DTLS_REC_STEP_NEXT;
}

// Once the client Finished completes the handshake, acknowledge it so the client stops retransmitting
// its final flight (RFC 9147 §5.8.3). The ACK is a content-type-26 record in the highest available epoch
// (3, application), covering the epoch-2 Finished record (§7). Sent at most once.
static void maybe_send_completion_ack(DtlsConn *c, uint8_t *out, size_t out_cap, size_t *out_len)
{
    if (!pc_dtls_conn_established(c) || c->hs_ack_sent)
    {
        return;
    }
    DtlsRecordNumber rn = {2, c->rx_ep2_seq};
    uint8_t ack_body[2 + 16];
    size_t bl = pc_dtls_ack_build(&rn, 1, ack_body, sizeof(ack_body));
    size_t rec = pc_dtls_ciphertext_protect(&c->ep3_srv, c->tx_seq_ep3++, PC_DTLS_CT_ACK, ack_body, bl, out + *out_len,
                                            out_cap - *out_len, c->cid_negotiated ? c->peer_cid : NULL,
                                            c->cid_negotiated ? c->peer_cid_len : 0);
    if (rec)
    {
        *out_len += rec;
        c->hs_ack_sent = PROTO_TRUE;
    }
}

void pc_dtls_conn_init(DtlsConn *c, const DtlsServerConfig *cfg, const uint8_t *peer_addr, size_t peer_addr_len)
{
    memset(c, 0, sizeof(*c));
    c->cfg = *cfg;
    c->state = DTLS_CONN_STATE_START;
    if (peer_addr && peer_addr_len)
    {
        if (peer_addr_len > PC_DTLS_PEER_ADDR_MAX)
        {
            peer_addr_len = PC_DTLS_PEER_ADDR_MAX;
        }
        memcpy(c->peer_addr, peer_addr, peer_addr_len);
        c->peer_addr_len = (uint8_t)peer_addr_len;
    }
    pc_sha256_init(&c->transcript);
    pc_dtls_replay_init(&c->replay_ep2);
    pc_dtls_replay_init(&c->replay_ep3);
    c->next_recv_msg_seq = 0;
    pc_dtls_hs_reasm_init(&c->reasm, 0, c->reasm_buf + 4, PC_DTLS_CONN_REASM_CAP);
}

int pc_dtls_conn_process(DtlsConn *c, const uint8_t *dgram, size_t len, uint8_t *out, size_t out_cap)
{
    if (c->state == DTLS_CONN_STATE_FAILED)
    {
        return -1;
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
            return -1;
        }
        if (step == DTLS_REC_STEP_STOP)
        {
            break;
        }
    }

    maybe_send_completion_ack(c, out, out_cap, &out_len);
    return (int)out_len;
}

int pc_dtls_conn_timeout_ms(const DtlsConn *c)
{
    if (!c->awaiting_reply || c->state == DTLS_CONN_STATE_FAILED || c->state == DTLS_CONN_STATE_DONE)
    {
        return -1;
    }
    // Wrap-safe remaining time: (deadline - now) as a signed delta, clamped at 0 (already due).
    int32_t remaining = (int32_t)(c->flight_sent_ms + c->pto_ms - pc_millis());
    return remaining > 0 ? remaining : 0;
}

int pc_dtls_conn_on_timeout(DtlsConn *c, uint8_t *out, size_t out_cap)
{
    if (!c->awaiting_reply || c->state == DTLS_CONN_STATE_FAILED || c->state == DTLS_CONN_STATE_DONE)
    {
        return 0;
    }
    if ((int32_t)(pc_millis() - (c->flight_sent_ms + c->pto_ms)) < 0)
    {
        return 0; // not yet due (spurious / early wake-up)
    }
    if (c->retransmits >= PC_DTLS_MAX_RETRANSMITS)
    {
        // Peer is gone; abandon the handshake. No alert - there is nobody to receive it.
        c->state = DTLS_CONN_STATE_FAILED;
        c->awaiting_reply = PROTO_FALSE;
        return -1;
    }
    size_t out_len = 0;
    if (!flight_transmit(c, out, out_cap, &out_len))
    {
        return -1;
    }
    c->retransmits++;
    c->pto_ms = c->pto_ms >= PC_DTLS_PTO_MAX_MS / 2 ? PC_DTLS_PTO_MAX_MS : c->pto_ms * 2; // §5.8.1 backoff, capped
    c->flight_sent_ms = pc_millis();
    return (int)out_len;
}

proto_bool pc_dtls_conn_established(const DtlsConn *c)
{
    return c->state == DTLS_CONN_STATE_DONE && c->ep3_ready;
}

uint8_t pc_dtls_conn_alert(const DtlsConn *c)
{
    return c->alert;
}

DtlsRecordKeys *pc_dtls_conn_app_write_keys(DtlsConn *c)
{
    return c->ep3_ready ? &c->ep3_srv : NULL;
}

DtlsRecordKeys *pc_dtls_conn_app_read_keys(DtlsConn *c)
{
    return c->ep3_ready ? &c->ep3_cli : NULL;
}

size_t pc_dtls_conn_local_cid(const DtlsConn *c, uint8_t *out)
{
    if (!c->cid_negotiated || c->local_cid_len == 0)
    {
        return 0;
    }
    memcpy(out, c->local_cid, c->local_cid_len);
    return c->local_cid_len;
}

proto_bool pc_dtls_conn_open_app(DtlsConn *c, const uint8_t *rec, size_t rec_len, uint8_t *out, size_t out_cap,
                                 size_t *out_len)
{
    if (!pc_dtls_conn_established(c))
    {
        return PROTO_FALSE;
    }
    DtlsCiphertext info;
    uint64_t next = c->replay_ep3.seeded ? c->replay_ep3.highest + 1 : 0;
    if (!pc_dtls_ciphertext_unprotect(&c->ep3_cli, next, rec, rec_len, out, out_cap, &info,
                                      c->cid_negotiated ? c->local_cid : NULL,
                                      c->cid_negotiated ? c->local_cid_len : 0))
    {
        return PROTO_FALSE;
    }
    if (!pc_dtls_replay_check(&c->replay_ep3, info.seq))
    {
        return PROTO_FALSE; // replay or too old
    }
    pc_dtls_replay_mark(&c->replay_ep3, info.seq);
    if (info.content_type != PC_DTLS_CT_APPLICATION_DATA)
    {
        return PROTO_FALSE;
    }
    *out_len = info.pt_len;
    return PROTO_TRUE;
}

size_t pc_dtls_conn_seal_app(DtlsConn *c, const uint8_t *data, size_t len, uint8_t *out, size_t out_cap)
{
    if (!pc_dtls_conn_established(c))
    {
        return 0;
    }
    // tx_seq_ep3 is shared with the completion ACK, so app records never reuse its sequence number.
    return pc_dtls_ciphertext_protect(&c->ep3_srv, c->tx_seq_ep3++, PC_DTLS_CT_APPLICATION_DATA, data, len, out,
                                      out_cap, c->cid_negotiated ? c->peer_cid : NULL,
                                      c->cid_negotiated ? c->peer_cid_len : 0);
}

#endif // PC_ENABLE_DTLS
