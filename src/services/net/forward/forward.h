// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file forward.h
 * @brief Interface forwarding plane (PC_ENABLE_FORWARD) - the v5 bridge / router.
 *
 * A forwarding plane over the ingest pipeline. You register **interfaces** (Wi-Fi STA /
 * AP, Ethernet, a peripheral bus, a radio), each with an egress **send callback**, then
 * add per-pair **rules** (`src -> dst`, allow or deny, with an optional rate cap). When a
 * frame arrives on an interface you call pc_forward_ingress(); the plane evaluates the
 * rules and forwards the bytes to **every allowed destination** by calling that
 * destination's send callback - so the device bridges / routes between its interfaces
 * instead of only terminating traffic.
 *
 * The canonical wiring is DMA-driven: an inbound DMA-complete event (services/system/dma) is
 * posted onto the FORWARD lane (services/system/preempt_queue), whose task calls
 * pc_forward_ingress(), and each destination's send callback hands the bytes to that
 * interface's egress DMA. The plane itself is decoupled from both - it only knows
 * interfaces, rules, and the send callbacks - so it is pure and host-testable.
 *
 * **Default-deny**: a `(src, dst)` pair is forwarded only when an ALLOW rule matches and
 * no DENY rule does (a DENY always wins). A frame is never reflected to its source
 * interface. **Fail-closed**: an exceeded rate cap or a send callback returning false
 * drops the frame for that destination and is counted - it never blocks. Storage is
 * static (zero heap): PC_FWD_MAX_IFACES interfaces, PC_FWD_MAX_RULES rules.
 *
 * **Policy routing** (route-by-tag): a policy route (pc_forward_route_add) matches a frame by
 * the same byte-pattern primitive as the ACL - so it keys on any field at a known offset
 * (EtherType, IP protocol, a port, an address prefix) - and binds the match to a single
 * **egress interface**. A matched frame is forwarded only to that interface, taking precedence
 * over the src->dst fan-out (first matching route wins); if no policy route matches, the normal
 * rules apply. This is policy-based routing layered on the plane: tagged traffic leaves a chosen
 * NIC / radio. The ingress ACL still runs first, and the same rate-cap / never-reflect /
 * fail-closed guarantees apply to the chosen egress.
 *
 * **Inspection hook** (PC_FWD_INSPECT, off by default for cost + privacy): when built in, an
 * app can register an inspector (pc_forward_set_inspector) that runs on every ingress frame
 * after the ACL and before routing - to observe / parse / meter, and optionally drop it. It is a
 * flexible app callback (arbitrary logic), complementing the fast fixed-offset ACL.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_FORWARD_H
#define PROTOCORE_FORWARD_H

#include "protocore_config.h"

#if PC_ENABLE_FORWARD

/** @brief Interface kind (informational; the plane treats all interfaces the same). */
typedef enum PROTO_ENUM_PACKED
{
    PC_IF_OTHER = 0,
    PC_IF_WIFI_STA,
    PC_IF_WIFI_AP,
    PC_IF_ETH,
    PC_IF_BUS,
    PC_IF_RADIO,
} pc_if_kind;

/** @brief Rule action for a `(src, dst)` interface pair or an ACL entry. */
typedef enum PROTO_ENUM_PACKED
{
    PC_FWD_DENY = 0,
    PC_FWD_ALLOW = 1,
} pc_fwd_action;

/** @brief Wildcard source interface for an ACL entry (matches a frame from any source). */
#define PC_FWD_IF_ANY 0xFF

/**
 * @brief Egress: emit @p len bytes on interface @p if_id.
 * @return true if the interface accepted the bytes; false drops (counted as a send fail).
 */
using pc_if_send_fn = proto_bool (*)(uint8_t if_id, const uint8_t *data, uint16_t len, void *ctx);

/** @brief Forwarding counters (monotonic since the last pc_forward_reset()). */
typedef struct
{
    uint32_t frames_in;       ///< ingress calls
    uint32_t forwarded;       ///< destination sends that succeeded
    uint32_t blocked;         ///< destinations refused by a DENY / default-deny
    uint32_t rate_dropped;    ///< destinations dropped by a rate cap
    uint32_t send_fail;       ///< destination send callbacks that returned false
    uint32_t acl_denied;      ///< frames dropped at ingress by the access-control list
    uint32_t policy_routed;   ///< frames that matched a policy route (routed to its chosen egress)
    uint32_t inspect_dropped; ///< frames dropped by the inspection hook (PC_FWD_INSPECT)
} pc_forward_stats;

/** @brief Clear all interfaces, rules, and stats (start from empty). */
void pc_forward_reset(void);

/**
 * @brief Register an interface and its egress send callback.
 * @return true; false if @p if_id is already registered, @p send is null, or the table
 *         is full (PC_FWD_MAX_IFACES).
 */
proto_bool pc_forward_add_if(uint8_t if_id, pc_if_kind kind, pc_if_send_fn send, void *ctx);

/**
 * @brief Add a forwarding rule. @p rate_cap_per_sec caps ALLOW rules (0 = unlimited); it
 *        is ignored for DENY rules.
 * @return true; false if the table is full (PC_FWD_MAX_RULES).
 */
proto_bool pc_forward_add_rule(uint8_t src_if, uint8_t dst_if, pc_fwd_action action, uint16_t rate_cap_per_sec);

/**
 * @brief Set the ACL default action - what happens to a frame that matches no ACL entry.
 *        Default PC_FWD_ALLOW (an empty ACL passes everything, so the ACL is opt-in);
 *        set PC_FWD_DENY for allowlist semantics (only explicitly permitted frames pass).
 */
void pc_forward_acl_set_default(pc_fwd_action action);

/**
 * @brief Add an ingress access-control entry (evaluated in add order, first match wins).
 *
 * A frame is matched when it arrived on @p src_if (or PC_FWD_IF_ANY) and its bytes at
 * `[offset, offset + patlen)` equal @p pattern under @p mask (each `byte & mask == pattern`).
 * @p patlen 0 matches any content (interface-only entry); a frame shorter than
 * `offset + patlen` does not match the entry (evaluation continues). The first matching
 * entry's @p action (permit/deny) decides; if none match, the ACL default applies.
 * Denied frames are dropped at ingress before any forwarding rule runs.
 * @return false if the ACL table is full or @p patlen exceeds PC_FWD_ACL_PATLEN.
 */
proto_bool pc_forward_acl_add(uint8_t src_if, uint16_t offset, const uint8_t *pattern, const uint8_t *mask,
                              uint8_t patlen, pc_fwd_action action);

/**
 * @brief Add a policy route: a frame matching this byte pattern is forwarded only to
 *        @p egress_if, taking precedence over the src->dst rules (first matching route wins).
 *
 * The match is the same offset/pattern/mask primitive as the ACL (each `byte & mask == pattern`
 * at `[offset, offset + patlen)`), keyed on frames from @p src_if or PC_FWD_IF_ANY; @p patlen 0
 * matches any content (a default route). On a match the frame goes only to @p egress_if -
 * subject to the same guarantees as a rule: never reflected to the source, dropped (counted) if
 * @p egress_if is not registered, if @p rate_cap_per_sec (0 = unlimited) is exceeded, or if the
 * egress send fails. A matched route ends the decision (the normal fan-out is skipped).
 *
 * @return true; false if @p patlen exceeds PC_FWD_ACL_PATLEN, a non-zero @p patlen has a null
 *         pattern/mask, or the table is full (PC_FWD_MAX_ROUTES).
 */
proto_bool pc_forward_route_add(uint8_t src_if, uint16_t offset, const uint8_t *pattern, const uint8_t *mask,
                                uint8_t patlen, uint8_t egress_if, uint16_t rate_cap_per_sec);

#if PC_FWD_INSPECT
/** @brief The verdict an inspection hook returns for a frame. */
typedef enum PROTO_ENUM_PACKED
{
    PC_FWD_INSPECT_PASS = 0, ///< let the frame continue to routing / forwarding
    PC_FWD_INSPECT_DROP = 1, ///< drop the frame (counted as inspect_dropped)
} pc_fwd_verdict;

/**
 * @brief Ingress inspection hook: observe / parse @p data (from @p src_if, @p len bytes) and
 *        return a ::pc_fwd_verdict. Runs after the ACL and before policy routes / the fan-out.
 *        The callback must not block; it may record metrics, log, or decide to drop.
 */
using pc_fwd_inspect_fn = pc_fwd_verdict (*)(uint8_t src_if, const uint8_t *data, uint16_t len, void *ctx);

/**
 * @brief Install (or clear, with @p fn null) the ingress inspection hook.
 *
 * Opt-in twice over: compiled in only when PC_FWD_INSPECT is set (cost + privacy), and inert
 * until an inspector is registered. A DROP verdict discards the frame before any forwarding.
 */
void pc_forward_set_inspector(pc_fwd_inspect_fn fn, void *ctx);
#endif

/**
 * @brief Forward a frame that arrived on @p src_if to every allowed destination.
 *
 * Evaluates the rules against each registered interface (never the source), applies the
 * per-rule rate cap, and calls the destination's send callback. Fail-closed.
 * @return the number of destinations the frame was successfully forwarded to.
 */
uint8_t pc_forward_ingress(uint8_t src_if, const uint8_t *data, uint16_t len);

/** @brief Copy the current forwarding counters into @p out. */
void pc_forward_get_stats(pc_forward_stats *out);

#if !PROTOCORE_HOT
/** @brief Host only: set the millisecond clock the rate cap uses (tests drive the window).
 *         On device the plane reads pc_millis(). */
void pc_forward_test_set_now(uint32_t ms);
#endif

#endif // PC_ENABLE_FORWARD

#endif // PROTOCORE_FORWARD_H
