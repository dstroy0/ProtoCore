// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file transport.h
 * @brief RFC 4253 transport layer: identification exchange, algorithm negotiation, key exchange.
 */

#ifndef PROTOCORE_TRANSPORT_TRANSPORT_H
#define PROTOCORE_TRANSPORT_TRANSPORT_H

#include "crypto/aead/aesgcm.h"
#include "crypto/aead/chachapoly.h"
#include "crypto/asymmetric/bignum.h"
#include "crypto/cipher/aes256ctr.h"
#include "crypto/hash/sha256.h" // PROTOCORE_SHA256_DIGEST_LEN - the exchange hash and session id
#include "mmgr/secure.h"        // protocore_secure_wipe (the canonical secure wipe)
#include "network_drivers/presentation/ssh/common.h"
#include "network_drivers/presentation/ssh/transport/phase_machine.h" // SshPhase: the session's phase

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

/**
 * @brief One direction's codec state, handed to the packet layer per call.
 *
 * RFC 4253 sec 6 sits under sec 7, so the codec is told which keys to use rather than reading the
 * session that negotiated them. Each direction switches on its own SSH_MSG_NEWKEYS (sec 7.3).
 */
typedef struct
{
    proto_bool enc; ///< that direction's cipher/MAC is active
    uint8_t epoch;  ///< key epoch it reads out of ssh_keys[slot][]
} SshDir;

/** @brief Negotiated key-exchange method. */
typedef enum PROTO_ENUM_PACKED
{
    SSH_KEX_DH_GROUP14 = 0,      ///< diffie-hellman-group14-sha256 (RFC 8268)
    SSH_KEX_CURVE25519 = 1,      ///< curve25519-sha256 (RFC 8731)
    SSH_KEX_MLKEM768_X25519 = 2, ///< mlkem768x25519-sha256
    SSH_KEX_ECDH_NISTP256 = 3,   ///< ecdh-sha2-nistp256 (RFC 5656 sec 4)
    SSH_KEX_SNTRUP761_X25519 = 4 ///< sntrup761x25519-sha512@openssh.com
} SshKexAlg;

/** @brief Negotiated host-key / signature algorithm. */
typedef enum PROTO_ENUM_PACKED
{
    SSH_HOSTKEY_RSA_SHA256 = 0,    ///< rsa-sha2-256 (RFC 8332)
    SSH_HOSTKEY_ED25519 = 1,       ///< ssh-ed25519 (RFC 8709)
    SSH_HOSTKEY_RSA_SHA512 = 2,    ///< rsa-sha2-512 (RFC 8332)
    SSH_HOSTKEY_ECDSA_NISTP256 = 3 ///< ecdsa-sha2-nistp256 (RFC 5656)
} SshHostkeyAlg;

/**
 * @brief SSH transport/session state for one connection (BSS pool).
 *
 * Holds the handshake phase plus the values that must persist across messages to compute the
 * exchange hash H: the identification strings (V_C, V_S) and the two KEXINIT payloads (I_C, I_S).
 * The exchange hash from the first KEX is retained as the session id.
 */
typedef struct
{
    SshPhase phase; ///< Current handshake phase.

    SshKexAlg kex_alg;         ///< negotiated in KEXINIT.
    SshHostkeyAlg hostkey_alg; ///< negotiated in KEXINIT.
    // RFC 4253 sec 7.1 negotiates each direction's cipher and MAC from its own name-list, so the two
    // directions may differ and are kept apart from KEXINIT through to key install.
    uint8_t cipher_alg_c2s; ///< SSH_CIPHER_* client-to-server.
    uint8_t cipher_alg_s2c; ///< SSH_CIPHER_* server-to-client.
    uint8_t mac_alg_c2s;    ///< SSH_MAC_* client-to-server (aes cipher only; 0 = hmac-sha2-256).
    uint8_t mac_alg_s2c;    ///< SSH_MAC_* server-to-client (aes cipher only; 0 = hmac-sha2-256).

    // Every buffer below is the constants region of the connection's span, at its own named offset.
    // Null until the connection claims the slot and splits that borrow.
    uint8_t *ecdh_sk; ///< 32B: X25519 scalar / P-256 d, ephemeral private. Wiped by ssh_dh_wipe().
    uint8_t *ecdh_pk; ///< 32B: X25519 ephemeral public (curve25519 KEX only).

    char *v_c;        ///< SSH_VERSION_MAX: client identification string (no CR LF).
    uint16_t v_c_len; ///< Length of v_c.
    char *v_s;        ///< SSH_VERSION_MAX: server identification string (no CR LF).
    uint16_t v_s_len; ///< Length of v_s.

    uint8_t *ident_buf; ///< SSH_VERSION_MAX: accumulator for the inbound identification string.
    uint16_t ident_len; ///< Bytes buffered in ident_buf.

    uint8_t *i_c;     ///< PROTOCORE_SSH_I_C_MAX: client KEXINIT payload (for H).
    uint16_t i_c_len; ///< Length of i_c.
    uint8_t *i_s;     ///< PROTOCORE_SSH_I_S_MAX: server KEXINIT payload (for H).
    uint16_t i_s_len; ///< Length of i_s.

    uint8_t *cpub;     ///< PROTOCORE_SSH_CPUB_MAX: exchange value the client sent - e, Q_C or C_INIT (for H).
    uint16_t cpub_len; ///< Length of cpub.

    uint8_t *session_id;        ///< SSH_KEXHASH_MAX_LEN: H from the first KEX (RFC 4253 sec 7.2).
    uint8_t session_id_len;     ///< Session id length (the first KEX's exchange-hash length).
    proto_bool have_session_id; ///< True once the first KEX completes.

    // RFC 4253 sec 6 is a layer under sec 7, so the codec is handed one of these per call rather
    // than reading them. Each switches on its own SSH_MSG_NEWKEYS (sec 7.3).
    SshDir out; ///< Our outbound direction: encrypted once we sent NEWKEYS, and the epoch it reads.
    SshDir in;  ///< Our inbound direction: encrypted once the peer's arrives.

    proto_bool kex_active; ///< An exchange is running, from KEXINIT to NEWKEYS (sec 9).
    // sec 7.1 restricts what may be sent from the moment THIS end sends its KEXINIT until it sends
    // its NEWKEYS, which is a narrower window than kex_active: an exchange is already running while
    // this end is still waiting to send its own KEXINIT, and that first one must not be refused.
    proto_bool kexinit_sent; ///< This end has sent its KEXINIT and not yet its NEWKEYS (sec 7.1).
    // sec 9: "the state of the higher-level protocols is not affected by the key exchange", so a
    // re-exchange has to put the connection back where it interrupted it. The phase alone cannot say
    // where that was - every exchange ends in SSH_PHASE_NEWKEYS however it started.
    SshPhase phase_before_kex;       ///< What to resume when this exchange completes (sec 9).
    proto_bool drop_guessed_kex_pkt; ///< The peer guessed a KEX that lost negotiation (sec 7.1).
    proto_bool ext_info_enabled;     ///< Peer offered its role's RFC 8308 sec 2.2 indicator.
    proto_bool ext_info_sent;        ///< EXT_INFO already went out; RFC 8308 sec 2.4 allows it once.
    proto_bool authed;               ///< True after successful user authentication.
    uint32_t last_kex_ms;            ///< protocore_millis() when the last KEX completed.
} SshSession;

/** @brief Static pool of SSH session state (BSS), one per SSH slot. */
extern SshSession ssh_sess[MAX_SSH_CONNS];

/**
 * @brief Steer KEX and host-key negotiation toward RSA with DH-group14, or toward curve25519 with
 *        ed25519.
 *
 * Both suites are advertised whatever this is set to; it orders them, so a peer that supports only
 * one still connects. Runtime-selectable, before the handshake.
 */
void ssh_kex_set_prefer_rsa(proto_bool prefer);

/** @brief Current negotiation preference (true = prefer RSA / DH). */
proto_bool ssh_kex_prefer_rsa(void);

/**
 * @brief The session identifier for slot @p i, or null before the first key exchange completes.
 *
 * RFC 4253 sec 10: "When the service starts, it may have access to the session identifier generated
 * during the key exchange." It is the first exchange's hash H (sec 7.2) and does not change on a
 * re-exchange, so a service that binds to it stays bound.
 *
 * @param i    SSH slot index.
 * @param len  Set to the identifier's length: 32 for the SHA-256 methods, 64 for the SHA-512 one.
 */
const uint8_t *ssh_session_id(uint8_t i, size_t *len);

/**
 * @brief Latch the first exchange's hash as slot @p i's session identifier (RFC 4253 sec 7.2).
 *
 * "The exchange hash H from the first key exchange is additionally used as the session identifier."
 * Both roles compute H in their own half of sec 8 and hand it here; the second and later exchanges
 * are ignored, so the identifier never moves under a service that bound to it.
 */
void ssh_session_id_latch(uint8_t i, const uint8_t *h, size_t h_len);

/** @brief RFC 4253 sec 6 binary packet: the bytes a receive consumes, and the body it carries. */
typedef struct
{
    const uint8_t *data;    ///< bytes a receive consumes
    const uint8_t *payload; ///< a KEXINIT or KEXDH payload
    size_t len;             ///< how many
    size_t consumed;        ///< bytes a receive took from data
} SshPacketArgs;

/** @brief Where a build or a send writes, and what it wrote. */
typedef struct
{
    uint8_t *out;   ///< where a build or a send writes
    size_t out_len; ///< what it wrote
    size_t cap;     ///< how much room it has
} SshTransportOut;

/** @brief RFC 4253 sec 8: every term the exchange hash H is taken over, and where H lands. */
typedef struct
{
    proto_bool pub_is_string;          ///< the peer public value is a string, not an mpint
    const uint8_t *cpub;               ///< the client public value
    size_t cpub_len;                   ///< its length
    const uint8_t *spub;               ///< the server public value
    size_t spub_len;                   ///< its length
    const uint8_t *k_be;               ///< the shared secret, big-endian
    size_t k_len;                      ///< its length
    const uint8_t *ks;                 ///< the host key blob
    size_t ks_len;                     ///< its length
    proto_bool k_is_string;            ///< that secret is a string, not an mpint
    proto_bool is512;                  ///< the method hashes with SHA-512
    uint8_t hash[SSH_KEXHASH_MAX_LEN]; ///< where the exchange hash H lands
    size_t hash_len;                   ///< its length: 32 for the SHA-256 methods, 64 for the SHA-512 one
} SshKexHashArgs;

/** @brief RFC 4253 sec 9 key re-exchange: what has passed since the last one, against its budget. */
typedef struct
{
    uint32_t seq_send;          ///< packets sent since the last exchange
    uint32_t seq_recv;          ///< packets received since it
    uint32_t elapsed_ms;        ///< time since it
    uint32_t pkt_threshold;     ///< the volume budget
    uint32_t time_threshold_ms; ///< the time budget
} SshRekeyArgs;

/**
 * @brief The RFC 4253 transport state machine, as the operations both roles consume.
 *
 * One member per rule the RFC states, so a section number resolves to exactly one implementation.
 * Role selects which half of a mirrored pair a caller sends - it does not select a second machine.
 *
 * @var SshTransportNs::recv_ident       sec 4.2 receive the peer identification string
 * @var SshTransportNs::send_ident       sec 4.2 send ours
 * @var SshTransportNs::kexinit_build    sec 7.1 build our KEXINIT, keeping a copy for H
 * @var SshTransportNs::kexinit_parse    sec 7.1 parse the peer's and negotiate every list
 * @var SshTransportNs::kex_generate     sec 8 generate this exchange's ephemeral
 * @var SshTransportNs::exchange_hash    sec 8 the exchange hash H over the negotiated method's terms
 * @var SshTransportNs::kexdh_reply      sec 8 the KEXDH half this role sends
 * @var SshTransportNs::newkeys_sent     sec 7.3 our outbound switches when we send NEWKEYS
 * @var SshTransportNs::newkeys_complete sec 7.3 our inbound switches when the peer's arrives
 * @var SshTransportNs::rekey_due        sec 9 the volume and time budget since the last exchange
 * @var SshTransportNs::begin_rekey      sec 9 start a re-exchange, when not already doing one
 *
 * A caller sets the members a call takes, invokes it through ::SshTransport, and reads the outcome
 * off the same handle.
 *
 * @var SshTransportNs::slot             the SSH slot a call acts on
 * @var SshTransportNs::pkt              sec 6 the bytes one message occupies
 * @var SshTransportNs::out_args         where a build or a send writes
 * @var SshTransportNs::kexhash          sec 8 the terms the exchange hash H is taken over
 * @var SshTransportNs::rekey            sec 9 the volume and time budget since the last exchange
 * @var SshTransportNs::ok               a call's true/false outcome
 * @var SshTransportNs::i32              a call's signed outcome
 * @var SshTransportNs::internal         the machine's state and the calls that reach it
 */
struct SshTransportInternal;

typedef struct
{
    uint8_t slot; ///< the SSH slot a call acts on

    SshPacketArgs pkt;        ///< sec 6 the bytes one message occupies
    SshTransportOut out_args; ///< where a build or a send writes
    SshKexHashArgs kexhash;   ///< sec 8 the terms the exchange hash H is taken over
    SshRekeyArgs rekey;       ///< sec 9 the volume and time budget since the last exchange

    proto_bool ok;
    int i32;

    void (*recv_ident)(struct SshTransportInternal *ctx);
    void (*send_ident)(struct SshTransportInternal *ctx);
    void (*kexinit_build)(struct SshTransportInternal *ctx);
    void (*kexinit_parse)(struct SshTransportInternal *ctx);
    void (*kex_generate)(struct SshTransportInternal *ctx);
    void (*exchange_hash)(struct SshTransportInternal *ctx);
    void (*kexdh_reply)(struct SshTransportInternal *ctx);
    void (*newkeys_sent)(struct SshTransportInternal *ctx);
    void (*newkeys_complete)(struct SshTransportInternal *ctx);
    void (*rekey_due)(struct SshTransportInternal *ctx);
    void (*begin_rekey)(struct SshTransportInternal *ctx);

    struct SshTransportInternal *internal;
} SshTransportNs;

/** @brief The one instance, defined in transport.c. */
extern SshTransportNs SshTransport;

/** @brief Reader shorthand: SSH_TRANSPORT->kexinit_parse(...). */
#define SSH_TRANSPORT (&SshTransport)

#if PROTOCORE_SSH_KEX_BENCH
// Wall-clock KEX bench (perf / FEATURE_PERFORMANCE): one owned context holding the two device-side compute
// spans of a key exchange, in microseconds. ssh_kex_generate records the ephemeral-keygen span (one X25519
// base multiply for a curve25519 KEX) into last_kexgen_us; ssh_kexdh_handle records the reply span
// (shared-secret X25519 + host-key sign + exchange hash + KDF + reply assembly) into last_kexreply_us and
// bumps kex_count. The rig firmware watches kex_count and prints both over its own serial - src writes and
// output. Compiled out entirely unless PROTOCORE_SSH_KEX_BENCH is defined (a rig-only measurement build).
typedef struct
{
    volatile long long last_kexgen_us;   ///< ssh_kex_generate: ephemeral X25519 base-multiply span.
    volatile long long last_kexreply_us; ///< ssh_kexdh_handle: reply span (shared secret + sign + hash + KDF).
    volatile unsigned kex_count;         ///< bumped after each completed KEX; the rig prints on change.
} SshKexBenchCtx;
extern SshKexBenchCtx protocore_ssh_kex_bench;
#endif

/** @brief Max bytes ssh_kdf_derive() can produce (4 SHA-256 blocks). */
#define SSH_KDF_MAX (4 * PROTOCORE_SHA256_DIGEST_LEN)

/**
 * @brief One key exchange's derivation inputs, passed by reference.
 *
 * Every value here is fixed for the whole of a KEX and read by each of the six derivations, so it
 * travels as one pointer: an argument list this wide spills past the register window and pays for it
 * on every call, at the deepest call depth in the library.
 */
typedef struct
{
    uint8_t *work;             ///< PROTOCORE_SSH_KDF_BORROW bytes: the hash context, then the K1 || K2 chain.
    const uint8_t *K_be;       ///< Shared secret K, big-endian, 256 bytes.
    const uint8_t *H;          ///< Current exchange hash.
    const uint8_t *session_id; ///< H of the first KEX; equals H until the first re-key.
    size_t h_len;              ///< Length of H.
    size_t sid_len;            ///< Length of session_id.
    proto_bool k_is_string;    ///< Encode K as a plain SSH string (hybrid KEX), not an mpint.
    proto_bool is512;          ///< Hash with SHA-512 instead of SHA-256.
} SshKdfInputs;

// ---------------------------------------------------------------------------
// Packet state per connection
// ---------------------------------------------------------------------------

/**
 * @brief Per-connection SSH binary packet state.
 *
 * Allocated in ssh_pool[] (BSS); one entry per SSH connection slot.
 * Key material is in ssh_keys[] - a separate BSS symbol to prevent linear
 * overflow from packet buffers into key material.
 */
typedef struct
{
    // RFC 4253 sec 6.4: incremented per packet and never reset, not even across a re-exchange, so
    // these stay with the codec rather than moving to the session with the protocol flags.
    uint32_t seq_no_send; ///< Outgoing sequence number.
    uint32_t seq_no_recv; ///< Incoming sequence number.

    // SSH keys are named by direction (client->server "c2s", server->client "s2c"), fixed by RFC 4253
    // §7.2 regardless of role. A server sends s2c / receives c2s; a client is the mirror. This flag
    // selects the direction at each cipher/MAC site so one packet implementation serves both roles.
    // Default false = server (so existing server code is unchanged); ssh_pkt_set_client() flips it.
    proto_bool is_client;

    // Receive reassembly: we may receive partial packets across TCP segments. rx_buf is the whole of
    // the data packet region of the connection's span, at its own named offset.
    uint8_t *rx_buf; ///< SSH_RX_ASM_CAP bytes at SSH_OFF_RX_ASM. Null until claimed.
    size_t rx_len;   ///< Bytes currently in rx_buf.

    // One finished packet waiting for a worker to put it on the wire. The codec frames into
    // tx_wire and raises tx_ready; the worker sends from tx_off and lowers the flag when the last
    // byte is out. The codec never reaches the wire itself.
    //
    // tx_wire is taken once from the secure pool's persistent end - the end no mark walks - and
    // reused for every packet on this slot. Releasing per packet would wipe the whole wire buffer
    // each time, and the mark end cannot hold it anyway: mark/release is a bump discipline, so one
    // slot's release would reclaim another slot's borrow.
    uint8_t *tx_wire;    ///< The wire buffer for this slot. Null until the first packet.
    size_t tx_len;       ///< Bytes of the framed packet.
    size_t tx_off;       ///< Bytes already put on the wire.
    proto_bool tx_ready; ///< A packet is framed and waiting for a worker.

    // The packet MAC and the key exchange work out of these, and they come from the same one borrow as
    // tx_wire. Held for the slot's life so neither costs a borrow or a wipe on the packet path.
    uint8_t *mac_work; ///< PROTOCORE_HMAC_SHA256_BORROW bytes. Null until the first packet.
    // SSH_CIPHER_WORK_LEN bytes for the negotiated record cipher, its own region so a MAC and a
    // cipher on the same packet never reach each other's bytes.
    uint8_t *cipher_work;
    // PROTOCORE_CRYPTO_BORROW_MAX bytes for the handshake's crypto: the exchange hash, the RFC 4253 sec 7.2 KDF,
    // the host-key signature and the userauth one. Those run in sequence, never at once, so they share.
    uint8_t *crypto_work;
} SshPacketState;

/** @brief Static packet state pool (BSS). One entry per SSH slot. */
extern SshPacketState ssh_pkt[MAX_SSH_CONNS];

/**
 * @brief Initialize the packet state for SSH connection slot @p i.
 *
 * Zeroes the sequence numbers and the transmit state, keeping the slot's storage pointers. The
 * protocol flags are the session's (ssh_transport.h) and are reset by ssh_transport_init().
 *
 * @param i  SSH slot index.
 */
void ssh_pkt_init(uint8_t i);

/**
 * @brief Bind the session state for SSH connection slot @p i to the slot's storage.
 *
 * Zeroes the session, then points each of its buffers and both key epochs at their offsets within
 * the slot, leaves the phase at SSH_PHASE_IDENT with the first key exchange already running, and
 * marks both epochs inactive.
 *
 * @param i  SSH slot index.
 */
void ssh_transport_init(uint8_t i);

/**
 * @brief Take the slot's one persistent borrow if it has none yet, and split it.
 *
 * Sets @ref SshPacketState::tx_wire, @ref SshPacketState::mac_work and @ref SshPacketState::crypto_work.
 * Idempotent, and false only when the pool cannot cover the slot.
 */
proto_bool ssh_pkt_slot_storage(SshPacketState *s);

/**
 * @brief Mark slot @p i as the SSH client role (call once, right after ssh_pkt_init).
 *
 * Flips the send/receive key direction: the client encrypts with the c2s key set and decrypts with
 * the s2c one, the mirror of the server. Without this a slot defaults to the server role.
 */
void ssh_pkt_set_client(uint8_t i);

/**
 * @brief Frame the @p payload_len bytes already written at @p wire + @ref SSH_WIRE_PAYLOAD_OFF.
 *
 * The in-place form of ssh_pkt_send(): same framing, padding, encryption and MAC, over a payload the
 * caller has already placed. @p wire holds the finished packet on return.
 *
 * @return 0 on success, -1 on overflow or sequence-number exhaustion.
 */
int ssh_pkt_send_at(uint8_t i, uint8_t *wire, size_t payload_len, size_t *out_len, size_t wire_cap, const SshDir *dir);

/**
 * @brief Frame @p payload for slot @p i into the secure pool and raise the flag a worker drains.
 *
 * Borrows the wire buffer itself, for a caller that already holds its message somewhere else -
 * handshake and control traffic, which is small and infrequent. On return the packet is framed and
 * @ref SshPacketState::tx_ready is set; a worker puts the bytes on the wire and releases the
 * borrow. This layer never reaches the wire.
 *
 * @return 0 on success, -1 if a packet is already pending, the pool is exhausted, or framing fails.
 */
int ssh_pkt_emit(uint8_t i, const uint8_t *payload, size_t len, const SshDir *dir);

/**
 * @brief Build and send one SSH binary packet.
 *
 * Frames @p payload according to RFC 4253 §6:
 *   - Adds random padding to align to 16-byte boundary.
 *   - If encrypted: encrypts with AES-256-CTR, appends HMAC-SHA2-256 MAC.
 *   - Increments seq_no_send; closes connection if threshold reached.
 *
 * The serialized packet is written into @p out.  *@p out_len is set to the
 * number of bytes written.  @p out must be at least
 * (4 + 1 + payload_len + 16 + 32) bytes.
 *
 * @param i           SSH slot index.
 * @param payload     Plaintext SSH message payload.
 * @param payload_len Length of @p payload.
 * @param out         Output buffer for the wire packet.
 * @param out_len     Set to the number of bytes written into @p out.
 * @param out_cap     Capacity of @p out.
 * @return 0 on success, -1 on overflow or sequence-number exhaustion.
 */
int ssh_pkt_send(uint8_t i, const uint8_t *payload, size_t payload_len, uint8_t *out, size_t *out_len, size_t out_cap,
                 const SshDir *dir);

/**
 * @brief Callback invoked once per complete, verified inbound SSH message.
 *
 * @param slot         SSH slot index.
 * @param msg_type     First payload byte (SSH message number).
 * @param payload      Decrypted message payload (includes @p msg_type at [0]).
 * @param payload_len  Length of @p payload.
 */
typedef void (*ssh_msg_handler_t)(uint8_t slot, uint8_t msg_type, const uint8_t *payload, size_t payload_len);

/**
 * @brief Receive and process one or more SSH binary packets from @p data.
 *
 * Appends @p len bytes from @p data to the receive buffer for slot @p i,
 * then extracts complete packets.  For each complete packet:
 *   - If encrypted: decrypts with AES-256-CTR, verifies HMAC-SHA2-256.
 *     Closes connection (returns -1) on MAC failure without processing payload.
 *   - Increments seq_no_recv; closes connection if threshold reached.
 *   - Calls @p handler(slot, msg_type, payload, payload_len) for the payload.
 *
 * @param i        SSH slot index.
 * @param data     Received bytes (from TCP).
 * @param len      Number of bytes in @p data.
 * @param handler  Callback invoked once per complete, verified packet.
 * @return 0 on success, -1 on MAC failure or sequence-number exhaustion
 *         (caller must close the TCP connection).
 */
int ssh_pkt_recv(uint8_t i, const uint8_t *data, size_t len, ssh_msg_handler_t handler, const SshDir *dir);

/**
 * @brief Send SSH_MSG_DISCONNECT with reason @p reason_code.
 *
 * Sends the packet, then zeroes the packet state and key material for slot @p i.
 *
 * @param i            SSH slot index.
 * @param reason_code  One of SSH_DISCONNECT_* constants.
 * @param out          Output buffer for the wire packet.
 * @param out_len      Set to the number of bytes written.
 * @param out_cap      Capacity of @p out.
 * @return 0 on success, -1 on error.
 */
int ssh_pkt_disconnect(uint8_t i, uint32_t reason_code, uint8_t *out, size_t *out_len, size_t out_cap,
                       const SshDir *dir);

// ---------------------------------------------------------------------------
// RFC 4253 sec 11.4 - Reserved Messages
// ---------------------------------------------------------------------------

/** @brief Bytes in an SSH_MSG_UNIMPLEMENTED payload: the message number and one uint32. */
#define SSH_UNIMPLEMENTED_LEN 5u

/**
 * @brief Build the SSH_MSG_UNIMPLEMENTED payload answering the packet slot @p i last received.
 *
 * "An implementation MUST respond to all unrecognized messages with an SSH_MSG_UNIMPLEMENTED
 * message in the order in which the messages were received." The sequence number it carries is
 * the receive counter's, which this layer owns; the caller frames and sends the payload.
 *
 * @param i        SSH slot index.
 * @param out      Output buffer for the payload.
 * @param out_len  Set to SSH_UNIMPLEMENTED_LEN.
 * @param out_cap  Capacity of @p out.
 * @return 0 on success, -1 on a bad slot or too small a buffer.
 */
int ssh_pkt_unimplemented(uint8_t i, uint8_t *out, size_t *out_len, size_t out_cap);

// ---------------------------------------------------------------------------
// RFC 4253 sec 7.2 - key material
// ---------------------------------------------------------------------------

// These two are anonymous enums: the constants are what the code names, and the negotiated value
// travels as a uint8_t through ssh_kex_install_keys, ssh_mac_is_etm, ssh_mac_len, and the
// per-direction cipher_mode_* / mac_mode_* session fields.

/** @brief Negotiated bulk cipher for a session. */
enum
{
    SSH_CIPHER_AES256CTR = 0,        ///< aes256-ctr + a separate HMAC (the fallback)
    SSH_CIPHER_CHACHA20POLY1305 = 1, ///< chacha20-poly1305@openssh.com (AEAD; no separate MAC)
    SSH_CIPHER_AES256GCM = 2,        ///< aes256-gcm@openssh.com (AEAD, RFC 5647; no separate MAC)
};

/** @brief Negotiated MAC for the aes256-ctr cipher (unused with the chacha AEAD). */
enum
{
    SSH_MAC_HMAC_SHA256 = 0,     ///< hmac-sha2-256 (encrypt-and-MAC, RFC 4253)
    SSH_MAC_HMAC_SHA512 = 1,     ///< hmac-sha2-512 (encrypt-and-MAC)
    SSH_MAC_HMAC_SHA256_ETM = 2, ///< hmac-sha2-256-etm@openssh.com (encrypt-then-MAC)
    SSH_MAC_HMAC_SHA512_ETM = 3, ///< hmac-sha2-512-etm@openssh.com (encrypt-then-MAC)
};

/** @brief True if @p mac_mode is an encrypt-then-MAC variant (length in the clear, MAC over ciphertext). */
static inline proto_bool ssh_mac_is_etm(uint8_t mac_mode)
{
    return mac_mode == SSH_MAC_HMAC_SHA256_ETM || mac_mode == SSH_MAC_HMAC_SHA512_ETM;
}
/** @brief MAC tag / key length in bytes for @p mac_mode (32 for SHA-256, 64 for SHA-512). */
static inline uint8_t ssh_mac_len(uint8_t mac_mode)
{
    return (mac_mode == SSH_MAC_HMAC_SHA512 || mac_mode == SSH_MAC_HMAC_SHA512_ETM) ? 64 : 32;
}

// Secure wipe: the canonical protocore_secure_wipe() lives in mmgr/secure.h (included above). Use it for
// any buffer that held key material - a volatile store the compiler may not elide, unlike a dead memset.

// ---------------------------------------------------------------------------
// Session key material  (one entry per SSH connection)
// ---------------------------------------------------------------------------

/**
 * @brief AES-256-CTR + HMAC-SHA2-256 session keys for one SSH connection.
 *
 * This struct occupies a separate BSS symbol (ssh_keys[]) from the packet
 * receive buffer (ssh_pool[].pkt_buf).  See the security model at the top
 * of this file for why that separation matters.
 *
 * Key derivation follows RFC 4253 §7.2.  After the DH exchange hash H is
 * known and K is available, six values are derived:
 *
 *   IV_c2s  = SHA256(K || H || "A" || session_id)   [16 bytes]
 *   IV_s2c  = SHA256(K || H || "B" || session_id)   [16 bytes]
 *   key_c2s = SHA256(K || H || "C" || session_id)   [32 bytes]
 *   key_s2c = SHA256(K || H || "D" || session_id)   [32 bytes]
 *   mac_c2s = SHA256(K || H || "E" || session_id)   [32 bytes]
 *   mac_s2c = SHA256(K || H || "F" || session_id)   [32 bytes]
 *
 * aes_key/aes_ctr C→S are the client-to-server key + counter (server decrypts inbound); S→C are the
 * reverse (server encrypts outbound).
 */
typedef struct
{
    // aes256-ctr stores the raw 32-byte key and the 16-byte IV per direction and rebuilds its key schedule
    // per packet in the shared crypto scratch, so no expanded CTR key lingers here. The IV is the running
    // 128-bit counter (see aes256ctr.h).
    //
    // aes256-gcm@openssh.com shares aes_iv_* (the modes are mutually exclusive) but NOT aes_key_* - see the
    // keyed contexts below.
    // Every key buffer below is one epoch of the kmt region of the connection's span, at its own
    // named offset. Null until the connection claims the slot and splits its borrow.
    uint8_t *aes_key_c2s; ///< PROTOCORE_AES256CTR_KEY_LEN: AES key C→S (server decrypts inbound).
    uint8_t *aes_key_s2c; ///< PROTOCORE_AES256CTR_KEY_LEN: AES key S→C (server encrypts outbound).
    uint8_t *aes_iv_c2s;  ///< PROTOCORE_AES256CTR_CTR_LEN: AES IV C→S (CTR counter / GCM nonce); advances per packet.
    uint8_t *aes_iv_s2c;  ///< PROTOCORE_AES256CTR_CTR_LEN: AES IV S→C (CTR counter / GCM nonce); advances per packet.

    uint8_t *mac_key_c2s; ///< 64B: HMAC key, client-to-server (aes mode); 32 bytes for SHA-256, 64 for SHA-512.
    uint8_t *mac_key_s2c; ///< 64B: HMAC key, server-to-client (aes mode).
    // RFC 4253 sec 7.1 negotiates the cipher and the MAC per direction, so each is stored per direction
    // and a session may run different ones each way (0 = aes256-ctr / hmac-sha2-256 E&M).
    uint8_t mac_mode_c2s;    ///< SSH_MAC_* client-to-server (aes256-ctr only).
    uint8_t mac_mode_s2c;    ///< SSH_MAC_* server-to-client (aes256-ctr only).
    uint8_t cipher_mode_c2s; ///< SSH_CIPHER_* client-to-server.
    uint8_t cipher_mode_s2c; ///< SSH_CIPHER_* server-to-client.
    // chacha20-poly1305@openssh.com: 512-bit key per direction (K_main || K_header); no IV, no MAC key.
    uint8_t *chacha_key_c2s; ///< PROTOCORE_CHACHAPOLY_KEY_LEN: client-to-server, used only in chacha mode.
    uint8_t *chacha_key_s2c; ///< PROTOCORE_CHACHAPOLY_KEY_LEN: server-to-client, used only in chacha mode.

    // aes256-gcm@openssh.com (RFC 5647) reuses aes_iv_* above (mode-exclusive with CTR): the low 12 bytes
    // are the nonce, advanced per packet by AesGcm.iv_increment. No separate MAC key.
    //
    // It does NOT use aes_key_*. A GCM key becomes a keyed context at install time and the raw key is
    // wiped there, so in this mode the expanded schedule is the only key material resident - strictly
    // less than CTR mode keeps. The context stays for the life of the key because standing one up costs
    // ~9,200 cycles on an ESP32-S3, a fixed price per packet that dominates small interactive traffic
    // (see aesgcm.h). Wiped on rekey and by ssh_keymat_wipe() on close.
    // These two open the kmt epoch, so the region's 8-alignment is the epoch's own start.
    uint8_t *gcm_ctx_c2s; ///< PROTOCORE_AESGCM_BORROW: keyed GCM context C→S (server opens inbound).
    uint8_t *gcm_ctx_s2c; ///< PROTOCORE_AESGCM_BORROW: keyed GCM context S→C (server seals outbound).

    proto_bool active; ///< True once keys are installed after successful KEX.
} SshKeyMat;

/**
 * @brief Pool of session key material, two epochs per MAX_SSH_CONNS.
 *
 * RFC 4253 sec 7.3 switches each direction on its own NEWKEYS, so a re-key derives into the epoch
 * neither direction is reading and each direction moves to it when its NEWKEYS crosses.
 * The SshDir the codec is handed selects which one that site reads.
 * Zeroed on connection close by ssh_keymat_wipe(slot).
 */
extern SshKeyMat ssh_keys[MAX_SSH_CONNS][2];

// ---------------------------------------------------------------------------
// DH ephemeral state  (one entry per SSH connection, zeroed after KEX)
// ---------------------------------------------------------------------------

/**
 * @brief Ephemeral Diffie-Hellman state for one SSH connection.
 *
 * The three protocore_bignum fields (y, f, K) together hold 768 bytes of sensitive
 * material.  The entire struct is wiped by ssh_dh_wipe() immediately after
 * session keys are derived from K.
 *
 * FIELD LIFETIME:
 *   y  - generated by ssh_dh_generate(); zeroed in ssh_dh_wipe().
 *   f  - computed in ssh_dh_generate() as g^y mod p; sent in KEXDH_REPLY;
 *         zeroed in ssh_dh_wipe().
 *   K  - computed in ssh_dh_finish() as e^y mod p; used for key derivation;
 *         zeroed in ssh_dh_wipe() AFTER keys are installed.
 */
// All three sit in the kmt region of the connection's span, each at its own named offset, after the
// two key epochs. Null until the connection claims the slot and splits its borrow.
typedef struct
{
    protocore_bignum *y; ///< Server ephemeral private DH scalar (SENSITIVE - wiped after KEX).
    protocore_bignum *f; ///< Server DH public value = g^y mod p (sent to client).
    protocore_bignum *K; ///< Shared DH secret = e^y mod p (SENSITIVE - wiped after key derivation).
} SshDhState;

/** @brief Pool of ephemeral DH state, one entry per MAX_SSH_CONNS. */
extern SshDhState ssh_dh[MAX_SSH_CONNS];

/**
 * @brief Generate slot @p i's DH ephemeral: a random y, and f = g^y mod p (RFC 4253 sec 8).
 * @return 0 on success, -1 if the slot has no storage.
 */
int ssh_dh_generate(uint8_t i);

/**
 * @brief Derive the RFC 4253 sec 7.2 keys from K, H and the session id into slot @p i's epoch,
 *        one letter per direction.
 */
/**
 * @brief One RFC 4253 sec 7.2 derivation: @p out_len bytes of the key @p label names.
 *
 * "Encryption keys MUST be computed as HASH, of a known value and K": K1 = HASH(K || H || X ||
 * session_id) with X the label byte, and where more bytes are wanted than one hash gives,
 * "the key is extended by computing HASH of the concatenation of K and H and the entire key so
 * far" - K2 = HASH(K || H || K1), K3 = HASH(K || H || K1 || K2), key = K1 || K2 || K3.
 *
 * @param label    'A'..'F', the six keys sec 7.2 lists in order.
 * @param out_len  Bytes wanted, clamped to SSH_KDF_MAX.
 */
void ssh_kdf_derive(const SshKdfInputs *in, char label, uint8_t *out, size_t out_len);

void ssh_kex_install_keys(uint8_t i, const SshKdfInputs *in);

// ---------------------------------------------------------------------------
// Wipe helpers
// ---------------------------------------------------------------------------

/**
 * @brief Zero all key material for slot @p i on disconnect or KEX failure.
 *
 * Each buffer is wiped through its pointer at its own declared length: the bytes are the
 * connection's, and zeroing the struct would clear the bindings and leave the keys in the span.
 */
static inline void ssh_keymat_wipe(uint8_t i)
{
    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    for (uint8_t e = 0; e < 2u; e++)
    {
        SshKeyMat *km = &ssh_keys[i][e];
        if (km->gcm_ctx_c2s != NULL)
        {
            // A keyed GCM context may hold what the accelerator arm attached, so zeroing the bytes alone
            // would leak it once per closed connection. Release first, then wipe. Each direction owns its
            // context, and only the direction that negotiated GCM stood one up.
            if (km->active && km->cipher_mode_c2s == SSH_CIPHER_AES256GCM)
            {
                AesGcm.key_wipe(km->gcm_ctx_c2s);
            }
            if (km->active && km->cipher_mode_s2c == SSH_CIPHER_AES256GCM)
            {
                AesGcm.key_wipe(km->gcm_ctx_s2c);
            }
            protocore_secure_wipe(km->gcm_ctx_c2s, PROTOCORE_AESGCM_BORROW);
            protocore_secure_wipe(km->gcm_ctx_s2c, PROTOCORE_AESGCM_BORROW);
            protocore_secure_wipe(km->chacha_key_c2s, PROTOCORE_CHACHAPOLY_KEY_LEN);
            protocore_secure_wipe(km->chacha_key_s2c, PROTOCORE_CHACHAPOLY_KEY_LEN);
            protocore_secure_wipe(km->mac_key_c2s, 64);
            protocore_secure_wipe(km->mac_key_s2c, 64);
            protocore_secure_wipe(km->aes_key_c2s, PROTOCORE_AES256CTR_KEY_LEN);
            protocore_secure_wipe(km->aes_key_s2c, PROTOCORE_AES256CTR_KEY_LEN);
            protocore_secure_wipe(km->aes_iv_c2s, PROTOCORE_AES256CTR_CTR_LEN);
            protocore_secure_wipe(km->aes_iv_s2c, PROTOCORE_AES256CTR_CTR_LEN);
            km->mac_mode_c2s = 0;
            km->mac_mode_s2c = 0;
            km->cipher_mode_c2s = 0;
            km->cipher_mode_s2c = 0;
            km->active = PROTO_FALSE;
        }
    }
}

/**
 * @brief Zero every ephemeral KEX private for slot @p i after keys are derived.
 *
 * The scalars live in the connection's kmt region, so the wipe follows the pointers to the bytes.
 * Zeroing the struct itself would only clear the pointers and leave the scalars in the span. The
 * X25519 / P-256 pair sits beside y, f and K, so one call covers both of its members.
 */
static inline void ssh_dh_wipe(uint8_t i)
{
    if (i >= MAX_SSH_CONNS)
    {
        return;
    }
    if (ssh_dh[i].y != NULL)
    {
        protocore_secure_wipe(ssh_dh[i].y, sizeof(protocore_bignum));
        protocore_secure_wipe(ssh_dh[i].f, sizeof(protocore_bignum));
        protocore_secure_wipe(ssh_dh[i].K, sizeof(protocore_bignum));
    }
    if (ssh_sess[i].ecdh_sk != NULL)
    {
        protocore_secure_wipe(ssh_sess[i].ecdh_sk, SSH_ECDH_PAIR_LEN);
    }
}

/** @brief Send DISCONNECT with the no-more-auth-methods reason, then drop. */
/**
 * @brief Build an SSH_MSG_DISCONNECT payload (RFC 4253 sec 11.1).
 *
 * @param reason_code  One of SSH_DISCONNECT_* constants.
 * @param desc         Description bytes, sent as the message's first string.
 * @param desc_len     Length of @p desc.
 * @param out          Output buffer for the payload.
 * @param out_len      Set to the number of bytes written.
 * @param cap          Capacity of @p out.
 * @return 0 on success, -1 when @p cap cannot hold the whole message.
 */
int ssh_pkt_build_disconnect(uint32_t reason_code, const char *desc, size_t desc_len, uint8_t *out, size_t *out_len,
                             size_t cap);

/** @brief Dispatch one decrypted message; 50 and above go up to the authentication protocol. */
int ssh_transport_dispatch(uint8_t i, uint8_t msg_type, const uint8_t *payload, size_t len);

/** @brief Emit a fresh KEXINIT for slot @p i once its volume or time budget is spent. */
void ssh_transport_key_re_exchange(uint8_t i);

/**
 * @brief Handle SSH_MSG_SERVICE_REQUEST; emit SERVICE_ACCEPT for ssh-userauth (RFC 4253 sec 10).
 * @return 0 and writes SERVICE_ACCEPT to @p out, or -1 if the service is not "ssh-userauth" or the
 *         message is malformed.
 */
int ssh_transport_service_request(const uint8_t *payload, size_t len, uint8_t *out, size_t *out_len, size_t cap);

/**
 * @brief Take the peer identification string off @p buf (RFC 4253 sec 4.2).
 * @return 1 once it is whole and the phase has advanced, 0 while more bytes are needed, -1 on a
 *         string the section does not admit. @p off is left at the first binary packet byte.
 */
int ssh_transport_version_exchange_recv(uint8_t i, const uint8_t *buf, size_t n, size_t *off);

// ---------------------------------------------------------------------------
// Public key algorithms (RFC 4253 sec 6.6)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// RFC 4253 sec 8 - the shared secret K, from the initiating role's side
// ---------------------------------------------------------------------------

/**
 * @brief The initiating role's ephemeral, as the sec 8 method needs to read it.
 *
 * @var SshKexEphemeral::alg        the negotiated method, which selects every term below
 * @var SshKexEphemeral::priv       32 bytes: X25519 scalar, P-256 d, or the DH exponent
 * @var SshKexEphemeral::hybrid_sk  a PQ/T hybrid's decapsulation key, null for the classical methods
 * @var SshKexEphemeral::work       crypto scratch, borrowed from the caller's slot
 */
typedef struct
{
    SshKexAlg alg;
    const uint8_t *priv;
    const uint8_t *hybrid_sk;
    uint8_t *work;
} SshKexEphemeral;

/**
 * @brief Compute K from the peer's exchange value, for the role that sent the first message.
 *
 * One switch over the negotiated method, beside the responder half that shares this file: RFC 4253
 * sec 8 is the transport's, whichever end is running it. K is written right-aligned into @p k_be,
 * which is how sec 8 and sec 7.2 both consume it - as an mpint for the classical methods, and as a
 * fixed 32 or 64-byte string for the hybrids.
 *
 * @param e             This end's ephemeral for the exchange.
 * @param peer_pub      The peer's exchange value: Q_S, f, or ciphertext || Q_S for a hybrid.
 * @param peer_pub_len  Length of @p peer_pub; each method checks it against its own.
 * @param k_be          256 bytes, zeroed then filled from the right.
 * @return false on a wrong length, a rejected point (RFC 7748 sec 6.1), or an unsupported method.
 */
proto_bool ssh_kex_shared_secret(const SshKexEphemeral *e, const uint8_t *peer_pub, uint32_t peer_pub_len,
                                 uint8_t k_be[256]);

/** @brief True when @p blob holds a public key in one of the formats this build decodes. */
proto_bool ssh_pubkey_blob_valid(const uint8_t *blob, uint32_t blob_len);

/**
 * @brief True when @p pk_algo names an algorithm this end verifies, and @p blob is of its key type.
 *
 * RFC 4252 sec 7: "Any public key algorithm may be offered for use in authentication... If the
 * server does not support some algorithm, it MUST simply reject the request." The name arrives
 * independently of the blob, so both are checked against what the blob parsers here accept: either
 * RSA signature name takes an "ssh-rsa" blob, the other two take a blob named for themselves.
 */
proto_bool ssh_pubkey_algo_supported(const char *pk_algo, const uint8_t *blob, uint32_t blob_len);

/**
 * @brief Verify @p sig over @p signed_data against the public key in @p blob, out of slot @p i's
 * crypto_work. The key type comes from the blob; @p pk_algo steers the RSA signature hash (RFC 8332).
 */
proto_bool ssh_pubkey_verify(uint8_t i, const char *pk_algo, const uint8_t *blob, uint32_t blob_len, const uint8_t *sig,
                             uint32_t sig_len, const uint8_t *signed_data, size_t signed_len);

PROTOCORE_END_DECLS

#endif // PROTOCORE_TRANSPORT_TRANSPORT_H
