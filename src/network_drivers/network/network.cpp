// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file network.cpp
 * @brief Layer 3 (Network) - IP routing and packet forwarding stub.
 *
 * IPv4/IPv6 routing, DHCP, ARP, ICMP, and DNS resolution are all
 * transparent to this library - they run inside the lwIP stack.  This
 * function is provided as an architectural extension point for future
 * work such as static-route injection or custom ICMP handling.
 */

#include "network.h"

void init_network_layer()
{
    // No-op: lwIP owns all L3 (IP) operations.
}
