// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file JWTAuth.ino
 * @brief Stateless route protection with JWT bearer tokens (HS256).
 *
 * A client presents `Authorization: Bearer <jwt>`; the device verifies the
 * token's HMAC-SHA-256 signature against a shared secret (no sessions, no
 * per-client state, no heap). Reads the `sub` claim... well, here it reads `exp`
 * to show claim access. Only HS256 is supported (the deterministic, shared-secret
 * choice for a constrained device).
 *
 * Flash, open Serial @ 115200 for the IP. With the demo secret "s3cr3t-key",
 * a token for {"sub":"alice","role":"admin","exp":2000000000} is:
 *   T=eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJzdWIiOiJhbGljZSIsInJvbGUiOiJhZG1pbiIsImV4cCI6MjAwMDAwMDAwMCwiaWF0IjoxNzAwMDAwMDAwfQ.oaEaMu7USfUlYDaLYQlogmRd_1ZPBr7cKrPIo5lXdxc
 *   curl -H "Authorization: Bearer $T" http://<ip>/protected   # 200
 *   curl http://<ip>/protected                                 # 401
 * Mint your own with any JWT library/jwt.io using HS256 + the secret below.
 *
 * NOTE: optional services are gated by a compile flag the *library* sources must
 * also see; for PlatformIO enable it for the whole build, e.g.:
 *     build_flags = -DPROTOCORE_ENABLE_JWT=1
 * (Arduino IDE: it is already set for you in the build_opt.h beside this sketch, so it builds as-is.)
 */

#define PROTOCORE_ENABLE_JWT 1

#include "protocore.h"
#include "network_drivers/physical/physical/physical.h"
#include "services/security/jwt/jwt.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// DEMO shared secret - the issuer signs tokens with this; keep it secret in production.
static const char *JWT_SECRET = "s3cr3t-key";

// The borrow every Jwt entry takes. The verifier keeps no state between calls, so a small
// static buffer serves; operands go in JwtV and each outcome comes back in JwtV.ok.
static uint8_t jwt_work[16];


static void protected_handler(uint8_t id, HttpReq *req)
{
    // req->authorization holds the FULL Authorization header (JWTs exceed
    // MAX_VAL_LEN; the parser captures it whole when PROTOCORE_ENABLE_JWT is set).
    JwtV.token.credentials = req->authorization;
    JwtV.key.secret = (const uint8_t *)JWT_SECRET;
    JwtV.key.secret_len = strlen(JWT_SECRET);
    Jwt.verify_bearer(jwt_work);
    if (!JwtV.ok)
    {
        proto_add_response_header(id, "WWW-Authenticate", "Bearer");
        send_text(id, 401, "text/plain", "invalid or missing token");
        return;
    }

    // Granular authorization from a token claim. Jwt.claim_str / Jwt.scope_allows read the bare
    // token, which verify_bearer left in JwtV.token.jws, past the "Bearer " scheme.
    const char *tok = JwtV.token.jws;
    JwtV.token.jws_len = strlen(tok);
    char role[16];
    JwtV.claim.name = "role";
    JwtV.claim.out = role;
    JwtV.claim.out_cap = sizeof(role);
    Jwt.claim_str(jwt_work);
    if (!JwtV.ok || strcmp(role, "admin") != 0)
    {
        send_text(id, 403, "text/plain", "forbidden: admin role required");
        return;
    }
    // For OAuth2 space-separated scopes, gate on the "scope" claim instead:
    //   char scope[64];
    //   JwtV.claim.name = "scope";
    //   JwtV.claim.out = scope;
    //   JwtV.claim.out_cap = sizeof(scope);
    //   Jwt.claim_str(jwt_work);
    //   JwtV.scope.claim = scope;
    //   JwtV.scope.required = "telemetry:write";
    //   if (JwtV.ok) Jwt.scope_allows(jwt_work);
    //   if (JwtV.ok) { ... }

    send_text(id, 200, "text/plain", "welcome admin - your token is valid");
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

    on_http("/protected", HTTP_GET, protected_handler);
    on_http("/", HTTP_GET, [](uint8_t id, HttpReq *) { send_text(id, 200, "text/plain", "public"); });

    int32_t result = begin_http(80, NULL);
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
    }
    else
    {
        Serial.println("JWT-protected server on :80 (GET /protected with a Bearer token)");
    }
}

void loop()
{
    handle();
}
