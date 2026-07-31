# JWTAuth - stateless bearer-token auth (JWT HS256)

**Layer:** L6 Presentation · **Build flags:** `PC_ENABLE_JWT`

## What this example teaches

A client presents `Authorization: Bearer <jwt>`; the device verifies the token's
HMAC-SHA-256 signature against a shared secret and reads claims from it. There are
no sessions, no per-client state, and no heap - verification is a hash check.
Only HS256 is supported (the deterministic, shared-secret choice for a
constrained device).

**Verify the signature.** `pc_jwt_bearer_valid()` checks the whole
`Authorization` header against the secret. The full header is in
`req->authorization` - JWTs exceed `MAX_VAL_LEN`, so the parser captures the
authorization header whole when `PC_ENABLE_JWT` is set:

```cpp
if (!pc_jwt_bearer_valid(req->authorization, (const uint8_t *)JWT_SECRET, strlen(JWT_SECRET))) {
    server.add_response_header(id, "WWW-Authenticate", "Bearer");
    server.send(id, 401, "text/plain", "invalid or missing token");
    return;
}
```

**Authorize on a claim.** Beyond "is it valid," you can gate on claims. Step past
the `"Bearer "` scheme to get the bare token, then read a claim with
`pc_jwt_claim_str()` - here requiring `role == admin` (else `403`):

```cpp
const char *tok = req->authorization + 7;        // skip "Bearer "
while (*tok == ' ') tok++;
char role[16];
if (!pc_jwt_claim_str(tok, strlen(tok), "role", role, sizeof(role)) || strcmp(role, "admin") != 0) {
    server.send(id, 403, "text/plain", "forbidden: admin role required");
    return;
}
```

For OAuth2-style space-separated scopes, `pc_jwt_scope_allows(scope, "telemetry:write")`
tests one scope within the `scope` claim (shown commented in the source).

**Minting a token.** Sign `{"sub":...,"role":"admin","exp":...}` with HS256 and
the demo secret `s3cr3t-key` using any JWT library or jwt.io; the header comment
includes a ready-made token to try.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_JWT=1" \
  --lib="." examples/L6-Presentation/JWTAuth/JWTAuth.ino
```

```sh
curl -H "Authorization: Bearer $T" http://<ip>/protected   # 200 with a valid admin token
curl http://<ip>/protected                                 # 401
```

## Annotated source

The complete sketch ([JWTAuth.ino](JWTAuth.ino)), reproduced verbatim with
added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_JWT 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/security/jwt/jwt.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// DEMO shared secret - the issuer signs tokens with this; keep it secret in production.
static const char *JWT_SECRET = "s3cr3t-key";

PC server;

static void protected_handler(uint8_t id, HttpReq *req)
{
    // req->authorization holds the FULL Authorization header (JWTs exceed
    // MAX_VAL_LEN; the parser captures it whole when PC_ENABLE_JWT is set).
    if (!pc_jwt_bearer_valid(req->authorization, (const uint8_t *)JWT_SECRET, strlen(JWT_SECRET)))
    {
        server.add_response_header(id, "WWW-Authenticate", "Bearer");
        server.send(id, 401, "text/plain", "invalid or missing token");
        return;
    }

    // Granular authorization from a token claim. pc_jwt_claim_str / pc_jwt_scope_allows
    // take the bare token, so step past the "Bearer " scheme first.
    const char *tok = req->authorization + 7;
    while (*tok == ' ')
        tok++;
    char role[16];
    if (!pc_jwt_claim_str(tok, strlen(tok), "role", role, sizeof(role)) || strcmp(role, "admin") != 0)
    {
        server.send(id, 403, "text/plain", "forbidden: admin role required");
        return;
    }
    // For OAuth2 space-separated scopes, gate on the "scope" claim instead:
    //   char scope[64];
    //   if (pc_jwt_claim_str(tok, strlen(tok), "scope", scope, sizeof(scope)) &&
    //       pc_jwt_scope_allows(scope, "telemetry:write")) { ... }

    server.send(id, 200, "text/plain", "welcome admin - your token is valid");
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
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    server.on("/protected", HttpMethod::HTTP_GET, protected_handler);
    server.on("/", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { server.send(id, 200, "text/plain", "public"); });

    int32_t result = server.begin(80);
    if (result < 0)
        Serial.printf("begin() failed (error %d)\n", result);
    else
        Serial.println("JWT-protected server on :80 (GET /protected with a Bearer token)");
}

void loop()
{
    server.handle();
}
```
