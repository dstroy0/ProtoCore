// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_conn.h
 * @brief Glue between the TCP transport (conn_pool) and the SSH protocol stack.
 *
 * Binds a ProtoConn::PROTO_SSH TcpConn slot to an SSH session slot, pumps ring-buffer
 * bytes through the banner exchange and binary-packet layer, and writes the
 * dispatcher's outbound packets back to the socket. This is the integration
 * layer the session loop calls for ProtoConn::PROTO_SSH connections.
 */

#ifndef PROTOCORE_SSH_CONN_H
#define PROTOCORE_SSH_CONN_H

#include <stddef.h>
#include <stdint.h>

#include "network_drivers/presentation/ssh/transport/ssh_keymat.h"
#include "network_drivers/presentation/ssh/transport/ssh_packet.h"
#include "network_drivers/presentation/ssh/transport/ssh_transport.h"

/**
 * @brief The connection's memory map: every byte it uses, at a named offset from its slot base.
 *
 * Laid out kmt | constants | control packet | data packet, each offset the previous one plus that
 * member's size. The storage is the connection's, in ssh_conn.c; a translation unit takes its
 * pointer as @c ssh_conn_slot(i) + the offset and already knows the member's size.
 */

// One key epoch: the six RFC 4253 sec 7.2 keys in every cipher mode negotiation can pick, at
// offsets from the epoch's own base. Both epochs are laid out this way.
#define SSH_OFF_GCM_C2S 0u
#define SSH_OFF_GCM_S2C (SSH_OFF_GCM_C2S + PC_WORK_AESGCM)
#define SSH_OFF_CHACHA_C2S (SSH_OFF_GCM_S2C + PC_WORK_AESGCM)
#define SSH_OFF_CHACHA_S2C (SSH_OFF_CHACHA_C2S + PC_CHACHAPOLY_KEY_LEN)
#define SSH_OFF_MAC_C2S (SSH_OFF_CHACHA_S2C + PC_CHACHAPOLY_KEY_LEN)
#define SSH_OFF_MAC_S2C (SSH_OFF_MAC_C2S + 64u)
#define SSH_OFF_AES_KEY_C2S (SSH_OFF_MAC_S2C + 64u)
#define SSH_OFF_AES_KEY_S2C (SSH_OFF_AES_KEY_C2S + PC_AES256CTR_KEY_LEN)
#define SSH_OFF_AES_IV_C2S (SSH_OFF_AES_KEY_S2C + PC_AES256CTR_KEY_LEN)
#define SSH_OFF_AES_IV_S2C (SSH_OFF_AES_IV_C2S + PC_AES256CTR_CTR_LEN)
#define SSH_EPOCH_STRIDE (SSH_OFF_AES_IV_S2C + PC_AES256CTR_CTR_LEN)

// kmt: two key epochs, then the DH ephemeral. The second epoch holds the keys a re-key derives
// while the first still decrypts, until both directions have switched (RFC 4253 sec 7.3).
#define SSH_OFF_EPOCH_0 0u
#define SSH_OFF_EPOCH_1 (SSH_OFF_EPOCH_0 + SSH_EPOCH_STRIDE)
#define SSH_OFF_DH_Y (SSH_OFF_EPOCH_1 + SSH_EPOCH_STRIDE)
#define SSH_OFF_DH_F (SSH_OFF_DH_Y + sizeof(pc_bignum))
#define SSH_OFF_DH_K (SSH_OFF_DH_F + sizeof(pc_bignum))

// constants: the values that persist across messages to compute the exchange hash H, and the
// session id the first KEX fixes (RFC 4253 sec 7.2).
#define SSH_OFF_V_C (SSH_OFF_DH_K + sizeof(pc_bignum))
#define SSH_OFF_V_S (SSH_OFF_V_C + SSH_VERSION_MAX)
#define SSH_OFF_BANNER (SSH_OFF_V_S + SSH_VERSION_MAX)
#define SSH_OFF_I_C (SSH_OFF_BANNER + SSH_VERSION_MAX)
#define SSH_OFF_I_S (SSH_OFF_I_C + SSH_KEXINIT_MAX)
#define SSH_OFF_SESSION_ID (SSH_OFF_I_S + PC_SSH_KEXINIT_S_MAX)
#define SSH_OFF_ECDH_SK (SSH_OFF_SESSION_ID + SSH_KEXHASH_MAX_LEN)
#define SSH_OFF_ECDH_PK (SSH_OFF_ECDH_SK + 32u)

// control packet: the wire buffer the codec frames into, the bytes the packet MAC works out of,
// and the ones the key exchange does.
#define SSH_OFF_WIRE (SSH_OFF_ECDH_PK + 32u)
#define SSH_OFF_MAC_WORK (SSH_OFF_WIRE + SSH_WIRE_CAP)
#define SSH_OFF_CRYPTO_WORK (SSH_OFF_MAC_WORK + PC_HMAC_SHA256_BORROW)

// data packet: the bytes drained off the transport ring, then the reassembly they feed.
#define SSH_OFF_RX_READ (SSH_OFF_CRYPTO_WORK + PC_CRYPTO_BORROW_MAX)
#define SSH_OFF_RX_ASM (SSH_OFF_RX_READ + RX_BUF_SIZE)

/** @brief One connection's whole span, and the stride between slots. */
#define SSH_SLOT_BORROW (SSH_OFF_RX_ASM + (size_t)SSH_PKT_BUF_SIZE)

/**
 * @brief The base of slot @p i's storage, or null when @p i is out of range.
 *
 * Allocated at compile time and identical for every slot, so a caller adds the offset it needs.
 */
uint8_t *ssh_conn_slot(uint8_t i);

/**
 * @brief One-time setup: install the dispatcher's binary-packet emit callback.
 *
 * Called from ssh_proto_handler() (the accessor every consumer uses to install SSH),
 * so registering the handler always wires the emit callback - it can never be
 * forgotten. Idempotent. Until it runs, the dispatcher's emit callback is null and
 * every framed SSH packet after the plaintext banner is silently dropped.
 */
void pc_ssh_conn_setup();

/** @brief The SSH connection ProtoHandler (accessor; installed by the builtins list, no session dep). */
struct ProtoHandler;
const struct ProtoHandler *ssh_proto_handler(void);

/**
 * @brief Handle a new ProtoConn::PROTO_SSH connection on @p conn_slot.
 *
 * Allocates an SSH session slot, initializes the transport/packet/channel
 * state, and sends the server identification banner. If no SSH slot is free
 * the connection is aborted.
 */
void pc_ssh_conn_accept(uint8_t conn_slot);

/**
 * @brief Drain @p conn_slot's receive ring buffer through the SSH stack.
 *
 * Feeds the banner parser until the client identification string completes,
 * then the binary-packet layer; complete messages are dispatched and any
 * responses are written to the socket. Closes the connection if the protocol
 * signals a fatal condition.
 */
void pc_ssh_conn_rx(uint8_t conn_slot);

/**
 * @brief Tear down SSH state for @p conn_slot (disconnect / error).
 */
void pc_ssh_conn_close(uint8_t conn_slot);

/**
 * @brief Send application data to the client over an SSH channel.
 *
 * Frames @p data as SSH_MSG_CHANNEL_DATA on channel @p channel, encrypts+MACs it,
 * and writes it to the socket. @p ssh_slot and @p channel are the values passed to
 * the data callback registered via pc_ssh_channel_set_data_cb(). A single call sends
 * at most one channel-data message (bounded by the peer's flow-control window).
 *
 * @return Number of bytes sent, or -1 on error (bad slot, channel closed/unknown,
 *         peer window/packet limit, or no active connection).
 */
int pc_ssh_conn_send(uint8_t ssh_slot, uint32_t channel, const uint8_t *data, size_t len);

/**
 * @brief Close an SSH channel from the server side: frame CHANNEL_EOF and
 *        CHANNEL_CLOSE as two binary packets and write them to the socket.
 *
 * Used by the port-forwarding owner when the forwarded TCP peer closes.
 * @return 0 on success, -1 on error (bad slot, channel closed/unknown, no
 *         active connection, or scratch exhausted).
 */
int pc_ssh_conn_close_channel(uint8_t ssh_slot, uint32_t channel);

/**
 * @brief Open a server-initiated "forwarded-tcpip" channel to the client (ssh -R):
 *        build the CHANNEL_OPEN (RFC 4254 §7.2) via the channel codec, frame + send it
 *        on @p ssh_slot's socket, and return the new local channel id. The client's
 *        CHANNEL_OPEN_CONFIRMATION (or FAILURE) later drives the forward-confirm callback.
 *
 * @param conn_addr / conn_port  the forward's bound address/port (address connected).
 * @param orig_addr / orig_port  the peer that connected to the forwarded port (advisory).
 * @return the local channel id (>= 0), or -1 (no active connection, channel pool full,
 *         or scratch exhausted). Used by the remote-forward owner.
 */
int pc_ssh_conn_open_forwarded(uint8_t ssh_slot, const char *conn_addr, uint16_t conn_port, const char *orig_addr,
                               uint16_t orig_port);

/**
 * @brief Per-loop poll hook for an SSH connection (registered as the SSH protocol
 *        handler's on_poll). Drives the port-forwarding pump; a no-op when
 *        forwarding is compiled out.
 */
void pc_ssh_conn_poll(uint8_t conn_slot);

#endif // PROTOCORE_SSH_CONN_H
