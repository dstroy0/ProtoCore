// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file gpio_map_routes.c
 * @brief GPIO pin-mapper routes (GET serves the JSON, POST drives an output).
 *
 * Separated from the host-testable core (gpio_map.cpp) so the serializer + control
 * parser unit-test without pulling in the server. The pin table is caller-owned.
 */

#include "services/system/gpio_map/gpio_map.h"

#if PC_ENABLE_GPIO_MAP

#include "protocore.h"
#include "shared_primitives/mime.h"

// All gpio-map-routes state, owned by one instance (internal linkage): the server handle plus
// the pin table pointer and count, grouped so it is one named owner, unreachable cross-TU.
// (The route handlers are fixed-signature callbacks, so they reach this single owner directly.)
typedef struct
{
    pc_gpio_pin *pins = NULL;
    uint8_t count = 0;
} GpioRoutesCtx;
static GpioRoutesCtx s_gpior;

static void gpio_get_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    pc_gpio_read(s_gpior.pins, s_gpior.count);
    char buf[PC_GPIO_JSON_BUF];
    pc_gpio_json(s_gpior.pins, s_gpior.count, buf, sizeof(buf));
    // No instance test: a handler only runs because this service registered the route, and the
    // response goes out through the server's own entry point rather than a pointer to it.
    send_text(slot_id, 200, PC_MIME_JSON, buf);
}

static void gpio_post_handler(uint8_t slot_id, HttpReq *req)
{
    uint8_t pin;
    uint8_t level;
    if (!pc_gpio_parse_set((const char *)req->body, req->body_len, &pin, &level))
    {
        send_text(slot_id, 400, PC_MIME_TEXT_PLAIN, "bad request");
        return;
    }
    if (!pc_gpio_is_output(s_gpior.pins, s_gpior.count, pin))
    {
        send_text(slot_id, 403, PC_MIME_TEXT_PLAIN, "pin not a mapped output");
        return;
    }
    pc_gpio_write(pin, level);
    pc_gpio_read(s_gpior.pins, s_gpior.count);
    char buf[PC_GPIO_JSON_BUF];
    pc_gpio_json(s_gpior.pins, s_gpior.count, buf, sizeof(buf));
    send_text(slot_id, 200, PC_MIME_JSON, buf);
}

void pc_gpio_map_begin(const char *path, pc_gpio_pin *pins, uint8_t count)
{
    s_gpior.pins = pins;
    s_gpior.count = count;
    pc_gpio_begin_pins(pins, count);
    const char *p = (path && path[0]) ? path : "/gpio";
    on_http(p, HttpMethod::HTTP_GET, gpio_get_handler);
    on_http(p, HttpMethod::HTTP_POST, gpio_post_handler);
}

#endif // PC_ENABLE_GPIO_MAP
