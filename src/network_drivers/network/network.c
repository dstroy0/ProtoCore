// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file network.c
 * @brief Layer 3 (Network) - the internet layer's call and its carried modules. See network.h.
 *
 * RFC 1122 sec 3 "INTERNET LAYER PROTOCOLS": the platform's TCP/IP stack carries the route table,
 * address assignment, ARP, ICMP, and the fragmentation and reassembly of RFC 791 sec 1.4, so the
 * bring-up call runs no work. This file binds the carried modules onto the layer: name resolution
 * (RFC 1034 sec 2.4), the address value (RFC 791, RFC 8200), and the forwarding plane
 * (RFC 1812 sec 5).
 *
 * The one symbol this file exports is @ref network.
 */

#include "network.h"

void protocore_network_init(uint8_t *restrict work)
{
    (void)work; // no work: the platform stack holds the route table and selects the path
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
networkVars networkV;
