// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file esp_crypto_hal.c
 * @brief Real-HAL implementation: single-owner RSA/MPI accelerator, brought up by DIRECT register writes.
 *
 * Self-contained: uses only this HAL's own register map (esp_crypto_hal.h) and FreeRTOS for the one shared
 * exclusivity mutex - no `soc/` header, no `esp_mpi_*` / `mpi_hal_*` / `mpi_ll_*` symbol. The exclusivity
 * mutex must be ONE global instance shared by every translation unit that drives the accelerator, so
 * acquire/release live here (a header-only `static` would give each TU its own copy - not a lock).
 *
 * Bring-up is ON DEMAND and the peripheral is then LEFT running: a run of MODMULTs is stateless (each reloads
 * all operands), so re-resetting/power-cycling between ops is unnecessary - and, measured on-device, a per-op
 * power-cycle is not even deterministic (a re-init right after a teardown can return a wrong first result). We
 * bring up only when the peripheral is not already clocked+powered: at cold boot, or after another RSA-
 * peripheral user (e.g. mbedTLS RSA/DH) powered it down on its own teardown. Detection reads the clock-domain
 * registers (always accessible), never the possibly-unclocked RSA block.
 */

#include "board_drivers/hal/esp/esp_crypto_hal.h"

#ifdef PC_RSA_MODMUL_HW

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// All RSA/MPI-accelerator ownership state in one owned context (internal linkage): the PC exclusivity mutex
// (one global instance, shared across every TU that drives the accelerator) and the spinlock guarding both its
// lazy creation and the clock/reset register-modify-write (those clock-domain registers are shared with other
// peripherals). One named owner, unreachable cross-TU.
typedef struct
{
    SemaphoreHandle_t lock; // PC recursive mutex; held across a whole scalar-mult run
    portMUX_TYPE hw_mux;    // guards lazy mutex creation and the shared clock/reset RMW + memory-init
} HalRsaCtx;
static HalRsaCtx s_rsa = {NULL, portMUX_INITIALIZER_UNLOCKED};

// Is the accelerator already clocked (and, where applicable, powered)? Reads clock-domain registers only,
// which are always accessible even when the RSA block itself is unclocked.
static proto_bool rsa_is_up(void)
{
    const proto_bool clocked = (PC_HW_REG(PC_RSA_CLK_REG) & PC_RSA_CLK_BIT) != 0u;
#if PC_RSA_HAS_PD
    const proto_bool powered = (PC_HW_REG(PC_RSA_PD_REG) & PC_RSA_PD_DOWN_BIT) == 0u;
    return clocked && powered;
#else
    return clocked;
#endif
}

// Bring the accelerator up by direct register writes: enable the bus clock, pulse the RSA reset (and release
// the sibling resets that would otherwise hold RSA in reset), power up the RSA memory (hw_ver1 only), then
// spin until the memory-init completes. The clock/reset registers are RMW-shared with other peripherals, so
// the caller holds s_rsa.hw_mux. Each RMW is an explicit read-modify-write of one bit.
static void rsa_bring_up(void)
{
    uint32_t v = PC_HW_REG(PC_RSA_CLK_REG);
    v |= PC_RSA_CLK_BIT; // bus clock on
    PC_HW_REG(PC_RSA_CLK_REG) = v;

    v = PC_HW_REG(PC_RSA_RST_REG);
    v |= PC_RSA_RST_BIT; // assert RSA reset
    PC_HW_REG(PC_RSA_RST_REG) = v;
    v = PC_HW_REG(PC_RSA_RST_REG);
    v &= ~PC_RSA_RST_BIT; // deassert RSA reset
    PC_HW_REG(PC_RSA_RST_REG) = v;

    v = PC_HW_REG(PC_RSA_HOLD_REG);
    v &= ~PC_RSA_HOLD_CLEAR; // release the sibling resets (DS / crypto / ECDSA) that would hold RSA in reset
    PC_HW_REG(PC_RSA_HOLD_REG) = v;

#ifdef PC_RSA_HOLD2_REG
    v = PC_HW_REG(PC_RSA_HOLD2_REG);
    v &= ~PC_RSA_HOLD2_CLEAR; // some dies (C5/H2) hold the ECDSA reset in a second, separate register
    PC_HW_REG(PC_RSA_HOLD2_REG) = v;
#endif

#if PC_RSA_HAS_PD
    v = PC_HW_REG(PC_RSA_PD_REG);
    v &= ~PC_RSA_PD_UP_CLEAR; // power up the RSA memory
    PC_HW_REG(PC_RSA_PD_REG) = v;
#endif

    while (PC_HW_REG(PC_RSA_CLEAN) != 0u) // wait until the accelerator's memory init completes
    {
    }
}

void pc_rsa_hw_acquire(void)
{
    if (s_rsa.lock == NULL)
    {
        portENTER_CRITICAL(&s_rsa.hw_mux);
        if (s_rsa.lock == NULL)
        {
            s_rsa.lock = xSemaphoreCreateRecursiveMutex();
        }
        portEXIT_CRITICAL(&s_rsa.hw_mux);
    }
    xSemaphoreTakeRecursive(s_rsa.lock, portMAX_DELAY);

    if (!rsa_is_up())
    {
        // Bring-up runs interrupts-off: the memory-init must not be preempted, or the idle task can gate the
        // RSA clock mid-init and the clean bit never clears.
        portENTER_CRITICAL(&s_rsa.hw_mux);
        rsa_bring_up();
        portEXIT_CRITICAL(&s_rsa.hw_mux);
    }
    PC_HW_REG(PC_RSA_INTENA) = 0u; // poll only, no completion IRQ
}

void pc_rsa_hw_release(void)
{
    // Leave the peripheral clocked+powered for the next op (a MODMULT run is stateless; a per-op power-cycle is
    // both wasteful and, measured, non-deterministic). It is re-brought-up on the next acquire only if some
    // other user powered it down meanwhile.
    xSemaphoreGiveRecursive(s_rsa.lock);
}

#endif // PC_RSA_MODMUL_HW
