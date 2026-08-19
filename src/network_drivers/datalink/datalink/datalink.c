// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file datalink.c
 * @brief Layer 2 (Data Link) - the LINK LAYER of RFC 1122 sec 2. See datalink.h.
 *
 * The platform's link driver and IP stack port carry out every Layer 2 operation: MAC framing,
 * medium access, and the RFC 894 / RFC 1042 encapsulation RFC 1122 sec 2.3.3 requires. The one call
 * here reports the layer up.
 *
 * The one symbol this file exports is @ref Datalink.
 */

#include "network_drivers/datalink/datalink/datalink.h"

// Reports the layer up. The driver below performs every RFC 1122 sec 2.3.3 encapsulation step.
static void datalink_init(uint8_t *restrict work)
{
    (void)work;
    Datalink.ok = PROTO_TRUE;
}

// Designated, so a member's position in the struct does not decide what it binds to.
DatalinkNs Datalink = {.init = datalink_init};
