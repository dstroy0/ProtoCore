// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_packet.h
 * @brief SSH binary packet protocol: framing, AES-256-CTR encryption,
 *        HMAC-SHA2-256 integrity, and sequence-number tracking.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * WIRE FORMAT (RFC 4253 §6)
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  Each SSH packet on the wire (after KEX completes):
 *
 *    [4 bytes] packet_length    - big-endian uint32 (does NOT include itself
 *                                 or the MAC; includes padding_length, payload,
 *                                 and random_padding fields)
 *    [1 byte]  padding_length   - number of bytes of random padding
 *    [N bytes] payload          - the SSH message
 *    [P bytes] random_padding   - random bytes; total pkt_len % blocksize == 0
 *    [32 bytes] MAC             - HMAC-SHA2-256 over:
 *                                   uint32(seq_no) || packet_length ||
 *                                   padding_length || payload || random_padding
 *
 *  AES-256-CTR encrypts: packet_length || padding_length || payload || padding
 *  The MAC is computed over the PLAINTEXT (before encryption) prepended with
 *  the 4-byte sequence number.  This is the "encrypt-then-MAC" variant used
 *  by openssh with hmac-sha2-256 (ETM) - but RFC 4253 default is MAC-then-
 *  encrypt.  We implement STANDARD RFC 4253 (MAC over plaintext, then encrypt)
 *  to match the base SSH specification.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * MAC-VERIFY-BEFORE-USE INVARIANT
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  On receive: decrypt → verify HMAC → then process the payload.
 *  If HMAC verification fails: close the connection immediately, zero the
 *  decrypted buffer.  Do NOT process or reflect any byte of the payload.
 *
 *  This ordering closes the "BEAST" class of attack where an attacker can
 *  influence decryption outputs by injecting invalid packets.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * SEQUENCE NUMBER OVERFLOW GUARD
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  SSH sequence numbers are 32-bit values that wrap at 2^32.  RFC 4253 does
 *  not mandate rekeying before wrap; however, CTR-mode keystream repetition
 *  at wrap would be a catastrophic confidentiality failure.
 *
 *  Policy: close the connection if seq_no_send or seq_no_recv reaches
 *  SSH_SEQ_CLOSE_THRESHOLD.  A future rekey implementation would reset the
 *  counters instead.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * PADDING
 * ═══════════════════════════════════════════════════════════════════════════
 *
 *  AES-256-CTR block size = 16 bytes.  RFC 4253 §6 requires the padded
 *  packet (packet_length + 1 + payload + padding) to be a multiple of
 *  max(8, cipher_block_size) = 16.  Minimum padding is 4 bytes.
 *
 *  padding_len = (16 - ((5 + payload_len) % 16)) % 16
 *  if (padding_len < 4) padding_len += 16;
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_SSH_PACKET_H
#define PROTOCORE_SSH_PACKET_H

#include "crypto/mac/hmac_sha256.h"
#include "network_drivers/presentation/ssh/transport/ssh_keymat.h"
#include "protocore_config.h"

PROTO_BEGIN_DECLS

// ---------------------------------------------------------------------------
// Sequence number overflow threshold
// ---------------------------------------------------------------------------

/**
 * @brief Close the connection when seq_no reaches this value.
 *
 * Set to 0xFFFFFFF0 (16 below the 32-bit wrap) as a conservative margin.
 * This prevents CTR keystream reuse that would occur at wrap.
 * A rekey implementation would reset this counter; until then, we close.
 */
#define SSH_SEQ_CLOSE_THRESHOLD 0xFFFFFFF0u

// ---------------------------------------------------------------------------
// SSH message type constants (RFC 4253)
// ---------------------------------------------------------------------------

#define SSH_MSG_DISCONNECT 1
#define SSH_MSG_IGNORE 2
#define SSH_MSG_UNIMPLEMENTED 3
#define SSH_MSG_SERVICE_REQUEST 5
#define SSH_MSG_SERVICE_ACCEPT 6
#define SSH_MSG_EXT_INFO 7 // RFC 8308 extension negotiation
#define SSH_MSG_KEXINIT 20
#define SSH_MSG_NEWKEYS 21
#define SSH_MSG_KEXDH_INIT 30
#define SSH_MSG_KEXDH_REPLY 31
#define SSH_MSG_USERAUTH_REQUEST 50
#define SSH_MSG_USERAUTH_FAILURE 51
#define SSH_MSG_USERAUTH_SUCCESS 52
#define SSH_MSG_USERAUTH_PK_OK 60
// 60 is method-specific: it is PK_OK for publickey and INFO_REQUEST for keyboard-interactive
// (RFC 4256 §3.2). The current auth phase/state disambiguates which handler owns an inbound 60.
#define SSH_MSG_USERAUTH_INFO_REQUEST 60  // RFC 4256 §3.2 (keyboard-interactive, server->client)
#define SSH_MSG_USERAUTH_INFO_RESPONSE 61 // RFC 4256 §3.4 (keyboard-interactive, client->server)
#define SSH_MSG_GLOBAL_REQUEST 80         // RFC 4254 §4 (e.g. tcpip-forward for ssh -R)
#define SSH_MSG_REQUEST_SUCCESS 81        // RFC 4254 §4 reply to a want_reply global request
#define SSH_MSG_REQUEST_FAILURE 82        // RFC 4254 §4 reply: request refused / unrecognized
#define SSH_MSG_CHANNEL_OPEN 90
#define SSH_MSG_CHANNEL_OPEN_CONFIRM 91
#define SSH_MSG_CHANNEL_OPEN_FAILURE 92
#define SSH_MSG_CHANNEL_WINDOW_ADJUST 93
#define SSH_MSG_CHANNEL_DATA 94
#define SSH_MSG_CHANNEL_EOF 96
#define SSH_MSG_CHANNEL_CLOSE 97
#define SSH_MSG_CHANNEL_REQUEST 98
#define SSH_MSG_CHANNEL_SUCCESS 99
#define SSH_MSG_CHANNEL_FAILURE 100

// ---------------------------------------------------------------------------
// Disconnect reason codes (RFC 4253 §11.1)
// ---------------------------------------------------------------------------

#define SSH_DISCONNECT_PROTOCOL_ERROR 2
#define SSH_DISCONNECT_MAC_ERROR 5
#define SSH_DISCONNECT_TOO_MANY_CONNECTIONS 11
#define SSH_DISCONNECT_NO_MORE_AUTH_METHODS_AVAILABLE 14 // RFC 4250 §4.2.2

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
    uint32_t seq_no_send;  ///< Outgoing sequence number (incremented per packet).
    uint32_t seq_no_recv;  ///< Incoming sequence number (incremented per packet).
    proto_bool kex_active; ///< True while KEX is in progress (no user data).
    // Encryption activates per direction (RFC 4253 sec 7.3): our outbound turns on when we send our
    // SSH_MSG_NEWKEYS, our inbound when we receive the peer's. The send path (pack) reads enc_out; the
    // receive path (unpack) reads enc_in. A strict peer may activate its send direction before we
    // activate ours, so the two are tracked independently rather than as one flag.
    proto_bool enc_out; ///< True once we have sent our NEWKEYS (outbound cipher/MAC active).
    proto_bool enc_in;  ///< True once we have received the peer's NEWKEYS (inbound cipher/MAC active).

    // SSH keys are named by direction (client->server "c2s", server->client "s2c"), fixed by RFC 4253
    // §7.2 regardless of role. A server sends s2c / receives c2s; a client is the mirror. This flag
    // selects the direction at each cipher/MAC site so one packet implementation serves both roles.
    // Default false = server (so existing server code is unchanged); SshPacket.set_client() flips it.
    proto_bool is_client;

    // Receive reassembly: we may receive partial packets across TCP segments.
    uint8_t rx_buf[SSH_PKT_BUF_SIZE]; ///< Raw receive buffer (from transport).
    size_t rx_len;                    ///< Bytes currently in rx_buf.
} SshPacketState;

/** @brief Static packet state pool (BSS). One entry per SSH slot. */
extern SshPacketState ssh_pkt[MAX_SSH_CONNS];

// ---------------------------------------------------------------------------
// Wire buffer sizing
// ---------------------------------------------------------------------------

// Worst-case on-wire bytes for a payload of up to SSH_PKT_BUF_SIZE: the 4-byte packet_length, the
// 1-byte padding_length, the effective payload, worst-case padding, and the largest MAC tag. When
// s2c compression is built in, the "effective payload" is the compressor's worst-case output
// (ssh_deflate_bound of a full payload) since fixed-Huffman can slightly expand incompressible data.
// Callers MUST size the wire buffer with this so a compressed packet never overflows and desyncs the
// stateful cipher / compression stream (a dropped packet mid-stream would corrupt the session).
#if PC_ENABLE_SSH_ZLIB
#define SSH_MAX_EFFECTIVE_PAYLOAD (2 + SSH_PKT_BUF_SIZE + (SSH_PKT_BUF_SIZE >> 3) + 32) // = ssh_deflate_bound()
#else
#define SSH_MAX_EFFECTIVE_PAYLOAD (SSH_PKT_BUF_SIZE)
#endif
#define SSH_MAX_PAD 32 // worst-case padding across block-8 / block-16 modes (min-4 rule)
#define SSH_MAX_MAC 64 // largest MAC tag (hmac-sha2-512); chacha's Poly1305 tag is 16
#define SSH_WIRE_CAP ((size_t)(4 + 1 + SSH_MAX_EFFECTIVE_PAYLOAD + SSH_MAX_PAD + SSH_MAX_MAC))

// Scratch the transport layer (RFC 4253) borrows to frame one packet, and nothing more - the wire
// buffer and the payload being framed belong to whoever called in, because this layer is the framer,
// not the wire. The receive side is the peak: a plaintext scratch (largest across the cipher modes)
// is live while the payload is decompressed into a second buffer. The send side borrows only the
// compressor's output bound. RFC 4251 sec 1 stacks auth and connection on top of this, so the arena
// sums the layers; it does not fold them into each other.
#if PC_ENABLE_SSH_ZLIB
#define PC_PLAINTEXT_WORK_SSH_TRANSPORT ((size_t)(SSH_PKT_BUF_SIZE + 64 + SSH_PKT_BUF_SIZE))
#else
#define PC_PLAINTEXT_WORK_SSH_TRANSPORT ((size_t)(SSH_PKT_BUF_SIZE + 64))
#endif

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

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
 * @brief The binary packet protocol (RFC 4253 sec 6): framing, the MAC, and DISCONNECT.
 *
 * @var SshPacketNs::init        Initialize the packet state for SSH connection slot @p i
 * @var SshPacketNs::set_client  Mark slot @p i as the SSH client role (call once, right after SshPacket.init)
 * @var SshPacketNs::send        Build and send one SSH binary packet
 * @var SshPacketNs::recv        Receive and process one or more SSH binary packets from @p data
 * @var SshPacketNs::disconnect  Send SSH_MSG_DISCONNECT with reason @p reason_code
 */
typedef struct
{
    void (*init)(uint8_t i);
    void (*set_client)(uint8_t i);
    int (*send)(uint8_t i, const uint8_t *payload, size_t payload_len, uint8_t *out, size_t *out_len, size_t out_cap);
    int (*recv)(uint8_t i, const uint8_t *data, size_t len, ssh_msg_handler_t handler);
    int (*disconnect)(uint8_t i, uint32_t reason_code, uint8_t *out, size_t *out_len, size_t out_cap);
} SshPacketNs;

/** @brief The one symbol this module exports. */
extern const SshPacketNs SshPacket;

PROTO_END_DECLS

#endif // PROTOCORE_SSH_PACKET_H
