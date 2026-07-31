// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file datalink.cpp
 * @brief Layer 2 (Data Link) - IEEE 802.11 frame handling stub.
 *
 * All Layer 2 operations (WLAN MAC, frame assembly/disassembly, CSMA/CA)
 * are handled by the Espressif WiFi driver and lwIP port.  This function
 * is provided as an architectural extension point.
 */

#include "datalink.h"

void init_datalink_layer()
{
    // No-op: lwIP + Espressif WiFi driver owns all L2 operations.
}
