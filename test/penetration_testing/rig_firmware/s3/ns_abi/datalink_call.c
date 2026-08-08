// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A consumer of the converted datalink module, in its own TU. Linked against
// src/network_drivers/datalink/datalink.c and disassembled, it shows whether Datalink.init is a
// direct call under LTO and an indirect one without it.

#include "network_drivers/datalink/datalink.h"

void app_main(void);
void app_main(void)
{
    Datalink.init();
}
