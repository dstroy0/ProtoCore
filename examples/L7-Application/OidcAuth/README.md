# OidcAuth - OpenID Connect ID-token auth (RS256)

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_OIDC`

## What this example teaches

OpenID Connect lets an external identity provider (Google, Auth0, Azure AD, ...)
vouch for a user with a signed ID token. A client presents
`Authorization: Bearer <id_token>` and the device verifies the RS256 signature
against the issuer's JWKS public key and checks `iss` / `aud` / `exp`, then serves
the request as the authenticated subject. `GET /whoami` returns the subject on a
valid token, `401` otherwise.

**Verify the token, then trust its claims:**

```cpp
pc_oidc_claims claims;
int rc = pc_oidc_verify(token, strlen(token), JWKS, ISSUER, AUDIENCE, now, &claims);
if (rc != pc_oidc_result::PC_OIDC_OK) { /* 401 */ }
// claims.sub / claims.email are now trusted
```

**The full Authorization header lives in `req->authorization`.** ID tokens exceed
the normal header-value cap, so they get their own field; the handler steps past the
`Bearer ` scheme itself:

```cpp
const char *hdr = req->authorization;
if (!hdr || strncasecmp(hdr, "Bearer ", 7) != 0) { /* 401, WWW-Authenticate: Bearer */ }
const char *token = hdr + 7;
```

**Production notes.** Fetch the JWKS from the issuer's discovery document
(`<issuer>/.well-known/openid-configuration` -> `jwks_uri`) over HTTPS off the
request hot path and cache it (re-fetch on an unknown `kid`); the demo embeds it.
Use a real NTP clock for `now`; the demo hardcodes a time so the bundled test token
validates.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_OIDC=1" \
  --lib="." examples/L7-Application/OidcAuth/OidcAuth.ino
```

```sh
curl -H "Authorization: Bearer $ID_TOKEN" http://<ip>/whoami
# 200 {"sub":"...","email":"..."} on a valid token, else 401 {"error":<code>}
```

## Annotated source

The complete sketch ([OidcAuth.ino](OidcAuth.ino)), reproduced verbatim with
added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

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

PC server;

void setup()
{
    Serial.begin(115200);
    init_wifi_physical(SSID, PASSWORD);
    while (!wifi_ready())
        delay(250);
    Serial.print("IP: ");
    uint32_t ip = pc_net_egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    server.on("/whoami", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *req) {
        // req->authorization holds the FULL Authorization header (ID tokens exceed
        // the normal header value cap). Step past the "Bearer " scheme.
        const char *hdr = req->authorization;
        if (!hdr || strncasecmp(hdr, "Bearer ", 7) != 0)
        {
            server.add_response_header(id, "WWW-Authenticate", "Bearer");
            server.send(id, 401, "application/json", "{\"error\":\"missing token\"}");
            return;
        }
        const char *token = hdr + 7;
        uint32_t now = 1700000100; // production: read from NTP

        pc_oidc_claims claims;
        int rc = pc_oidc_verify(token, strlen(token), JWKS, ISSUER, AUDIENCE, now, &claims);
        if (rc != pc_oidc_result::PC_OIDC_OK)
        {
            char b[40];
            snprintf(b, sizeof(b), "{\"error\":%d}", rc);
            server.add_response_header(id, "WWW-Authenticate", "Bearer");
            server.send(id, 401, "application/json", b);
            return;
        }
        char b[192];
        snprintf(b, sizeof(b), "{\"sub\":\"%s\",\"email\":\"%s\"}", claims.sub, claims.email);
        server.send(id, 200, "application/json", b);
    });

    server.begin(80);
}

void loop()
{
    server.handle();
}
```
