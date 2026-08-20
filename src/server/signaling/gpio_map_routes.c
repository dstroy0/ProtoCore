// ProtoCore v1.0.16 - Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file gpio_map_routes.c
 * @brief GPIO pin-mapper routes (GET serves the JSON, POST drives an output).
 *
 * Separated from the host-testable core (gpio_map.cpp) so the serializer + control
 * parser unit-test without pulling in the server. The pin table is caller-owned.
 */

#include "server/signaling/gpio_map/gpio_map.h"

static uint8_t gpio_map_work[16]; // the borrow an entry takes; GpioMap never reads it

#if PROTOCORE_ENABLE_GPIO_MAP

#include "protocore.h"
#include "shared/mime/mime.h"

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
    GpioMapV.args.pins_rw = s_gpior.pins;
    GpioMapV.args.count = s_gpior.count;
    GpioMap.sample(gpio_map_work);
    char buf[PROTOCORE_GPIO_JSON_BUF];
    GpioMapV.args.pins = s_gpior.pins;
    GpioMapV.out_args.out = buf;
    GpioMapV.out_args.cap = sizeof(buf);
    GpioMap.json(gpio_map_work);
    // No instance test: a handler only runs because this service registered the route, and the
    // response goes out through the server's own entry point rather than a pointer to it.
    send_text(slot_id, 200, PROTOCORE_MIME_JSON, buf);
}

static void gpio_post_handler(uint8_t slot_id, HttpReq *req)
{
    uint8_t pin;
    uint8_t level;
    GpioMapV.parse_args.body = (const char *)req->body;
    GpioMapV.parse_args.len = req->body_len;
    GpioMapV.parse_args.pin_out = &pin;
    GpioMapV.parse_args.level_out = &level;
    GpioMap.parse_set(gpio_map_work);
    if (!GpioMapV.ok)
    {
        send_text(slot_id, 400, PROTOCORE_MIME_TEXT_PLAIN, "bad request");
        return;
    }
    GpioMapV.args.pins = s_gpior.pins;
    GpioMapV.args.count = s_gpior.count;
    GpioMapV.args.pin = pin;
    GpioMap.is_output(gpio_map_work);
    if (!GpioMapV.ok)
    {
        send_text(slot_id, 403, PROTOCORE_MIME_TEXT_PLAIN, "pin not a mapped output");
        return;
    }
    GpioMapV.args.pin = pin;
    GpioMapV.args.level = level;
    GpioMap.write(gpio_map_work);
    GpioMapV.args.pins_rw = s_gpior.pins;
    GpioMapV.args.count = s_gpior.count;
    GpioMap.sample(gpio_map_work);
    char buf[PROTOCORE_GPIO_JSON_BUF];
    GpioMapV.args.pins = s_gpior.pins;
    GpioMapV.out_args.out = buf;
    GpioMapV.out_args.cap = sizeof(buf);
    GpioMap.json(gpio_map_work);
    send_text(slot_id, 200, PROTOCORE_MIME_JSON, buf);
}

void protocore_gpio_route_begin(uint8_t *restrict work)
{
    (void)work;
    protocore_gpio_pin *pins = GpioMapV.args.pins_rw;
    const uint8_t count = GpioMapV.args.count;
    const char *path = GpioMapV.args.path;

    s_gpior.pins = pins;
    s_gpior.count = count;
    GpioMapV.args.pins = pins;
    GpioMap.begin_pins(work);
    const char *p = (path && path[0]) ? path : "/gpio";
    on_http(p, HTTP_GET, gpio_get_handler);
    on_http(p, HTTP_POST, gpio_post_handler);
}

#endif // PROTOCORE_ENABLE_GPIO_MAP
