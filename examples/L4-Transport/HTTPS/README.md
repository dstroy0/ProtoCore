# HTTPS - deterministic TLS with a static memory pool

**Layer:** L4 Transport · **Build flags:** `PC_ENABLE_TLS`

## What this example teaches

This serves the same routes over HTTPS on port 443. The notable part is _how_:
mbedTLS is pointed at a fixed BSS arena (`PC_TLS_ARENA_SIZE`) through a custom
allocator, so the TLS handshake and session use **no heap** and the determinism
guarantee holds. The RNG is the ESP32 hardware CSPRNG.

**One call to go encrypted.** Instead of `begin(port)`, you call `begin_tls()`
with the certificate and private key (PEM), and the server listens on 443:

```cpp
int32_t result = server.begin_tls(443,
    (const uint8_t *)CERT_PEM, sizeof(CERT_PEM),
    (const uint8_t *)KEY_PEM,  sizeof(KEY_PEM));
if (result < 0)
    Serial.printf("begin_tls() failed (%d) - check the cert/key and arena size\n", result);
```

The handlers are ordinary - TLS is a transport concern, so the same
`server.send()` works; a `/status` route just reports `tls:true` and free heap so
you can confirm nothing leaked to the heap during the handshake.

**Arena sizing.** If `begin_tls()` fails, the usual cause is a `PC_TLS_ARENA_SIZE`
too small for the handshake (a compile-time check rejects absurdly small values).
The arena is the only memory TLS uses; size it for your cert chain and cipher.

> **Demo certificate warning.** The cert and private key in the sketch are a
> throwaway self-signed pair committed for the example - the key is public. Never
> use them in production; generate your own and keep the key secret. The README
> elides the PEM blocks below (they are opaque base64); they are in the `.ino`.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_TLS=1 -DMAX_CONNS=4 -DPC_TLS_ARENA_SIZE=32768" \
  --lib="." examples/L4-Transport/HTTPS/HTTPS.ino
```

The cert is self-signed, so use `-k`:

```sh
curl -k https://<ip>/
openssl s_client -connect <ip>:443        # inspect the handshake
```

## Annotated source

The complete sketch ([HTTPS.ino](HTTPS.ino)). The demo PEM cert/key blocks
are elided here for brevity (see the `.ino`); the C++ is verbatim with comments.

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_TLS 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// Test-only self-signed ECDSA (P-256) server certificate + key. DEMO ONLY.
static const char CERT_PEM[] = R"PEM(-----BEGIN CERTIFICATE-----
... self-signed demo certificate (full PEM in the .ino) ...
-----END CERTIFICATE-----
)PEM";

static const char KEY_PEM[] = R"PEM(-----BEGIN EC PRIVATE KEY-----
... demo private key - PUBLIC, never use in production (full PEM in the .ino) ...
-----END EC PRIVATE KEY-----
)PEM";

PC server;

void handle_root(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    server.send(slot_id, 200, "text/plain", "hello over TLS (deterministic, static-pool mbedTLS)\n");
}

void handle_status(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    char body[96];
    // tls:true plus free_heap, to show the handshake touched no heap.
    snprintf(body, sizeof(body), "{\"tls\":true,\"uptime_ms\":%lu,\"free_heap\":%u}", millis(), ESP.getFreeHeap());
    server.send(slot_id, 200, "application/json", body);
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


    server.on("/", HttpMethod::HTTP_GET, handle_root);
    server.on("/status", HttpMethod::HTTP_GET, handle_status);

    // Load cert/key into the static-pool TLS engine and listen on 443.
    int32_t result =
        server.begin_tls(443, (const uint8_t *)CERT_PEM, sizeof(CERT_PEM), (const uint8_t *)KEY_PEM, sizeof(KEY_PEM));
    if (result < 0)
    {
        Serial.printf("begin_tls() failed (error %d) - check the cert/key and arena size\n", result);
        return;
    }
    Serial.println("HTTPS server started on port 443  (curl -k https://<ip>/)");
}

void loop()
{
    server.handle();
}
```
