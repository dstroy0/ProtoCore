// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file network.h
 * @brief Layer 3 (Network) - IP routing and packet forwarding.
 *
 * On ESP32 the network layer is fully managed by the lwIP TCP/IP stack.
 * IP address assignment (DHCP or static), routing, and ICMP are all
 * transparent to this library.  This header exists as an architectural
 * placeholder and extension point.
 *
 * The current implementation is a no-op stub.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_NETWORK_H
#define PROTOCORE_NETWORK_H

#include <Arduino.h>

/**
 * @brief Initialize the network layer.
 *
 * Currently a no-op; lwIP manages IP routing internally.  Call this if you
 * later add static-route configuration, ICMP echo handling, or custom
 * network-layer diagnostics.
 */
void init_network_layer();

#endif
