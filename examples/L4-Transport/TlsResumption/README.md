# TlsResumption - cheap repeat handshakes via session tickets

**Layer:** L4 Transport · **Build flags:** `PC_ENABLE_TLS`, `PC_ENABLE_TLS_RESUMPTION`

## What this example teaches

This is the [HTTPS server](../HTTPS) with one addition: RFC 5077 session
tickets. A returning client completes an **abbreviated** handshake - no
certificate exchange, no full ECDHE/RSA key agreement - which is dramatically
cheaper on a constrained device. The handler code is identical; resumption is a
transport-layer optimization toggled by a build flag.

**Why it stays deterministic.** Resumption here is _stateless_: the session is
serialized into a ticket and handed to the client, sealed with a server-held key.
The server stores nothing per session, so the zero-heap guarantee holds no matter
how many clients resume:

```cpp
int32_t r = server.begin_tls(443, CERT_PEM, sizeof(CERT_PEM), KEY_PEM, sizeof(KEY_PEM));
// session tickets are issued/accepted automatically because the flag is built in
```

**Verifying it works.** OpenSSL's `-reconnect` makes several connections reusing
the session; look for `Reused` on the later ones:

```sh
openssl s_client -connect <ip>:443 -tls1_2 -reconnect
```

**Build dependency.** `PC_ENABLE_TLS_RESUMPTION` requires `PC_ENABLE_TLS`
(enforced by a compile-time `#error`), and the platform's mbedTLS build must
provide the session-ticket support; pass both flags to the library build.

> Demo cert/key as in [HTTPS](../HTTPS) - public, demo-only. The PEM blocks
> are elided below; see the `.ino`.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_TLS=1 -DPC_ENABLE_TLS_RESUMPTION=1 -DMAX_CONNS=4 -DPC_TLS_ARENA_SIZE=32768" \
  --lib="." examples/L4-Transport/TlsResumption/TlsResumption.ino
```

## Annotated source

The complete sketch ([TlsResumption.ino](TlsResumption.ino)). The demo PEM
cert/key are elided here (see the `.ino`); the C++ is verbatim with comments.

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_TLS 1
#define PC_ENABLE_TLS_RESUMPTION 1

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

void setup()
{
    Serial.begin(115200);

    Physical.wifi->init(SSID, PASSWORD);
    Serial.print("Connecting to WiFi");
    while (!Physical.wifi->ready())
    {
        delay(250);
        Serial.print('.');
    }
    uint32_t ip = Physical.link->egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    server.on("/", HttpMethod::HTTP_GET,
              [](uint8_t id, HttpReq *) { server.send(id, 200, "text/plain", "hello over resumable TLS\n"); });

    // Same begin_tls() as plain HTTPS; tickets are issued/accepted automatically
    // because PC_ENABLE_TLS_RESUMPTION is compiled in.
    int32_t r =
        server.begin_tls(443, (const uint8_t *)CERT_PEM, sizeof(CERT_PEM), (const uint8_t *)KEY_PEM, sizeof(KEY_PEM));
    if (r < 0)
    {
        Serial.printf("begin_tls() failed (%d)\n", (int)r);
        return;
    }
    Serial.println("HTTPS + session resumption on :443  (openssl s_client -reconnect to see 'Reused')");
}

void loop()
{
    server.handle();
}
```
