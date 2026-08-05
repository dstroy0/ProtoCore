// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file network.h
 * @brief Layer 3 (Network) - IP routing and packet forwarding.
 *
 * On ESP32 the network layer is fully managed by the lwIP TCP/IP stack. IP address assignment (DHCP
 * or static), routing, and ICMP are all transparent to this library. This header exists as an
 * architectural placeholder and extension point. The current implementation is a no-op stub.
 *
 * The module exports one symbol, @ref Network. Everything in network.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_NETWORK_H
#define PROTOCORE_NETWORK_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

/**
 * @brief The network module.
 *
 * @var NetworkNs::init  initialize the layer. Currently a no-op; lwIP manages IP routing internally.
 *                       Call it if static-route configuration, ICMP echo handling, or custom
 *                       network-layer diagnostics are added.
 *
 * No storage member: the layer holds nothing of its own.
 */
typedef struct
{
    void (*init)(void);
} NetworkNs;

/** @brief The one symbol this module exports. */
extern const NetworkNs Network;

PROTO_END_DECLS

#endif
