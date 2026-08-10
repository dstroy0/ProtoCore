// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file gpio_map.c
 * @brief GPIO pin-mapper direction names, JSON serializer, control parser, and
 *        the digital read / write helpers.
 *
 * The serializer and the `pin=&level=` parser are pure (host-tested); the digital
 * I/O goes through the board profile's pc_platform_gpio_* where a pin seam exists
 * and is a no-op where there is none. No server dependency lives here.
 */

#include "server/signaling/gpio_map.h"
#include "mmgr/protomem.h"
#include "server/clock/clock.h" // pc_millis()

#if PC_ENABLE_GPIO_MAP

#include "mmgr/protoframe.h"

const char *pc_gpio_dir_name(pc_gpio_dir dir)
{
    switch (dir)
    {
    case PC_GPIO_DIR_IN:
        return "in";
    case PC_GPIO_DIR_IN_PULLUP:
        return "in_pullup";
    case PC_GPIO_DIR_IN_PULLDOWN:
        return "in_pulldown";
    case PC_GPIO_DIR_OUT:
        return "out";
    default:
        return "in";
    }
}

// The document is three frames: the object that opens the array, one object per pin, and the
// close. The separating comma is the pin frame's first field so a pin is one append either way.
// The item index selects it; !!i is 0 or 1, so the separator is a load rather than a branch.
static const char *const PC_JSON_SEP[2] = {"", ","};

static const pc_field GPIO_OPEN[] = {{PC_FK_LIT, 0, 9, "{\"pins\":["}, PC_END};
static const pc_field GPIO_PIN[] = {
    PC_STR,                           // "," from the second pin on
    {PC_FK_LIT, 0, 7, "{\"pin\":"},   //
    PC_U32,                           // pin number
    {PC_FK_LIT, 0, 9, ",\"label\":"}, //
    PC_JSON,                          // label, quoted and escaped
    {PC_FK_LIT, 0, 7, ",\"dir\":"},   //
    PC_JSON,                          // direction name
    {PC_FK_LIT, 0, 9, ",\"level\":"}, //
    PC_U32,                           // 0 or 1
    {PC_FK_LIT, 0, 1, "}"},           //
    PC_END,
};
static const pc_field GPIO_CLOSE[] = {{PC_FK_LIT, 0, 2, "]}"}, PC_END};

int32_t pc_gpio_json(const pc_gpio_pin *pins, uint8_t count, char *out, uint32_t cap)
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
        const pc_gpio_pin *p = &pins[i];
        if (frame.append(out, cap, GPIO_PIN,
                         (const pc_fval[]){PC_VSTR(PC_JSON_SEP[!!i]), PC_VU32((uint32_t)p->pin), PC_VJSON(p->label),
                                           PC_VJSON(pc_gpio_dir_name(p->dir)), PC_VU32((uint32_t)(!!p->level))},
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

proto_bool pc_gpio_parse_set(const char *body, size_t len, uint8_t *pin, uint8_t *level)
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

proto_bool pc_gpio_is_output(const pc_gpio_pin *pins, uint8_t count, uint8_t pin)
{
    if (!pins)
    {
        return PROTO_FALSE;
    }
    for (uint8_t i = 0; i < count; i++)
    {
        if (pins[i].pin == pin && pins[i].dir == PC_GPIO_DIR_OUT)
        {
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

#if PC_HAS_GPIO

void pc_gpio_begin_pins(const pc_gpio_pin *pins, uint8_t count)
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
        case PC_GPIO_DIR_OUT:
            pc_platform_gpio_mode((uint8_t)(pins[i].pin), PC_GPIO_OUT);
            break;
        case PC_GPIO_DIR_IN_PULLUP:
            pc_platform_gpio_mode((uint8_t)(pins[i].pin), PC_GPIO_IN_PULLUP);
            break;
        case PC_GPIO_DIR_IN_PULLDOWN:
            pc_platform_gpio_mode((uint8_t)(pins[i].pin), PC_GPIO_IN_PULLDOWN);
            break;
        default:
            pc_platform_gpio_mode((uint8_t)(pins[i].pin), PC_GPIO_IN);
            break;
        }
    }
}

void pc_gpio_read(pc_gpio_pin *pins, uint8_t count)
{
    if (!pins)
    {
        return;
    }
    for (uint8_t i = 0; i < count; i++)
    {
        pins[i].level = (uint8_t)(pc_platform_gpio_read((uint8_t)(pins[i].pin)) ? 1 : 0);
    }
}

// level indexes this; !!level is 0 or 1, so the selection is a load rather than a branch.
static const uint8_t PC_GPIO_LEVEL[2] = {PC_GPIO_LOW, PC_GPIO_HIGH};

void pc_gpio_write(uint8_t pin, uint8_t level)
{
    pc_platform_gpio_write((uint8_t)(pin), PC_GPIO_LEVEL[!!level]);
}

#else // no pin seam

void pc_gpio_begin_pins(const pc_gpio_pin *pins, uint8_t count)
{
    (void)pins;
    (void)count;
}

void pc_gpio_read(pc_gpio_pin *pins, uint8_t count)
{
    (void)pins;
    (void)count;
}

void pc_gpio_write(uint8_t pin, uint8_t level)
{
    (void)pin;
    (void)level;
}

#endif // PC_HAS_GPIO

#endif // PC_ENABLE_GPIO_MAP
