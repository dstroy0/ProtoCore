// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file link_manager.h
 * @brief Multi-interface egress selection + graceful escalation/failover (PC_ENABLE_LINK_MANAGER).
 *
 * Once a device has more than one network interface (a wired Ethernet PHY brought up alongside WiFi STA,
 * plus maybe a softAP), something has to decide which one carries traffic and when to switch: escalate to
 * the wired link when it comes up (usually faster / more reliable), and fail over to WiFi when it drops.
 * The stack owns the routes and `pc_net_egress()` reports the live one; this is the *policy* that drives
 * it - a small table of interfaces (each a kind + priority + up/down) with a deterministic "best link
 * that is up" selection, plus change detection so the app only reconfigures on an actual transition.
 *
 * Pure, no heap, no stdlib, host-testable. The real PHY bring-up and the netif reconfigure are
 * the app's; this just says which interface should be active.
 */

#ifndef PROTOCORE_LINK_MANAGER_H
#define PROTOCORE_LINK_MANAGER_H

#include "protocore_config.h"

#if PC_ENABLE_LINK_MANAGER

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

/** @brief Initialize over caller storage and compute the initial active egress. */
void pc_link_init(LinkManager *m, LinkIface *ifaces, size_t n);

/** @brief Best interface that is up (highest priority, lower index breaks ties). @return index or -1. */
int pc_link_select(const LinkManager *m);

/** @brief The current active egress index (-1 if none). */
int pc_link_active(const LinkManager *m);

/**
 * @brief Set an interface's up/down state and recompute the active egress.
 * @param from (may be null) the previous active index.
 * @param to   (may be null) the new active index.
 * @return true if the active egress changed (escalation or failover happened).
 */
proto_bool pc_link_set(LinkManager *m, size_t idx, proto_bool up, int *from, int *to);

#endif // PC_ENABLE_LINK_MANAGER
#endif // PROTOCORE_LINK_MANAGER_H
