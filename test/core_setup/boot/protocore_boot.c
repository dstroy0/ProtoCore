// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file protocore_boot.c
 * @brief The reset handler's loops. See protocore_boot.h.
 */

#include "test/core_setup/boot/protocore_boot.h"

void protocore_boot_init_memory(const protocore_boot_regions *r)
{
    if (!r)
    {
        return;
    }
    if (r->data_load && r->data_run)
    {
        for (uint32_t i = 0; i < r->data_words; ++i)
        {
            r->data_run[i] = r->data_load[i];
        }
    }
    if (r->bss)
    {
        for (uint32_t i = 0; i < r->bss_words; ++i)
        {
            r->bss[i] = 0u;
        }
    }
}

uint32_t protocore_boot_paint_stack(uint32_t *stack_low, uint32_t stack_words)
{
    uint32_t frame;
    const uintptr_t ceiling = (uintptr_t)&frame;

    if (!stack_low)
    {
        return 0u;
    }
    uint32_t n = 0;
    while (n < stack_words && (uintptr_t)&stack_low[n] < ceiling)
    {
        stack_low[n] = PROTOCORE_BOOT_STACK_PAINT;
        ++n;
    }
    return n;
}

uint32_t protocore_boot_stack_unused(const uint32_t *stack_low, uint32_t stack_words)
{
    if (!stack_low)
    {
        return 0u;
    }
    uint32_t n = 0;
    while (n < stack_words && stack_low[n] == PROTOCORE_BOOT_STACK_PAINT)
    {
        ++n;
    }
    return n << 2;
}
