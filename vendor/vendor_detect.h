// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file vendor_detect.h
 * @brief Toolchain macros in, one PROTOCORE_VENDOR_* out.
 *
 * The only place a toolchain's own target macro is read. Every layer downstream keys off
 * `PROTOCORE_VENDOR_*` instead of `CONFIG_IDF_TARGET_*` / `STM32*` / `PICO_*`, so nothing under
 * src/ names a vendor and adding one is a subdirectory here rather than an edit across the tree.
 *
 * Exactly one is 1; every other is defined 0, so `#if PROTOCORE_VENDOR_ESP` is always valid and
 * never relies on an undefined-macro-is-0 fallback.
 *
 *   - `PROTOCORE_VENDOR_ESP` - any Espressif target (ESP-IDF `ESP_PLATFORM` / Arduino-ESP32 `ARDUINO_ARCH_ESP32`).
 *   - `PROTOCORE_VENDOR_STM` - STM32 (Arduino_Core_STM32 `ARDUINO_ARCH_STM32` / STM32Cube `USE_HAL_DRIVER`).
 *   - `PROTOCORE_VENDOR_RP`  - Raspberry Pi silicon (RP2040 / RP2350: `ARDUINO_ARCH_RP2040` / `PICO_*`).
 *   - `PROTOCORE_VENDOR_TI`  - Texas Instruments (`__TI_COMPILER_VERSION__` or an explicit force).
 *   - `PROTOCORE_HOST` - the build runs on the machine that compiled it: no silicon, no scheduler,
 *     portable software everywhere. The test envs state it; otherwise it is what no detected vendor means.
 *
 * A file with a device path and a host path selects on `PROTOCORE_HOST`, never on a vendor's own macro:
 * `ARDUINO` names one vendor's toolchain, so keying on it drops every other vendor down the host path.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_VENDOR_DETECT_H
#define PROTOCORE_VENDOR_DETECT_H

// sdkconfig.h carries CONFIG_IDF_TARGET_* on ESP-IDF / Arduino-ESP32 builds and is absent on host and on
// other vendors' toolchains. Pull it in here (guarded by __has_include) so vendor + die detection is
// include-order-independent. Preceded only by the include guard.
#if defined(__has_include)
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif

// ---------------------------------------------------------------------------
// Vendor axis - exactly one is 1.
// ---------------------------------------------------------------------------
#if defined(ESP_PLATFORM) || defined(ARDUINO_ARCH_ESP32)
#define PROTOCORE_VENDOR_ESP 1
#elif defined(ARDUINO_ARCH_STM32) || defined(USE_HAL_DRIVER) || defined(STM32_CORE_VERSION)
#define PROTOCORE_VENDOR_STM 1
#elif defined(ARDUINO_ARCH_RP2040) || defined(PICO_RP2040) || defined(PICO_RP2350) || defined(PICO_SDK_VERSION_MAJOR)
#define PROTOCORE_VENDOR_RP 1
#elif defined(__TI_COMPILER_VERSION__) || defined(PROTOCORE_VENDOR_TI_FORCE)
#define PROTOCORE_VENDOR_TI 1
#else
#define PROTOCORE_HOST 1
#endif

// Every one is defined (0 when not selected) so downstream code can `#if` it freely.
#ifndef PROTOCORE_VENDOR_ESP
#define PROTOCORE_VENDOR_ESP 0
#endif
#ifndef PROTOCORE_VENDOR_STM
#define PROTOCORE_VENDOR_STM 0
#endif
#ifndef PROTOCORE_VENDOR_RP
#define PROTOCORE_VENDOR_RP 0
#endif
#ifndef PROTOCORE_VENDOR_TI
#define PROTOCORE_VENDOR_TI 0
#endif
#ifndef PROTOCORE_HOST
#define PROTOCORE_HOST 0
#endif

// A single "targets real silicon" convenience (any vendor backend, i.e. not the host software floor).
#define PROTOCORE_VENDOR_SILICON                                                                                       \
    (PROTOCORE_VENDOR_ESP || PROTOCORE_VENDOR_STM || PROTOCORE_VENDOR_RP || PROTOCORE_VENDOR_TI)

// The detected vendor's answers: the capability values, the SDK includes, and the scheduler and
// network aliases. Exactly one is compiled. A vendor with no header here answers nothing, and
// protocore_platform.h then refuses the build on the capability it was not told about.
#if PROTOCORE_VENDOR_ESP
#include "vendor/esp/esp_answers.h"
#elif PROTOCORE_HOST
#include "test/core_setup/hal/host/host_answers.h"
#endif

#endif // PROTOCORE_VENDOR_DETECT_H
