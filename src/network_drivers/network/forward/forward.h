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
 * The canonical wiring is DMA-driven: an inbound DMA-complete event (mmgr/dma) is
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

PROTO_BEGIN_DECLS

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
typedef proto_bool (*pc_if_send_fn)(uint8_t if_id, const uint8_t *data, uint16_t len, void *ctx);

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
typedef pc_fwd_verdict (*pc_fwd_inspect_fn)(uint8_t src_if, const uint8_t *data, uint16_t len, void *ctx);

#endif

/**
 * @brief The forwarding plane.
 *
 * @var ForwardNs::reset           clear every interface, rule, route and counter
 * @var ForwardNs::add_if          register an interface and its egress callback
 * @var ForwardNs::add_rule        add a (src, dst) rule with an optional rate cap
 * @var ForwardNs::acl_set_default what happens to a frame no ACL entry matches
 * @var ForwardNs::acl_add         add an ingress access-control entry, first match wins
 * @var ForwardNs::route_add       add a policy route, taking precedence over the rules
 * @var ForwardNs::set_inspector   install the ingress inspection hook
 * @var ForwardNs::ingress         forward one received frame; returns the destinations it reached
 * @var ForwardNs::get_stats       copy out the counters
 * @var ForwardNs::test_set_now    host only: drive the clock the rate cap reads
 */
typedef struct
{
    void (*reset)(void);
    proto_bool (*add_if)(uint8_t if_id, pc_if_kind kind, pc_if_send_fn send, void *ctx);
    proto_bool (*add_rule)(uint8_t src_if, uint8_t dst_if, pc_fwd_action action, uint16_t rate_cap_per_sec);
    void (*acl_set_default)(pc_fwd_action action);
    proto_bool (*acl_add)(uint8_t src_if, uint16_t offset, const uint8_t *pattern, const uint8_t *mask, uint8_t patlen,
                          pc_fwd_action action);
    proto_bool (*route_add)(uint8_t src_if, uint16_t offset, const uint8_t *pattern, const uint8_t *mask,
                            uint8_t patlen, uint8_t egress_if, uint16_t rate_cap_per_sec);
#if PC_FWD_INSPECT
    void (*set_inspector)(pc_fwd_inspect_fn fn, void *ctx);
#endif
    uint8_t (*ingress)(uint8_t src_if, const uint8_t *data, uint16_t len);
    void (*get_stats)(pc_forward_stats *out);
#if !PROTOCORE_HOT
    void (*test_set_now)(uint32_t ms);
#endif
} ForwardNs;

/** @brief The one symbol this module exports. */
extern const ForwardNs Forward;

#endif // PC_ENABLE_FORWARD

PROTO_END_DECLS

#endif // PROTOCORE_FORWARD_H
