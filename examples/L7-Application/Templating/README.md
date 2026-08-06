# Templating - `{{name}}` placeholder substitution

**Layer:** L7 Application · **Build flags:** none (core features only)

## What this example teaches

`send_template()` streams an HTML (or text) template, replacing each `{{name}}`
token with a value from a resolver callback. Like the chunked writer, it never
buffers the body whole - it walks the template twice (once to size, once to
write), so memory use is constant regardless of page size.

**A resolver maps names to values.** Your callback receives a placeholder name and
returns the replacement (or `nullptr` for empty). Because it is called twice, it
must be deterministic; values formatted into a `static` buffer must stay valid
across both passes:

```cpp
static const char *resolver(const char *name) {
    static char buf[32];
    if (strcmp(name, "title") == 0) return "Templating Demo";
    if (strcmp(name, "uptime") == 0) { snprintf(buf, sizeof(buf), "%lu", millis()); return buf; }
    return nullptr; // unknown -> empty
}
...
server.send_template(slot_id, 200, "text/html", page, resolver);
```

Unknown, over-long, or unterminated placeholders are emitted literally, so a
malformed template degrades gracefully rather than corrupting output.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --lib="." examples/L7-Application/Templating/Templating.ino
```

```sh
curl http://<ip>/      # the {{...}} tokens are filled in
```

## Annotated source

The complete sketch ([Templating.ino](Templating.ino)), reproduced verbatim
with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

static unsigned long hit_count = 0;

// Resolver: map a placeholder name to its replacement. Returned pointers must
// stay valid for the duration of the send_template() call; the resolver is
// invoked twice (sizing pass + write pass) so it must be deterministic.
static const char *resolver(const char *name)
{
    static char buf[32];
    if (strcmp(name, "title") == 0)
        return "Templating Demo";
    if (strcmp(name, "uptime") == 0)
    {
        snprintf(buf, sizeof(buf), "%lu", millis());
        return buf;
    }
    if (strcmp(name, "hits") == 0)
    {
        snprintf(buf, sizeof(buf), "%lu", hit_count);
        return buf;
    }
    if (strcmp(name, "heap") == 0)
    {
        snprintf(buf, sizeof(buf), "%u", ESP.getFreeHeap());
        return buf;
    }
    return nullptr; // unknown -> empty
}

// GET / - render an HTML page from a template.
void handle_root(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    hit_count++;
    static const char page[] = "<!doctype html><html><body>"
                               "<h1>{{title}}</h1>"
                               "<p>uptime: {{uptime}} ms</p>"
                               "<p>hits: {{hits}}</p>"
                               "<p>free heap: {{heap}} bytes</p>"
                               "</body></html>";
    server.send_template(slot_id, 200, "text/html", page, resolver);
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


    server.on("/", HttpMethod::HTTP_GET, handle_root);

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
