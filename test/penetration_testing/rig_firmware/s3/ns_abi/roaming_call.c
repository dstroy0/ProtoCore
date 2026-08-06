// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A consumer of the converted roaming module, in its own TU. Linked against
// src/network_drivers/datalink/roaming.c and disassembled, it shows whether Roam.* is a direct call
// under LTO and an indirect one without it.

#include "network_drivers/datalink/roaming.h"

volatile uint8_t roam_sink = 0;

void app_main(void);
void app_main(void)
{
    static const uint8_t cur[6] = {1, 2, 3, 4, 5, 6};
    static const pc_roam_neighbor nb[1] = {{{9, 8, 7, 6, 5, 4}, 6, -40}};
    pc_roam_decision d;

    Roam.decide(cur, -80, nb, 1, 0, 0, &d);
    roam_sink = (uint8_t)d.reason;
}
