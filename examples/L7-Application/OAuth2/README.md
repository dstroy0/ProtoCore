# OAuth2 - authorization-code exchange

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_OAUTH2`, `PC_ENABLE_HTTP_CLIENT`

## What this example teaches

This is the redirect-callback half of an OAuth/OIDC login. After the user authorizes
at the provider, the browser is redirected back to the device with `?code=...`; this
handler exchanges that code at the provider's token endpoint for tokens. Where
[OidcAuth](../OidcAuth) _verified_ a token someone else obtained, this one
_obtains_ the tokens.

**Exchange the code for tokens in one call:**

```cpp
const char *code = http_get_query(req, "code");
pc_o_auth2_tokens t;
int st = pc_oauth2_exchange_code(TOKEN_URL, code, REDIRECT_URI, CLIENT_ID, CLIENT_SECRET, nullptr, &t);
if (st != 200) { /* 502 */ }
// t.access_token / t.id_token / t.refresh_token are now populated
```

`pc_oauth2_exchange_code()` POSTs the code to the token endpoint through the
outbound HTTP client ([HttpClient](../HttpClient)) and fills a tokens struct.
The `nullptr` argument is the PKCE `code_verifier` - pass it (and `nullptr` for the
client secret) for a public client using PKCE instead of a client secret.

**Next steps.** Pair this with [OidcAuth](../OidcAuth) to verify the returned
`id_token`, and call `pc_oauth2_refresh()` later with the `refresh_token` for
fresh access tokens. In production use `https://` token URLs and set a CA or pin on
the HTTP client.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_OAUTH2=1 -DPC_ENABLE_HTTP_CLIENT=1" \
  --lib="." examples/L7-Application/OAuth2/OAuth2.ino
```

```sh
# the provider redirects the browser here after consent:
curl "http://<ip>/callback?code=<auth_code>"   # {"token_type":"Bearer","expires_in":3600}
```

## Annotated source

The complete sketch ([OAuth2.ino](OAuth2.ino)), reproduced verbatim with added
explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_OAUTH2 1
#define PC_ENABLE_HTTP_CLIENT 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/security/oauth2/oauth2.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// From your OAuth provider:
static const char *TOKEN_URL = "https://provider.example/oauth/token";
static const char *CLIENT_ID = "your-client-id";
static const char *CLIENT_SECRET = "your-client-secret"; // or nullptr + PKCE code_verifier
static const char *REDIRECT_URI = "http://device.local/callback";

PC server;

void setup()
{
    Serial.begin(115200);
    Physical.wifi->init(SSID, PASSWORD);
    while (!Physical.wifi->ready())
        delay(250);
    Serial.print("IP: ");
    uint32_t ip = Physical.link->egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    server.on("/callback", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *req) {
        const char *code = http_get_query(req, "code");
        if (!code)
        {
            server.send(id, 400, "application/json", "{\"error\":\"missing code\"}");
            return;
        }
        pc_o_auth2_tokens t;
        int st = pc_oauth2_exchange_code(TOKEN_URL, code, REDIRECT_URI, CLIENT_ID, CLIENT_SECRET, nullptr, &t);
        if (st != 200)
        {
            char b[48];
            snprintf(b, sizeof(b), "{\"error\":\"exchange failed\",\"status\":%d}", st);
            server.send(id, 502, "application/json", b);
            return;
        }
        // t.access_token / t.id_token / t.refresh_token are now populated.
        char b[96];
        snprintf(b, sizeof(b), "{\"token_type\":\"%s\",\"expires_in\":%ld}", t.token_type, t.expires_in);
        server.send(id, 200, "application/json", b);
    });

    server.begin(80);
}

void loop()
{
    server.handle();
}
```
