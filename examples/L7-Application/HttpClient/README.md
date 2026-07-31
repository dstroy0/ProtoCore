# HttpClient - the device makes outbound HTTP(S) requests

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_HTTP_CLIENT` (optional `PC_ENABLE_TLS` + `PC_ENABLE_HTTP_CLIENT_TLS` for `https://`)

## What this example teaches

So far the device has been a server; here it is a **client**. `http_get(url, &r)`
performs a blocking GET over raw lwIP (with client-side mbedTLS for `https://`),
for webhooks, telemetry push, or REST calls out to a remote service. The host is
resolved via DNS (or used directly if it is a dotted-quad IP) and the response
status and body land in a fixed static buffer (no heap).

**One blocking call, result in a static struct:**

```cpp
HttpClientResult r;
int status = http_get(URL, &r);   // < 0 on transport error
if (status >= 0) {
    Serial.printf("HTTP %d, %u body bytes:\n", r.status, (unsigned)r.body_len);
    Serial.write(r.body, r.body_len);
}
```

The return value is negative on a transport-level failure; on success `r.status`
holds the HTTP status code and `r.body`/`r.body_len` the (bounded) body.

**TLS trust.** `https://` is encrypt-only by default - the device has no trust
store, so the peer is **unauthenticated**. To authenticate the server, install a
CA trust anchor with `http_client_set_ca(pem, len)` (verifies chain + hostname)
and/or a SHA-256 certificate pin with `http_client_set_pin(hash32)`; call once
before issuing requests, and a verification failure aborts the request.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_HTTP_CLIENT=1" \
  --lib="." examples/L7-Application/HttpClient/HttpClient.ino
```

For `https://`, add `-DPC_ENABLE_TLS=1 -DPC_ENABLE_HTTP_CLIENT_TLS=1`. The
sketch fetches `URL` once at boot and prints the result to Serial @ 115200.

## Annotated source

The complete sketch ([HttpClient.ino](HttpClient.ino)), reproduced verbatim
with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_HTTP_CLIENT 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/net/http_client/http_client.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

static const char *URL = "http://example.com/";

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

    // Blocking GET; result (status + bounded body) lands in a static struct.
    HttpClientResult r;
    int status = http_get(URL, &r);
    if (status < 0)
    {
        Serial.printf("request failed (error %d)\n", status); // transport-level failure
    }
    else
    {
        Serial.printf("HTTP %d, %u body bytes:\n", r.status, (unsigned)r.body_len);
        Serial.write(r.body, r.body_len);
        Serial.println();
    }
}

void loop()
{
    delay(1000);
}
```
