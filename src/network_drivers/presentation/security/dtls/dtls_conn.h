// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_dtls_conn.h
 * @brief DTLS 1.3 server handshake state machine (RFC 9147 §5-6).
 *
 * The transport-neutral core that drives one DTLS 1.3 server handshake: it consumes inbound
 * datagrams and produces the outbound flight, wiring the reused TLS 1.3 message builders and key
 * schedule (pc_tls13_msg, pc_tls13_kdf) through the DTLS record layer (pc_dtls_record) and handshake framing
 * (pc_dtls_handshake). Like pc_coap_server_process it has no sockets - the UDP glue (a later CoAPs
 * front-end) feeds it datagrams and sends whatever it emits.
 *
 * Profile: the single spec-valid suite the whole hand-rolled TLS 1.3 stack uses -
 * TLS_AES_128_GCM_SHA256, X25519 key exchange, an Ed25519 server certificate. The handshake is the
 * one-round-trip full handshake (no PSK, no 0-RTT, no client auth):
 *
 *   epoch 0  ClientHello ->
 *            <- ServerHello                                          (epoch 0, DTLSPlaintext)
 *   epoch 2  <- EncryptedExtensions, Certificate, CertificateVerify, Finished  (DTLSCiphertext)
 *   epoch 2  Finished ->
 *   epoch 3  application data (CoAP) protected with the app-traffic keys
 *
 * Each handshake message fits one record in this profile. When the client does not offer an X25519
 * key_share up front, the server answers the first ClientHello with a HelloRetryRequest carrying a
 * stateless, address-bound cookie and renegotiates the group to X25519 (RFC 9147 §5.1); the second
 * ClientHello must echo the cookie before any asymmetric crypto is spent. Full ACK/timeout
 * retransmission (§5.8, §7) beyond the Finished acknowledgement is a follow-on increment; the framing
 * it needs already exists in pc_dtls_handshake.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DTLS_CONN_H
#define PROTOCORE_DTLS_CONN_H

#include "protocore_config.h"

#if PC_ENABLE_DTLS

#include "crypto/hash/sha256.h"
#include "network_drivers/presentation/security/dtls/dtls_handshake.h"
#include "network_drivers/presentation/security/dtls/dtls_record.h"
#include "network_drivers/tls/tls13_kdf.h"

/** @brief Largest inbound handshake message body reassembled (ClientHello / client Finished). */
#define PC_DTLS_CONN_REASM_CAP 1024

/** @brief Largest single outbound handshake message (Certificate-dominated; one record per message
 *         in this phase, so the certificate plus framing must fit one record). */
#define PC_DTLS_CONN_MSG_CAP 1024

/** @brief Largest serialized peer address the HelloRetryRequest cookie binds (IPv6 16 + port 2). */
#define PC_DTLS_PEER_ADDR_MAX 18

/** @brief Length of the connection id the server chooses for itself (RFC 9146 / RFC 9147 §9): the id the
 *         client must place in every record it sends, so the server can route by it across an address
 *         change. 4 bytes is ample per-connection entropy; must be <= @ref PC_DTLS_CID_MAX. */
#define PC_DTLS_CONN_LOCAL_CID_LEN 4

/** @brief Most handshake messages in one outbound flight (ServerHello + EE + Cert + CV + Finished). */
#define PC_DTLS_FLIGHT_MSGS 6

/** @brief Buffer for the current flight's DTLS handshake fragments, so it can be retransmitted with
 *         fresh record sequence numbers. Sized for the Certificate-dominated server flight. */
#define PC_DTLS_FLIGHT_CAP (PC_DTLS_CONN_MSG_CAP + 512)

/** @brief Retransmission timer (RFC 9147 §5.8.1): initial PTO, its cap, and the retransmission ceiling
 *         after which the handshake is abandoned. Times are in the units of @ref pc_millis (ms). */
#define PC_DTLS_PTO_INITIAL_MS 1000u
#define PC_DTLS_PTO_MAX_MS 60000u
#define PC_DTLS_MAX_RETRANSMITS 8

/** @brief One buffered outbound handshake message: where its DTLS fragment sits in @ref DtlsConn.flight_buf
 *         and which epoch protects it. */
typedef struct
{
    uint16_t off;  ///< byte offset of the fragment in flight_buf
    uint16_t len;  ///< fragment length
    uint8_t epoch; ///< 0 (DTLSPlaintext) or 2 (DTLSCiphertext)
} DtlsFlightMsg;

/** @brief Handshake progress. */
typedef enum PROTO_ENUM_PACKED
{
    DTLS_CONN_STATE_START,         ///< awaiting ClientHello
    DTLS_CONN_STATE_WAIT_FINISHED, ///< server flight sent; awaiting client Finished
    DTLS_CONN_STATE_DONE,          ///< handshake complete; application keys installed
    DTLS_CONN_STATE_FAILED         ///< fatal error (see @ref pc_dtls_conn_alert)
} DtlsConnState;

/**
 * @brief The server's long-lived identity plus this handshake's fresh randomness.
 *
 * @c cert_der / @c ed25519_seed are the server's certificate and matching signing key (long-lived).
 * @c ephemeral_priv and @c server_random must be freshly generated per connection by the caller
 * (from a CSPRNG); they are the X25519 ephemeral private key and the ServerHello random.
 */
typedef struct
{
    const uint8_t *cert_der; ///< Ed25519 leaf certificate, DER
    size_t cert_len;
    const uint8_t *ed25519_seed;   ///< 32-byte Ed25519 signing seed (matches @c cert_der)
    const uint8_t *ephemeral_priv; ///< 32-byte X25519 server ephemeral private key (fresh per handshake)
    const uint8_t *server_random;  ///< 32-byte ServerHello random (fresh per handshake)
    const uint8_t *cookie_key;     ///< 32-byte server-wide secret keying the HelloRetryRequest cookie MAC (§5.1)
} DtlsServerConfig;

/** @brief One DTLS 1.3 server handshake. Owns all per-connection state; no heap. */
typedef struct
{
    DtlsServerConfig cfg;
    DtlsConnState state;
    uint8_t alert; ///< RFC 8446 §6 alert code when @c state is FAILED (0 otherwise)

    pc_sha256_ctx transcript;                       ///< running Transcript-Hash over the TLS handshake messages
    Tls13KeySchedule ks;                            ///< TLS 1.3 key schedule
    DtlsRecordKeys ep2_srv;                         ///< epoch 2 server write keys (handshake traffic)
    DtlsRecordKeys ep2_cli;                         ///< epoch 2 client read keys
    DtlsRecordKeys ep3_srv;                         ///< epoch 3 server write keys (application traffic)
    DtlsRecordKeys ep3_cli;                         ///< epoch 3 client read keys
    proto_bool ep2_ready;                           ///< epoch 2 keys installed
    proto_bool ep3_ready;                           ///< epoch 3 keys installed
    uint8_t hs_finished_hash[PC_SHA256_DIGEST_LEN]; ///< Transcript-Hash(CH..server Finished)

    uint64_t tx_seq_ep0;         ///< next outbound record sequence number, epoch 0
    uint64_t tx_seq_ep2;         ///< next outbound record sequence number, epoch 2
    uint64_t tx_seq_ep3;         ///< next outbound record sequence number, epoch 3
    uint16_t tx_msg_seq;         ///< next outbound handshake message_seq (advances across an optional HRR)
    proto_bool hrr_sent;         ///< a HelloRetryRequest was sent; the next ClientHello is the retry (§5.1)
    uint16_t next_recv_msg_seq;  ///< handshake message_seq expected next from the client
    DtlsReplayWindow replay_ep2; ///< anti-replay window for inbound epoch-2 records
    DtlsReplayWindow replay_ep3; ///< anti-replay window for inbound epoch-3 (application) records
    uint64_t rx_ep2_seq;         ///< sequence number of the last inbound epoch-2 record (the client Finished)
    proto_bool hs_ack_sent;      ///< the client Finished has been acknowledged (RFC 9147 §5.8.3 / §7)
    uint8_t peer_addr[PC_DTLS_PEER_ADDR_MAX]; ///< serialized peer address the HRR cookie is bound to (§5.1)
    uint8_t peer_addr_len;                    ///< bytes of @ref peer_addr in use (0 = no address bound)

    // Connection ids (RFC 9146 / RFC 9147 §9), negotiated by the connection_id extension.
    proto_bool cid_negotiated; ///< the client offered connection_id and we accepted it
    uint8_t
        peer_cid[PC_DTLS_CID_MAX]; ///< the client's CID: placed in every record we send to the client (may be empty)
    uint8_t peer_cid_len;          ///< bytes of @ref peer_cid in use
    uint8_t local_cid[PC_DTLS_CID_MAX]; ///< the CID we chose: the client places it in every record it sends to us
    uint8_t local_cid_len;              ///< bytes of @ref local_cid in use

    // Retransmission (RFC 9147 §5.8): the current outbound flight, buffered as fragments so it can be
    // re-sent with fresh record sequence numbers, plus the exponential-backoff timer state.
    DtlsFlightMsg flight_msgs[PC_DTLS_FLIGHT_MSGS];   ///< the flight's messages (index into @ref flight_buf)
    DtlsRecordNumber flight_rec[PC_DTLS_FLIGHT_MSGS]; ///< record numbers of each message's last transmission (for ACKs)
    uint8_t flight_count;                             ///< messages in the current flight
    uint16_t flight_len;                              ///< bytes used in @ref flight_buf
    proto_bool awaiting_reply; ///< a flight is outstanding and a peer reply is expected (timer runs)
    uint8_t retransmits;       ///< times the current flight has been retransmitted
    uint32_t pto_ms;           ///< current retransmission timeout (doubles each retransmit)
    uint32_t flight_sent_ms;   ///< pc_millis() when the flight was last (re)transmitted

    DtlsHsReasm reasm;                             ///< inbound handshake reassembler
    uint8_t reasm_buf[4 + PC_DTLS_CONN_REASM_CAP]; ///< TLS message = 4-byte header [0..3] + body [4..]
    uint8_t msgbuf[PC_DTLS_CONN_MSG_CAP];          ///< scratch for one outbound TLS message
    uint8_t flight_buf[PC_DTLS_FLIGHT_CAP]; ///< the current flight's DTLS handshake fragments, for retransmission
} DtlsConn;

/**
 * @brief Initialize a connection for a new handshake. @p cfg is copied (its pointers must outlive @p c).
 *
 * @param peer_addr      serialized peer address (e.g. IP || port) the HelloRetryRequest cookie binds,
 *                       so a cookie minted for one peer is worthless to another (RFC 9147 §5.1). May
 *                       be NULL / 0 when no HRR is expected (the one-round-trip happy path).
 * @param peer_addr_len  length of @p peer_addr; clamped to @ref PC_DTLS_PEER_ADDR_MAX.
 */
void pc_dtls_conn_init(DtlsConn *c, const DtlsServerConfig *cfg, const uint8_t *peer_addr, size_t peer_addr_len);

/**
 * @brief Feed one inbound datagram; append any response records to @p out.
 *
 * Demultiplexes the datagram's records (DTLSPlaintext for epoch 0, DTLSCiphertext for epoch 2),
 * unprotects and reassembles the handshake, and drives the state machine, writing the server's
 * response flight to @p out.
 *
 * @return the number of bytes written to @p out (0 if nothing to send), or -1 on a fatal error
 *         (then @c state is FAILED and @ref pc_dtls_conn_alert gives the reason).
 */
int pc_dtls_conn_process(DtlsConn *c, const uint8_t *dgram, size_t len, uint8_t *out, size_t out_cap);

/**
 * @brief Milliseconds (in @ref pc_millis units) until the retransmission timer fires, or -1 if no
 *        timer is running (no flight is outstanding, or the handshake is done or failed).
 *
 * The transport-neutral core keeps no timer of its own; the caller polls this to schedule a wake-up and
 * calls @ref pc_dtls_conn_on_timeout when it expires (RFC 9147 §5.8). 0 means the timer is already due.
 */
int pc_dtls_conn_timeout_ms(const DtlsConn *c);

/**
 * @brief Fire the retransmission timer: re-send the outstanding flight into @p out with fresh record
 *        sequence numbers and double the timeout (RFC 9147 §5.8.1 exponential backoff).
 *
 * Retransmits only if the PTO has actually elapsed, so a spurious/early call is a no-op (returns 0).
 * After @ref PC_DTLS_MAX_RETRANSMITS attempts the handshake is abandoned (state becomes FAILED, returns -1).
 *
 * @return bytes written to @p out (0 if nothing was due), or -1 if the retransmission ceiling was hit.
 */
int pc_dtls_conn_on_timeout(DtlsConn *c, uint8_t *out, size_t out_cap);

/** @brief True once the handshake has completed and the application-traffic keys are installed. */
proto_bool pc_dtls_conn_established(const DtlsConn *c);

/** @brief The alert code (RFC 8446 §6) set when the handshake failed, or 0. */
uint8_t pc_dtls_conn_alert(const DtlsConn *c);

/** @brief Application-epoch (epoch 3) server write keys - protect outbound application records. */
DtlsRecordKeys *pc_dtls_conn_app_write_keys(DtlsConn *c);

/** @brief Application-epoch (epoch 3) client read keys - unprotect inbound application records. */
DtlsRecordKeys *pc_dtls_conn_app_read_keys(DtlsConn *c);

/**
 * @brief The server's connection id (RFC 9146 / RFC 9147 §9) for this connection, if one was negotiated.
 *
 * The client places this id in every record it sends, so a UDP front-end can route inbound records by it
 * and follow the peer across an address change (NAT rebinding). Copies the id to @p out (which must hold
 * at least @ref PC_DTLS_CID_MAX bytes).
 *
 * @return the connection-id length, or 0 if no connection id was negotiated.
 */
size_t pc_dtls_conn_local_cid(const DtlsConn *c, uint8_t *out);

/**
 * @brief Decrypt one inbound epoch-3 application record into @p out (RFC 9147 §4).
 *
 * Only valid once established. Enforces the epoch-3 anti-replay window (§4.5.1) and that the record's
 * true content type is application_data. A replayed, too-old, or non-application record yields false.
 *
 * @return true and sets @p *out_len on success; false if the connection is not established, the record
 *         fails to open, it is a replay, or it is not application data.
 */
proto_bool pc_dtls_conn_open_app(DtlsConn *c, const uint8_t *rec, size_t rec_len, uint8_t *out, size_t out_cap,
                                 size_t *out_len);

/**
 * @brief Seal @p data as one outbound epoch-3 application record (RFC 9147 §4), advancing the shared
 *        epoch-3 send sequence so it never collides with the handshake-completion ACK.
 * @return record bytes written to @p out, or 0 if not established or on overflow.
 */
size_t pc_dtls_conn_seal_app(DtlsConn *c, const uint8_t *data, size_t len, uint8_t *out, size_t out_cap);

#endif // PC_ENABLE_DTLS
#endif // PROTOCORE_DTLS_CONN_H
