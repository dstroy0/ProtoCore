// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file HttpClient.ino
 * @brief Outbound HTTP(S) client: the device makes requests to a remote server.
 *
 * A blocking GET/POST over raw lwIP (https:// via client-side mbedTLS), for
 * webhooks, telemetry push, or REST calls from the device. The host is resolved
 * via DNS (or used directly if it is a dotted-quad IP); the response status and
 * body are returned in a fixed static buffer (no heap).
 *
 * Flash, open Serial @ 115200. It fetches a URL once at boot and prints the
 * status + body. Point URL at your own endpoint.
 *
 * NOTE: optional services are gated by a compile flag the *library* sources must
 * also see; for PlatformIO enable it for the whole build, e.g.:
 *     build_flags = -DPROTOCORE_ENABLE_HTTP_CLIENT=1
 *     ; to trace where a request stalls, add: -DPROTOCORE_HTTP_CLIENT_DEBUG
 * (Arduino IDE: they are already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 *
 * https:// note: encrypt-only by default (the device has no trust store), so the
 * peer is unauthenticated. To authenticate the server, install a CA trust anchor
 * with http_client_set_ca(pem, len) - verifies the chain + hostname - and/or a
 * SHA-256 certificate pin with http_client_set_pin(hash32); a failure aborts the
 * request. Call once before issuing requests.
 */

#define PROTOCORE_ENABLE_HTTP_CLIENT 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "services/net/http_client/http_client.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

static const char *URL = "http://example.com/";

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

    // The client reads its operands from HttpClientV and writes the outcome back there; the body
    // points into the module's receive buffer and stays valid until the next exchange.
    HttpClientV.target.url = URL;
    HttpClient.get(protocore_http_client_span());
    int status = (int)HttpClientV.status;
    if (status < 0)
    {
        Serial.printf("request failed (error %d)\n", status);
    }
    else
    {
        Serial.printf("HTTP %d, %u body bytes:\n", status, (unsigned)HttpClientV.body_len);
        Serial.write(HttpClientV.body, HttpClientV.body_len);
        Serial.println();
    }
}

void loop()
{
    delay(1000);
}
