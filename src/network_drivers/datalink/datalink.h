// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file datalink.h
 * @brief Layer 2 (Data Link) - Ethernet / 802.11 frame handling.
 *
 * The data link layer is managed by the vendor lwIP port (WLAN device driver + IEEE 802.11 MAC).
 * This header completes the OSI layering and is the extension point for a target that needs direct
 * MAC-level access. The implementation is a no-op stub.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DATALINK_H
#define PROTOCORE_DATALINK_H

/**
 * @brief Initialize the data-link layer.
 *
 * A no-op: the vendor WiFi and lwIP stack handle every Layer 2 operation internally. Call it if
 * MAC-level extensions are added.
 */
void init_datalink_layer(void);

#endif
