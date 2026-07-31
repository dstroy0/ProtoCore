// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file board_profile.h
 * @brief Per-variant default sizing: pick sane PC_* defaults for the target board.
 *
 * The library's sizing defaults used to be a single flat set tuned to fit the smallest
 * classic-ESP32 DRAM ceiling, so a board with far more RAM/flash silently inherited the
 * same cramped numbers. This selector instead layers per-variant default files:
 *
 *   - chip variant  (classic / s3 / c6 / p4) - HW-specific switches + chip-appropriate
 *     defaults (internal SRAM, core count, crypto HW accel).
 *   - PSRAM size    (8 / 16 / 32 MB)          - RAM-backed buffer sizes; a given chip
 *     ships with or without PSRAM, so it is its own axis.
 *   - flash size    (8 / 16 / 32 MB)          - flash-backed sizing; likewise independent.
 *
 * Every default is set behind an `#ifndef`, so precedence is "first definition wins":
 *   your -D / build_opt.h override  >  PSRAM profile  >  flash profile  >  chip profile
 * (chip files pull in classic_defaults.h last as the universal floor). Nothing here forces
 * a value you set yourself.
 *
 * Chip is auto-detected from the SoC target macro. PSRAM/flash size can't be read reliably
 * from the Arduino core, so set them for your board (they default to "none / smallest"):
 * @code
 *   build_flags = -DPC_PSRAM_MB=8 -DPC_FLASH_MB=16
 * @endcode
 * ESP-IDF builds auto-fill both from the sdkconfig below.
 */

#ifndef PROTOCORE_BOARD_PROFILE_H
#define PROTOCORE_BOARD_PROFILE_H

// The chip / flash / PSRAM selection below keys off the ESP-IDF sdkconfig macros (CONFIG_IDF_TARGET_*,
// CONFIG_ESPTOOLPY_FLASHSIZE_*, CONFIG_SPIRAM_SIZE). sdkconfig.h is NOT force-included in every translation
// unit, and protocore_config.h pulls this header in before any esp/Arduino header - so a TU that includes
// protocore_config.h first (e.g. tls.cpp) would see none of those macros and EVERY board would silently fall
// through to the classic floor (wrong sizing + HW-accel flags, and inconsistent with TUs that happen to pull
// an Arduino header in first). Pull sdkconfig.h in here when it is on the include path (all ESP-IDF /
// Arduino-ESP32 builds have it) so the detection is include-order-independent. It is absent on host/native
// builds, where the classic profile is the correct choice anyway.
#if defined(__has_include)
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif

// The one vendor/die selector: derives PC_VENDOR_ESP / _STM / _RP / _TI / _HOST from the toolchain target
// macros so the per-vendor partitioning keys off PC_VENDOR_* instead of re-testing CONFIG_IDF_TARGET_* here.
#include "pc_platform.h"

// --- flash size (MB): honor an explicit -DPC_FLASH_MB, else read the ESP-IDF sdkconfig ---
#if !defined(PC_FLASH_MB)
#if defined(CONFIG_ESPTOOLPY_FLASHSIZE_32MB)
#define PC_FLASH_MB 32
#elif defined(CONFIG_ESPTOOLPY_FLASHSIZE_16MB)
#define PC_FLASH_MB 16
#elif defined(CONFIG_ESPTOOLPY_FLASHSIZE_8MB)
#define PC_FLASH_MB 8
#elif defined(CONFIG_ESPTOOLPY_FLASHSIZE_4MB)
#define PC_FLASH_MB 4
#elif defined(CONFIG_ESPTOOLPY_FLASHSIZE_2MB)
#define PC_FLASH_MB 2
#endif
#endif

// --- PSRAM size (MB): honor an explicit -DPC_PSRAM_MB, else read the ESP-IDF sdkconfig ---
#if !defined(PC_PSRAM_MB) && defined(CONFIG_SPIRAM_SIZE)
#if CONFIG_SPIRAM_SIZE >= (32 * 1024 * 1024)
#define PC_PSRAM_MB 32
#elif CONFIG_SPIRAM_SIZE >= (16 * 1024 * 1024)
#define PC_PSRAM_MB 16
#elif CONFIG_SPIRAM_SIZE >= (8 * 1024 * 1024)
#define PC_PSRAM_MB 8
#elif CONFIG_SPIRAM_SIZE >= (4 * 1024 * 1024)
#define PC_PSRAM_MB 4
#elif CONFIG_SPIRAM_SIZE >= (2 * 1024 * 1024)
#define PC_PSRAM_MB 2
#endif
#endif

// --- PSRAM-size profile (most specific: RAM-backed buffers scale with available PSRAM) ---
#if defined(PC_PSRAM_MB)
#if PC_PSRAM_MB >= 32
#include "esp/32mbpsram.h"
#elif PC_PSRAM_MB >= 16
#include "esp/16mbpsram.h"
#elif PC_PSRAM_MB >= 8
#include "esp/8mbpsram.h"
#elif PC_PSRAM_MB >= 4
#include "esp/4mbpsram.h"
#elif PC_PSRAM_MB >= 2
#include "esp/2mbpsram.h"
#endif
#endif

// --- flash-size profile (flash-backed sizing scales with available flash) ---
#if defined(PC_FLASH_MB)
#if PC_FLASH_MB >= 32
#include "esp/32mbflash.h"
#elif PC_FLASH_MB >= 16
#include "esp/16mbflash.h"
#elif PC_FLASH_MB >= 8
#include "esp/8mbflash.h"
#elif PC_FLASH_MB >= 4
#include "esp/4mbflash.h"
#elif PC_FLASH_MB >= 2
#include "esp/2mbflash.h"
#endif
#endif

// --- chip profile (auto-selected from the SoC target macro; each pulls classic_defaults.h in
//     last as the universal sizing floor). Every macro name is uppercase with no hyphen/underscore
//     in the suffix (e.g. ...ESP32C61), verified against ESP-IDF's components/soc/<target>/.
//     S31/H4/H21 are preview targets (in ESP-IDF master, not a stable release yet). ---
#if PC_VENDOR_ESP
#if defined(CONFIG_IDF_TARGET_ESP32P4)
#include "esp/p4_defaults.h"
#elif defined(CONFIG_IDF_TARGET_ESP32S31)
#include "esp/s31_defaults.h"
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#include "esp/s3_defaults.h"
#elif defined(CONFIG_IDF_TARGET_ESP32S2)
#include "esp/s2_defaults.h"
#elif defined(CONFIG_IDF_TARGET_ESP32C2)
#include "esp/c2_defaults.h"
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
#include "esp/c3_defaults.h"
#elif defined(CONFIG_IDF_TARGET_ESP32C5)
#include "esp/c5_defaults.h"
#elif defined(CONFIG_IDF_TARGET_ESP32C61)
#include "esp/c61_defaults.h"
#elif defined(CONFIG_IDF_TARGET_ESP32C6)
#include "esp/c6_defaults.h"
#elif defined(CONFIG_IDF_TARGET_ESP32H21)
#include "esp/h21_defaults.h"
#elif defined(CONFIG_IDF_TARGET_ESP32H2)
#include "esp/h2_defaults.h"
#elif defined(CONFIG_IDF_TARGET_ESP32H4)
#include "esp/h4_defaults.h"
#else
// Classic ESP32 (no dedicated profile) lands on the universal floor.
#include "classic_defaults.h"
#endif
#else
// Non-ESP vendors (STM / RP / TI) and host/native builds: the universal sizing floor until per-vendor
// profiles land (the multi-vendor portability track). Behavior-identical to the pre-selector path, which
// fell through to classic_defaults.h whenever no CONFIG_IDF_TARGET_* macro was defined.
#include "classic_defaults.h"
#endif

#endif // PROTOCORE_BOARD_PROFILE_H
