// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

/** @brief RFC 4254 sec 5: which pool a handle indexes, and the channel it carries. */
typedef struct
{
    SshStreamKind kind; ///< which pool that handle indexes
    uint32_t channel;   ///< the channel a bridge call names
} SshStreamRef;

/** @brief The message bytes an emit or a write carries. */
typedef struct
{
    const uint8_t *payload; ///< the message bytes an emit or a write carries
    size_t len;             ///< how many
    size_t plen;            ///< the payload length already built in the region
} SshNetMsgArgs;

/** @brief RFC 4254 sec 7.2: where a bridged channel dials, and the client slot it takes. */
typedef struct
{
    const char *host;    ///< the destination a channel dials
    uint16_t port;       ///< its port
    uint32_t timeout_ms; ///< what that open is given
    int cid;             ///< the client slot a channel adopts, or the one a lookup names
} SshChanDialArgs;

/** @brief Where a channel read writes. */
typedef struct
{
    uint8_t *out; ///< where a channel read writes
    size_t cap;   ///< how much room it has; a region lookup reports its own here
} SshChanReadArgs;

/**
 * @brief The binding between an SSH slot and the byte stream underneath it.
 *
 * RFC 4253 sec 4: "SSH works over any 8-bit clean, binary-transparent transport." Nothing here is
 * one of the three components of RFC 4251 sec 1; this is the stream they run on, and the roles that
 * drive them (server sec 4.1, client sec 4) live above it.
 *
 * A caller sets the members a call takes, invokes it through ::SshNetwork, and reads the outcome off
 * the same handle.
 *
 * @var SshNetworkNs::claim    bind an SSH slot to a stream: the handle and which pool it indexes
 * @var SshNetworkNs::release  mark an SSH slot unowned
 * @var SshNetworkNs::slot_free  lowest unowned SSH slot, or 0xFF when the pool is full
 * @var SshNetworkNs::owns     true when @c ssh_slot is bound to @c conn_slot
 * @var SshNetworkNs::tx_drain put the packet the codec flagged on the wire, as the window allows
 * @var SshNetworkNs::emit     frame one SSH message and hand it to the slot's worker
 * @var SshNetworkNs::write_msg      frame one built SSH message and write it now
 * @var SshNetworkNs::payload_region the span a message may be built in for a frame without a copy
 * @var SshNetworkNs::write_msg_at   frame what is already in that span and write it
 *
 * @var SshNetworkNs::ssh_slot    the SSH slot a call acts on
 * @var SshNetworkNs::conn_slot   the stream slot it is bound to
 * @var SshNetworkNs::handle      the stream handle a claim binds
 * @var SshNetworkNs::stream      which pool that handle indexes, and the channel it carries
 * @var SshNetworkNs::msg         the message bytes an emit or a write carries
 * @var SshNetworkNs::dial        where a bridged channel dials
 * @var SshNetworkNs::read_args   where a channel read writes
 * @var SshNetworkNs::ok          a call's true/false outcome
 * @var SshNetworkNs::i32         a call's signed outcome
 * @var SshNetworkNs::u8          the lowest unowned slot, or 0xFF when the pool is full
 * @var SshNetworkNs::n           a byte count a call reports
 * @var SshNetworkNs::region      the span a message may be built in for a frame without a copy
 */

typedef struct
{
    uint8_t ssh_slot;  ///< the SSH slot a call acts on
    uint8_t conn_slot; ///< the stream slot it is bound to
    int handle;        ///< the stream handle a claim binds

    SshStreamRef stream;       ///< which pool that handle indexes, and the channel it carries
    SshNetMsgArgs msg;         ///< the message bytes an emit or a write carries
    SshChanDialArgs dial;      ///< where a bridged channel dials
    SshChanReadArgs read_args; ///< where a channel read writes

    proto_bool ok;
    int i32;
    uint8_t u8;
    size_t n;
    uint8_t *region;

    void (*const claim)(uint8_t *restrict work);
    void (*const release)(uint8_t *restrict work);
    void (*const slot_free)(uint8_t *restrict work);
    void (*const owns)(uint8_t *restrict work);
    void (*const tx_drain)(uint8_t *restrict work);
    void (*const emit)(uint8_t *restrict work);
    void (*const write_msg)(uint8_t *restrict work);
    void (*const payload_region)(uint8_t *restrict work);
    void (*const write_msg_at)(uint8_t *restrict work);
#if PROTOCORE_NEED_CLIENT
    // Bridging a channel to a socket of our own needs the client half of the transport, so these
    // exist exactly when TcpClient does.
    void (*const chan_open)(uint8_t *restrict work);
    void (*const chan_adopt)(uint8_t *restrict work);
    void (*const chan_by_cid)(uint8_t *restrict work);
    void (*const chan_write)(uint8_t *restrict work);
    void (*const chan_read)(uint8_t *restrict work);
    void (*const chan_avail)(uint8_t *restrict work);
    void (*const chan_drained)(uint8_t *restrict work);
    void (*const chan_close)(uint8_t *restrict work);
    void (*const chan_close_all)(uint8_t *restrict work);
#endif // PROTOCORE_NEED_CLIENT
} SshNetworkNs;

/** @brief The one instance, defined in network.c. */
extern SshNetworkNs SshNetwork;

/**
 * @brief The PROTOCORE_SSH_NETWORK_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span, or NULL while the pool was short - which every entry refuses.
 */
uint8_t *protocore_ssh_network_span(void);

/** @brief Put the server identification string on the wire, raw, before any binary packet. */
void ssh_net_version_exchange_send(uint8_t i, uint8_t conn_slot);

PROTOCORE_END_DECLS

#endif // PROTOCORE_NETWORK_NETWORK_H
