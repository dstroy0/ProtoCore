// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
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

#include "server/signaling/gpio_map.h"
#include "mmgr/protomem.h"
#include "server/clock/clock.h" // protocore_millis()

#if PROTOCORE_ENABLE_GPIO_MAP

#include "mmgr/protoframe.h"

const char *protocore_gpio_dir_name(protocore_gpio_dir dir)
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

int32_t protocore_gpio_json(const protocore_gpio_pin *pins, uint8_t count, char *out, uint32_t cap)
{
    if (!out || cap == 0)
    {
        return 0;
    }
    out[0] = '\0';
    if (!pins)
    {
        return 0;
    }
    if (frame.append(out, cap, GPIO_OPEN, NULL, 0) == 0)
    {
        return 0;
    }
    for (uint8_t i = 0; i < count; i++)
    {
        const protocore_gpio_pin *p = &pins[i];
        if (frame.append(out, cap, GPIO_PIN,
                         (const protocore_fval[]){PROTOCORE_VSTR(PROTOCORE_JSON_SEP[!!i]),
                                                  PROTOCORE_VU32((uint32_t)p->pin), PROTOCORE_VJSON(p->label),
                                                  PROTOCORE_VJSON(protocore_gpio_dir_name(p->dir)),
                                                  PROTOCORE_VU32((uint32_t)(!!p->level))},
                         5) == 0)
        {
            return 0;
        }
    }
    return (int32_t)frame.append(out, cap, GPIO_CLOSE, NULL, 0);
}

// Read the decimal integer that follows "name=" in a form-encoded body. Returns
// false if the field is absent or has no digits.
static proto_bool form_field_uint(const char *body, size_t len, const char *name, unsigned *out)
{
    size_t nlen = strnlen(name, len + 1);
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
            v = v * 10 + (unsigned)(body[j] - '0');
        }
        *out = v;
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

proto_bool protocore_gpio_parse_set(const char *body, size_t len, uint8_t *pin, uint8_t *level)
{
    if (!body || !pin || !level)
    {
        return PROTO_FALSE;
    }
    unsigned p;
    unsigned l;
    if (!form_field_uint(body, len, "pin", &p) || !form_field_uint(body, len, "level", &l))
    {
        return PROTO_FALSE;
    }
    *pin = (uint8_t)p;
    *level = l ? 1 : 0;
    return PROTO_TRUE;
}

proto_bool protocore_gpio_is_output(const protocore_gpio_pin *pins, uint8_t count, uint8_t pin)
{
    if (!pins)
    {
        return PROTO_FALSE;
    }
    for (uint8_t i = 0; i < count; i++)
    {
        if (pins[i].pin == pin && pins[i].dir == PROTOCORE_GPIO_DIR_OUT)
        {
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

#if PROTOCORE_HAS_GPIO

void protocore_gpio_begin_pins(const protocore_gpio_pin *pins, uint8_t count)
{
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

void protocore_gpio_read(protocore_gpio_pin *pins, uint8_t count)
{
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

void protocore_gpio_write(uint8_t pin, uint8_t level)
{
    protocore_platform_gpio_write((uint8_t)(pin), PROTOCORE_GPIO_LEVEL[!!level]);
}

#else // no pin seam

void protocore_gpio_begin_pins(const protocore_gpio_pin *pins, uint8_t count)
{
    (void)pins;
    (void)count;
}

void protocore_gpio_read(protocore_gpio_pin *pins, uint8_t count)
{
    (void)pins;
    (void)count;
}

void protocore_gpio_write(uint8_t pin, uint8_t level)
{
    (void)pin;
    (void)level;
}

#endif // PROTOCORE_HAS_GPIO

#endif // PROTOCORE_ENABLE_GPIO_MAP
