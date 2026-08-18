// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file esp_sha_hal.c
 * @brief SHA-accelerator ownership: the exclusivity mutex and the direct-register bring-up.
 *
 * The sibling of esp_crypto_hal.c, same construction. The exclusivity mutex must be ONE global
 * instance shared by every translation unit that drives the accelerator, so acquire/release live here
 * (a header-only `static` would give each TU its own copy - not a lock). No `soc/` header, no
 * `esp_sha_*` / `sha_hal_*` / `sha_ll_*` symbol.
 */

#include "test/core_setup/hal/esp/esp_sha_hal.h"

#if PROTOCORE_HAS_HW_SHA

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// All SHA-accelerator ownership state in one owned context (internal linkage): the PC exclusivity mutex
// (one global instance, shared across every TU that drives the accelerator) and the spinlock guarding both
// its lazy creation and the clock/reset register-modify-write (those clock-domain registers are shared with
// other peripherals). One named owner, unreachable cross-TU.
typedef struct
{
    SemaphoreHandle_t lock; // PC recursive mutex; held across a whole streaming digest
    portMUX_TYPE hw_mux;    // guards lazy mutex creation and the shared clock/reset RMW
} HalShaCtx;
static HalShaCtx s_sha = {NULL, portMUX_INITIALIZER_UNLOCKED};

// Is the accelerator already clocked? Reads clock-domain registers only, which are always accessible
// even when the SHA block itself is unclocked.
static proto_bool sha_is_up(void)
{
    return (PROTOCORE_HW_RD(PROTOCORE_SHA_CLK_REG) & PROTOCORE_SHA_CLK_BIT) != 0u;
}

// Bring the accelerator up by direct register writes: enable the bus clock, pulse the SHA reset, then
// release the sibling resets that would otherwise hold SHA in reset. The clock/reset registers are
// RMW-shared with other peripherals, so the caller holds s_sha.hw_mux. Each RMW is an explicit
// read-modify-write of one bit.
static void sha_bring_up(void)
{
    uint32_t v = PROTOCORE_HW_RD(PROTOCORE_SHA_CLK_REG);
    v |= PROTOCORE_SHA_CLK_BIT; // bus clock on
    PROTOCORE_HW_WR(PROTOCORE_SHA_CLK_REG, v);

    v = PROTOCORE_HW_RD(PROTOCORE_SHA_RST_REG);
    v |= PROTOCORE_SHA_RST_BIT; // assert SHA reset
    PROTOCORE_HW_WR(PROTOCORE_SHA_RST_REG, v);
    v = PROTOCORE_HW_RD(PROTOCORE_SHA_RST_REG);
    v &= ~PROTOCORE_SHA_RST_BIT; // deassert SHA reset
    PROTOCORE_HW_WR(PROTOCORE_SHA_RST_REG, v);

#if PROTOCORE_SHA_HOLD_CLEAR
    v = PROTOCORE_HW_RD(PROTOCORE_SHA_HOLD_REG);
    v &= ~(uint32_t)PROTOCORE_SHA_HOLD_CLEAR; // release the sibling resets (DS / HMAC) that would hold SHA in reset
    PROTOCORE_HW_WR(PROTOCORE_SHA_HOLD_REG, v);
#endif
}

void protocore_sha_hw_acquire(void)
{
    if (s_sha.lock == NULL)
    {
        portENTER_CRITICAL(&s_sha.hw_mux);
        if (s_sha.lock == NULL)
        {
            s_sha.lock = xSemaphoreCreateRecursiveMutex();
        }
        portEXIT_CRITICAL(&s_sha.hw_mux);
    }
    xSemaphoreTakeRecursive(s_sha.lock, portMAX_DELAY);

    if (!sha_is_up())
    {
        portENTER_CRITICAL(&s_sha.hw_mux);
        sha_bring_up();
        portEXIT_CRITICAL(&s_sha.hw_mux);
    }
    PROTOCORE_HW_WR(PROTOCORE_SHA_INT_ENA, 0u); // poll only, no completion IRQ
}

void protocore_sha_hw_release(void)
{
    // Leave the peripheral clocked for the next digest; it is re-brought-up on the next acquire only if
    // some other user gated its clock meanwhile. The H bank is restored per block by the caller, so
    // nothing of this digest is left relied upon across the release.
    (void)xSemaphoreGiveRecursive(s_sha.lock); // a give on a mutex this task holds has no failure to report
}

#endif // PROTOCORE_HAS_HW_SHA
