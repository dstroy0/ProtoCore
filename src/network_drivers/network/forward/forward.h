// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file forward.h
 * @brief Layer 3 (Network) - the interface forwarding plane (PROTOCORE_ENABLE_FORWARD).
 *
 * RFC 1812 sec 5 "INTERNET LAYER - FORWARDING". A frame that arrives on one interface runs the
 * forwarding walk-through (sec 5.2) and leaves on every interface it is permitted to leave on. The
 * steps this plane runs over opaque bytes are the access list (sec 5.3.9 "Packet Filtering and
 * Access Lists"), the next hop (sec 5.2.4.3 "Next Hop Address"), the controls on forwarding
 * (sec 5.3.11 "Controls on Forwarding"), and the drop under load (sec 5.3.6 "Congestion Control").
 *
 * The interfaces are layer 1's, over PROTOCORE_PHY_MAX_IFACES rows; this plane reads that registry
 * and calls its send. A (src, dst) pair forwards when an ALLOW control matches and no DENY does; a
 * frame never leaves on the interface it arrived on. An exceeded rate cap, or an interface that
 * refuses the bytes, drops the frame for that next hop and counts it. Storage is static:
 * PROTOCORE_FWD_MAX_RULES controls, PROTOCORE_FWD_MAX_ACL access-list entries,
 * PROTOCORE_FWD_MAX_ROUTES policy routes.
 *
 * A policy route matches a frame by the same byte pattern the access list uses - any field at a
 * known offset, an EtherType, an IP protocol, a port, an address prefix - and binds it to one
 * egress interface ahead of the (src, dst) fan-out; the first matching route wins. The access list
 * runs first, and the rate cap, the never-reflect step and the counted drop apply to the chosen
 * next hop.
 *
 * The inspection hook (PROTOCORE_FWD_INSPECT) runs an application callback on every frame after the
 * access list and before the route lookup; the callback returns a verdict that passes or drops it.
 *
 * The rate cap reads Clock.millis (server/clock/clock.h), the library's one time source; Clock.set_ms
 * moves the window for every module at once.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_FORWARD_H
#define PROTOCORE_FORWARD_H

#include "network_drivers/physical/physical/physical.h"

#include "protocore_config.h"
// An interface is a physical thing: its id, its kind, and how bytes reach the wire all live at L1.
// This plane picks the interface a frame leaves on and asks L1 to put it there.

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_FORWARD

/**
 * @brief What an access-list entry or a control on forwarding does to a frame.
 *        RFC 1812 sec 5.3.9, sec 5.3.11.
 */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_FWD_DENY = 0,
    PROTOCORE_FWD_ALLOW = 1,
} protocore_fwd_action;

/** @brief Wildcard source interface: an entry carrying it matches a frame from any interface. */
#define PROTOCORE_FWD_IF_ANY 0xFF

/** @brief Forwarding counters, monotonic since the last reset. */
typedef struct
{
    uint32_t frames_in;       ///< frames offered to the forwarding algorithm (RFC 1812 sec 5.2.1)
    uint32_t forwarded;       ///< next hops that took the frame (RFC 1812 sec 5.2.4.3)
    uint32_t blocked;         ///< next hops refused by a DENY or by default-deny (RFC 1812 sec 5.3.11)
    uint32_t rate_dropped;    ///< frames dropped at a rate cap (RFC 1812 sec 5.3.6)
    uint32_t send_fail;       ///< next hops whose interface refused the bytes
    uint32_t acl_denied;      ///< frames dropped by the access list (RFC 1812 sec 5.3.9)
    uint32_t policy_routed;   ///< frames a policy route bound to one next hop
    uint32_t inspect_dropped; ///< frames dropped by the inspection hook (PROTOCORE_FWD_INSPECT)
} protocore_forward_stats;
#if PROTOCORE_FWD_INSPECT
/** @brief The verdict an inspection hook returns for a frame. */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_FWD_INSPECT_PASS = 0, ///< the frame continues to the route lookup and the fan-out
    PROTOCORE_FWD_INSPECT_DROP = 1, ///< the frame is dropped and counted as inspect_dropped
} protocore_fwd_verdict;
/**
 * @brief Ingress inspection hook: reads @p len bytes at @p data arriving on @p src_if and returns a
 *        ::protocore_fwd_verdict. Runs after the access list and before the route lookup.
 */
typedef protocore_fwd_verdict (*protocore_fwd_inspect_fn)(uint8_t src_if, const uint8_t *data, uint16_t len, void *ctx);
#endif
/** @brief One control on forwarding: the destination of a (src, dst) pair. RFC 1812 sec 5.3.11. */
typedef struct
{
    uint8_t dst_if;              ///< the destination interface the pair names
    protocore_fwd_action action; ///< ALLOW forwards the pair, DENY blocks it
    uint16_t rate_cap_per_sec;   ///< frames per second the pair takes; 0 is uncapped (RFC 1812 sec 5.3.6)
} FwdRuleArgs;
/**
 * @brief The byte pattern an access-list entry or a policy route matches on: @c patlen bytes at
 *        @c offset, compared under @c mask. RFC 1812 sec 5.3.9.
 */
typedef struct
{
    const uint8_t *pattern; ///< the bytes to match, read only during the call that stores them
    const uint8_t *mask;    ///< the bits of each pattern byte that are compared
    uint16_t offset;        ///< byte offset into the frame where the pattern starts
    uint8_t patlen;         ///< pattern length, up to PROTOCORE_FWD_ACL_PATLEN; 0 matches any content
} FwdMatchArgs;
/** @brief The access list's two verdicts: one entry's, and the one a frame matching none takes. */
typedef struct
{
    protocore_fwd_action action;   ///< what the entry being added does to a matching frame
    protocore_fwd_action fallback; ///< what a frame matching no entry takes
} FwdAclArgs;
/** @brief The next hop a policy route binds a matched frame to. RFC 1812 sec 5.2.4.3. */
typedef struct
{
    uint8_t egress_if;         ///< the one interface a matched frame leaves on
    uint16_t rate_cap_per_sec; ///< frames per second to that next hop; 0 is uncapped
} FwdRouteArgs;
/** @brief The received frame the forwarding algorithm runs on. RFC 1812 sec 5.2.1. */
typedef struct
{
    const uint8_t *data; ///< the frame bytes, valid for the duration of the call
    uint16_t len;        ///< how many of them there are
} FwdFrameArgs;
#if PROTOCORE_FWD_INSPECT
/** @brief The inspection hook and the pointer it is handed back. */
typedef struct
{
    protocore_fwd_inspect_fn fn; ///< what runs on every frame; NULL leaves the hook off
    void *ctx;                   ///< passed to @c fn unread by this module
} FwdInspectArgs;
#endif
/** @brief The plane's own tables and the calls that reach them, described only in forward.c. */
/**
 * @brief The interface forwarding plane. RFC 1812 sec 5.2 "FORWARDING WALK-THROUGH".
 *
 * A caller sets the members a call takes, invokes it through ::Forward, and reads the outcome off
 * the same handle. The tables are behind @ref internal.
 *
 * @var ForwardNs::src_if           the interface a frame arrives on, and the one an entry scopes to;
 *                                  PROTOCORE_FWD_IF_ANY on an entry matches every interface
 * @var ForwardNs::rule             the destination of a (src, dst) pair (RFC 1812 sec 5.3.11)
 * @var ForwardNs::match            the byte pattern an entry or a route matches on (RFC 1812 sec 5.3.9)
 * @var ForwardNs::acl              the access list's entry verdict and its fallback (RFC 1812 sec 5.3.9)
 * @var ForwardNs::route            the next hop a policy route binds to (RFC 1812 sec 5.2.4.3)
 * @var ForwardNs::frame            the received frame the forwarding algorithm runs on
 * @var ForwardNs::inspect          the inspection hook and the pointer it is handed back
 * @var ForwardNs::ok               a call's true/false outcome
 * @var ForwardNs::n                next hops a frame reached
 * @var ForwardNs::stats            the counters a read reports
 * @var ForwardNs::reset            empty every control, entry and route, and zero the counters
 * @var ForwardNs::add_rule         add a control on forwarding for one (src, dst) pair
 * @var ForwardNs::acl_set_default  set what a frame matching no access-list entry takes
 * @var ForwardNs::acl_add          add an access-list entry; the first match decides
 * @var ForwardNs::route_add        add a policy route, taken ahead of the (src, dst) fan-out
 * @var ForwardNs::set_inspector    install the ingress inspection hook
 * @var ForwardNs::ingress          run one received frame through the forwarding walk-through
 * @var ForwardNs::get_stats        copy the counters onto the handle
 */
typedef struct
{
    uint8_t src_if;     ///< the source interface a forwarding call names
    FwdRuleArgs rule;   ///< the (src, dst) pair a control governs
    FwdMatchArgs match; ///< the byte pattern an entry or a route matches on
    FwdAclArgs acl;     ///< the access list's verdicts
    FwdRouteArgs route; ///< the next hop a policy route binds to
    FwdFrameArgs frame; ///< the frame the forwarding algorithm runs on
#if PROTOCORE_FWD_INSPECT
    FwdInspectArgs inspect; ///< the ingress inspection hook
#endif
    proto_bool ok;
    uint8_t n;
    protocore_forward_stats stats;
#if PROTOCORE_FWD_INSPECT
#endif
} ForwardVars;

/** @brief The operands and the outcome. */
extern ForwardVars ForwardV;

/** @brief The entries. */
typedef struct
{
    void (*const reset)(uint8_t *restrict work);
    void (*const add_rule)(uint8_t *restrict work);
    void (*const acl_set_default)(uint8_t *restrict work);
    void (*const acl_add)(uint8_t *restrict work);
    void (*const route_add)(uint8_t *restrict work);
    void (*const set_inspector)(uint8_t *restrict work);
    void (*const ingress)(uint8_t *restrict work);
    void (*const get_stats)(uint8_t *restrict work);
} ForwardNs;

// What the table binds, defined once in the .c and taking one parameter each: everything
// else an entry needs is an operand in ForwardV or a region of the borrow at a fixed offset.
void protocore_forward_reset(uint8_t *restrict work);
void protocore_forward_add_rule(uint8_t *restrict work);
void protocore_forward_acl_set_default(uint8_t *restrict work);
void protocore_forward_acl_add(uint8_t *restrict work);
void protocore_forward_route_add(uint8_t *restrict work);
void protocore_forward_set_inspector(uint8_t *restrict work);
void protocore_forward_ingress(uint8_t *restrict work);
void protocore_forward_get_stats(uint8_t *restrict work);

// `static const`, initialised HERE rather than `extern` against a definition in the .c: a
// const object whose initializer every translation unit can see is a COMPILE-TIME FACT, so
// `Forward.reset(work)` resolves to a named function and becomes a DIRECT call. An extern table
// leaves the call indirect and the symbol live at every level, -O2 -flto included.
static const ForwardNs Forward __attribute__((unused)) = {
    .reset = protocore_forward_reset,
    .add_rule = protocore_forward_add_rule,
    .acl_set_default = protocore_forward_acl_set_default,
    .acl_add = protocore_forward_acl_add,
    .route_add = protocore_forward_route_add,
    .set_inspector = protocore_forward_set_inspector,
    .ingress = protocore_forward_ingress,
    .get_stats = protocore_forward_get_stats,
};

/**
 * @brief The PROTOCORE_FORWARD_BORROW bytes this module's state lives in.
 *
 * Stated beside the namespace rather than on it: an entry takes a borrow, and this is where
 * that borrow comes from. Taken once from the end of the pool, which no mark and no release
 * walks, so the state lasts the life of the program.
 *
 * @return the span.
 */
uint8_t *protocore_forward_span(void);

#endif // PROTOCORE_ENABLE_FORWARD

PROTOCORE_END_DECLS

#endif // PROTOCORE_FORWARD_H
