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

/** @brief One channel's flow-control state (RFC 4254 sec 5.2). */
typedef struct
{
    uint32_t local_window; ///< Bytes the peer may still send us before we WINDOW_ADJUST.
    uint32_t local_max;    ///< The window we advertised, and replenish back up to.
    uint32_t peer_window;  ///< Bytes we may still send the peer.
    uint32_t peer_max_pkt; ///< Peer's maximum packet size; caps a single send independently of the window.
} SshFlow;

/**
 * @brief Start a channel's windows: ours at @p local_window, the peer's at what it advertised.
 *
 * The local window is a parameter rather than a baked-in constant because the server and the
 * client advertise different sizes, and the value we replenish to must be the value we told the
 * peer about. Passing it here keeps the two from drifting apart.
 *
 * @param local_window  what we advertise in CHANNEL_OPEN / CONFIRMATION; also the replenish target.
 * @param peer_window   the peer's initial window from CHANNEL_OPEN / CONFIRMATION.
 * @param peer_max_pkt  the peer's maximum packet size from the same message.
 */
void pc_ssh_flow_init(SshFlow *f, uint32_t local_window, uint32_t peer_window, uint32_t peer_max_pkt);

/**
 * @brief Account @p n inbound bytes against our window.
 *
 * @return false if @p n exceeds what we advertised - the peer overran the window (RFC 4254 sec 5.2)
 *         and the caller must fail the channel. The window is left untouched on failure.
 */
proto_bool pc_ssh_flow_recv_take(SshFlow *f, uint32_t n);

/**
 * @brief Decide whether a WINDOW_ADJUST is due, and for how much. Does not mutate.
 *
 * Replenishes once the window has drained past half, which keeps a bulk transfer from stalling
 * without emitting an adjust per packet.
 *
 * Pair it with pc_ssh_flow_local_credit() only after the adjust has actually gone out. Deciding and
 * crediting are separate because on some paths the send can fail, and crediting first would leave us
 * believing we advertised bytes the peer never heard about - the peer then stops at its smaller
 * window while we wait for data, and the transfer deadlocks.
 *
 * @return true if a WINDOW_ADJUST is due; @p *add receives the delta to advertise.
 */
proto_bool pc_ssh_flow_replenish_due(const SshFlow *f, uint32_t *add);

/** @brief Credit our window by @p add, once that WINDOW_ADJUST has actually been sent. */
void pc_ssh_flow_local_credit(SshFlow *f, uint32_t add);

/** @brief True if @p len bytes fit both the peer's remaining window and its maximum packet size. */
proto_bool pc_ssh_flow_send_allows(const SshFlow *f, size_t len);

/**
 * @brief Clamp a would-be send to what the peer currently permits.
 *
 * A producer that pulls from a local source sizes its read to this, so it never reads bytes it
 * cannot legally forward. Returns 0 when the window is closed, which the caller treats as
 * "stop pumping until a WINDOW_ADJUST arrives".
 *
 * @return min(@p want, peer window, peer maximum packet size).
 */
uint32_t pc_ssh_flow_send_cap(const SshFlow *f, uint32_t want);

/** @brief Account @p n outbound bytes against the peer's window (call only after send_allows()). */
void pc_ssh_flow_send_take(SshFlow *f, uint32_t n);

/**
 * @brief Credit the peer's window from an inbound WINDOW_ADJUST.
 *
 * Saturates at UINT32_MAX rather than wrapping: a peer advertising a total past 2^32 is out of spec,
 * and wrapping would hand us a tiny window and stall the transfer.
 */
void pc_ssh_flow_peer_add(SshFlow *f, uint32_t add);

/** @brief Bytes we may still send the peer - the bound a producer sizes its next read to. */
uint32_t pc_ssh_flow_peer_window(const SshFlow *f);

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

/** @brief CHANNEL_OPEN_FAILURE. @p reason: 1 admin-prohibited, 2 connect-failed, 3 unknown-type, 4 resource. */
int32_t pc_ssh_sig_build_open_failure(uint8_t *out, size_t cap, uint32_t peer_id, uint32_t reason, size_t *out_len);

/** @brief CHANNEL_OPEN_CONFIRMATION, advertising our current window and maximum packet size. */
int32_t pc_ssh_sig_build_open_confirm(const SshFlow *f, uint32_t peer_id, uint32_t local_id, uint8_t *out, size_t cap,
                                      size_t *out_len);

/**
 * @brief CHANNEL_DATA carrying @p len bytes, and account them against the peer's window.
 *
 * Refuses when the send would exceed the peer's window or its maximum packet size, so the RFC 4254
 * sec 5.2 limit cannot be violated by any caller - the check and the debit are one step here.
 */
int32_t pc_ssh_sig_build_data(SshFlow *f, uint32_t peer_id, const uint8_t *data, size_t len, uint8_t *out, size_t cap,
                              size_t *out_len);

/** @brief CHANNEL_WINDOW_ADJUST granting @p add more bytes. Credit the window only once this is sent. */
int32_t pc_ssh_sig_build_window_adjust(uint32_t peer_id, uint32_t add, uint8_t *out, size_t cap, size_t *out_len);

/** @brief CHANNEL_EOF followed by CHANNEL_CLOSE, as one 10-byte pair. */
int32_t pc_ssh_sig_build_close(uint32_t peer_id, uint8_t *out, size_t cap, size_t *out_len);

#endif // PROTOCORE_SSH_FLOW_CONTROL_H
