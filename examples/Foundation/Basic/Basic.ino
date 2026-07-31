// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Basic.ino
 * @brief The smallest complete server: connect to WiFi, answer one route.
 *
 * This is the whole shape of a ProtoCore program. Everything else in examples/
 * is additive: more routes, optional protocols, TLS. Nothing here is optional
 * or gated behind a feature flag.
 *
 * The framework answers these without any application code:
 *   - 400 Bad Request      : RFC 7230 character violation in method/path/headers
 *   - 413 Payload Too Large: Content-Length exceeds BODY_BUF_SIZE
 *   - 414 URI Too Long     : path exceeds MAX_PATH_LEN
 *   - 501 Not Implemented  : Transfer-Encoding present (chunked not supported)
 *
 * This file is also the README's quick-start block, generated from it verbatim
 * by ci_tooling/generate/gen_readme_intro.py, so the two can never disagree.
 *
 * Flash to any ESP32 board, open Serial @ 115200 for the IP, then:
 *   curl http://<ip>/
 */

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server; // one global; all state lives in BSS, no heap

// Every handler takes the connection's slot_id and the parsed request, and
// replies through server.send(). Pass slot_id back so the reply reaches the
// connection that asked.
void handle_root(uint8_t slot_id, HttpReq *req)
{
    server.send(slot_id, 200, "text/plain", "Welcome to ProtoCore!");
}

void setup()
{
    Serial.begin(115200);

    init_wifi_physical(SSID, PASSWORD);
    while (!wifi_ready())
    {
        delay(250);
    }

    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    server.on("/", HttpMethod::HTTP_GET, handle_root);

    // begin() returns 1 on success and a negative value on failure (listener
    // pool full or lwIP error). -1 is truthy, so test for "< 0", not "!result".
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
    // Drives the full pipeline each iteration:
    //   1. Timeout sweep (force-closes idle connections)
    //   2. Event queue drain (TCP connect/data/disconnect events)
    //   3. Route dispatch for completed requests
    //   4. Auto-sends 400 / 413 / 414 / 501 for parser error states
    // No request is processed off this call, so loop() must never block.
    server.handle();
}
