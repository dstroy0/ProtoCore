// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file FormParams.ino
 * @brief Reading `application/x-www-form-urlencoded` POST fields.
 *
 * HttpParser.get_form() parses a named field out of the request body on demand into a
 * caller-supplied buffer. It is gated on the
 * `Content-Type: application/x-www-form-urlencoded` header and returns the raw
 * (un-decoded) value, matching HttpParser.get_query(). Query-string params
 * (HttpParser.get_query) and request headers (HttpParser.get_header) are also shown.
 *
 * Flash, open Serial @ 115200 for the IP, then:
 *   curl -X POST http://<ip>/form -d "name=ada&email=ada@example.com"
 *   curl -X POST "http://<ip>/form?debug=1" -d "name=ada&email=ada@example.com"
 */

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// POST /form - echo the urlencoded "name" and "email" fields back as JSON.
void handle_form(uint8_t slot_id, HttpReq *req)
{
    char name[48];
    char email[64];
    bool have_name = HttpParser.get_form(protocore_http_parser_span(), req, "name", name, sizeof(name));
    bool have_email = HttpParser.get_form(protocore_http_parser_span(), req, "email", email, sizeof(email));

    if (!have_name && !have_email)
    {
        send_text(slot_id, 400, "text/plain", "expected urlencoded body with name= / email=");
        return;
    }

    // ?debug=1 in the query string mirrors the User-Agent header back too.
    const char *debug = HttpParser.get_query(protocore_http_parser_span(), req, "debug");
    char body[256];
    if (debug && strcmp(debug, "1") == 0)
    {
        const char *ua = HttpParser.get_header(protocore_http_parser_span(), req, "User-Agent");
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

    // Every Physical entry reads its operands from PhysicalV and writes its outcome back there.
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
