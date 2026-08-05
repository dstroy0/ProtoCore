// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file network.c
 * @brief Layer 3 (Network) - IP routing and packet forwarding stub. See network.h.
 *
 * IPv4/IPv6 routing, DHCP, ARP, ICMP, and DNS resolution are all transparent to this library - they
 * run inside the lwIP stack. This is the extension point for static-route injection or custom ICMP
 * handling.
 *
 * The one symbol this file exports is @ref Network.
 */

#include "network.h"

static void init(void)
{
    // No-op: lwIP owns all L3 (IP) operations.
}

const NetworkNs network = {init, &RouteTable, &Dns};
