// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file link_manager.h
 * @brief Multi-interface egress selection + graceful escalation/failover (PROTOCORE_ENABLE_LINK_MANAGER).
 *
 * Once a device has more than one network interface (a wired Ethernet PHY brought up alongside WiFi STA,
 * plus maybe a softAP), something has to decide which one carries traffic and when to switch: escalate to
 * the wired link when it comes up (usually faster / more reliable), and fail over to WiFi when it drops.
 * The stack owns the routes and `Physical.egress(Physical.internal)` reports the live one in
 * `Physical.if_kind`; this is the *policy* that drives it - a small table of interfaces (each a kind +
 * priority + up/down) with a deterministic "best link that is up" selection, plus change detection so
 * the app only reconfigures on an actual transition.
 *
 * Pure, no heap, no stdlib, host-testable. The real PHY bring-up and the netif reconfigure are
 * the app's; this just says which interface should be active.
 */

#ifndef PROTOCORE_LINK_MANAGER_H
#define PROTOCORE_LINK_MANAGER_H

#include "protocore_config.h"

#if PROTOCORE_ENABLE_LINK_MANAGER

PROTOCORE_BEGIN_DECLS

/** @brief Interface kind (informational; selection is by priority). Stored in a uint8_t field and
 *  compared, so integer constants in a namespacing struct - cast-free. */
#define LINK_KIND_ETH 0      ///< wired Ethernet PHY.
#define LINK_KIND_WIFI_STA 1 ///< WiFi station.
#define LINK_KIND_WIFI_AP 2  ///< WiFi softAP.
#define LINK_KIND_OTHER 3

/** @brief One managed interface. */
typedef struct
{
    uint8_t kind;     ///< LINK_KIND_*.
    uint8_t priority; ///< higher wins when up (ties break to the lower index).
    proto_bool up;    ///< link currently up.
} LinkIface;

/** @brief The link-manager state over a caller-owned interface table. */
typedef struct
{
    LinkIface *ifaces;
    size_t n;
    int active; ///< index of the active egress, or -1 if none is up.
} LinkManager;

/** @brief The table a call acts on, and the interface it names. */
typedef struct
{
    LinkManager *m;          ///< the manager a call acts on
    const LinkManager *m_ro; ///< the same manager, where a call only reads it
    LinkIface *ifaces;       ///< the interface table a bind installs
    size_t n;                ///< how many entries it has
    size_t idx;              ///< the interface whose state a set changes
    proto_bool up;           ///< that interface's new carrier state
} LinkArgs;

/**
 * @brief The interface failover policy over a caller-owned manager.
 *
 * A caller sets the members a call takes, invokes it through ::Link, and reads the outcome off the
 * same handle. The manager and its table are the caller's.
 *
 * @var LinkManagerNs::args      the table a call acts on, and the interface it names
 * @var LinkManagerNs::i32       the interface a select or an active lookup reports, -1 for none
 * @var LinkManagerNs::from      the interface that was active before a set
 * @var LinkManagerNs::to        the one active after it
 * @var LinkManagerNs::changed   that set moved the active interface
 * @var LinkManagerNs::init      bind the table and pick the first active interface
 * @var LinkManagerNs::select    the highest-priority interface that is up, -1 when none is
 * @var LinkManagerNs::active    the interface currently carrying traffic
 * @var LinkManagerNs::set       change one interface's carrier state and reselect
 *
 * Higher priority wins; the lower index breaks a tie. No storage member: the manager is the
 * caller's, so nothing is held here.
 */
typedef struct
{
    LinkArgs args;

    int i32;
    int from;
    int to;
    proto_bool changed;

    void (*const init)(uint8_t *restrict work);
    void (*const select)(uint8_t *restrict work);
    void (*const active)(uint8_t *restrict work);
    void (*const set)(uint8_t *restrict work);
} LinkManagerNs;

/** @brief The one symbol this module exports. */
extern LinkManagerNs Link;

PROTOCORE_END_DECLS

#endif // PROTOCORE_ENABLE_LINK_MANAGER

#endif // PROTOCORE_LINK_MANAGER_H
