// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file gpio_map.h
 * @brief Browser GPIO pin-mapper / diagnostics (PROTOCORE_ENABLE_GPIO_MAP).
 *
 * Exposes a compile-time table of GPIO pins (number, label, configured direction,
 * live level) as JSON so a browser diag panel can show the pin map and toggle
 * outputs. The live read and write go through protocore_platform_gpio_* where a pin seam
 * exists; the JSON serializer and the control-POST parser are pure and host-tested.
 * No allocation: the pin table is caller-owned and the JSON is written into a
 * caller buffer.
 *
 * @author  Douglas Quigg (dstroy0)
 * @date    2026
 */

#ifndef PROTOCORE_GPIO_MAP_H
#define PROTOCORE_GPIO_MAP_H

#include "protocore_config.h"

PROTOCORE_BEGIN_DECLS

#if PROTOCORE_ENABLE_GPIO_MAP

/**
 * @brief Configured direction of a mapped pin (how the panel renders / drives it).
 *
 * The members carry the type's own name, `PROTOCORE_GPIO_DIR_`, and not a bare `PROTOCORE_GPIO_`. The board
 * profiles spell their pin-mode argument `PROTOCORE_GPIO_IN` / `PROTOCORE_GPIO_OUT` as `#define`s, and a macro
 * rewrites a token before the compiler sees a declaration at all (docs/SYMBOLS.md section 2), so a
 * member sharing one of those names is replaced by its number wherever both headers reach one
 * translation unit. The two encodings are also different numbers, which is what made the collision
 * silent: the profile numbers a direction 0/1/2/3 as IN/OUT/PULLUP/PULLDOWN, this enum in
 * declaration order.
 */
typedef enum PROTO_ENUM_PACKED
{
    PROTOCORE_GPIO_DIR_IN = 0,      ///< read-only input.
    PROTOCORE_GPIO_DIR_IN_PULLUP,   ///< input with internal pull-up.
    PROTOCORE_GPIO_DIR_IN_PULLDOWN, ///< input with internal pull-down.
    PROTOCORE_GPIO_DIR_OUT,         ///< output (drivable from the panel).
} protocore_gpio_dir;

/** @brief One mapped GPIO pin. */
typedef struct
{
    uint8_t pin;            ///< GPIO number.
    const char *label;      ///< human label (null-terminated, caller-owned).
    protocore_gpio_dir dir; ///< pin direction.
    uint8_t level;          ///< live level (0 / 1); filled by protocore_gpio_read.
} protocore_gpio_pin;

// ---------------------------------------------------------------------------
// Host-testable core
// ---------------------------------------------------------------------------

/** @brief Short name for a direction ("in", "in_pullup", "in_pulldown", "out"). */
const char *protocore_gpio_dir_name(protocore_gpio_dir dir);

/**
 * @brief Serialize a pin array as JSON `{"pins":[...]}` into @p out.
 * @return characters written, or 0 if @p cap is too small (fail-closed).
 */
int32_t protocore_gpio_json(const protocore_gpio_pin *pins, uint8_t count, char *out, uint32_t cap);

/**
 * @brief Parse a control body of the form `pin=<n>&level=<0|1>` (form-encoded).
 * @return true if both fields parsed into @p pin / @p level.
 */
proto_bool protocore_gpio_parse_set(const char *body, size_t len, uint8_t *pin, uint8_t *level);

/** @brief True if @p pin is a drivable output in the table (guards a control POST). */
proto_bool protocore_gpio_is_output(const protocore_gpio_pin *pins, uint8_t count, uint8_t pin);

// ---------------------------------------------------------------------------
// Pin integration (no-ops with no pin seam)
// ---------------------------------------------------------------------------

/** @brief Set the mode of every entry per its direction (call once at setup). */
void protocore_gpio_begin_pins(const protocore_gpio_pin *pins, uint8_t count);

/** @brief Refresh each pin's live @c level from the seam. */
void protocore_gpio_read(protocore_gpio_pin *pins, uint8_t count);

/** @brief Drive an output @p pin to @p level. */
void protocore_gpio_write(uint8_t pin, uint8_t level);

/**
 * @brief Serve the GPIO map at @p path: GET returns the JSON, POST drives an
 *        output (body `pin=<n>&level=<0|1>`, only pins marked PROTOCORE_GPIO_DIR_OUT).
 *        The pin table is caller-owned and must outlive the server.
 */
void protocore_gpio_map_begin(const char *path, protocore_gpio_pin *pins, uint8_t count);

#endif // PROTOCORE_ENABLE_GPIO_MAP

PROTOCORE_END_DECLS

#endif // PROTOCORE_GPIO_MAP_H
