// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file esp_platform.c
 * @brief Espressif answers to the platform questions the core asks.
 *
 * The core cannot name FreeRTOS, so it asks protocore_platform_context_id() and this supplies it. Only ever
 * compared for equality by the pools' debug tripwire, so the task handle is exactly the right answer
 * and needs no interpretation.
 */

#include "core_setup/board_profiles/protocore_platform.h"

#if PROTOCORE_VENDOR_ESP

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

uintptr_t protocore_platform_context_id(void)
{
    return (uintptr_t)xTaskGetCurrentTaskHandle();
}

#endif // PROTOCORE_VENDOR_ESP
