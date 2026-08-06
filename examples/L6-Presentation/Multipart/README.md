# Multipart - parsing multipart/form-data in place

**Layer:** L6 Presentation · **Build flags:** none (core; multipart is on by default)

## What this example teaches

This parses a `multipart/form-data` POST body (RFC 7578) without allocation:
`pc_multipart_parse()` splits the in-buffer body into parts, and
`pc_multipart_get_field()` returns a named text field. A test form is served at `/`.

**In-place, bounded parsing.** The whole body must fit in `BODY_BUF_SIZE` (there
is no streaming), so this is for small form fields and tiny uploads. The parser
populates a `Multipart` struct that indexes into the existing body buffer rather
than copying:

```cpp
Multipart mp;
if (!pc_multipart_parse(req, &mp)) {            // false if not multipart, or too big for BODY_BUF_SIZE
    server.send(id, 400, "text/plain", "expected multipart/form-data (and within BODY_BUF_SIZE)");
    return;
}
const char *name = pc_multipart_get_field(&mp, "name");   // a named text part, or nullptr
```

`mp.part_count` tells you how many parts were found. For large/streamed uploads
straight to a file, see [FileUpload](../../L7-Application/FileUpload).

The HTML form at `/` posts a `name` field and a `file` input so you can exercise
it from a browser.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --lib="." examples/L6-Presentation/Multipart/Multipart.ino
```

Flash, then browse to `http://<ip>/` and submit the form.

## Annotated source

The complete sketch ([Multipart.ino](Multipart.ino)), reproduced verbatim
with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// A tiny multipart test form (name field + file input).
static const char FORM[] = "<!doctype html><meta charset=utf-8><title>upload</title>"
                           "<form method=POST action=/upload enctype=multipart/form-data>"
                           "<input name=name placeholder=name> "
                           "<input type=file name=file> <button>upload</button></form>";

void handle_upload(uint8_t id, HttpReq *req)
{
    Multipart mp;
    if (!pc_multipart_parse(req, &mp)) // requires multipart/form-data and a body <= BODY_BUF_SIZE
    {
        server.send(id, 400, "text/plain", "expected multipart/form-data (and within BODY_BUF_SIZE)");
        return;
    }
    const char *name = pc_multipart_get_field(&mp, "name"); // index into the body, no copy
    char out[160];
    snprintf(out, sizeof(out), "parsed %d part(s); field 'name' = %s", mp.part_count, name ? name : "(absent)");
    server.send(id, 200, "text/plain", out);
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

    server.on("/", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) { server.send(id, 200, "text/html", FORM); });
    server.on("/upload", HttpMethod::HTTP_POST, handle_upload);
    server.begin(80);
}

void loop()
{
    server.handle();
}
```
