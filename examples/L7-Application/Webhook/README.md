# Webhook - outbound webhooks / IFTTT

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_HTTP_CLIENT`, `PC_ENABLE_WEBHOOK`

## What this example teaches

A webhook pushes an event _from_ the device to some other service: Slack, Discord,
IFTTT, or your own API. This builds a small JSON payload and POSTs it through the
outbound HTTP client ([HttpClient](../HttpClient)), with a helper for the
IFTTT Maker `value1/2/3` shape. It fires once at boot.

**Build a payload, then POST it:**

```cpp
char body[128];
pc_ifttt_payload("boot", "esp32", nullptr, body, sizeof(body)); // {"value1":...,"value2":...}
int status = pc_webhook_post(WEBHOOK_URL, body);                // returns the HTTP status
```

`pc_ifttt_payload()` formats the three IFTTT values into JSON;
`pc_webhook_post()` sends it. There is also a one-shot
`pc_ifttt_trigger(event, key, v1, v2, v3)` that builds the Maker URL for you.

**Where it fires matters.** The POST is blocking, so the example fires it from
`loop()` (guarded by a `fired` flag), not from a request handler - a blocking
outbound call inside a handler would stall the worker serving this device's own
server.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_HTTP_CLIENT=1 -DPC_ENABLE_WEBHOOK=1" \
  --lib="." examples/L7-Application/Webhook/Webhook.ino
```

```sh
# receive the POST on a host while the device boots:
nc -l 8080   # set WEBHOOK_URL to http://<this-host>:8080/hook
```

## Annotated source

The complete sketch ([Webhook.ino](Webhook.ino)), reproduced verbatim with
added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_HTTP_CLIENT 1
#define PC_ENABLE_WEBHOOK 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/net/webhook/webhook.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// A plain webhook endpoint (Slack/Discord/your API). For IFTTT use the helper below.
static const char *WEBHOOK_URL = "http://192.168.1.10:8080/hook";

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

    server.begin(80);
}

void loop()
{
    // Fire a webhook once at startup (and you could fire on any event). Done from
    // loop() (not a handler) so the blocking POST never stalls the worker that
    // serves this server.
    static bool fired = false;
    if (!fired && Physical.wifi->ready())
    {
        fired = true;
        char body[128];
        pc_ifttt_payload("boot", "esp32", nullptr, body, sizeof(body));
        int status = pc_webhook_post(WEBHOOK_URL, body);
        Serial.printf("[webhook] POST -> status %d\n", status);

        // IFTTT Maker form (needs your real key):
        //   pc_ifttt_trigger("device_boot", "YOUR_IFTTT_KEY", "esp32", nullptr, nullptr);
    }
}
```
