# mTLS - mutual TLS (verified client certificates)

**Layer:** L4 Transport · **Build flags:** `PC_ENABLE_TLS`, `PC_ENABLE_MTLS`

## What this example teaches

Mutual TLS turns the handshake into two-way authentication: the server demands a
client certificate and verifies it against a configured trust-anchor CA. A client
that presents no certificate, or one not signed by that CA, is rejected before
any HTTP is exchanged - strong transport-level client auth with no passwords.
Inside a handler, `tls_client_subject()` gives you the verified peer's subject DN
to identify or authorize the caller.

**A different setup path than plain HTTPS.** Where [HTTPS](../HTTPS) used
the one-shot `begin_tls()`, mTLS configures the engine in steps, then `begin()`:

```cpp
server.tls_cert(SERVER_CERT_PEM, ..., SERVER_KEY_PEM, ...);   // server identity
server.tls_require_client_cert(CA_CERT_PEM, ...);             // require + verify against this CA
server.listen_tls(443);                                       // TLS listener
int32_t result = server.begin();                             // activate
```

**Reading the verified identity.** Once the handshake succeeds, the peer's
certificate subject is available to handlers:

```cpp
char subject[PC_MTLS_SUBJECT_MAX];
if (server.tls_client_subject(id, subject, sizeof(subject)) > 0)
    server.send(id, 200, "text/plain", subject);    // e.g. "CN=alice@example.com"
else
    server.send(id, 200, "text/plain", "(no client certificate)");
```

A request with no client cert never reaches the handler - it fails in the
handshake - so the `/whoami` route is reached only by verified peers.

> **Demo material warning.** The CA, server, and client certs/keys in the sketch
> are throwaway demo material committed for the example - the keys are public.
> Generate your own in production. The README elides the PEM blocks (opaque
> base64); the full CA/server/client PEMs are in the `.ino`, including the client
> pair to split into `client.crt` / `client.key`.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_TLS=1 -DPC_ENABLE_MTLS=1 -DMAX_CONNS=4 -DPC_TLS_ARENA_SIZE=32768" \
  --lib="." examples/L4-Transport/mTLS/mTLS.ino
```

Extract the demo client PEM blocks from the `.ino` into `client.crt`/`client.key`,
then:

```sh
curl -k --cert client.crt --key client.key https://<ip>/whoami   # 200 + your DN
curl -k https://<ip>/whoami                                       # handshake fails (no cert)
```

## Annotated source

The complete sketch ([mTLS.ino](mTLS.ino)). The demo CA/server/client PEM
blocks are elided here for brevity (see the `.ino`); the C++ is verbatim with
comments.

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_TLS 1
#define PC_ENABLE_MTLS 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// DEMO server identity (ECDSA P-256), presented to the client.
static const char SERVER_CERT_PEM[] = R"PEM(-----BEGIN CERTIFICATE-----
... demo server cert (full PEM in the .ino) ...
-----END CERTIFICATE-----
)PEM";
static const char SERVER_KEY_PEM[] = R"PEM(-----BEGIN EC PRIVATE KEY-----
... demo server key - PUBLIC (full PEM in the .ino) ...
-----END EC PRIVATE KEY-----
)PEM";

// DEMO trust-anchor CA: client certs must chain to this to be accepted.
static const char CA_CERT_PEM[] = R"PEM(-----BEGIN CERTIFICATE-----
... demo CA cert (full PEM in the .ino) ...
-----END CERTIFICATE-----
)PEM";

// (The .ino also lists a DEMO client cert + key that chains to the CA above,
//  to split into client.crt / client.key for curl --cert/--key.)

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

    // Reached only by verified clients; report their certificate subject DN.
    server.on("/whoami", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) {
        char subject[PC_MTLS_SUBJECT_MAX];
        if (server.tls_client_subject(id, subject, sizeof(subject)) > 0)
            server.send(id, 200, "text/plain", subject);
        else
            server.send(id, 200, "text/plain", "(no client certificate)");
    });

    // 1) load the server identity, 2) require a CA-signed client cert, 3) listen.
    if (!server.tls_cert((const uint8_t *)SERVER_CERT_PEM, sizeof(SERVER_CERT_PEM), (const uint8_t *)SERVER_KEY_PEM,
                         sizeof(SERVER_KEY_PEM)))
    {
        Serial.println("TLS cert/key load failed");
        return;
    }
    if (!server.tls_require_client_cert((const uint8_t *)CA_CERT_PEM, sizeof(CA_CERT_PEM)))
    {
        Serial.println("client CA load failed");
        return;
    }
    server.listen_tls(443);

    int32_t result = server.begin();
    if (result < 0)
        Serial.printf("begin() failed (error %d)\n", result);
    else
        Serial.println("mTLS server on :443 (curl -k --cert client.crt --key client.key https://<ip>/whoami)");
}

void loop()
{
    server.handle();
}
```
