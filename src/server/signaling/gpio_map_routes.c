// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file gpio_map_routes.c
 * @brief GPIO pin-mapper routes (GET serves the JSON, POST drives an output).
 *
 * Separated from the host-testable core (gpio_map.cpp) so the serializer + control
 * parser unit-test without pulling in the server. The pin table is caller-owned.
 */

#include "server/signaling/gpio_map.h"

#if PROTOCORE_ENABLE_GPIO_MAP

#include "protocore.h"
#include "shared_primitives/mime.h"

// All gpio-map-routes state, owned by one instance (internal linkage): the server handle plus
// the pin table pointer and count, grouped so it is one named owner, unreachable cross-TU.
// (The route handlers are fixed-signature callbacks, so they reach this single owner directly.)
typedef struct
{
    protocore_gpio_pin *pins;
    uint8_t count;
} GpioRoutesCtx;
static GpioRoutesCtx s_gpior;

static void gpio_get_handler(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    protocore_gpio_read(s_gpior.pins, s_gpior.count);
    char buf[PROTOCORE_GPIO_JSON_BUF];
    protocore_gpio_json(s_gpior.pins, s_gpior.count, buf, sizeof(buf));
    // No instance test: a handler only runs because this service registered the route, and the
    // response goes out through the server's own entry point rather than a pointer to it.
    send_text(slot_id, 200, PROTOCORE_MIME_JSON, buf);
}

static void gpio_post_handler(uint8_t slot_id, HttpReq *req)
{
    uint8_t pin;
    uint8_t level;
    if (!protocore_gpio_parse_set((const char *)req->body, req->body_len, &pin, &level))
    {
        send_text(slot_id, 400, PROTOCORE_MIME_TEXT_PLAIN, "bad request");
        return;
    }
    if (!protocore_gpio_is_output(s_gpior.pins, s_gpior.count, pin))
    {
        send_text(slot_id, 403, PROTOCORE_MIME_TEXT_PLAIN, "pin not a mapped output");
        return;
    }
    protocore_gpio_write(pin, level);
    protocore_gpio_read(s_gpior.pins, s_gpior.count);
    char buf[PROTOCORE_GPIO_JSON_BUF];
    protocore_gpio_json(s_gpior.pins, s_gpior.count, buf, sizeof(buf));
    send_text(slot_id, 200, PROTOCORE_MIME_JSON, buf);
}

void protocore_gpio_map_begin(const char *path, protocore_gpio_pin *pins, uint8_t count)
{
    s_gpior.pins = pins;
    s_gpior.count = count;
    protocore_gpio_begin_pins(pins, count);
    const char *p = (path && path[0]) ? path : "/gpio";
    on_http(p, HTTP_GET, gpio_get_handler);
    on_http(p, HTTP_POST, gpio_post_handler);
}

#endif // PROTOCORE_ENABLE_GPIO_MAP
