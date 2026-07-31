# PathParams - capturing `:name` path segments

**Layer:** L7 Application · **Build flags:** none (core features only)

## What this example teaches

A route path can contain `:name` segments that capture the matching part of the
request path; you read the captured value back with `http_get_param()`. Literal
segments must match exactly, and a route may capture up to `MAX_PATH_PARAMS`
(default 4) segments.

**Declaring and reading params.** Put `:id` (etc.) in the path; in the handler,
`http_get_param(req, "id")` returns the captured value:

```cpp
// route: /users/:id
const char *id = http_get_param(req, "id");
// route: /users/:id/posts/:slug  -> two captures
const char *slug = http_get_param(req, "slug");
```

**Registration order matters.** Routes are matched in the order they are
registered, so register the more specific route first - here
`/users/:id/posts/:slug` before `/users/:id`, otherwise the latter would also
match the former's paths:

```cpp
server.on("/users/:id/posts/:slug", HttpMethod::HTTP_GET, handle_user_post); // more specific first
server.on("/users/:id", HttpMethod::HTTP_GET, handle_user);
```

For pattern matching beyond simple segments (numeric-only, file extensions), see
[RegexRoutes](../RegexRoutes).

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --lib="." examples/L7-Application/PathParams/PathParams.ino
```

```sh
curl http://<ip>/users/42
curl http://<ip>/users/42/posts/hello-world
```

## Annotated source

The complete sketch ([PathParams.ino](PathParams.ino)), reproduced verbatim
with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// GET /users/:id  - one captured segment.
void handle_user(uint8_t slot_id, HttpReq *req)
{
    const char *id = http_get_param(req, "id"); // nullptr if absent
    char body[96];
    snprintf(body, sizeof(body), "{\"user_id\":\"%s\"}", id ? id : "?");
    server.send(slot_id, 200, "application/json", body);
}

// GET /users/:id/posts/:slug  - two captured segments.
void handle_user_post(uint8_t slot_id, HttpReq *req)
{
    const char *id = http_get_param(req, "id");
    const char *slug = http_get_param(req, "slug");
    char body[160];
    snprintf(body, sizeof(body), "{\"user_id\":\"%s\",\"slug\":\"%s\"}", id ? id : "?", slug ? slug : "?");
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


    // Register the more specific route first; routes match in registration order.
    server.on("/users/:id/posts/:slug", HttpMethod::HTTP_GET, handle_user_post);
    server.on("/users/:id", HttpMethod::HTTP_GET, handle_user);

    int32_t result = server.begin(80);
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
