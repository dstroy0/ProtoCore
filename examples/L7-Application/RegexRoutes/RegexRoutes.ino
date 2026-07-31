// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file RegexRoutes.ino
 * @brief Match routes with a bounded, allocation-free regular expression.
 *
 * on_regex() matches the whole request path against a small regex subset:
 * `.`, quantifiers `* + ?`, classes `[...]`/`[^...]` with `a-z` ranges, the
 * shorthands `\d \w \s`, and `\` escapes. It is non-capturing (use `:name` path
 * params to capture) and bounded by RE_MAX_STEPS so it stays deterministic.
 *
 * Flash, open Serial @ 115200 for the IP, then:
 *   curl http://<ip>/sensor/42        -> 200   (digits only)
 *   curl http://<ip>/sensor/abc       -> 404
 *   curl http://<ip>/img/cat.png      -> 200
 *   curl http://<ip>/img/cat.gif      -> 404
 */

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

void handle_sensor(uint8_t slot_id, HttpReq *req)
{
    char body[96];
    snprintf(body, sizeof(body), "{\"path\":\"%s\",\"matched\":\"numeric sensor id\"}", req->path);
    server.send(slot_id, 200, "application/json", body);
}

void handle_png(uint8_t slot_id, HttpReq *req)
{
    char body[96];
    snprintf(body, sizeof(body), "{\"path\":\"%s\",\"matched\":\"png image\"}", req->path);
    server.send(slot_id, 200, "application/json", body);
}

void setup()
{
    Serial.begin(115200);

    init_wifi_physical(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!wifi_ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    server.on_regex("/sensor/[0-9]+", HttpMethod::HTTP_GET, handle_sensor); // only numeric ids
    server.on_regex("/img/.+\\.png", HttpMethod::HTTP_GET, handle_png);     // only *.png paths

    int32_t result = server.begin(80);
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
        return;
    }
    Serial.println("Server started on port 80");
}

void loop()
{
    server.handle();
}
