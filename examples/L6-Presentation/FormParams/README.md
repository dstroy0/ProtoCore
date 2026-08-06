# FormParams - reading urlencoded form fields

**Layer:** L6 Presentation · **Build flags:** none (core features only)

## What this example teaches

This shows the three request-data accessors working together: `http_get_form()`
for `application/x-www-form-urlencoded` body fields, `http_get_query()` for
query-string parameters, and `http_get_header()` for request headers - all
allocation-free, reading into caller-supplied buffers.

**Form fields on demand.** `http_get_form()` parses a named field out of the body
into your buffer and returns whether it was found. It is gated on the
`Content-Type: application/x-www-form-urlencoded` header and returns the raw
(un-decoded) value, matching `http_get_query()`:

```cpp
char name[48], email[64];
bool have_name  = http_get_form(req, "name",  name,  sizeof(name));
bool have_email = http_get_form(req, "email", email, sizeof(email));
```

**Mixing sources.** A `?debug=1` query parameter (read with `http_get_query`)
switches on extra output that also reflects the `User-Agent` header (read with
`http_get_header`) - so one request reads from the body, the query string, and a
header at once:

```cpp
const char *debug = http_get_query(req, "debug");
if (debug && strcmp(debug, "1") == 0) {
    const char *ua = http_get_header(req, "User-Agent");
    // ...include ua in the response...
}
```

Missing both fields yields a `400`; otherwise the handler echoes them back as
JSON built with `snprintf`.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --lib="." examples/L6-Presentation/FormParams/FormParams.ino
```

```sh
curl -X POST http://<ip>/form -d "name=ada&email=ada@example.com"
curl -X POST "http://<ip>/form?debug=1" -d "name=ada&email=ada@example.com"
```

## Annotated source

The complete sketch ([FormParams.ino](FormParams.ino)), reproduced verbatim
with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// POST /form - echo the urlencoded "name" and "email" fields back as JSON.
void handle_form(uint8_t slot_id, HttpReq *req)
{
    char name[48];
    char email[64];
    // http_get_form returns false if the field is absent (or the body is not
    // urlencoded). Values are raw (un-decoded), like http_get_query.
    bool have_name = http_get_form(req, "name", name, sizeof(name));
    bool have_email = http_get_form(req, "email", email, sizeof(email));

    if (!have_name && !have_email)
    {
        server.send(slot_id, 400, "text/plain", "expected urlencoded body with name= / email=");
        return;
    }

    // ?debug=1 in the query string mirrors the User-Agent header back too.
    const char *debug = http_get_query(req, "debug");
    char body[256];
    if (debug && strcmp(debug, "1") == 0)
    {
        const char *ua = http_get_header(req, "User-Agent");
        snprintf(body, sizeof(body), "{\"name\":\"%s\",\"email\":\"%s\",\"ua\":\"%s\"}", have_name ? name : "",
                 have_email ? email : "", ua ? ua : "");
    }
    else
    {
        snprintf(body, sizeof(body), "{\"name\":\"%s\",\"email\":\"%s\"}", have_name ? name : "",
                 have_email ? email : "");
    }
    server.send(slot_id, 200, "application/json", body);
}

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


    server.on("/form", HttpMethod::HTTP_POST, handle_form);

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
