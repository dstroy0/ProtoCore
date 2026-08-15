// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file common.h
 * @brief Root infrastructure: fixed widths, serializers, opcodes and sizes, for every layer above.
 */

#ifndef PROTOCORE_SSH_COMMON_H
#define PROTOCORE_SSH_COMMON_H

#include "mmgr/bytes.h"       // protocore_span, bytes.* writers, bytes.rd_str / bytes.rd_u32 readers
#include "mmgr/protostr.h"    // str.len: the length prefix on a written string
#include "protocore_config.h" // protocore_types.h for the fixed widths, PROTOCORE_INLINE, the SSH sizing constants

#include "crypto/aead/chachapoly.h"   // PROTOCORE_CHACHAPOLY_KEY_LEN - the chacha keys in the memory map
#include "crypto/asymmetric/bignum.h" // protocore_bignum - the DH ephemeral in the memory map
#include "crypto/cipher/aes256ctr.h"  // PROTOCORE_AES256CTR_KEY_LEN / _CTR_LEN - the aes keys and IVs
#include "crypto/mac/hmac_sha256.h"   // PROTOCORE_HMAC_SHA256_BORROW - the packet MAC scratch
#include "network_drivers/presentation/ssh/transport/ssh_kexhash.h" // SSH_KEXHASH_MAX_LEN - the session id span

// ---------------------------------------------------------------------------
// Sizing
// ---------------------------------------------------------------------------

/** @brief Max stored length of an SSH identification string (RFC 4253 sec 4.2: 255). */
#define SSH_VERSION_MAX 256

/** @brief Longest identification string RFC 4253 sec 4.2 admits: 255 on the wire, CR and LF counted. */
#define SSH_VERSION_CONTENT_MAX 253

/**
 * @brief Max stored size of our own KEXINIT (I_S). Sized for the full advertised suite: the
 * kex list (mlkem + dh + ecdsa-nistp256 + curve25519 x2 + ext-info-s), all three host-key types,
 * the cipher (chacha + 2x aes) and MAC (2x aes + 2x plain) lists, and zlib s2c compression
 * (worst case ~580 bytes; 704 leaves headroom for future algorithm additions).
 */
#define PROTOCORE_SSH_KEXINIT_S_MAX 704

/**
 * @brief Capacity of I_C and I_S. A buffer takes the peer bound in the role that receives into it,
 * our own bound in the role that writes it.
 */
#if PROTOCORE_ENABLE_SSH_CLIENT && PROTOCORE_ENABLE_SSH_SERVER
#define PROTOCORE_SSH_I_C_MAX SSH_KEXINIT_MAX
#define PROTOCORE_SSH_I_S_MAX SSH_KEXINIT_MAX
#elif PROTOCORE_ENABLE_SSH_CLIENT
#define PROTOCORE_SSH_I_C_MAX PROTOCORE_SSH_KEXINIT_S_MAX
#define PROTOCORE_SSH_I_S_MAX SSH_KEXINIT_MAX
#else
#define PROTOCORE_SSH_I_C_MAX SSH_KEXINIT_MAX
#define PROTOCORE_SSH_I_S_MAX PROTOCORE_SSH_KEXINIT_S_MAX
#endif

/** @brief Server identification string (no CR LF; appended on the wire). */
#define SSH_SERVER_VERSION "SSH-2.0-1.0"

/** @brief Client identification string (no CR LF; appended on the wire). */
#define SSH_CLIENT_VERSION "SSH-2.0-PROTOCORE_client_1.0"

PROTOCORE_BEGIN_DECLS

/** @brief Protocol opcodes, by the number each RFC assigns it (RFC 4250 sec 4.1.2). */
typedef enum PROTO_ENUM_PACKED
{
    // Transport layer, RFC 4253: generic 1 to 19, negotiation 20 to 29, method specific 30 to 49.
    SSH_MSG_DISCONNECT = 1,
    SSH_MSG_IGNORE = 2,
    SSH_MSG_UNIMPLEMENTED = 3,
    SSH_MSG_DEBUG = 4,
    SSH_MSG_SERVICE_REQUEST = 5,
    SSH_MSG_SERVICE_ACCEPT = 6,
    SSH_MSG_EXT_INFO = 7, // RFC 8308 extension negotiation
    SSH_MSG_KEXINIT = 20,
    SSH_MSG_NEWKEYS = 21,
    SSH_MSG_KEXDH_INIT = 30,
    SSH_MSG_KEXDH_REPLY = 31,

    // User authentication, RFC 4252: generic 50 to 59, method specific 60 to 79.
    SSH_MSG_USERAUTH_REQUEST = 50,
    SSH_MSG_USERAUTH_FAILURE = 51,
    SSH_MSG_USERAUTH_SUCCESS = 52,
    SSH_MSG_USERAUTH_BANNER = 53, // RFC 4252 sec 5.4: text for the user, any time before success
    SSH_MSG_USERAUTH_PK_OK = 60,
    // 60 is method specific: PK_OK for publickey, INFO_REQUEST for keyboard-interactive
    // (RFC 4256 sec 3.2). The current auth phase decides which handler owns an inbound 60.
    SSH_MSG_USERAUTH_INFO_REQUEST = 60,
    SSH_MSG_USERAUTH_INFO_RESPONSE = 61, // RFC 4256 sec 3.4

    // Connection protocol, RFC 4254: global 80 to 89, channel 90 to 127.
    SSH_MSG_GLOBAL_REQUEST = 80,
    SSH_MSG_REQUEST_SUCCESS = 81,
    SSH_MSG_REQUEST_FAILURE = 82,
    SSH_MSG_CHANNEL_OPEN = 90,
    SSH_MSG_CHANNEL_OPEN_CONFIRMATION = 91,
    SSH_MSG_CHANNEL_OPEN_FAILURE = 92,
    SSH_MSG_CHANNEL_WINDOW_ADJUST = 93,
    SSH_MSG_CHANNEL_DATA = 94,
    SSH_MSG_CHANNEL_EXTENDED_DATA = 95, // data_type_code + string
    SSH_MSG_CHANNEL_EOF = 96,
    SSH_MSG_CHANNEL_CLOSE = 97,
    SSH_MSG_CHANNEL_REQUEST = 98,
    SSH_MSG_CHANNEL_SUCCESS = 99,
    SSH_MSG_CHANNEL_FAILURE = 100,
} SshMsgId;

/** @brief Disconnect reason codes (RFC 4253 sec 11.1, numbered by RFC 4250 sec 4.2.2). */
typedef enum PROTO_ENUM_PACKED
{
    SSH_DISCONNECT_PROTOCOL_ERROR = 2,
    SSH_DISCONNECT_MAC_ERROR = 5,
    SSH_DISCONNECT_SERVICE_NOT_AVAILABLE = 7,
    SSH_DISCONNECT_BY_APPLICATION = 11,
    SSH_DISCONNECT_TOO_MANY_CONNECTIONS = 12,
    SSH_DISCONNECT_NO_MORE_AUTH_METHODS_AVAILABLE = 14,
} SshDisconnectReason;

/** @brief Channel open failure reason codes (RFC 4254 sec 5.1). */
typedef enum PROTO_ENUM_PACKED
{
    SSH_OPEN_ADMINISTRATIVELY_PROHIBITED = 1,
    SSH_OPEN_CONNECT_FAILED = 2,
    SSH_OPEN_UNKNOWN_CHANNEL_TYPE = 3,
    SSH_OPEN_RESOURCE_SHORTAGE = 4,
} SshOpenFailureReason;

/**
 * @brief The connection's memory map: every byte it uses, at a named offset from its slot base.
 *
 * Laid out kmt | constants | control packet | data packet, each offset the previous one plus that
 * member's size. The storage is ssh.c's; a translation unit takes its pointer as
 * @c ssh_conn_slot(i) + the offset and already knows the member's size.
 */

// One key epoch: the six RFC 4253 sec 7.2 keys in every cipher mode negotiation can pick, at
// offsets from the epoch's own base. Both epochs are laid out this way.
#define SSH_OFF_GCM_C2S 0u
#define SSH_OFF_GCM_S2C (SSH_OFF_GCM_C2S + PROTOCORE_WORK_AESGCM)
#define SSH_OFF_CHACHA_C2S (SSH_OFF_GCM_S2C + PROTOCORE_WORK_AESGCM)
#define SSH_OFF_CHACHA_S2C (SSH_OFF_CHACHA_C2S + PROTOCORE_CHACHAPOLY_KEY_LEN)
#define SSH_OFF_MAC_C2S (SSH_OFF_CHACHA_S2C + PROTOCORE_CHACHAPOLY_KEY_LEN)
#define SSH_OFF_MAC_S2C (SSH_OFF_MAC_C2S + 64u)
#define SSH_OFF_AES_KEY_C2S (SSH_OFF_MAC_S2C + 64u)
#define SSH_OFF_AES_KEY_S2C (SSH_OFF_AES_KEY_C2S + PROTOCORE_AES256CTR_KEY_LEN)
#define SSH_OFF_AES_IV_C2S (SSH_OFF_AES_KEY_S2C + PROTOCORE_AES256CTR_KEY_LEN)
#define SSH_OFF_AES_IV_S2C (SSH_OFF_AES_IV_C2S + PROTOCORE_AES256CTR_CTR_LEN)
#define SSH_EPOCH_STRIDE (SSH_OFF_AES_IV_S2C + PROTOCORE_AES256CTR_CTR_LEN)

// Order: wire | session | exchange | packet | rx. Slots are contiguous, so the region an overrun
// carries into the next slot is the one at offset 0 - the wire, which is framing, not key material.
// A run off the end of rx therefore kills both connections rather than reaching either one's keys.
// Bounds are enforced where bytes enter; this ordering is what remains if one is ever missed.
//
// The regions are grouped by how long their contents live, because that is what decides how many
// copies a build needs:
//
//   wire, rx    one per connection, and the only regions whose size follows the packet size.
//   session     one per connection, alive from the first key exchange to the last packet.
//   exchange    alive only from KEXINIT to NEWKEYS (RFC 4253 sec 7.1), so one per exchange in
//               flight rather than one per connection.
//   packet      alive only for the message being framed or verified, so one per worker.
//
// exchange and packet are still per-slot here; they are grouped so that stays visible, and so
// lifting them out is a change of base pointer rather than a re-layout.

// wire: the framed packet. The payload is written at SSH_WIRE_PAYLOAD_OFF and framed in place.
#define SSH_OFF_WIRE 0u

// session: what outlives a single exchange - the identification strings both ends hash into every
// exchange hash, the session id the first KEX fixes (RFC 4253 sec 7.2), and the two key epochs. The
// second epoch holds the keys a re-key derives while the first still decrypts, until both
// directions have switched (sec 7.3).
#define SSH_OFF_V_C (SSH_OFF_WIRE + SSH_WIRE_CAP)
#define SSH_OFF_V_S (SSH_OFF_V_C + SSH_VERSION_MAX)
#define SSH_OFF_SESSION_ID (SSH_OFF_V_S + SSH_VERSION_MAX)
#define SSH_OFF_EPOCH_0 (SSH_OFF_SESSION_ID + SSH_KEXHASH_MAX_LEN)
#define SSH_OFF_EPOCH_1 (SSH_OFF_EPOCH_0 + SSH_EPOCH_STRIDE)
#define SSH_SESSION_END (SSH_OFF_EPOCH_1 + SSH_EPOCH_STRIDE)

// exchange: everything an exchange needs and nothing else reads once NEWKEYS is sent - the peer
// identification being collected, both KEXINIT payloads the exchange hash covers, the client's
// public value, and every ephemeral private. One exchange runs at a time per worker.
#define SSH_OFF_IDENT SSH_SESSION_END
#define SSH_OFF_I_C (SSH_OFF_IDENT + SSH_VERSION_MAX)
#define SSH_OFF_I_S (SSH_OFF_I_C + PROTOCORE_SSH_I_C_MAX)
#define SSH_OFF_KEXINIT (SSH_OFF_I_S + PROTOCORE_SSH_I_S_MAX)
#define SSH_OFF_CPUB (SSH_OFF_KEXINIT + PROTOCORE_SSH_KEXINIT_S_MAX)
#define SSH_OFF_DH_Y (SSH_OFF_CPUB + PROTOCORE_SSH_CPUB_MAX)
#define SSH_OFF_DH_F (SSH_OFF_DH_Y + sizeof(protocore_bignum))
#define SSH_OFF_DH_K (SSH_OFF_DH_F + sizeof(protocore_bignum))
#define SSH_OFF_ECDH_SK (SSH_OFF_DH_K + sizeof(protocore_bignum))
#define SSH_OFF_ECDH_PK (SSH_OFF_ECDH_SK + 32u)
/** @brief The ECDH ephemeral pair, private then public: what one wipe covers. */
#define SSH_ECDH_PAIR_LEN 64u
#define SSH_OFF_CRYPTO_WORK (SSH_OFF_ECDH_PK + 32u)
#define SSH_EXCHANGE_END (SSH_OFF_CRYPTO_WORK + PROTOCORE_CRYPTO_BORROW_MAX)

// packet: the bytes one message's MAC works out of. Live for that message only.
#define SSH_OFF_MAC_WORK SSH_EXCHANGE_END
#define SSH_PACKET_END (SSH_OFF_MAC_WORK + PROTOCORE_HMAC_SHA256_BORROW)

// rx: the bytes drained off the transport ring, then the reassembly they feed. Last, so what it
// runs into is the next slot's wire.
#define SSH_OFF_RX_READ SSH_PACKET_END
#define SSH_OFF_RX_ASM (SSH_OFF_RX_READ + RX_BUF_SIZE)

/** @brief Capacity of the reassembly region at SSH_OFF_RX_ASM: what rx_buf actually spans. */
#define SSH_RX_ASM_CAP ((size_t)SSH_RFC_MAX_PAYLOAD)

/** @brief One connection's whole span, and the stride between slots. */
#define SSH_SLOT_BORROW (SSH_OFF_RX_ASM + SSH_RX_ASM_CAP)

// What each region costs, so the count a build needs is arithmetic rather than a guess. exchange
// and packet are the two that do not have to be replicated per connection: one exchange runs at a
// time per worker, and one message is framed at a time per worker.
#define SSH_SESSION_SIZE (SSH_SESSION_END - SSH_OFF_V_C)
#define SSH_EXCHANGE_SIZE (SSH_EXCHANGE_END - SSH_OFF_IDENT)
#define SSH_PACKET_SIZE (SSH_PACKET_END - SSH_OFF_MAC_WORK)
#define SSH_RX_SIZE (SSH_SLOT_BORROW - SSH_OFF_RX_READ)

// ---------------------------------------------------------------------------
// SSH wire types (RFC 4251 sec 5, RFC 4253 sec 7.1)
// ---------------------------------------------------------------------------

/** @brief Read a uint32 in network byte order (RFC 4251 sec 5). */
static inline uint32_t read_u32_be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/** @brief Write a uint32 in network byte order (RFC 4251 sec 5). */
static inline void write_u32_be(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)(v);
}

/** @brief Append a string: uint32 length, then @p n bytes of @p data. */
PROTOCORE_INLINE void protocore_ssh_wr_str(protocore_span *w, const void *data, size_t n)
{
    bytes.put_be(w, (uint64_t)n, 4);
    bytes.raw(w, data, n);
}

/**
 * @brief Append a NUL-terminated @p s as a string, its length taken up to the span's capacity.
 *
 * A comma-separated name-list (RFC 4253 sec 7.1) is one of these.
 */
PROTOCORE_INLINE void protocore_ssh_wr_cstr(protocore_span *w, const char *s)
{
    protocore_ssh_wr_str(w, s, str.len(s, w->cap));
}

/**
 * @brief Append @p len big-endian bytes as an mpint: leading zero bytes stripped, a 0x00 prepended
 *        when the top bit is set, and a zero value written as the empty string.
 */
PROTOCORE_INLINE void protocore_ssh_wr_mpint(protocore_span *w, const uint8_t *be, size_t len)
{
    size_t off = 0;
    while (off < len && be[off] == 0)
    {
        off++;
    }
    if (off == len)
    {
        bytes.put_be(w, 0, 4);
        return;
    }
    proto_bool pad = (be[off] & 0x80u) != 0;
    uint64_t mlen = (uint64_t)(len - off);
    if (pad)
    {
        mlen++;
    }
    bytes.put_be(w, mlen, 4);
    if (pad)
    {
        bytes.put(w, 0x00);
    }
    bytes.raw(w, be + off, len - off);
}

// ---------------------------------------------------------------------------
// Sequence number overflow threshold
// ---------------------------------------------------------------------------

/**
 * @brief Close the connection when seq_no reaches this value.
 *
 * Set to 0xFFFFFFF0 (16 below the 32-bit wrap) as a conservative margin.
 * This prevents CTR keystream reuse that would occur at wrap. The counter is
 * never reset; a re-key at SSH_REKEY_PACKET_THRESHOLD keeps it far from here.
 */
#define SSH_SEQ_CLOSE_THRESHOLD 0xFFFFFFF0u

// ---------------------------------------------------------------------------
// Wire buffer sizing
// ---------------------------------------------------------------------------

// Worst-case on-wire bytes for a payload of up to SSH_PKT_BUF_SIZE: the 4-byte packet_length, the
// 1-byte padding_length, the effective payload, worst-case padding, and the largest MAC tag. When
// s2c compression is built in, the "effective payload" is the compressor's worst-case output
// (ssh_deflate_bound of a full payload) since fixed-Huffman can slightly expand incompressible data.
// Callers MUST size the wire buffer with this so a compressed packet never overflows and desyncs the
// stateful cipher / compression stream (a dropped packet mid-stream would corrupt the session).
#if PROTOCORE_ENABLE_SSH_ZLIB
#define SSH_MAX_EFFECTIVE_PAYLOAD (2 + SSH_RFC_MAX_PAYLOAD + (SSH_RFC_MAX_PAYLOAD >> 3) + 32) // = ssh_deflate_bound()
#else
#define SSH_MAX_EFFECTIVE_PAYLOAD (SSH_RFC_MAX_PAYLOAD)
#endif
#define SSH_MAX_PAD 32 // worst-case padding across block-8 / block-16 modes (min-4 rule)
#define SSH_MAX_MAC 64 // largest MAC tag (hmac-sha2-512); chacha's Poly1305 tag is 16
#define SSH_PKT_WIRE_MAX ((size_t)(4 + 1 + SSH_MAX_EFFECTIVE_PAYLOAD + SSH_MAX_PAD + SSH_MAX_MAC))

/**
 * @brief Uncompressed payload RFC 4253 sec 6.1 requires an implementation to process.
 *
 * "All implementations MUST be able to process packets with an uncompressed payload length of
 * 32768 bytes or less". This tree sizes its own spans on this rather than on SSH_PKT_BUF_SIZE,
 * which the pre-move tree still shares.
 */
#define SSH_RFC_MAX_PAYLOAD 32768u

/** @brief Largest total packet RFC 4253 sec 6.1 requires an implementation to process. */
#define SSH_RFC_MAX_PACKET 35000u

// Two framed packets. ssh_pkt_emit() appends at tx_len when a packet is framed and not yet drained,
// so a pair (KEXDH_REPLY then NEWKEYS at the kex boundary) leaves on one drain instead of the second
// being refused. The payload is written at SSH_WIRE_PAYLOAD_OFF and framed in place by
// ssh_pkt_send_at(), so each packet costs its own bytes and no copy of them.
#define SSH_WIRE_CAP ((size_t)131072u)
static_assert(SSH_WIRE_CAP >= 2u * SSH_PKT_WIRE_MAX, "the wire span must frame two of this end's largest packets");
static_assert(SSH_WIRE_CAP >= 2u * SSH_RFC_MAX_PACKET,
              "the wire span must hold two of the 35000-byte packets RFC 4253 sec 6.1 requires processing");
static_assert((SSH_WIRE_CAP & (SSH_WIRE_CAP - 1u)) == 0u, "SSH_WIRE_CAP must stay a power of two");

// Scratch the transport layer (RFC 4253) borrows to frame one packet, and nothing more - the wire
// buffer and the payload being framed belong to whoever called in, because this layer is the framer,
// not the wire. The receive side is the peak: a plaintext scratch (largest across the cipher modes)
// is live while the payload is decompressed into a second buffer. The send side borrows only the
// compressor's output bound. RFC 4251 sec 1 stacks auth and connection on top of this, so the arena
// sums the layers; it does not fold them into each other.
#if PROTOCORE_ENABLE_SSH_ZLIB
#define PROTOCORE_PLAINTEXT_WORK_SSH_TRANSPORT ((size_t)(SSH_PKT_BUF_SIZE + 64 + SSH_PKT_BUF_SIZE))
#else
#define PROTOCORE_PLAINTEXT_WORK_SSH_TRANSPORT ((size_t)(SSH_PKT_BUF_SIZE + 64))
#endif

// The secure-pool term the connection declares against PROTOCORE_SECURE_ARENA_SIZE, proved against what
// is actually borrowed. The wire is not borrowed: it is the slot's own span at SSH_OFF_WIRE, framed in
// place by ssh_pkt_send_at(). What remains on the pool is a payload-sized transient.
static_assert(PROTOCORE_WORK_SSH_CONN >= (size_t)SSH_PKT_BUF_SIZE,
              "PROTOCORE_WORK_SSH_CONN must cover one transient payload: raise it in protocore_config.h");

// PROTOCORE_SSH_CPUB_MAX is sized in protocore_config.h, which cannot see the PQC key sizes. This is the
// translation unit that includes both, so it is where the two spellings are checked against it.
#if PROTOCORE_ENABLE_PQC_KEX
static_assert(PROTOCORE_SSH_CPUB_MAX >= MLKEM768_EK_BYTES + 32u,
              "PROTOCORE_SSH_CPUB_MAX must cover an ML-KEM-768 C_INIT: raise it in protocore_config.h");
#endif
#if PROTOCORE_ENABLE_SSH_SNTRUP761
static_assert(PROTOCORE_SSH_CPUB_MAX >= PROTOCORE_SNTRUP761_PK_BYTES + 32u,
              "PROTOCORE_SSH_CPUB_MAX must cover an sntrup761 C_INIT: raise it in protocore_config.h");
#endif

// The library's own caller is the connection: every KDF here runs out of slot i's crypto_work.
static_assert(PROTOCORE_CRYPTO_BORROW_MAX >= PROTOCORE_SSH_KDF_BORROW,
              "a slot's crypto_work must cover the RFC 4253 sec 7.2 KDF: raise PROTOCORE_CRYPTO_BORROW_MAX");

/**
 * @brief Where a payload sits inside a wire buffer: past packet_length and padding_length.
 *
 * Every cipher mode lays those two fields down ahead of the payload and encrypts from there, so a
 * caller that writes its message at this offset hands ssh_pkt_send_at() a packet the framer never
 * has to move. A pipe (forwarding, a channel data pump) reads its source straight into that slot and
 * the bytes are copied once, into the packet they leave in.
 */
#define SSH_WIRE_PAYLOAD_OFF 5

// ---------------------------------------------------------------------------
// RFC 4251 sec 5 - bounded reader over a payload
// ---------------------------------------------------------------------------

// Reader over a payload with bounds checking.
typedef struct
{
    const uint8_t *buf;
    size_t len;
    size_t off;
    proto_bool ok;
} Rd;
PROTOCORE_INLINE uint8_t protocore_ssh_rd_u8(Rd *r)
{
    if (r->off + 1 > r->len)
    {
        r->ok = PROTO_FALSE;
        return 0;
    }
    return r->buf[r->off++];
}
PROTOCORE_INLINE uint32_t protocore_ssh_rd_u32(Rd *r)
{
    uint32_t v = 0;
    if (!bytes.rd_u32(r->buf, r->len, &r->off, &v))
    {
        r->ok = PROTO_FALSE;
        return 0;
    }
    return v;
}
// Returns a pointer to an in-place string of length *n; advances past it. Fails closed on overflow.
PROTOCORE_INLINE const uint8_t *protocore_ssh_rd_string(Rd *r, uint32_t *n)
{
    const uint8_t *p = NULL;
    if (!bytes.rd_str(r->buf, r->len, &r->off, &p, n))
    {
        r->ok = PROTO_FALSE;
        *n = 0;
        return NULL;
    }
    return p;
}

// ---------------------------------------------------------------------------
// Wire helpers
// ---------------------------------------------------------------------------

// Copy an SSH string into a fixed buffer and null-terminate it. Advances *off.
// Returns false on truncation or if the string does not fit (buffer too small).
//
// Reading the field by reference is bytes.rd_str()'s job; this only adds the copy and the terminator,
// which is what separates it from the by-reference reads below.
static proto_bool read_string(const uint8_t *p, size_t len, size_t *off, char *out, size_t outcap)
{
    size_t start = *off;
    const uint8_t *s = NULL;
    uint32_t n = 0;
    if (!bytes.rd_str(p, len, off, &s, &n))
    {
        return PROTO_FALSE;
    }
    if (n >= outcap)
    {
        *off = start;       // same contract as bytes.rd_str: a failed read leaves the offset on its own field
        return PROTO_FALSE; // does not fit our fixed buffer
    }
    mem.cpy(out, s, n);
    out[n] = '\0';
    return PROTO_TRUE;
}

PROTOCORE_END_DECLS

#endif // PROTOCORE_SSH_COMMON_H
