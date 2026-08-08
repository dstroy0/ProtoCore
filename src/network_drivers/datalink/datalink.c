// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file datalink.c
 * @brief Layer 2 (Data Link) - IEEE 802.11 frame handling stub. See datalink.h.
 *
 * Every Layer 2 operation (WLAN MAC, frame assembly/disassembly, CSMA/CA) is owned by the vendor
 * WiFi driver and the lwIP port. This is the extension point for a target where it is not.
 *
 * The one symbol this file exports is @ref Datalink.
 */

#include "datalink.h"

static void init(void)
{
    // No-op: the vendor WiFi driver and lwIP own every L2 operation.
}

const DatalinkNs Datalink = {init};
