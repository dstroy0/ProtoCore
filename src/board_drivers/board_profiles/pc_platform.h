// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file pc_platform.h
 * @brief The one vendor/die selector for the whole library.
 *
 * Multi-vendor portability rests on a single rule: every silicon-specific layer (board profiles,
 * the crypto accelerator HAL, the physical MAC + PHY) is partitioned into a per-vendor subdir and a
 * common API header pulls in exactly ONE backend per build. This header owns the "which vendor" decision
 * so nothing downstream has to re-test toolchain-specific macros - a backend keys off `PC_VENDOR_*`, not
 * off `CONFIG_IDF_TARGET_*` / `STM32*` / `PICO_*` scattered across the tree.
 *
 * Exactly one `PC_VENDOR_*` is 1; every other is defined 0 (so `#if PC_VENDOR_ESP` is always valid, never
 * relies on an undefined-macro-is-0 fallback). The vendor is derived from the toolchain's own target macro:
 *
 *   - `PC_VENDOR_ESP`  - any Espressif target (ESP-IDF `ESP_PLATFORM` / Arduino-ESP32 `ARDUINO_ARCH_ESP32`).
 *   - `PC_VENDOR_STM`  - STM32 (Arduino_Core_STM32 `ARDUINO_ARCH_STM32` / STM32Cube `USE_HAL_DRIVER`).
 *   - `PC_VENDOR_RP`   - Raspberry Pi silicon (RP2040 / RP2350: `ARDUINO_ARCH_RP2040` / `PICO_*`).
 *   - `PC_VENDOR_TI`   - Texas Instruments (`__TI_COMPILER_VERSION__` or an explicit force).
 *   - `PC_VENDOR_HOST` - native / host build (unit tests): no accelerator, portable software everywhere.
 *
 * ESP is detected first and stays byte-for-byte compatible with the pre-selector behavior: on every ESP
 * build `PC_VENDOR_ESP` is 1, and on host builds it is 0, exactly matching the old
 * `#if defined(CONFIG_IDF_TARGET_*)` test in board_profile.h.
 */

#ifndef PROTOCORE_PC_PLATFORM_H
#define PROTOCORE_PC_PLATFORM_H

#include <stdint.h>

// sdkconfig.h carries CONFIG_IDF_TARGET_* on ESP-IDF / Arduino-ESP32 builds and is absent on host and on
// other vendors' toolchains. Pull it in here (guarded by __has_include) so vendor + die detection is
// include-order-independent, the same reason board_profile.h does it. Preceded only by the include guard.
#if defined(__has_include)
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif

// ---------------------------------------------------------------------------
// Vendor axis - exactly one is 1.
// ---------------------------------------------------------------------------
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#define PC_VENDOR_ESP 1
#elif defined(ARDUINO_ARCH_STM32) || defined(USE_HAL_DRIVER) || defined(STM32_CORE_VERSION)
#define PC_VENDOR_STM 1
#elif defined(ARDUINO_ARCH_RP2040) || defined(PICO_RP2040) || defined(PICO_RP2350) || defined(PICO_SDK_VERSION_MAJOR)
#define PC_VENDOR_RP 1
#elif defined(__TI_COMPILER_VERSION__) || defined(PC_VENDOR_TI_FORCE)
#define PC_VENDOR_TI 1
#else
#define PC_VENDOR_HOST 1
#endif

// Every vendor macro is defined (0 when not selected) so downstream code can `#if PC_VENDOR_x` freely.
#ifndef PC_VENDOR_ESP
#define PC_VENDOR_ESP 0
#endif
#ifndef PC_VENDOR_STM
#define PC_VENDOR_STM 0
#endif
#ifndef PC_VENDOR_RP
#define PC_VENDOR_RP 0
#endif
#ifndef PC_VENDOR_TI
#define PC_VENDOR_TI 0
#endif
#ifndef PC_VENDOR_HOST
#define PC_VENDOR_HOST 0
#endif

// ---------------------------------------------------------------------------
// Vendor capabilities - what backend a vendor's board_drivers/ provides.
// ---------------------------------------------------------------------------
//
// The core never tests these. board_drivers/ does, to decide which backend TU compiles. Each is a
// deliberate statement by the vendor, not a default: choosing software crypto is legitimate (on some
// parts it is the only option) but it must be chosen. There is no weak symbol behind any of these -
// linking no backend is an undefined reference, linking two is a duplicate definition, and both fail
// the build rather than silently selecting one.

// AES-GCM. 1 = the vendor supplies an accelerated AEAD (board_drivers/hal/<vendor>); 0 = the portable
// software backend, which is software AES plus a table GHASH.
//
// Not a small difference and not a preference: measured sealing 1 KiB on an ESP32-S3 at 240 MHz, the
// vendor AEAD is 81,085 cycles and the software path 616,567 - 7.6x. Hand it to the vendor whenever
// there is one; choosing software is legitimate where there is not, but it has to be chosen.
#ifndef PC_HAS_HW_AESGCM
#if PC_VENDOR_ESP
#define PC_HAS_HW_AESGCM 1
#elif PC_VENDOR_HOST
#define PC_HAS_HW_AESGCM 0 // a unit-test build has no silicon by definition
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PC_HAS_HW_AESGCM (1 = accelerated AEAD in board_drivers/hal/<vendor>, 0 = portable software AES + table GHASH, ~7.6x slower where measured). Choosing software is fine; defaulting into it is not."
#endif
#endif

// DH-2048 / RSA modexp. 1 = the vendor supplies an accelerated backend; 0 = the portable software
// Montgomery backend (board_drivers/hal/portable), which is data-dependent and NOT constant time -
// see SECURITY.md, timing.
#ifndef PC_HAS_HW_BIGNUM
#if PC_VENDOR_ESP
#define PC_HAS_HW_BIGNUM 1
#elif PC_VENDOR_HOST
#define PC_HAS_HW_BIGNUM 0 // a unit-test build has no silicon by definition
#else
#error                                                                                                                 \
    "ProtoCore: this vendor must state PC_HAS_HW_BIGNUM (1 = accelerated backend in board_drivers/hal/<vendor>, 0 = portable software Montgomery, which is not constant time). Choosing software crypto is fine; defaulting into it is not."
#endif
#endif

// ---------------------------------------------------------------------------
// Execution context identity
// ---------------------------------------------------------------------------
//
// "Which execution context is running me" is a platform question, not a core one, so the core asks
// here instead of naming an RTOS. Used by the pools' debug owner tripwire to catch a borrow crossing
// tasks; it is only ever compared for equality, never interpreted.
//
// Returns 0 where there is no such concept (host builds): a single context, so every comparison
// trivially agrees and the tripwire is a no-op rather than a false alarm.
uintptr_t pc_platform_context_id(void);

// A single "targets real silicon" convenience (any vendor backend, i.e. not the host software floor).
#define PC_VENDOR_SILICON (PC_VENDOR_ESP || PC_VENDOR_STM || PC_VENDOR_RP || PC_VENDOR_TI)

#endif // PROTOCORE_PC_PLATFORM_H
