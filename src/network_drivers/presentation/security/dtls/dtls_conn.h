// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_dtls_conn.h
 * @brief DTLS 1.3 server handshake state machine (RFC 9147 §5-6).
 *
 * The transport-neutral core that drives one DTLS 1.3 server handshake: it consumes inbound
 * datagrams and produces the outbound flight, wiring the reused TLS 1.3 message builders and key
 * schedule (protocore_tls13_msg, protocore_tls13_kdf) through the DTLS record layer (protocore_dtls_record) and
 * handshake framing (protocore_dtls_handshake). Like Coap.process it has no sockets - the UDP glue (a
 * later CoAPs front-end) feeds it datagrams and sends whatever it emits.
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
 * it needs already exists in protocore_dtls_handshake.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DTLS_CONN_H
#define PROTOCORE_DTLS_CONN_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_DTLS

#include "crypto/hash/sha256.h"
#include "network_drivers/presentation/security/dtls/dtls_handshake.h"
#include "network_drivers/presentation/security/dtls/dtls_record.h"
#include "network_drivers/tls/key_schedule/key_schedule.h"

/** @brief Largest inbound handshake message body reassembled (ClientHello / client Finished). */
#define PROTOCORE_DTLS_CONN_REASM_CAP 1024

/** @brief Largest single outbound handshake message (Certificate-dominated; one record per message
 *         in this phase, so the certificate plus framing must fit one record). */
#define PROTOCORE_DTLS_CONN_MSG_CAP 1024

/** @brief Largest serialized peer address the HelloRetryRequest cookie binds (IPv6 16 + port 2). */
#define PROTOCORE_DTLS_PEER_ADDR_MAX 18

/** @brief Length of the connection id the server chooses for itself (RFC 9146 / RFC 9147 §9): the id the
 *         client must place in every record it sends, so the server can route by it across an address
 *         change. 4 bytes is ample per-connection entropy; must be <= @ref PROTOCORE_DTLS_CID_MAX. */
#define PROTOCORE_DTLS_CONN_LOCAL_CID_LEN 4

/** @brief Most handshake fragments in one outbound flight. A message longer than the connection's
 *         PMTU becomes several, so this is a fragment count, not a message count (§4.3, §5.5). */
#define PROTOCORE_DTLS_FLIGHT_MSGS 16

/** @brief Most a record adds around a handshake fragment: the unified header carrying a connection
 *         id, a 16-bit sequence and a length, plus the AEAD tag (§4). The plaintext form is smaller,
 *         so this bounds both. */
#define PROTOCORE_DTLS_REC_OVERHEAD_MAX (1 + PROTOCORE_DTLS_CID_MAX + 2 + 2 + PROTOCORE_DTLS_TAG_LEN)

/** @brief Buffer for the current flight's DTLS handshake fragments, so it can be retransmitted with
 *         fresh record sequence numbers. Sized for the Certificate-dominated server flight. */
#define PROTOCORE_DTLS_FLIGHT_CAP (PROTOCORE_DTLS_CONN_MSG_CAP + 512)

/** @brief Retransmission timer (RFC 9147 §5.8.1): initial PTO, its cap, and the retransmission ceiling
 *         after which the handshake is abandoned. Times are in the units of @ref protocore_millis (ms). */
#define PROTOCORE_DTLS_PTO_INITIAL_MS 1000u
#define PROTOCORE_DTLS_PTO_MAX_MS 60000u
#define PROTOCORE_DTLS_MAX_RETRANSMITS 8

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
    DTLS_CONN_STATE_FAILED         ///< fatal error (see @ref protocore_dtls_conn_alert)
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
    uint16_t pmtu;                 ///< largest datagram this path takes; 0 uses PROTOCORE_DTLS_PMTU_DEFAULT (§4.3)
} DtlsServerConfig;

/** @brief One DTLS 1.3 server handshake. Owns all per-connection state; no heap. */
typedef struct
{
    DtlsServerConfig cfg;
    DtlsConnState state;
    uint8_t alert; ///< RFC 8446 §6 alert code when @c state is FAILED (0 otherwise)

    protocore_sha256_ctx transcript;             ///< running Transcript-Hash over the TLS handshake messages
    Tls13KeySchedule ks;                         ///< TLS 1.3 key schedule, over @ref ks_store
    uint8_t ks_store[PROTOCORE_TLS13_KS_BORROW]; ///< the schedule's terms and its HKDF's bytes
    // The transcript hash and the one-off hashes taken beside it work out of these. Live and die with
    // this connection, so no hash on the handshake path touches a pool.
    uint8_t hash_work[PROTOCORE_SHA256_BORROW];
    uint8_t hash_work2[PROTOCORE_SHA256_BORROW];
    uint8_t sign_work[PROTOCORE_SHA512_BORROW];            ///< the CertificateVerify signature's SHA-512
    uint8_t mac_work[PROTOCORE_HMAC_SHA256_BORROW];        ///< the stateless HelloRetryRequest cookie's MAC
    DtlsRecordKeys ep2_srv;                                ///< epoch 2 server write keys (handshake traffic)
    DtlsRecordKeys ep2_cli;                                ///< epoch 2 client read keys
    DtlsRecordKeys ep3_srv;                                ///< epoch 3 server write keys (application traffic)
    DtlsRecordKeys ep3_cli;                                ///< epoch 3 client read keys
    proto_bool ep2_ready;                                  ///< epoch 2 keys installed
    proto_bool ep3_ready;                                  ///< epoch 3 keys installed
    uint8_t hs_finished_hash[PROTOCORE_SHA256_DIGEST_LEN]; ///< Transcript-Hash(CH..server Finished)

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
    uint8_t peer_addr[PROTOCORE_DTLS_PEER_ADDR_MAX]; ///< serialized peer address the HRR cookie is bound to (§5.1)
    uint8_t peer_addr_len;                           ///< bytes of @ref peer_addr in use (0 = no address bound)

    // Connection ids (RFC 9146 / RFC 9147 §9), negotiated by the connection_id extension.
    proto_bool cid_negotiated;                ///< the client offered connection_id and we accepted it
    uint8_t peer_cid[PROTOCORE_DTLS_CID_MAX]; ///< the client's CID: placed in every record we send to the client (may
                                              ///< be empty)
    uint8_t peer_cid_len;                     ///< bytes of @ref peer_cid in use
    uint8_t
        local_cid[PROTOCORE_DTLS_CID_MAX]; ///< the CID we chose: the client places it in every record it sends to us
    uint8_t local_cid_len;                 ///< bytes of @ref local_cid in use

    // Retransmission (RFC 9147 §5.8): the current outbound flight, buffered as fragments so it can be
    // re-sent with fresh record sequence numbers, plus the exponential-backoff timer state.
    DtlsFlightMsg flight_msgs[PROTOCORE_DTLS_FLIGHT_MSGS]; ///< the flight's messages (index into @ref flight_buf)
    DtlsRecordNumber
        flight_rec[PROTOCORE_DTLS_FLIGHT_MSGS]; ///< record numbers of each message's last transmission (for ACKs)
    uint8_t flight_count;                       ///< messages in the current flight
    uint16_t flight_len;                        ///< bytes used in @ref flight_buf
    proto_bool awaiting_reply;                  ///< a flight is outstanding and a peer reply is expected (timer runs)
    uint8_t retransmits;                        ///< times the current flight has been retransmitted
    uint16_t pmtu;                              ///< largest datagram this connection puts on the wire (sec 4.3)
    uint32_t pto_ms;                            ///< current retransmission timeout (doubles each retransmit)
    uint32_t flight_sent_ms;                    ///< protocore_millis() when the flight was last (re)transmitted

    DtlsHsReasm reasm;                                    ///< inbound handshake reassembler
    uint8_t reasm_buf[4 + PROTOCORE_DTLS_CONN_REASM_CAP]; ///< TLS message = 4-byte header [0..3] + body [4..]
    uint8_t msgbuf[PROTOCORE_DTLS_CONN_MSG_CAP];          ///< scratch for one outbound TLS message
    uint8_t
        flight_buf[PROTOCORE_DTLS_FLIGHT_CAP]; ///< the current flight's DTLS handshake fragments, for retransmission
} DtlsConn;

/**
 * @brief The server side of a DTLS connection. DtlsConn is the caller's struct; this drives it.
 *
 * @var DtlsConnNs::init           bind a connection to its configuration and peer address
 * @var DtlsConnNs::process        turn one received datagram, writing whatever it owes back into @p out
 * @var DtlsConnNs::timeout_ms     milliseconds until the flight needs retransmitting
 * @var DtlsConnNs::on_timeout     retransmit the current flight once that timeout has passed
 * @var DtlsConnNs::established    whether the handshake has completed
 * @var DtlsConnNs::alert          the alert that ended the connection, or 0
 * @var DtlsConnNs::app_write_keys the application-epoch write keys
 * @var DtlsConnNs::app_read_keys  the application-epoch read keys
 * @var DtlsConnNs::local_cid      this side's connection id, and its length
 * @var DtlsConnNs::open_app       open one application-data record once the handshake is done
 * @var DtlsConnNs::seal_app       seal application data into one record
 */
typedef struct
{
    void (*init)(DtlsConn *c, const DtlsServerConfig *cfg, const uint8_t *peer_addr, size_t peer_addr_len);
    int (*process)(DtlsConn *c, const uint8_t *dgram, size_t len, uint8_t *out, size_t out_cap);
    int (*timeout_ms)(const DtlsConn *c);
    int (*on_timeout)(DtlsConn *c, uint8_t *out, size_t out_cap);
    proto_bool (*established)(const DtlsConn *c);
    uint8_t (*alert)(const DtlsConn *c);
    DtlsRecordKeys *(*app_write_keys)(DtlsConn *c);
    DtlsRecordKeys *(*app_read_keys)(DtlsConn *c);
    size_t (*local_cid)(const DtlsConn *c, uint8_t *out);
    proto_bool (*open_app)(DtlsConn *c, const uint8_t *rec, size_t rec_len, uint8_t *out, size_t out_cap,
                           size_t *out_len);
    size_t (*seal_app)(DtlsConn *c, const uint8_t *data, size_t len, uint8_t *out, size_t out_cap);
} DtlsConnNs;

/** @brief The one symbol this module exports. */
extern const DtlsConnNs DtlsServer;

#endif // PROTOCORE_ENABLE_DTLS
#endif // PROTOCORE_DTLS_CONN_H
