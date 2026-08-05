// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file FormParams.ino
 * @brief Reading `application/x-www-form-urlencoded` POST fields.
 *
 * http_get_form() parses a named field out of the request body on demand into a
 * caller-supplied buffer. It is gated on the
 * `Content-Type: application/x-www-form-urlencoded` header and returns the raw
 * (un-decoded) value, matching http_get_query(). Query-string params
 * (http_get_query) and request headers (http_get_header) are also shown.
 *
 * Flash, open Serial @ 115200 for the IP, then:
 *   curl -X POST http://<ip>/form -d "name=ada&email=ada@example.com"
 *   curl -X POST "http://<ip>/form?debug=1" -d "name=ada&email=ada@example.com"
 */

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// POST /form - echo the urlencoded "name" and "email" fields back as JSON.
void handle_form(uint8_t slot_id, HttpReq *req)
{
    char name[48];
    char email[64];
    bool have_name = http_get_form(req, "name", name, sizeof(name));
    bool have_email = http_get_form(req, "email", email, sizeof(email));

    if (!have_name && !have_email)
    {
        send_text(slot_id, 400, "text/plain", "expected urlencoded body with name= / email=");
        return;
    }

    // ?debug=1 in the query string mirrors the User-Agent header back too.
    const char *debug = http_get_query(req, "debug");
    char body[256];
    if (debug && strcmp(debug, "1") == 0)
    {
        const char *ua = http_get_header(req, "User-Agent");
        snprintf(body, sizeof(body), "{\"name\":\"%s\",\"email\":\"%s\",\"ua\":\"%s\"}", have_name ? name : "",
                 have_email ? email : "", ua ? ua : "");
    }
    else
    {
        snprintf(body, sizeof(body), "{\"name\":\"%s\",\"email\":\"%s\"}", have_name ? name : "",
                 have_email ? email : "");
    }
    send_text(slot_id, 200, "application/json", body);
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

    on_http("/form", HTTP_POST, handle_form);

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
