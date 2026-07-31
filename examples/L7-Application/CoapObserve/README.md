# CoapObserve - CoAP resource observation (server push)

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_COAP`, `PC_ENABLE_COAP_OBSERVE`

## What this example teaches

CoAP Observe (RFC 7641) turns request/response into publish/subscribe: a client
sends a GET with the Observe option and the server keeps pushing updates as the
resource changes - the CoAP equivalent of server-sent events, but over UDP. This
serves an observable `/count` resource; every second it increments the counter and
notifies all observers. It builds on the plain CoAP server in
[CoAP](../CoAP).

**Register an ordinary resource, then notify on change.** The handler is the same
shape as any CoAP handler; what makes it observable is that observers are tracked
by the library and you call `pc_coap_notify()` when the representation changes:

```cpp
pc_coap_server_add_resource("/count", CoapMethodMask::COAP_ALLOW_GET, h_count);
pc_coap_server_begin(5683);
```

```cpp
void loop() {
    if (/* once a second */) {
        g_count++;
        pc_coap_notify("/count"); // push the new value to every observer
    }
}
```

Each notification carries an increasing Observe sequence number so a client can
detect reordering. Observe with `coap-client -m get -s 30 coap://<ip>/count` or
`aiocoap-client --observe coap://<ip>/count`.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_COAP=1 -DPC_ENABLE_COAP_OBSERVE=1" \
  --lib="." examples/L7-Application/CoapObserve/CoapObserve.ino
```

```sh
coap-client -m get -s 30 coap://<ip>/count   # libcoap, -s = observe for 30s
aiocoap-client --observe coap://<ip>/count
```

## Annotated source

The complete sketch ([CoapObserve.ino](CoapObserve.ino)), reproduced
verbatim with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_COAP 1
#define PC_ENABLE_COAP_OBSERVE 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/iot/coap/coap.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

static uint32_t g_count = 0;

// GET /count -> the current counter value (also pushed to observers on change).
void h_count(const CoapRequest *req, CoapResponse *resp)
{
    (void)req;
    int n = snprintf((char *)resp->payload, resp->payload_cap, "%lu", (unsigned long)g_count);
    resp->payload_len = (n > 0) ? (size_t)n : 0;
    resp->content_format = CoapContentFormat::COAP_CF_TEXT;
    resp->code = (uint8_t)CoapResponseCode::COAP_RSP_CONTENT;
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

    pc_coap_server_reset();
    pc_coap_server_add_resource("/count", CoapMethodMask::COAP_ALLOW_GET, h_count);
    pc_coap_server_begin(5683);
    Serial.println("CoAP server on :5683, observe coap://<ip>/count");
}

void loop()
{
    static uint32_t last = 0;
    if (millis() - last >= 1000)
    {
        last = millis();
        g_count++;
        pc_coap_notify("/count"); // push the new value to every observer
    }
}
```
