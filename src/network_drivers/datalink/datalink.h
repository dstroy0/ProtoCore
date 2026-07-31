// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file datalink.h
 * @brief Layer 2 (Data Link) - Ethernet / 802.11 frame handling.
 *
 * On ESP32 the data link layer is fully managed by the Espressif lwIP port
 * (WLAN device driver + IEEE 802.11 MAC).  This header exists to complete
 * the OSI-layered architecture and provide an extension point should direct
 * MAC-level access ever be required (e.g., raw socket experiments).
 *
 * The current implementation is a no-op stub.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DATALINK_H
#define PROTOCORE_DATALINK_H

#include <Arduino.h>

/**
 * @brief Initialize the data-link layer.
 *
 * Currently a no-op; the Espressif WiFi + lwIP stack handles all Layer 2
 * operations internally.  Call this if you later add MAC-level extensions.
 */
void init_datalink_layer();

#endif
