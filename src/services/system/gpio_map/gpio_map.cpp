// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file gpio_map.cpp
 * @brief GPIO pin-mapper direction names, JSON serializer, control parser, and
 *        the ESP32 digital read / write helpers.
 *
 * The serializer and the `pin=&level=` parser are pure (host-tested); the digital
 * I/O uses the Arduino API on ESP32 and is a no-op on host builds. No server
 * dependency lives here - the route is in gpio_map_routes.cpp.
 */

#include "services/system/gpio_map/gpio_map.h"

#if PC_ENABLE_GPIO_MAP

#include <stdio.h>
#include <string.h>

#include "shared_primitives/fmtbuf.h"

#if defined(ARDUINO)
#include <Arduino.h>
#endif
const char *pc_gpio_dir_name(pc_gpio_dir dir)
{
    switch (dir)
    {
    case pc_gpio_dir::PC_GPIO_IN:
        return "in";
    case pc_gpio_dir::PC_GPIO_IN_PULLUP:
        return "in_pullup";
    case pc_gpio_dir::PC_GPIO_IN_PULLDOWN:
        return "in_pulldown";
    case pc_gpio_dir::PC_GPIO_OUT:
        return "out";
    default:
        return "in";
    }
}

int pc_gpio_json(const pc_gpio_pin *pins, uint8_t count, char *out, size_t cap)
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
    size_t pos = 0;
    if (pc_fmt_append(out, cap, &pos, "{\"pins\":[") != 0)
    {
        return 0;
    }
    for (uint8_t i = 0; i < count; i++)
    {
        const pc_gpio_pin *p = &pins[i];
        if (pc_fmt_append(out, cap, &pos, "%s{\"pin\":%u,\"label\":\"%s\",\"dir\":\"%s\",\"level\":%u}", i ? "," : "",
                          (unsigned)p->pin, p->label ? p->label : "", pc_gpio_dir_name(p->dir),
                          p->level ? 1u : 0u) != 0)
        {
            return 0;
        }
    }
    if (pc_fmt_append(out, cap, &pos, "]}") != 0)
    {
        return 0;
    }
    return (int)pos;
}

// Read the decimal integer that follows "name=" in a form-encoded body. Returns
// false if the field is absent or has no digits.
static bool form_field_uint(const char *body, size_t len, const char *name, unsigned *out)
{
    size_t nlen = strnlen(name, len + 1);
    for (size_t i = 0; i + nlen + 1 <= len; i++)
    {
        bool at_field = (i == 0) || body[i - 1] == '&';
        if (!at_field || memcmp(body + i, name, nlen) != 0 || body[i + nlen] != '=')
        {
            continue;
        }
        size_t j = i + nlen + 1;
        if (j >= len || body[j] < '0' || body[j] > '9')
        {
            return false;
        }
        unsigned v = 0;
        for (; j < len && body[j] >= '0' && body[j] <= '9'; j++)
        {
            v = v * 10 + (unsigned)(body[j] - '0');
        }
        *out = v;
        return true;
    }
    return false;
}

bool pc_gpio_parse_set(const char *body, size_t len, uint8_t *pin, uint8_t *level)
{
    if (!body || !pin || !level)
    {
        return false;
    }
    unsigned p;
    unsigned l;
    if (!form_field_uint(body, len, "pin", &p) || !form_field_uint(body, len, "level", &l))
    {
        return false;
    }
    *pin = (uint8_t)p;
    *level = l ? 1 : 0;
    return true;
}

bool pc_gpio_is_output(const pc_gpio_pin *pins, uint8_t count, uint8_t pin)
{
    if (!pins)
    {
        return false;
    }
    for (uint8_t i = 0; i < count; i++)
    {
        if (pins[i].pin == pin && pins[i].dir == pc_gpio_dir::PC_GPIO_OUT)
        {
            return true;
        }
    }
    return false;
}

#ifdef ARDUINO

void pc_gpio_begin_pins(const pc_gpio_pin *pins, uint8_t count)
{
    if (!pins)
    {
        return;
    }
    for (uint8_t i = 0; i < count; i++)
    {
        switch (pins[i].dir)
        {
        case pc_gpio_dir::PC_GPIO_OUT:
            pinMode(pins[i].pin, OUTPUT);
            break;
        case pc_gpio_dir::PC_GPIO_IN_PULLUP:
            pinMode(pins[i].pin, INPUT_PULLUP);
            break;
        case pc_gpio_dir::PC_GPIO_IN_PULLDOWN:
            pinMode(pins[i].pin, INPUT_PULLDOWN);
            break;
        default:
            pinMode(pins[i].pin, INPUT);
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
        pins[i].level = (uint8_t)(digitalRead(pins[i].pin) ? 1 : 0);
    }
}

void pc_gpio_write(uint8_t pin, uint8_t level)
{
    digitalWrite(pin, level ? HIGH : LOW);
}

#else // host build - no GPIO

void pc_gpio_begin_pins(const pc_gpio_pin *, uint8_t)
{
}

void pc_gpio_read(pc_gpio_pin *, uint8_t)
{
}

void pc_gpio_write(uint8_t, uint8_t)
{
}

#endif // ARDUINO

#endif // PC_ENABLE_GPIO_MAP
