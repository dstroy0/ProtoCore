// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file ResponseHeaders.ino
 * @brief Custom response headers and cookies.
 *
 * Demonstrates queuing extra headers onto a response from inside a handler:
 *   - add_response_header()   - arbitrary `Name: value` lines
 *   - set_cookie()            - `Set-Cookie` with optional attributes
 *   - clear_response_headers() - discard anything queued so far
 *
 * Queued headers live in a fixed per-connection buffer and are injected into the
 * next send() / send_empty() / redirect() on that slot, then cleared
 * automatically at the start of the next request. Oversized headers are dropped
 * whole (never truncated mid-line).
 *
 * Flash, open Serial @ 115200 for the IP, then:
 *   # see X-Api-Version + Set-Cookie in the response headers
 *   curl -D - http://<ip>/headers
 *   # clear_response_headers() wins: no X-Scratch on this one
 *   curl -D - http://<ip>/cleared
 */

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


// GET /headers - attach a custom header and a cookie, then respond.
void handle_headers(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    add_response_header(slot_id, "X-Api-Version", "1.2.5");
    set_cookie(slot_id, "session", "abc123", "Path=/; HttpOnly; Max-Age=3600");
    send_text(slot_id, 200, "text/plain", "custom header + cookie attached");
}

// GET /cleared - queue a header, then change our mind before sending.
void handle_cleared(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    add_response_header(slot_id, "X-Scratch", "should-not-appear");
    clear_response_headers(slot_id);
    send_text(slot_id, 200, "text/plain", "headers were cleared before send");
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

    // Keep the radio responsive (the Arduino default modem sleep delays the
    // first packet after an idle gap).

    on_http("/headers", HttpMethod::HTTP_GET, handle_headers);
    on_http("/cleared", HttpMethod::HTTP_GET, handle_cleared);

    int32_t result = begin_http(80);
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
