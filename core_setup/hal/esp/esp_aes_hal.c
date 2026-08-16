// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file esp_aes_hal.c
 * @brief AES-accelerator ownership: the exclusivity mutex and the direct-register bring-up.
 *
 * The sibling of esp_sha_hal.c, same construction. The exclusivity mutex must be ONE global instance
 * shared by every translation unit that drives the accelerator, so acquire/release live here (a
 * header-only `static` would give each TU its own copy - not a lock). No `soc/` header, no `esp_aes_*`
 * / `aes_hal_*` / `aes_ll_*` symbol.
 */

#include "core_setup/hal/esp/esp_aes_hal.h"

#if PROTOCORE_HAS_HW_AES

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// All AES-accelerator ownership state in one owned context (internal linkage): the PC exclusivity mutex
// (one global instance, shared across every TU that drives the accelerator) and the spinlock guarding both
// its lazy creation and the clock/reset register-modify-write (those clock-domain registers are shared with
// other peripherals). One named owner, unreachable cross-TU.
typedef struct
{
    SemaphoreHandle_t lock; // PC recursive mutex; held across a whole keyed operation
    portMUX_TYPE hw_mux;    // guards lazy mutex creation and the shared clock/reset RMW
} HalAesCtx;
static HalAesCtx s_aes = {NULL, portMUX_INITIALIZER_UNLOCKED};

// Is the accelerator already clocked? Reads clock-domain registers only, which are always accessible
// even when the AES block itself is unclocked.
static proto_bool aes_is_up(void)
{
    return (PROTOCORE_HW_RD(PROTOCORE_AES_CLK_REG) & PROTOCORE_AES_CLK_BIT) != 0u;
}

// Bring the accelerator up by direct register writes: enable the bus clock, then pulse the AES reset.
// The clock/reset registers are RMW-shared with other peripherals, so the caller holds s_aes.hw_mux.
// Each RMW is an explicit read-modify-write of one bit.
static void aes_bring_up(void)
{
    uint32_t v = PROTOCORE_HW_RD(PROTOCORE_AES_CLK_REG);
    v |= PROTOCORE_AES_CLK_BIT; // bus clock on
    PROTOCORE_HW_WR(PROTOCORE_AES_CLK_REG, v);

    v = PROTOCORE_HW_RD(PROTOCORE_AES_RST_REG);
    v |= PROTOCORE_AES_RST_BIT; // assert AES reset
    PROTOCORE_HW_WR(PROTOCORE_AES_RST_REG, v);
    v = PROTOCORE_HW_RD(PROTOCORE_AES_RST_REG);
    v &= ~PROTOCORE_AES_RST_BIT; // deassert AES reset
    PROTOCORE_HW_WR(PROTOCORE_AES_RST_REG, v);
}

void protocore_aes_hw_acquire(void)
{
    if (s_aes.lock == NULL)
    {
        portENTER_CRITICAL(&s_aes.hw_mux);
        if (s_aes.lock == NULL)
        {
            s_aes.lock = xSemaphoreCreateRecursiveMutex();
        }
        portEXIT_CRITICAL(&s_aes.hw_mux);
    }
    xSemaphoreTakeRecursive(s_aes.lock, portMAX_DELAY);

    if (!aes_is_up())
    {
        portENTER_CRITICAL(&s_aes.hw_mux);
        aes_bring_up();
        portEXIT_CRITICAL(&s_aes.hw_mux);
    }
}

void protocore_aes_hw_release(void)
{
    // Leave the peripheral clocked for the next operation; it is re-brought-up on the next acquire only
    // if some other user gated its clock meanwhile. The key bank is reloaded per operation by the
    // caller, so nothing of this operation is left relied upon across the release.
    (void)xSemaphoreGiveRecursive(s_aes.lock); // a give on a mutex this task holds has no failure to report
}

#endif // PROTOCORE_HAS_HW_AES
