// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file DigestAuth.ino
 * @brief Per-route HTTP Digest authentication (RFC 7616, SHA-256, qop="auth").
 *
 * Passing digest=true to the authenticated on() overload protects a route with
 * Digest auth instead of Basic: the password never crosses the wire, only a
 * salted hash. Unauthenticated requests get 401 + a `WWW-Authenticate: Digest`
 * challenge automatically.
 *
 * Flash, open Serial @ 115200 for the IP, then:
 *   curl --digest -u admin:s3cret http://<ip>/secret
 *
 *   NOTE: curl on *Windows* routes Digest through SSPI/SChannel, which rejects
 *   this SHA-256 / qop="auth" challenge with SEC_E_QOP_NOT_SUPPORTED and never
 *   sends credentials - a Windows-curl limitation, not a server issue. The
 *   challenge is standard RFC 7616 and works with a browser, wget, or
 *   Linux/macOS curl.
 */

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// GET /secret - only reached after successful Digest authentication.
void handle_secret(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    server.send(slot_id, 200, "text/plain", "authenticated: top secret payload");
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

    // on(path, method, handler, realm, user, pass, digest=true)
    server.on("/secret", HttpMethod::HTTP_GET, handle_secret, "demo", "admin", "s3cret", true);

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
