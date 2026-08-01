// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file gpio_map.h
 * @brief Browser GPIO pin-mapper / diagnostics (PC_ENABLE_GPIO_MAP).
 *
 * Exposes a compile-time table of GPIO pins (number, label, configured direction,
 * live level) as JSON so a browser diag panel can show the pin map and toggle
 * outputs. The live read (digitalRead) and write (pinMode / digitalWrite) use the
 * Arduino API on ESP32; the JSON serializer and the control-POST parser are pure
 * and host-tested. No allocation: the pin table is caller-owned and the JSON is
 * written into a caller buffer.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_GPIO_MAP_H
#define PROTOCORE_GPIO_MAP_H

#include "protocore_config.h"
#include <stddef.h>
#include <stdint.h>

#if PC_ENABLE_GPIO_MAP

/** @brief Configured direction of a mapped pin (how the panel renders / drives it). */
enum class pc_gpio_dir : uint8_t
{
    PC_GPIO_IN = 0,      ///< read-only input.
    PC_GPIO_IN_PULLUP,   ///< input with internal pull-up.
    PC_GPIO_IN_PULLDOWN, ///< input with internal pull-down.
    PC_GPIO_OUT,         ///< output (drivable from the panel).
};

/** @brief One mapped GPIO pin. */
struct pc_gpio_pin
{
    uint8_t pin;       ///< GPIO number.
    const char *label; ///< human label (null-terminated, caller-owned).
    pc_gpio_dir dir;   ///< pin direction.
    uint8_t level;     ///< live level (0 / 1); filled by pc_gpio_read.
};

// ---------------------------------------------------------------------------
// Host-testable core
// ---------------------------------------------------------------------------

/** @brief Short name for a direction ("in", "in_pullup", "in_pulldown", "out"). */
const char *pc_gpio_dir_name(pc_gpio_dir dir);

/**
 * @brief Serialize a pin array as JSON `{"pins":[...]}` into @p out.
 * @return characters written, or 0 if @p cap is too small (fail-closed).
 */
int32_t pc_gpio_json(const pc_gpio_pin *pins, uint8_t count, char *out, uint32_t cap);

/**
 * @brief Parse a control body of the form `pin=<n>&level=<0|1>` (form-encoded).
 * @return true if both fields parsed into @p pin / @p level.
 */
bool pc_gpio_parse_set(const char *body, size_t len, uint8_t *pin, uint8_t *level);

/** @brief True if @p pin is a drivable output in the table (guards a control POST). */
bool pc_gpio_is_output(const pc_gpio_pin *pins, uint8_t count, uint8_t pin);

// ---------------------------------------------------------------------------
// ESP32 integration (no-ops on host builds)
// ---------------------------------------------------------------------------

/** @brief Apply pinMode() for every entry per its direction (call once at setup). */
void pc_gpio_begin_pins(const pc_gpio_pin *pins, uint8_t count);

/** @brief Refresh each pin's live @c level via digitalRead (no-op on host). */
void pc_gpio_read(pc_gpio_pin *pins, uint8_t count);

/** @brief Drive an output @p pin to @p level via digitalWrite (no-op on host). */
void pc_gpio_write(uint8_t pin, uint8_t level);

/**
 * @brief Serve the GPIO map at @p path: GET returns the JSON, POST drives an
 *        output (body `pin=<n>&level=<0|1>`, only pins marked pc_gpio_dir::PC_GPIO_OUT).
 *        The pin table is caller-owned and must outlive the server.
 */
void pc_gpio_map_begin(const char *path, pc_gpio_pin *pins, uint8_t count);

#endif // PC_ENABLE_GPIO_MAP
#endif // PROTOCORE_GPIO_MAP_H
