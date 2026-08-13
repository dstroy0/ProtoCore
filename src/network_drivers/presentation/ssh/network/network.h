// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file network.h
 * @brief Decoupled stream I/O: ring buffer in, framed bytes out, one SSH slot per socket.
 */

#ifndef PROTOCORE_NETWORK_NETWORK_H
#define PROTOCORE_NETWORK_NETWORK_H

#include "network_drivers/presentation/ssh/common.h"

PROTOCORE_BEGIN_DECLS

struct ProtoHandler;

/**
 * @brief Which transport a slot's byte stream belongs to.
 *
 * RFC 4253 sec 4: "SSH works over any 8-bit clean, binary-transparent transport", and sec 4 gives
 * the two ends different ones - the listening role's stream is a socket this end accepted, the
 * initiating role's is one it dialled. They are separate pools with separate handle spaces, so the
 * handle alone does not say which array it indexes.
 */
typedef enum PROTO_ENUM_PACKED
{
    SSH_STREAM_ACCEPTED = 0, ///< inbound: the handle indexes conn_pool[]
    SSH_STREAM_DIALED        ///< outbound: the handle indexes the client transport's own pool
} SshStreamKind;

/**
 * @brief The binding between an SSH slot and the byte stream underneath it.
 *
 * RFC 4253 sec 4: "SSH works over any 8-bit clean, binary-transparent transport." Nothing here is
 * one of the three components of RFC 4251 sec 1; this is the stream they run on, and the roles that
 * drive them (server sec 4.1, client sec 4) live above it.
 *
 * @var SshNetworkNs::claim    bind an SSH slot to a stream: the handle and which pool it indexes
 * @var SshNetworkNs::release  mark an SSH slot unowned
 * @var SshNetworkNs::slot_free  lowest unowned SSH slot, or 0xFF when the pool is full
 * @var SshNetworkNs::owns     true when @p ssh_slot is bound to @p conn_slot
 * @var SshNetworkNs::tx_drain put the packet the codec flagged on the wire, as the window allows
 * @var SshNetworkNs::emit     frame one SSH message and hand it to the slot's worker
 * @var SshNetworkNs::write_msg      frame one built SSH message and write it now
 * @var SshNetworkNs::payload_region the span a message may be built in for a frame without a copy
 * @var SshNetworkNs::write_msg_at   frame what is already in that span and write it
 */
typedef struct
{
    int (*claim)(uint8_t ssh_slot, int handle, SshStreamKind kind);
    void (*release)(uint8_t ssh_slot);
    uint8_t (*slot_free)(void);
    proto_bool (*owns)(uint8_t ssh_slot, uint8_t conn_slot);
    void (*tx_drain)(uint8_t conn_slot, uint8_t ssh_slot);
    void (*emit)(uint8_t i, const uint8_t *payload, size_t len);
    int (*write_msg)(uint8_t ssh_slot, const uint8_t *msg, size_t len);
    uint8_t *(*payload_region)(uint8_t ssh_slot, size_t *cap);
    int (*write_msg_at)(uint8_t ssh_slot, size_t plen);
#if PROTOCORE_NEED_CLIENT
    // Bridging a channel to a socket of our own needs the client half of the transport, so these
    // exist exactly when Tcp.client does.
    int (*chan_open)(uint8_t ssh_slot, uint32_t channel, const char *host, uint16_t port, uint32_t timeout_ms);
    int (*chan_adopt)(uint8_t ssh_slot, uint32_t channel, int cid);
    proto_bool (*chan_by_cid)(int cid, uint8_t *ssh_slot, uint32_t *channel);
    int (*chan_write)(uint8_t ssh_slot, uint32_t channel, const uint8_t *data, size_t len);
    size_t (*chan_read)(uint8_t ssh_slot, uint32_t channel, uint8_t *out, size_t cap);
    size_t (*chan_avail)(uint8_t ssh_slot, uint32_t channel);
    proto_bool (*chan_drained)(uint8_t ssh_slot, uint32_t channel);
    void (*chan_close)(uint8_t ssh_slot, uint32_t channel);
    void (*chan_close_all)(uint8_t ssh_slot);
#endif // PROTOCORE_NEED_CLIENT
} SshNetworkNs;

/** @brief The one instance, defined in network.c. */
extern const SshNetworkNs SshNetwork;

/** @brief Put the server identification string on the wire, raw, before any binary packet. */
void ssh_net_version_exchange_send(uint8_t i, uint8_t conn_slot);

PROTOCORE_END_DECLS

#endif // PROTOCORE_NETWORK_NETWORK_H
