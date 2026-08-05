// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file OidcAuth.ino
 * @brief OpenID Connect ID-token auth, RS256 (PC_ENABLE_OIDC).
 *
 * A client presents `Authorization: Bearer <id_token>`; the device verifies the
 * RS256 signature against the issuer's JWKS public key and checks iss / aud / exp,
 * then serves the request as the authenticated subject.
 *
 *   curl -H "Authorization: Bearer $ID_TOKEN" http://<ip>/whoami
 *     -> 200 {"sub":"...","email":"..."}   on a valid token
 *     -> 401 {"error":<code>}              otherwise
 *
 * PRODUCTION NOTES:
 *  - Fetch the JWKS from the issuer's discovery document
 *    (`<issuer>/.well-known/openid-configuration` -> `jwks_uri`) over HTTPS with
 *    the HTTP client, off the request hot path, and cache it (re-fetch on an
 *    unknown kid). Here the JWKS is embedded for a self-contained demo.
 *  - Use a real clock (NTP) for `now`; this demo hardcodes a fixed time so the
 *    bundled test token validates.
 *
 * NOTE: enable it for the whole build. In platformio.ini:
 *     build_flags = -DPC_ENABLE_OIDC=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PC_ENABLE_OIDC 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/security/oidc/oidc.h"
#include <string.h>

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// The issuer's JWKS (normally fetched from <issuer>/.well-known/openid-configuration).
static const char *JWKS = "{\"keys\":[{\"kty\":\"RSA\",\"kid\":\"your-kid\",\"alg\":\"RS256\","
                          "\"n\":\"<base64url-modulus>\",\"e\":\"AQAB\"}]}";
static const char *ISSUER = "https://issuer.example";
static const char *AUDIENCE = "your-client-id";


void setup()
{
    Serial.begin(115200);
    init_wifi_physical(SSID, PASSWORD);
    while (!wifi_ready())
    {
        delay(250);
    }
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    on_http("/whoami", HTTP_GET, [](uint8_t id, HttpReq *req) {
        // req->authorization holds the FULL Authorization header (ID tokens exceed
        // the normal header value cap). Step past the "Bearer " scheme.
        const char *hdr = req->authorization;
        if (!hdr || strncasecmp(hdr, "Bearer ", 7) != 0)
        {
            proto_add_response_header(id, "WWW-Authenticate", "Bearer");
            send_text(id, 401, "application/json", "{\"error\":\"missing token\"}");
            return;
        }
        const char *token = hdr + 7;
        uint32_t now = 1700000100; // production: read from NTP

        pc_oidc_claims claims;
        pc_oidc_result rc = pc_oidc_verify(token, strlen(token), JWKS, ISSUER, AUDIENCE, now, &claims);
        if (rc != pc_oidc_result::PC_OIDC_OK)
        {
            char b[40];
            snprintf(b, sizeof(b), "{\"error\":%d}", (int)rc);
            proto_add_response_header(id, "WWW-Authenticate", "Bearer");
            send_text(id, 401, "application/json", b);
            return;
        }
        char b[192];
        snprintf(b, sizeof(b), "{\"sub\":\"%s\",\"email\":\"%s\"}", claims.sub, claims.email);
        send_text(id, 200, "application/json", b);
    });

    begin_http(80, NULL);
}

void loop()
{
    handle();
}
