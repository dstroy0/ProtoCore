// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file udp.c
 * @brief The two sides of UDP, joined. See udp.h.
 *
 * Nothing runs here. The file exists to hold the one table that names the listener and the client,
 * so a caller reaches both through @ref Udp and neither half has to know the other exists.
 */

#include "network_drivers/transport/udp.h"

const UdpNs Udp = {
    &UdpListener,
    &UdpClient,
};
