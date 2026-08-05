// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A consumer of the converted route module, in its own TU. Linked against
// src/network_drivers/network/route.c and disassembled, it shows whether network.route->count() is
// a direct call under LTO and an indirect one without it.

#include "network_drivers/network/network.h"

volatile unsigned char route_sink = 0;

void app_main(void);
void app_main(void)
{
    route_sink = network.route->count();
}
