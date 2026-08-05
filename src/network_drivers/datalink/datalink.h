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
 * The module exports one symbol, @ref Datalink. Everything in datalink.c has internal linkage.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_DATALINK_H
#define PROTOCORE_DATALINK_H

#include "protocore_config.h"

PROTO_BEGIN_DECLS

/**
 * @brief The data-link module.
 *
 * @var DatalinkNs::init  initialize the layer. A no-op: the vendor WiFi and lwIP stack handle every
 *                        Layer 2 operation internally. Call it if MAC-level extensions are added.
 *
 * No storage member: the layer holds nothing of its own, so there is no context to hand out.
 */
typedef struct
{
    void (*init)(void);
} DatalinkNs;

/** @brief The one symbol this module exports. */
extern const DatalinkNs Datalink;

PROTO_END_DECLS

#endif
