// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file esp_crypto_hal.h
 * @brief Single owner of the ESP32 RSA/MPI accelerator, by DIRECT register access - a self-contained HAL.
 *
 * A hardware-abstraction layer, deliberately outside `crypto/` (very distinct, register-level code): the
 * library's big-field crypto (the GF(2^255-19) layer in @ref fe25519.h for X25519 / Ed25519 and the NIST P-256
 * field/scalar layer in @ref ecdsa.cpp) drives ONE primitive on the RSA accelerator - a single-shot 256-bit
 * modular multiply `Z = X*Y mod M`.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * SELF-CONTAINED - our own register map, no vendor headers or symbols
 * ═══════════════════════════════════════════════════════════════════════════
 * The objective is a platform-agnostic library that reaches the silicon by DIRECT register access. So this HAL
 * reproduces the accelerator's register map ITSELF (bases, offsets, clock/reset/power bits - values below,
 * cross-checked against each die's TRM) and pokes it with @ref PC_HW_REG. It includes NO `soc/` header and
 * references NO vendor symbol (no `esp_mpi_*` / `mpi_hal_*` / `mpi_ll_*`, no `RSA_*_REG`, no `SYSTEM`/
 * `HP_SYS_CLKRST` struct). An upstream rename or header reshuffle therefore cannot touch our crypto; only a
 * genuine silicon change would, caught by the per-die `#error`. The one build dependency is `sdkconfig.h`,
 * solely to learn which die we are compiling for (`CONFIG_IDF_TARGET_*`) - that is the build target, not a
 * register.
 *
 * Register generations: S3/S2 = "hw_ver1", ESP32-P4 and newer = "hw_ver3". The RSA block's own register
 * offsets are identical across both; only the peripheral base and the clock/reset/power control registers
 * differ. The classic ESP32 has no single-shot MODMULT, so it (and native builds) leave PC_RSA_MODMUL_HW
 * undefined and callers use their software field layer.
 *
 * ═══════════════════════════════════════════════════════════════════════════
 * EXCLUSIVITY
 * ═══════════════════════════════════════════════════════════════════════════
 * The accelerator is shared state. This HAL owns a single PC recursive mutex (see esp_crypto_hal.cpp) taken
 * by @ref pc_rsa_hw_acquire and dropped by @ref pc_rsa_hw_release; a scalar-mult holds it across its whole
 * run of multiplies.
 */

#ifndef PROTOCORE_ESP_CRYPTO_HAL_H
#define PROTOCORE_ESP_CRYPTO_HAL_H

#include <stdint.h>

#ifdef ARDUINO
#include "sdkconfig.h" // CONFIG_IDF_TARGET_* : which die we build for (the build target, not a register header)
#endif

// Every ESP32 die whose RSA/MPI accelerator has the single-shot MODMULT operation. HW-verified on-device
// (main_cryptobench RFC KATs): S3, P4, C6. Register-map-complete and compile-verified pending an on-device
// KAT on their rigs: S2, C3, C5, H2. The classic ESP32 has no single-shot MODMULT (two MULT passes), so it -
// and native builds - leave this undefined and callers use the software field layer.
#if defined(ARDUINO) && ((defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3) ||                          \
                         (defined(CONFIG_IDF_TARGET_ESP32S2) && CONFIG_IDF_TARGET_ESP32S2) ||                          \
                         (defined(CONFIG_IDF_TARGET_ESP32C3) && CONFIG_IDF_TARGET_ESP32C3) ||                          \
                         (defined(CONFIG_IDF_TARGET_ESP32C5) && CONFIG_IDF_TARGET_ESP32C5) ||                          \
                         (defined(CONFIG_IDF_TARGET_ESP32C6) && CONFIG_IDF_TARGET_ESP32C6) ||                          \
                         (defined(CONFIG_IDF_TARGET_ESP32H2) && CONFIG_IDF_TARGET_ESP32H2) ||                          \
                         (defined(CONFIG_IDF_TARGET_ESP32P4) && CONFIG_IDF_TARGET_ESP32P4))
#define PC_RSA_MODMUL_HW 1
#endif

#ifdef PC_RSA_MODMUL_HW

// Raw 32-bit memory-mapped register access - the only primitive; no vendor REG_* macro.
#define PC_HW_REG(a) (*(volatile uint32_t *)(uintptr_t)(a))

// ── Per-die peripheral base + clock/reset/power control (reproduced from each die's TRM) ──────────────────
// Model (fits every die's quirks): a clock-enable reg+bit, an RSA-reset reg+bit, a "hold-clear" reg+mask (the
// sibling reset bits - DS / parent-crypto / ECDSA - that would otherwise keep RSA held in reset; on some dies
// this is a different register from the RSA reset), and optional RSA-memory power (up-clear mask + a
// down-status bit). Clock/reset live in SYSTEM (hw_ver1), HP_SYS_CLKRST (P4), or PCR (C-series).
#if defined(CONFIG_IDF_TARGET_ESP32S3) && CONFIG_IDF_TARGET_ESP32S3 // hw_ver1, SYSTEM clock/reset
#define PC_RSA_BASE 0x6003C000u
#define PC_RSA_CLK_REG 0x600C001Cu  // SYSTEM_PERIP_CLK_EN1_REG
#define PC_RSA_CLK_BIT (1u << 3)    // SYSTEM_CRYPTO_RSA_CLK_EN
#define PC_RSA_RST_REG 0x600C0024u  // SYSTEM_PERIP_RST_EN1_REG
#define PC_RSA_RST_BIT (1u << 3)    // SYSTEM_CRYPTO_RSA_RST
#define PC_RSA_HOLD_REG 0x600C0024u // SYSTEM_PERIP_RST_EN1_REG (same reg)
#define PC_RSA_HOLD_CLEAR (1u << 4) // SYSTEM_CRYPTO_DS_RST
#define PC_RSA_HAS_PD 1
#define PC_RSA_PD_REG 0x600C0040u                                     // SYSTEM_RSA_PD_CTRL_REG
#define PC_RSA_PD_UP_CLEAR (1u << 0)                                  // clear SYSTEM_RSA_MEM_PD to power up
#define PC_RSA_PD_DOWN_BIT (1u << 0)                                  // SYSTEM_RSA_MEM_PD reads 1 when powered down
#elif defined(CONFIG_IDF_TARGET_ESP32S2) && CONFIG_IDF_TARGET_ESP32S2 // hw_ver1, DPORT clock/reset
#define PC_RSA_BASE 0x6003C000u
#define PC_RSA_CLK_REG 0x3F4C0044u  // DPORT_PERIP_CLK_EN1_REG
#define PC_RSA_CLK_BIT (1u << 3)    // DPORT_CRYPTO_RSA_CLK_EN
#define PC_RSA_RST_REG 0x3F4C004Cu  // DPORT_PERIP_RST_EN1_REG
#define PC_RSA_RST_BIT (1u << 3)    // DPORT_CRYPTO_RSA_RST
#define PC_RSA_HOLD_REG 0x3F4C004Cu // DPORT_PERIP_RST_EN1_REG (same reg)
#define PC_RSA_HOLD_CLEAR (1u << 4) // DPORT_CRYPTO_DS_RST
#define PC_RSA_HAS_PD 1
#define PC_RSA_PD_REG 0x3F4C0068u                                     // DPORT_RSA_PD_CTRL_REG
#define PC_RSA_PD_UP_CLEAR (1u << 0)                                  // clear DPORT_RSA_MEM_PD to power up
#define PC_RSA_PD_DOWN_BIT (1u << 0)                                  // DPORT_RSA_MEM_PD reads 1 when powered down
#elif defined(CONFIG_IDF_TARGET_ESP32C3) && CONFIG_IDF_TARGET_ESP32C3 // hw_ver1, SYSTEM clock/reset
#define PC_RSA_BASE 0x6003C000u
#define PC_RSA_CLK_REG 0x600C0014u  // SYSTEM_PERIP_CLK_EN1_REG
#define PC_RSA_CLK_BIT (1u << 3)    // SYSTEM_CRYPTO_RSA_CLK_EN
#define PC_RSA_RST_REG 0x600C001Cu  // SYSTEM_PERIP_RST_EN1_REG
#define PC_RSA_RST_BIT (1u << 3)    // SYSTEM_CRYPTO_RSA_RST
#define PC_RSA_HOLD_REG 0x600C001Cu // SYSTEM_PERIP_RST_EN1_REG (same reg)
#define PC_RSA_HOLD_CLEAR (1u << 4) // SYSTEM_CRYPTO_DS_RST
#define PC_RSA_HAS_PD 1
#define PC_RSA_PD_REG 0x600C0038u                                     // SYSTEM_RSA_PD_CTRL_REG
#define PC_RSA_PD_UP_CLEAR (1u << 0)                                  // clear SYSTEM_RSA_MEM_PD to power up
#define PC_RSA_PD_DOWN_BIT (1u << 0)                                  // SYSTEM_RSA_MEM_PD reads 1 when powered down
#elif defined(CONFIG_IDF_TARGET_ESP32P4) && CONFIG_IDF_TARGET_ESP32P4 // hw_ver3, HP_SYS_CLKRST clock/reset
#define PC_RSA_BASE 0x50092000u
#define PC_RSA_CLK_REG 0x500E60A8u                                    // HP_SYS_CLKRST_PERI_CLK_CTRL25_REG
#define PC_RSA_CLK_BIT (1u << 18)                                     // REG_CRYPTO_RSA_CLK_EN
#define PC_RSA_RST_REG 0x500E60C8u                                    // HP_SYS_CLKRST_HP_RST_EN2_REG
#define PC_RSA_RST_BIT (1u << 21)                                     // REG_RST_EN_RSA
#define PC_RSA_HOLD_REG 0x500E60C8u                                   // HP_SYS_CLKRST_HP_RST_EN2_REG (same reg)
#define PC_RSA_HOLD_CLEAR ((1u << 14) | (1u << 17) | (1u << 20))      // parent-crypto | DS | ECDSA
#define PC_RSA_HAS_PD 0                                               // P4 RSA memory is always powered
#elif defined(CONFIG_IDF_TARGET_ESP32C6) && CONFIG_IDF_TARGET_ESP32C6 // hw_ver3 regs, PCR clock/reset
#define PC_RSA_BASE 0x6008A000u
#define PC_RSA_CLK_REG 0x600960D0u  // PCR_RSA_CONF_REG
#define PC_RSA_CLK_BIT (1u << 0)    // PCR_RSA_CLK_EN
#define PC_RSA_RST_REG 0x600960D0u  // PCR_RSA_CONF_REG (same reg)
#define PC_RSA_RST_BIT (1u << 1)    // PCR_RSA_RST_EN
#define PC_RSA_HOLD_REG 0x600960E0u // PCR_DS_CONF_REG (a DIFFERENT register)
#define PC_RSA_HOLD_CLEAR (1u << 1) // PCR_DS_RST_EN
#define PC_RSA_HAS_PD 1
#define PC_RSA_PD_REG 0x600960D4u                  // PCR_RSA_PD_CTRL_REG
#define PC_RSA_PD_UP_CLEAR ((1u << 0) | (1u << 2)) // clear PCR_RSA_MEM_PD + PCR_RSA_MEM_FORCE_PD to power up
#define PC_RSA_PD_DOWN_BIT (1u << 0)               // PCR_RSA_MEM_PD reads 1 when powered down
#elif defined(CONFIG_IDF_TARGET_ESP32C5) && CONFIG_IDF_TARGET_ESP32C5 // hw_ver3 regs, PCR (DS + ECDSA resets)
#define PC_RSA_BASE 0x6008A000u
#define PC_RSA_CLK_REG 0x600960D4u   // PCR_RSA_CONF_REG
#define PC_RSA_CLK_BIT (1u << 0)     // PCR_RSA_CLK_EN
#define PC_RSA_RST_REG 0x600960D4u   // PCR_RSA_CONF_REG (same reg)
#define PC_RSA_RST_BIT (1u << 1)     // PCR_RSA_RST_EN
#define PC_RSA_HOLD_REG 0x600960E4u  // PCR_DS_CONF_REG
#define PC_RSA_HOLD_CLEAR (1u << 1)  // PCR_DS_RST_EN
#define PC_RSA_HOLD2_REG 0x600960ECu // PCR_ECDSA_CONF_REG (a second, separate register)
#define PC_RSA_HOLD2_CLEAR (1u << 1) // PCR_ECDSA_RST_EN
#define PC_RSA_HAS_PD 1
#define PC_RSA_PD_REG 0x600960D8u                  // PCR_RSA_PD_CTRL_REG
#define PC_RSA_PD_UP_CLEAR ((1u << 0) | (1u << 2)) // clear PCR_RSA_MEM_PD + PCR_RSA_MEM_FORCE_PD to power up
#define PC_RSA_PD_DOWN_BIT (1u << 0)               // PCR_RSA_MEM_PD reads 1 when powered down
#elif defined(CONFIG_IDF_TARGET_ESP32H2) && CONFIG_IDF_TARGET_ESP32H2 // hw_ver3 regs, PCR (DS + ECDSA resets)
#define PC_RSA_BASE 0x6008A000u
#define PC_RSA_CLK_REG 0x600960CCu   // PCR_RSA_CONF_REG
#define PC_RSA_CLK_BIT (1u << 0)     // PCR_RSA_CLK_EN
#define PC_RSA_RST_REG 0x600960CCu   // PCR_RSA_CONF_REG (same reg)
#define PC_RSA_RST_BIT (1u << 1)     // PCR_RSA_RST_EN
#define PC_RSA_HOLD_REG 0x600960DCu  // PCR_DS_CONF_REG
#define PC_RSA_HOLD_CLEAR (1u << 1)  // PCR_DS_RST_EN
#define PC_RSA_HOLD2_REG 0x600960E4u // PCR_ECDSA_CONF_REG (a second, separate register)
#define PC_RSA_HOLD2_CLEAR (1u << 1) // PCR_ECDSA_RST_EN
#define PC_RSA_HAS_PD 1
#define PC_RSA_PD_REG 0x600960D0u                  // PCR_RSA_PD_CTRL_REG
#define PC_RSA_PD_UP_CLEAR ((1u << 0) | (1u << 2)) // clear PCR_RSA_MEM_PD + PCR_RSA_MEM_FORCE_PD to power up
#define PC_RSA_PD_DOWN_BIT (1u << 0)               // PCR_RSA_MEM_PD reads 1 when powered down
#else
#error "esp_crypto_hal: PC_RSA_MODMUL_HW is on but no RSA register map for this die - add its bases/bits"
#endif

// ── RSA block register offsets (identical across hw_ver1 and hw_ver3) ────────────────────────────────────
#define PC_RSA_MEM_M (PC_RSA_BASE + 0x000u)  // operand M block (words)
#define PC_RSA_MEM_Z (PC_RSA_BASE + 0x200u)  // result Z block (also seeded with R^2 mod m)
#define PC_RSA_MEM_Y (PC_RSA_BASE + 0x400u)  // operand Y block
#define PC_RSA_MEM_X (PC_RSA_BASE + 0x600u)  // operand X block
#define PC_RSA_MPRIME (PC_RSA_BASE + 0x800u) // Montgomery m' (mod 2^32)
#define PC_RSA_MODE (PC_RSA_BASE + 0x804u)   // operand length in words, minus 1
#define PC_RSA_CLEAN (PC_RSA_BASE + 0x808u)  // memory-init: reads non-zero while initializing, 0 when ready
#define PC_RSA_START (PC_RSA_BASE + 0x810u)  // write 1 to start the modular multiply
#define PC_RSA_DONE (PC_RSA_BASE + 0x818u)   // reads 1 once the op is complete (interrupt/idle bit)
#define PC_RSA_INTCLR (PC_RSA_BASE + 0x81Cu) // write 1 to clear the completion flag
#define PC_RSA_INTENA (PC_RSA_BASE + 0x82Cu) // completion-interrupt enable (we poll: keep 0)

/**
 * @brief Acquire the RSA accelerator for a run of modular multiplies (PC lock + direct-register bring-up).
 * @note  Bracket every batch of @ref pc_rsa_modmul with acquire/release. Implemented in esp_crypto_hal.cpp
 *        (the exclusivity mutex must be one global instance, so it cannot live in this header). Poll-only.
 */
void pc_rsa_hw_acquire(void);

/** @brief Release the RSA accelerator (drop the PC lock). */
void pc_rsa_hw_release(void);

/**
 * @brief `z = x*y mod m` (@p words limbs) on the RSA accelerator. Requires @ref pc_rsa_hw_acquire first.
 * @param z      result, @p words little-endian limbs (safe to alias @p x or @p y).
 * @param x,y    operands, canonical (< m).
 * @param m      the modulus (canonical @p words limbs).
 * @param mprime Montgomery m' = -m^-1 mod 2^32.
 * @param rinv   R^2 mod m; preloaded into the result block so MODMULT returns the plain residue x*y mod m
 *               rather than a Montgomery form. Output is canonical (< m).
 */
static inline void pc_rsa_modmul(uint32_t *z, const uint32_t *x, const uint32_t *y, const uint32_t *m, uint32_t mprime,
                                 const uint32_t *rinv, unsigned words)
{
    volatile uint32_t *M = (volatile uint32_t *)(uintptr_t)PC_RSA_MEM_M;
    volatile uint32_t *X = (volatile uint32_t *)(uintptr_t)PC_RSA_MEM_X;
    volatile uint32_t *Y = (volatile uint32_t *)(uintptr_t)PC_RSA_MEM_Y;
    volatile uint32_t *Z = (volatile uint32_t *)(uintptr_t)PC_RSA_MEM_Z;
    PC_HW_REG(PC_RSA_MODE) = words - 1u; // mode = words - 1
    PC_HW_REG(PC_RSA_MPRIME) = mprime;
    for (unsigned i = 0; i < words; i++)
    {
        M[i] = m[i];
        X[i] = x[i];
        Y[i] = y[i];
        Z[i] = rinv[i]; // R^2 mod m in the result block -> plain (non-Montgomery) output
    }
    PC_HW_REG(PC_RSA_INTCLR) = 1u; // clear any stale done flag before starting
    PC_HW_REG(PC_RSA_START) = 1u;
    while (PC_HW_REG(PC_RSA_DONE) == 0u) // wait until the done bit reads 1
    {
    }
    PC_HW_REG(PC_RSA_INTCLR) = 1u;
    for (unsigned i = 0; i < words; i++)
    {
        z[i] = Z[i];
    }
}

#endif // PC_RSA_MODMUL_HW
#endif // PROTOCORE_ESP_CRYPTO_HAL_H
