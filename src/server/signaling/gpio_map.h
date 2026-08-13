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

/** @brief The pin table a call walks, and the pin it names. */
typedef struct
{
    const protocore_gpio_pin *pins; ///< the table a call reads
    protocore_gpio_pin *pins_rw;    ///< the same table, where a sample writes levels back into it
    uint8_t count;                  ///< how many pins it holds
    uint8_t pin;                    ///< the pin a write or a lookup names
    uint8_t level;                  ///< the level a write drives
    protocore_gpio_dir dir;         ///< the direction a name lookup names
    const char *path;               ///< the route the map is served on
} GpioArgs;

/** @brief The request body a set parses, and where its two fields land. */
typedef struct
{
    const char *body;  ///< the submitted body
    size_t len;        ///< its length
    uint8_t *pin_out;  ///< where the parsed pin lands
    uint8_t *level_out; ///< where the parsed level lands
} GpioParseArgs;

/** @brief Where a report is written. */
typedef struct
{
    char *out;     ///< where the JSON lands
    uint32_t cap;  ///< how much room it has
} GpioOutArgs;

/** @brief The map's own calls, described only in gpio_map.c. */
struct GpioMapInternal;

/**
 * @brief The pin map and its HTTP surface.
 *
 * A caller sets the members a call takes, invokes it through ::GpioMap, and reads the outcome off
 * the same handle. The pin table is the caller's.
 *
 * @var GpioMapNs::args        the pin table a call walks, and the pin it names
 * @var GpioMapNs::parse_args  the request body a set parses, and where its fields land
 * @var GpioMapNs::out_args    where a report is written
 * @var GpioMapNs::ok          a call's true/false outcome
 * @var GpioMapNs::text        the direction name a lookup reports
 * @var GpioMapNs::n           bytes a report wrote, or < 0 when it did not fit
 * @var GpioMapNs::dir_name    the wire name for a direction
 * @var GpioMapNs::json        serialize the pin table
 * @var GpioMapNs::parse_set   parse a set request into its pin and level
 * @var GpioMapNs::is_output   whether the named pin is configured as an output
 * @var GpioMapNs::begin_pins  drive every pin in the table to its configured direction
 * @var GpioMapNs::sample      read every input pin's level back into the table
 * @var GpioMapNs::write       drive one output pin
 * @var GpioMapNs::begin       install the map's route and arm its pins
 * @var GpioMapNs::internal    the calls that drive and report the pins
 *
 * No storage member: the pin table is the caller's and the pins themselves live in the part.
 */
typedef struct
{
    GpioArgs args;
    GpioParseArgs parse_args;
    GpioOutArgs out_args;

    proto_bool ok;
    const char *text;
    int32_t n;

    void (*dir_name)(struct GpioMapInternal *ctx);
    void (*json)(struct GpioMapInternal *ctx);
    void (*parse_set)(struct GpioMapInternal *ctx);
    void (*is_output)(struct GpioMapInternal *ctx);
    void (*begin_pins)(struct GpioMapInternal *ctx);
    void (*sample)(struct GpioMapInternal *ctx);
    void (*write)(struct GpioMapInternal *ctx);
    void (*begin)(struct GpioMapInternal *ctx);

    struct GpioMapInternal *internal;
} GpioMapNs;

/** @brief The one symbol this module exports. */
extern GpioMapNs GpioMap;

#endif // PROTOCORE_ENABLE_GPIO_MAP

PROTOCORE_END_DECLS

#endif // PROTOCORE_GPIO_MAP_H
