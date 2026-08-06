// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A consumer of the real converted module, in its own TU. Linked against src/network_drivers/
// physical/radio_power.c and disassembled, it answers whether Radio.* is a direct call in this
// library rather than in a four-function stand-in.

#include "network_drivers/physical/radio_power.h"

void app_main(void);
void app_main(void)
{
    Radio.power();
    Radio.busy_hold();
    Radio.busy_release();
}
