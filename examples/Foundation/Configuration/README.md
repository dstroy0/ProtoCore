# Configuration - sizing the server for your build

**Layer:** Foundation (tutorial) · **Build flags:** core (this example _overrides_ defaults rather than enabling features)

## What this example teaches

Everything in the library is sized at compile time, and this example is the
reference for the knobs. It strips optional features down to a lean REST-only
profile, tightens every capacity constant, exposes routes that echo the active
configuration so you can confirm your overrides took effect, and demonstrates a
runtime config struct. The sketch's header comment is itself a cheat-sheet of
every flag and constant.

**Overrides go BEFORE the include.** Each `#define` must appear before
`#include "protocore.h"` so the library compiles with your
value instead of the default. Here the optional subsystems are stripped and the
buffers are shrunk for a small sensor node:

```cpp
#define PC_ENABLE_WEBSOCKET 0   // strip features entirely: no code, no RAM, no flash
#define PC_ENABLE_SSE 0
#define PC_ENABLE_MULTIPART 0
#define PC_ENABLE_FILE_SERVING 0
#define PC_ENABLE_AUTH 0
#define MAX_CONNS 2                 // tighten capacity to roughly halve RAM
#define RX_BUF_SIZE 512
#define BODY_BUF_SIZE 128
#include "protocore.h"
```

> When compiling with `pio ci` (separate library translation units), these
> in-sketch `#define`s only affect the sketch. Pass them as `build_flags` so the
> library compiles with them too - see the
> [build-flag tree](../../../README.md#build-flag-dependencies) and the
> repository's contributing notes.

**Capacity constants are visible at runtime.** `GET /config` serializes the
active sizing constants as JSON, so you can verify a build without a debugger:

```cpp
snprintf(body, sizeof(body), "{\"MAX_CONNS\":%u,\"RX_BUF_SIZE\":%u,\"BODY_BUF_SIZE\":%u,...}",
         (unsigned)MAX_CONNS, (unsigned)RX_BUF_SIZE, (unsigned)BODY_BUF_SIZE, ...);
```

**The limits are real.** With `BODY_BUF_SIZE=128`, a POST body over 128 bytes is
rejected with an automatic `413` before your handler runs. With
`MAX_QUERY_PARAMS=4`, a fifth query parameter is parsed but silently dropped -
`GET /search` echoes exactly what was kept so you can see the boundary.

**Runtime config struct.** A `WebServerConfig` passed to `begin()` overrides the
idle timeout without a rebuild (pass `nullptr` to use the built-in default,
`CONN_TIMEOUT_MS`):

```cpp
WebServerConfig cfg = {.conn_timeout_ms = CONN_TIMEOUT_MS};
int32_t result = server.begin(80, &cfg);
```

The header comment also documents the **feature-dependency tree** (a child flag
needs its parent; illegal combinations fail with a compile-time `#error`).

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_DIAG=1" \
  --lib="." examples/Foundation/Configuration/Configuration.ino
```

```sh
curl http://<ip>/config                          # see the active sizing constants
curl -X POST http://<ip>/echo -d "hello"         # bodies > BODY_BUF_SIZE get a 413
curl "http://<ip>/search?q=esp32&sort=date"      # extra params past MAX_QUERY_PARAMS are dropped
```

## Annotated source

The complete sketch ([Configuration.ino](Configuration.ino)). The long
header comment (a full flag/constant reference) is kept verbatim; the C++ is
annotated.

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

/**
 * @file Configuration.ino
 * @brief Reference for every user-configurable flag and constant, plus runnable
 *        routes that echo the active configuration so you can confirm your
 *        overrides took effect.
 *
 * All #defines must appear BEFORE the library include (or be supplied as
 * -D build flags in platformio.ini).  Any that are omitted get their documented
 * defaults.  Illegal combinations (e.g. pool sizes that exceed MAX_CONNS)
 * produce a #error in the compiler output rather than a silent misbehavior.
 *
 *   FEATURE FLAGS, CAPACITY CONSTANTS, FEATURE DEPENDENCIES, and RUNTIME CONFIG
 *   are all cataloged in the .ino header - keep it open as a reference.
 */

// -------------------------------------------------------------------
// A low-footprint REST-only profile: no WS, SSE, multipart, or file IO.
// These values roughly halve RAM use versus the defaults; good for sensor
// nodes sharing the heap with other subsystems. Each #define MUST precede the
// library include to take effect.
// -------------------------------------------------------------------
#define PC_ENABLE_WEBSOCKET 0
#define PC_ENABLE_SSE 0
#define PC_ENABLE_MULTIPART 0
#define PC_ENABLE_FILE_SERVING 0
#define PC_ENABLE_AUTH 0

// Diagnostic endpoint: exposes the compile-time config; disable in production.
#define PC_ENABLE_DIAG 1

// Tightened capacity to match a small REST API (every value sizes a BSS array).
#define MAX_CONNS 2
#define EVT_QUEUE_DEPTH 8 // must be >= MAX_CONNS * 4
#define RX_BUF_SIZE 512
#define BODY_BUF_SIZE 128
#define MAX_ROUTES 8
#define MAX_HEADERS 6
#define MAX_PATH_LEN 48
#define MAX_KEY_LEN 16
#define MAX_VAL_LEN 32
#define MAX_QUERY_LEN 64
#define MAX_QUERY_PARAMS 4
#define QUERY_KEY_LEN 16
#define QUERY_VAL_LEN 24
#define CONN_TIMEOUT_MS 3000

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// GET /config - return every active sizing constant as JSON so you can verify
// your build flags / #defines took effect without a debugger.
void handle_config(uint8_t slot_id, HttpReq *req)
{
    char body[384];
    snprintf(body, sizeof(body),
             "{"
             "\"MAX_CONNS\":%u,"
             "\"RX_BUF_SIZE\":%u,"
             "\"CONN_TIMEOUT_MS\":%u,"
             "\"MAX_HEADERS\":%u,"
             "\"MAX_PATH_LEN\":%u,"
             "\"MAX_KEY_LEN\":%u,"
             "\"MAX_VAL_LEN\":%u,"
             "\"MAX_QUERY_LEN\":%u,"
             "\"MAX_QUERY_PARAMS\":%u,"
             "\"QUERY_KEY_LEN\":%u,"
             "\"QUERY_VAL_LEN\":%u,"
             "\"BODY_BUF_SIZE\":%u,"
             "\"MAX_ROUTES\":%u"
             "}",
             (unsigned)MAX_CONNS, (unsigned)RX_BUF_SIZE, (unsigned)CONN_TIMEOUT_MS, (unsigned)MAX_HEADERS,
             (unsigned)MAX_PATH_LEN, (unsigned)MAX_KEY_LEN, (unsigned)MAX_VAL_LEN, (unsigned)MAX_QUERY_LEN,
             (unsigned)MAX_QUERY_PARAMS, (unsigned)QUERY_KEY_LEN, (unsigned)QUERY_VAL_LEN, (unsigned)BODY_BUF_SIZE,
             (unsigned)MAX_ROUTES);

    server.send(slot_id, 200, "application/json", body);
}

// POST /echo - echo the body. With BODY_BUF_SIZE=128, a body over 128 bytes is
// auto-rejected with 413 BEFORE this handler is ever called.
void handle_echo(uint8_t slot_id, HttpReq *req)
{
    server.send(slot_id, 200, "text/plain", (const char *)req->body);
}

// GET /search - echo the parsed query parameters. With MAX_QUERY_PARAMS=4, a
// fifth parameter is parsed but silently dropped: this shows what was kept.
void handle_search(uint8_t slot_id, HttpReq *req)
{
    char body[256] = "params: ";
    for (uint8_t i = 0; i < req->query_count; i++)
    {
        if (i > 0)
            strncat(body, ", ", sizeof(body) - strlen(body) - 1);
        strncat(body, req->query_params[i].key, sizeof(body) - strlen(body) - 1);
        strncat(body, "=", sizeof(body) - strlen(body) - 1);
        strncat(body, req->query_params[i].val, sizeof(body) - strlen(body) - 1);
    }
    server.send(slot_id, 200, "text/plain", body);
}

void setup()
{
    Serial.begin(115200);

    // Print the active config so you can confirm overrides without curl.
    Serial.println("\n--- Active PC config ---");
    Serial.printf("  MAX_CONNS        = %u\n", (unsigned)MAX_CONNS);
    Serial.printf("  RX_BUF_SIZE      = %u\n", (unsigned)RX_BUF_SIZE);
    Serial.printf("  CONN_TIMEOUT_MS  = %u\n", (unsigned)CONN_TIMEOUT_MS);
    Serial.printf("  BODY_BUF_SIZE    = %u\n", (unsigned)BODY_BUF_SIZE);
    Serial.printf("  MAX_QUERY_PARAMS = %u\n", (unsigned)MAX_QUERY_PARAMS);
    Serial.println("----------------------------------");

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

    server.on("/config", HttpMethod::HTTP_GET, handle_config);
    server.on("/echo", HttpMethod::HTTP_POST, handle_echo);
    server.on("/search", HttpMethod::HTTP_GET, handle_search);

    // Diagnostic route (PC_ENABLE_DIAG=1): remove or protect in production.
    server.on("/diag", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { server.diag(id); });

    // Pass a runtime config to override the idle timeout without a rebuild.
    WebServerConfig cfg = {.conn_timeout_ms = CONN_TIMEOUT_MS};
    int32_t result = server.begin(80, &cfg);
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
        return;
    }
    Serial.println("Server started on port 80");
}

void loop()
{
    server.handle();
}
```
