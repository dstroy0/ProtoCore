// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file gpio_map.c
 * @brief GPIO pin-mapper direction names, JSON serializer, control parser, and
 *        the digital read / write helpers.
 *
 * The serializer and the `pin=&level=` parser are pure (host-tested); the digital
 * I/O goes through the board profile's protocore_platform_gpio_* where a pin seam exists
 * and is a no-op where there is none. No server dependency lives here.
 */

#include "protocore_config.h" // the entry point: the enable gate below, and the widths

#if PROTOCORE_ENABLE_GPIO_MAP

#include "mmgr/protomem/protomem.h"
#include "mmgr/protostr/protostr.h"
#include "server/clock/clock.h" // protocore_millis()
#include "server/signaling/gpio_map/gpio_map.h"

#include "mmgr/protoframe/protoframe.h"

// The wire name for a direction, so the serializer names one without going through the handle it is
// itself being called on.
static const char *dir_name_of(protocore_gpio_dir dir)
{
    switch (dir)
    {
    case PROTOCORE_GPIO_DIR_IN:
        return "in";
    case PROTOCORE_GPIO_DIR_IN_PULLUP:
        return "in_pullup";
    case PROTOCORE_GPIO_DIR_IN_PULLDOWN:
        return "in_pulldown";
    case PROTOCORE_GPIO_DIR_OUT:
        return "out";
    default:
        return "in";
    }
}

void protocore_gpio_map_dir_name(uint8_t *restrict work)
{
    (void)work;
    GpioMapV.text = dir_name_of(GpioMapV.args.dir);
}

// The document is three frames: the object that opens the array, one object per pin, and the
// close. The separating comma is the pin frame's first field so a pin is one append either way.
// The item index selects it; !!i is 0 or 1, so the separator is a load rather than a branch.
static const char *const PROTOCORE_JSON_SEP[2] = {"", ","};

static const protocore_field GPIO_OPEN[] = {{PROTOCORE_FK_LIT, 0, 9, "{\"pins\":["}, PROTOCORE_END};
static const protocore_field GPIO_PIN[] = {
    PROTOCORE_STR,                           // "," from the second pin on
    {PROTOCORE_FK_LIT, 0, 7, "{\"pin\":"},   //
    PROTOCORE_U32,                           // pin number
    {PROTOCORE_FK_LIT, 0, 9, ",\"label\":"}, //
    PROTOCORE_JSON,                          // label, quoted and escaped
    {PROTOCORE_FK_LIT, 0, 7, ",\"dir\":"},   //
    PROTOCORE_JSON,                          // direction name
    {PROTOCORE_FK_LIT, 0, 9, ",\"level\":"}, //
    PROTOCORE_U32,                           // 0 or 1
    {PROTOCORE_FK_LIT, 0, 1, "}"},           //
    PROTOCORE_END,
};
static const protocore_field GPIO_CLOSE[] = {{PROTOCORE_FK_LIT, 0, 2, "]}"}, PROTOCORE_END};

void protocore_gpio_map_json(uint8_t *restrict work)
{
    (void)work;
    const protocore_gpio_pin *pins = GpioMapV.args.pins;
    const uint8_t count = GpioMapV.args.count;
    char *out = GpioMapV.out_args.out;
    const uint32_t cap = GpioMapV.out_args.cap;

    if (!out || cap == 0)
    {
        GpioMapV.n = -1;
        return;
    }
    out[0] = '\0';
    if (!pins)
    {
        GpioMapV.n = -1;
        return;
    }
    if (frame.append(out, cap, GPIO_OPEN, NULL, 0) == 0)
    {
        GpioMapV.n = -1;
        return;
    }
    for (uint8_t i = 0; i < count; i++)
    {
        const protocore_gpio_pin *p = &pins[i];
        if (frame.append(out, cap, GPIO_PIN,
                         (const protocore_fval[]){PROTOCORE_VSTR(PROTOCORE_JSON_SEP[!!i]),
                                                  PROTOCORE_VU32((uint32_t)p->pin), PROTOCORE_VJSON(p->label),
                                                  PROTOCORE_VJSON(dir_name_of(p->dir)),
                                                  PROTOCORE_VU32((uint32_t)(!!p->level))},
                         5) == 0)
        {
            GpioMapV.n = -1;
            return;
        }
    }
    // A document always carries its two braces, so nothing written is the close not fitting.
    const int32_t written = (int32_t)frame.append(out, cap, GPIO_CLOSE, NULL, 0);
    GpioMapV.n = (written == 0) ? -1 : written;
}

// Read the decimal integer that follows "name=" in a form-encoded body. Returns
// false if the field is absent or has no digits.
static proto_bool form_field_uint(const char *body, size_t len, const char *name, unsigned *out)
{
    size_t nlen = str.len(name, len + 1);
    for (size_t i = 0; i + nlen + 1 <= len; i++)
    {
        proto_bool at_field = (i == 0) || body[i - 1] == '&';
        if (!at_field || mem.cmp(body + i, name, nlen) != 0 || body[i + nlen] != '=')
        {
            continue;
        }
        size_t j = i + nlen + 1;
        if (j >= len || body[j] < '0' || body[j] > '9')
        {
            return PROTO_FALSE;
        }
        unsigned v = 0;
        for (; j < len && body[j] >= '0' && body[j] <= '9'; j++)
        {
            const unsigned d = (unsigned)(body[j] - '0');
            // The bound is checked before the multiply, so the accumulator never wraps.
            const unsigned uint_max = (unsigned)-1;
            if (v > (uint_max - d) / 10u)
            {
                return PROTO_FALSE;
            }
            v = v * 10u + d;
        }
        *out = v;
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

void protocore_gpio_map_parse_set(uint8_t *restrict work)
{
    (void)work;
    const char *body = GpioMapV.parse_args.body;
    const size_t len = GpioMapV.parse_args.len;
    uint8_t *pin = GpioMapV.parse_args.pin_out;
    uint8_t *level = GpioMapV.parse_args.level_out;

    if (!body || !pin || !level)
    {
        GpioMapV.ok = PROTO_FALSE;
        return;
    }
    unsigned p;
    unsigned l;
    if (!form_field_uint(body, len, "pin", &p) || !form_field_uint(body, len, "level", &l))
    {
        GpioMapV.ok = PROTO_FALSE;
        return;
    }
    // The field is a uint8_t, so a larger value has no pin to name. Narrowing it would deliver a
    // different pin, one the table may well declare an output.
    if (p > 0xFFu)
    {
        GpioMapV.ok = PROTO_FALSE;
        return;
    }
    *pin = (uint8_t)p;
    *level = l ? 1 : 0;
    GpioMapV.ok = PROTO_TRUE;
}

void protocore_gpio_map_is_output(uint8_t *restrict work)
{
    (void)work;
    const protocore_gpio_pin *pins = GpioMapV.args.pins;
    const uint8_t count = GpioMapV.args.count;
    const uint8_t pin = GpioMapV.args.pin;

    if (!pins)
    {
        GpioMapV.ok = PROTO_FALSE;
        return;
    }
    for (uint8_t i = 0; i < count; i++)
    {
        if (pins[i].pin == pin && pins[i].dir == PROTOCORE_GPIO_DIR_OUT)
        {
            GpioMapV.ok = PROTO_TRUE;
            return;
        }
    }
    GpioMapV.ok = PROTO_FALSE;
}

#if PROTOCORE_HAS_GPIO

void protocore_gpio_map_begin_pins(uint8_t *restrict work)
{
    (void)work;
    const protocore_gpio_pin *pins = GpioMapV.args.pins;
    const uint8_t count = GpioMapV.args.count;

    if (!pins)
    {
        return;
    }
    for (uint8_t i = 0; i < count; i++)
    {
        // The case label is this enum; the argument is the board profile's own pin-mode number.
        switch (pins[i].dir)
        {
        case PROTOCORE_GPIO_DIR_OUT:
            protocore_platform_gpio_mode((uint8_t)(pins[i].pin), PROTOCORE_GPIO_OUT);
            break;
        case PROTOCORE_GPIO_DIR_IN_PULLUP:
            protocore_platform_gpio_mode((uint8_t)(pins[i].pin), PROTOCORE_GPIO_IN_PULLUP);
            break;
        case PROTOCORE_GPIO_DIR_IN_PULLDOWN:
            protocore_platform_gpio_mode((uint8_t)(pins[i].pin), PROTOCORE_GPIO_IN_PULLDOWN);
            break;
        default:
            protocore_platform_gpio_mode((uint8_t)(pins[i].pin), PROTOCORE_GPIO_IN);
            break;
        }
    }
}

void protocore_gpio_map_sample(uint8_t *restrict work)
{
    (void)work;
    protocore_gpio_pin *pins = GpioMapV.args.pins_rw;
    const uint8_t count = GpioMapV.args.count;

    if (!pins)
    {
        return;
    }
    for (uint8_t i = 0; i < count; i++)
    {
        pins[i].level = (uint8_t)(protocore_platform_gpio_read((uint8_t)(pins[i].pin)) ? 1 : 0);
    }
}

// level indexes this; !!level is 0 or 1, so the selection is a load rather than a branch.
static const uint8_t PROTOCORE_GPIO_LEVEL[2] = {PROTOCORE_GPIO_LOW, PROTOCORE_GPIO_HIGH};

void protocore_gpio_map_write(uint8_t *restrict work)
{
    (void)work;
    const uint8_t pin = GpioMapV.args.pin;
    const uint8_t level = GpioMapV.args.level;

    protocore_platform_gpio_write((uint8_t)(pin), PROTOCORE_GPIO_LEVEL[!!level]);
}

#else // no pin seam

void protocore_gpio_map_begin_pins(uint8_t *restrict work)
{
    (void)work;
    const protocore_gpio_pin *pins = GpioMapV.args.pins;
    const uint8_t count = GpioMapV.args.count;

    (void)pins;
    (void)count;
}

void protocore_gpio_map_sample(uint8_t *restrict work)
{
    (void)work;
    protocore_gpio_pin *pins = GpioMapV.args.pins_rw;
    const uint8_t count = GpioMapV.args.count;

    (void)pins;
    (void)count;
}

void protocore_gpio_map_write(uint8_t *restrict work)
{
    (void)work;
    const uint8_t pin = GpioMapV.args.pin;
    const uint8_t level = GpioMapV.args.level;

    (void)pin;
    (void)level;
}

#endif // PROTOCORE_HAS_GPIO

// The route installer lives in gpio_map_routes.c, the arm that has an HTTP surface to install on;
// it is bound here so the whole surface is one initializer rather than a runtime install. Weak, so
// the pin core links on its own - the serializer and the parser are host-tested without the server,
// and gpio_map_routes.c overrides this the moment it is in the build.
__attribute__((weak)) void protocore_gpio_map_begin(uint8_t *restrict work)
{
}

// Designated, so a member's position in the struct does not decide what it binds to.
/** @brief The operands and the outcome. */
GpioMapVars GpioMapV;

#endif // PROTOCORE_ENABLE_GPIO_MAP
