// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Middleware.ino
 * @brief Composable middleware chain + the built-in rate limiter.
 *
 * use() registers a Middleware that runs - in registration order - before every
 * request reaches its route handler. A middleware returns MwResult::MW_NEXT to fall
 * through, or MwResult::MW_HALT to short-circuit (after sending its own response). Common
 * uses: request logging, header stamping, custom gating.
 *
 * enable_rate_limit(max, window_ms) installs a fixed-window limiter that runs
 * ahead of the chain: once more than `max` requests arrive within `window_ms`,
 * further requests get 429 + Retry-After instead of being dispatched.
 *
 * Flash, open Serial @ 115200 for the IP, then:
 *   curl -D - http://<ip>/        # note X-Powered-By stamped by middleware
 *   # trip the limiter (5 req / 10 s here): some 429s
 *   for i in $(seq 1 8); do curl -s -o /dev/null -w "%{http_code} " http://<ip>/ping; done; echo
 * Watch the Serial log: every request prints "[req N] METHOD path".
 */

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";


static unsigned long request_count = 0;

// Logging middleware: observe every request (even unmatched 404s), fall through.
static MwResult mw_log(uint8_t slot_id, HttpReq *req)
{
    (void)slot_id;
    request_count++;
    Serial.printf("[req %lu] %s %s\n", request_count, req->method, req->path);
    return MwResult::MW_NEXT;
}

// Header-stamp middleware: queue a header onto every response.
static MwResult mw_brand(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    proto_add_response_header(slot_id, "X-Powered-By", "ProtoCore");
    return MwResult::MW_NEXT;
}

// Gate middleware: block a path outright, demonstrating MwResult::MW_HALT.
static MwResult mw_block_admin(uint8_t slot_id, HttpReq *req)
{
    if (strcmp(req->path, "/admin") == 0)
    {
        send_text(slot_id, 403, "text/plain", "blocked by middleware");
        return MwResult::MW_HALT; // handler is never reached
    }
    return MwResult::MW_NEXT;
}

void handle_root(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    send_text(slot_id, 200, "text/plain", "hello from the handler");
}

void handle_ping(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    send_text(slot_id, 200, "text/plain", "pong");
}

// Registered, but the gate middleware blocks it before we get here.
void handle_admin(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    send_text(slot_id, 200, "text/plain", "you should never see this");
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

    use(mw_log);
    use(mw_brand);
    use(mw_block_admin);
    enable_rate_limit(5, 10000); // 5 requests / 10 s window

    on_http("/", HttpMethod::HTTP_GET, handle_root);
    on_http("/ping", HttpMethod::HTTP_GET, handle_ping);
    on_http("/admin", HttpMethod::HTTP_GET, handle_admin);

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
