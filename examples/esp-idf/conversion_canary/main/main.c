// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file main.c
 * @brief Compile and link the library on the target, from C.
 *
 * Touches a converted module through its namespace struct, so the struct is instantiated, the entry
 * points are reached, and the linker has to resolve them; then prints what it read back.
 *
 * Add a line per module as the conversion reaches it.
 */

#include "protocore.h"

#include <stdio.h>

#if PC_ENABLE_RADIO_POWER
#include "network_drivers/physical/radio_power.h"
#endif

void app_main(void);
void app_main(void)
{
    printf("pc: conversion canary\n");

#if PC_ENABLE_RADIO_POWER
    // Radio is the module's whole footprint in the symbol space; reaching it through the struct is
    // what makes the linker resolve the entry points behind it.
    Radio.power();
    Radio.busy_hold();
    printf("pc: radio modem-sleep %s\n", Radio.ps_name(Radio.ps_mode()));
    Radio.busy_release();
#endif
}
