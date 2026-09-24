// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file PathParams.ino
 * @brief Capturing `:name` segments from the request path.
 *
 * A route path containing one or more `:name` segments captures the matching
 * path segment; the value is read back with HttpParser.get_param(). Literal segments
 * must match exactly. Up to MAX_PATH_PARAMS (default 4) per route.
 *
 * Flash, open Serial @ 115200 for the IP, then:
 *   curl http://<ip>/users/42
 *   curl http://<ip>/users/42/posts/hello-world
 */

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// GET /users/:id
void handle_user(uint8_t slot_id, HttpReq *req)
{
    const char *id = HttpParser.get_param(protocore_http_parser_span(), req, "id");
    char body[96];
    snprintf(body, sizeof(body), "{\"user_id\":\"%s\"}", id ? id : "?");
    send_text(slot_id, 200, "application/json", body);
}

// GET /users/:id/posts/:slug  - two captured segments.
void handle_user_post(uint8_t slot_id, HttpReq *req)
{
    const char *id = HttpParser.get_param(protocore_http_parser_span(), req, "id");
    const char *slug = HttpParser.get_param(protocore_http_parser_span(), req, "slug");
    char body[160];
    snprintf(body, sizeof(body), "{\"user_id\":\"%s\",\"slug\":\"%s\"}", id ? id : "?", slug ? slug : "?");
    send_text(slot_id, 200, "application/json", body);
}

void setup()
{
    Serial.begin(115200);

    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    Serial.print("Connecting to WiFi");
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok; Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
        Serial.print('.');
    }
    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32; // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Register the more specific route first; routes match in registration order.
    on_http("/users/:id/posts/:slug", HTTP_GET, handle_user_post);
    on_http("/users/:id", HTTP_GET, handle_user);

    int32_t result = begin_http(80, NULL);
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
        return;
    }
    Serial.println("Server started on port 80");
}

void loop()
{
    handle();
}
