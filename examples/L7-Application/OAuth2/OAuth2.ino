// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file OAuth2.ino
 * @brief OAuth2 authorization-code exchange (PROTOCORE_ENABLE_OAUTH2).
 *
 * The redirect-callback half of an OAuth/OIDC login: after the user authorizes at
 * the provider, the browser is redirected back with `?code=...`; this handler
 * exchanges that code at the provider's token endpoint for tokens.
 *
 *   GET /callback?code=<auth_code>
 *     -> exchanges the code, returns {"expires_in":3600,...}
 *
 * Pair it with services/security/oidc to verify the returned id_token, and call
 * Oauth2.refresh later with the refresh_token to get fresh access tokens.
 * Needs the HTTP(S) client (PROTOCORE_ENABLE_HTTP_CLIENT); use https:// token URLs in
 * production and set a CA / pin on the client.
 *
 * NOTE: enable it (and the HTTP client) for the whole build. In platformio.ini:
 *     build_flags = -DPROTOCORE_ENABLE_OAUTH2=1 -DPROTOCORE_ENABLE_HTTP_CLIENT=1
 */

#define PROTOCORE_ENABLE_OAUTH2 1
#define PROTOCORE_ENABLE_HTTP_CLIENT 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "services/security/oauth2/oauth2.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// From your OAuth provider:
static const char *TOKEN_URL = "https://provider.example/oauth/token";
static const char *CLIENT_ID = "your-client-id";
static const char *CLIENT_SECRET = "your-client-secret"; // or nullptr + PKCE code_verifier
static const char *REDIRECT_URI = "http://device.local/callback";


void setup()
{
    Serial.begin(115200);
    PhysicalV.wifi.ssid = SSID;
    PhysicalV.wifi.password = PASSWORD;
    Physical.wifi_init(protocore_physical_span());
    for (Physical.wifi_ready(protocore_physical_span()); !PhysicalV.ok; Physical.wifi_ready(protocore_physical_span()))
    {
        delay(250);
    }
    Physical.egress_ip(protocore_physical_span());
    uint32_t ip = PhysicalV.u32; // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("\nIP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    on_http("/callback", HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *code = HttpParser.get_query(protocore_http_parser_span(), req, "code");
        if (!code)
        {
            send_text(id, 400, "application/json", "{\"error\":\"missing code\"}");
            return;
        }
        static Oauth2Tokens t; // static: three token buffers are too big for the handler's stack
        Oauth2V.request.token_endpoint = TOKEN_URL;
        Oauth2V.client.client_id = CLIENT_ID;
        Oauth2V.client.client_secret = CLIENT_SECRET;
        Oauth2V.code_grant.code = code;
        Oauth2V.code_grant.redirect_uri = REDIRECT_URI;
        Oauth2V.code_grant.code_verifier = nullptr;
        Oauth2V.response.tokens = &t;
        Oauth2.exchange_code(protocore_oauth2_span());
        int st = (int)Oauth2V.i32;
        if (st != 200)
        {
            char b[48];
            snprintf(b, sizeof(b), "{\"error\":\"exchange failed\",\"status\":%d}", st);
            send_text(id, 502, "application/json", b);
            return;
        }
        // t.access_token / t.id_token / t.refresh_token are now populated.
        char b[96];
        snprintf(b, sizeof(b), "{\"token_type\":\"%s\",\"expires_in\":%ld}", t.token_type, t.expires_in);
        send_text(id, 200, "application/json", b);
    });

    begin_http(80, NULL);
}

void loop()
{
    handle();
}
