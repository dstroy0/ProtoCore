# CoAP - a zero-heap CoAP server

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_COAP`

## What this example teaches

CoAP (RFC 7252) is a compact request/response protocol for constrained devices,
running over UDP. This stands up a CoAP server on UDP/5683 alongside the HTTP
server, exposing a few resources dispatched by Uri-Path. It is callback-driven
over the transport-layer UDP service (no per-loop servicing) and every buffer is
static.

**Build a resource table, then bind.** Each resource has a path, an allowed-method
mask, and a handler:

```cpp
pc_coap_server_reset();
pc_coap_server_add_resource("/info", CoapMethodMask::COAP_ALLOW_GET, coap_info);
pc_coap_server_add_resource("/led", CoapMethodMask::COAP_ALLOW_GET | CoapMethodMask::COAP_ALLOW_PUT,
                         coap_led);
pc_coap_server_add_resource("/hello", CoapMethodMask::COAP_ALLOW_GET, coap_hello);
pc_coap_server_begin(5683);
```

**The handler shape.** A handler fills a `CoapResponse` from the `CoapRequest`:
write into `resp->payload` (bounded by `resp->payload_cap`), set `payload_len`,
the `content_format`, and the response `code`. The LED handler shows reading the
method and the request payload:

```cpp
static void coap_led(const CoapRequest *req, CoapResponse *resp) {
    if (req->method == CoapMethod::COAP_PUT) {
        g_led_state = (req->payload_len && req->payload[0] != '0') ? 1 : 0;
        digitalWrite(LED_BUILTIN, g_led_state ? HIGH : LOW);
        resp->code = (uint8_t)CoapResponseCode::COAP_RSP_CHANGED; resp->payload_len = 0; return;
    }
    resp->payload[0] = g_led_state ? '1' : '0';
    resp->payload_len = 1; resp->content_format = CoapContentFormat::COAP_CF_TEXT; resp->code = (uint8_t)CoapResponseCode::COAP_RSP_CONTENT;
}
```

CON requests get a piggybacked ACK; NON requests get a NON response - handled by
the library. For server-push and large transfers, see
[CoapObserve](../CoapObserve) and [CoapBlock](../CoapBlock).

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_COAP=1" \
  --lib="." examples/L7-Application/CoAP/CoAP.ino
```

```sh
coap-client -m get coap://<ip>/info
coap-client -m put -e 1 coap://<ip>/led
coap-client -m get coap://<ip>/hello
```

## Annotated source

The complete sketch ([CoAP.ino](CoAP.ino)), reproduced verbatim with added
explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_COAP 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/iot/coap/coap.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

#ifndef LED_BUILTIN
#define LED_BUILTIN 2
#endif

PC server;
static int g_led_state = 0;

// GET /info -> a small JSON document with uptime and free heap.
static void coap_info(const CoapRequest *req, CoapResponse *resp)
{
    (void)req;
    int n = snprintf((char *)resp->payload, resp->payload_cap, "{\"uptime_ms\":%lu,\"free_heap\":%u}",
                     (unsigned long)millis(), (unsigned)ESP.getFreeHeap());
    if (n < 0)
        n = 0;
    resp->payload_len = (size_t)n;
    resp->content_format = CoapContentFormat::COAP_CF_JSON;
    resp->code = (uint8_t)CoapResponseCode::COAP_RSP_CONTENT;
}

// GET/PUT /led -> read or drive the on-board LED.
static void coap_led(const CoapRequest *req, CoapResponse *resp)
{
    if (req->method == CoapMethod::COAP_PUT)
    {
        // Treat any payload beginning with a non-'0' character as "on".
        g_led_state = (req->payload_len && req->payload[0] != '0') ? 1 : 0;
        digitalWrite(LED_BUILTIN, g_led_state ? HIGH : LOW);
        resp->code = (uint8_t)CoapResponseCode::COAP_RSP_CHANGED;
        resp->payload_len = 0;
        return;
    }
    resp->payload[0] = g_led_state ? '1' : '0';
    resp->payload_len = 1;
    resp->content_format = CoapContentFormat::COAP_CF_TEXT;
    resp->code = (uint8_t)CoapResponseCode::COAP_RSP_CONTENT;
}

// GET /hello -> a constant greeting.
static void coap_hello(const CoapRequest *req, CoapResponse *resp)
{
    (void)req;
    static const char msg[] = "hello from a deterministic CoAP server";
    size_t n = sizeof(msg) - 1;
    if (n > resp->payload_cap)
        n = resp->payload_cap;
    memcpy(resp->payload, msg, n);
    resp->payload_len = n;
    resp->content_format = CoapContentFormat::COAP_CF_TEXT;
    resp->code = (uint8_t)CoapResponseCode::COAP_RSP_CONTENT;
}

void setup()
{
    Serial.begin(115200);
    pinMode(LED_BUILTIN, OUTPUT);

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

    // Build the resource table, then bind the server to UDP/5683.
    pc_coap_server_reset();
    pc_coap_server_add_resource("/info", CoapMethodMask::COAP_ALLOW_GET, coap_info);
    pc_coap_server_add_resource("/led", CoapMethodMask::COAP_ALLOW_GET | CoapMethodMask::COAP_ALLOW_PUT,
                         coap_led);
    pc_coap_server_add_resource("/hello", CoapMethodMask::COAP_ALLOW_GET, coap_hello);
    pc_coap_server_begin(5683);
    Serial.println("CoAP server listening on UDP/5683 (try: coap-client -m get coap://<ip>/info)");

    int32_t result = server.begin(80);
    if (result < 0)
        Serial.printf("begin() failed (error %d)\n", result);
}

void loop()
{
    server.handle(); // CoAP is serviced by lwIP callbacks; this drives the TCP server.
}
```
