// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ssh_flow_control.h
 * @brief SSH channel flow control - the RFC 4254 sec 5.2 window pair and nothing else.
 *
 * Every channel carries two independent counters: how many bytes the peer may still send us before
 * we replenish (local), and how many we may still send it (peer). Getting either wrong desynchronizes
 * the session - overrun the peer's window and it drops the channel, forget to replenish ours and the
 * transfer stalls forever with both sides waiting.
 *
 * The accounting lived in four places before this file existed: ssh_channel.cpp held the counters and
 * the rules, ssh_forward.cpp read `peer_window` directly to size its reads, ssh_server.cpp routed the
 * adjust message, and ssh_client.cpp carried a second implementation of the same arithmetic. One
 * concern, one owner: the counters are only correct if a single piece of code decides what they mean.
 *
 * Channel multiplexing stays in ssh_channel.* - this file knows nothing about channel ids, types, or
 * the pool. It is the arithmetic and the rules, over one channel's pair of counters.
 */

#ifndef PROTOCORE_SSH_FLOW_CONTROL_H
#define PROTOCORE_SSH_FLOW_CONTROL_H

#include "protocore_config.h" // the entry point: types.h for the widths and PC_INLINE

PROTO_BEGIN_DECLS

/** @brief One channel's flow-control state (RFC 4254 sec 5.2). */
typedef struct
{
    uint32_t local_window; ///< Bytes the peer may still send us before we WINDOW_ADJUST.
    uint32_t local_max;    ///< The window we advertised, and replenish back up to.
    uint32_t peer_window;  ///< Bytes we may still send the peer.
    uint32_t peer_max_pkt; ///< Peer's maximum packet size; caps a single send independently of the window.
} SshFlow;

// ---------------------------------------------------------------------------
// Channel signaling (RFC 4254 sec 5)
//
// Every channel-related message is a transition on the window state above: OPEN / OPEN_CONFIRMATION
// establish it (they carry the initial window and maximum packet size), WINDOW_ADJUST increments it,
// DATA consumes it, EOF / CLOSE terminate it. Because the transitions and the state are the same
// concern, they live together - the RFC's rule that no data may be sent until the window allows it is
// only enforceable where the window is.
//
// These take the flow plus the ids the wire carries, never a channel struct: resolving a recipient
// channel number to a channel is multiplexing, and that stays in ssh_channel.
// ---------------------------------------------------------------------------

/**
 * @brief Per-channel windowing (RFC 4254 sec 5.2) and the message builders that carry it. SshFlow is
 * one channel's window; this is the arithmetic over it. Owns no state of its own.
 *
 * @var SshFlowControlNs::init                 Start a channel's windows: ours at @p local_window, the peer's at
 *                                             what it advertised
 * @var SshFlowControlNs::recv_take            Account @p n inbound bytes against our window
 * @var SshFlowControlNs::replenish_due        Decide whether a WINDOW_ADJUST is due, and for how much. Does not
 *                                             mutate
 * @var SshFlowControlNs::local_credit         Credit our window by @p add, once that WINDOW_ADJUST has actually
 *                                             been sent
 * @var SshFlowControlNs::send_allows          True if @p len bytes fit both the peer's remaining window and its
 *                                             maximum packet size
 * @var SshFlowControlNs::send_cap             Clamp a would-be send to what the peer currently permits
 * @var SshFlowControlNs::send_take            Account @p n outbound bytes against the peer's window (call only
 *                                             after send_allows())
 * @var SshFlowControlNs::peer_add             Credit the peer's window from an inbound WINDOW_ADJUST
 * @var SshFlowControlNs::peer_window          Bytes we may still send the peer - the bound a producer sizes its
 *                                             next read to
 * @var SshFlowControlNs::build_open_failure   CHANNEL_OPEN_FAILURE. @p reason: 1 admin-prohibited, 2
 *                                             connect-failed, 3 unknown-type, 4 resource
 * @var SshFlowControlNs::build_open_confirm   CHANNEL_OPEN_CONFIRMATION, advertising our current window and
 *                                             maximum packet size
 * @var SshFlowControlNs::build_data           CHANNEL_DATA carrying @p len bytes, and account them against the
 *                                             peer's window
 * @var SshFlowControlNs::build_window_adjust  CHANNEL_WINDOW_ADJUST granting @p add more bytes. Credit the
 *                                             window only once this is sent
 * @var SshFlowControlNs::build_close          CHANNEL_EOF followed by CHANNEL_CLOSE, as one 10-byte pair
 */
typedef struct
{
    void (*init)(SshFlow *f, uint32_t local_window, uint32_t peer_window, uint32_t peer_max_pkt);
    proto_bool (*recv_take)(SshFlow *f, uint32_t n);
    proto_bool (*replenish_due)(const SshFlow *f, uint32_t *add);
    void (*local_credit)(SshFlow *f, uint32_t add);
    proto_bool (*send_allows)(const SshFlow *f, size_t len);
    uint32_t (*send_cap)(const SshFlow *f, uint32_t want);
    void (*send_take)(SshFlow *f, uint32_t n);
    void (*peer_add)(SshFlow *f, uint32_t add);
    uint32_t (*peer_window)(const SshFlow *f);
    int32_t (*build_open_failure)(uint8_t *out, size_t cap, uint32_t peer_id, uint32_t reason, size_t *out_len);
    int32_t (*build_open_confirm)(const SshFlow *f, uint32_t peer_id, uint32_t local_id, uint8_t *out, size_t cap,
                                  size_t *out_len);
    int32_t (*build_data)(SshFlow *f, uint32_t peer_id, const uint8_t *data, size_t len, uint8_t *out, size_t cap,
                          size_t *out_len);
    int32_t (*build_window_adjust)(uint32_t peer_id, uint32_t add, uint8_t *out, size_t cap, size_t *out_len);
    int32_t (*build_close)(uint32_t peer_id, uint8_t *out, size_t cap, size_t *out_len);
} SshFlowControlNs;

/** @brief The one symbol this module exports. */
extern const SshFlowControlNs SshFlowControl;

PROTO_END_DECLS

#endif // PROTOCORE_SSH_FLOW_CONTROL_H
