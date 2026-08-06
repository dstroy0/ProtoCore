# Json - zero-heap JSON read and write

**Layer:** L6 Presentation · **Build flags:** none (core features only)

## What this example teaches

The library's JSON support never allocates. `JsonWriter` formats into a fixed
buffer you own, and `json_get_str` / `json_get_int` / `json_get_bool` read
top-level members of a request body in place. This example builds a response with
the writer and parses a request with the readers.

**Building with `JsonWriter`.** You hand it a stack buffer and call structural
methods; it tracks nesting and overflow for you. Check `ok()` before sending -
if the buffer was too small, `ok()` is false rather than truncating silently:

```cpp
char buf[160];
JsonWriter w(buf, sizeof(buf));
w.begin_object();
w.kv_str("lib", "ProtoCore");
w.kv_uint("uptime_ms", (unsigned long)millis());
w.key("features"); w.begin_array(); w.str("json"); w.str("chunked"); w.end_array();
w.end_object();
if (w.ok()) server.send(slot_id, 200, "application/json", w.c_str());
else        server.send(slot_id, 500, "text/plain", "json buffer overflow");
```

**Reading in place.** The `json_get_*` readers scan the body for a top-level key
and extract the value without copying the whole document or allocating a parse
tree. Each returns whether the key was found:

```cpp
char name[32]; long age = 0; bool admin = false;
bool have_name = json_get_str((const char *)req->body, "name", name, sizeof(name));
json_get_int((const char *)req->body, "age", &age);
json_get_bool((const char *)req->body, "admin", &admin);
```

This is the proper tool for JSON bodies; the hand-rolled scanners in
[Advanced](../../Foundation/Advanced) were just to show the mechanism.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --lib="." examples/L6-Presentation/Json/Json.ino
```

```sh
curl http://<ip>/api/info
curl -X POST http://<ip>/api/echo -H "Content-Type: application/json" \
     -d '{"name":"ada","age":36,"admin":true}'
```

## Annotated source

The complete sketch ([Json.ino](Json.ino)), reproduced verbatim with added
explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "protocore.h"
#include "network_drivers/physical/physical.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// GET /api/info - build a JSON object with JsonWriter (no heap).
void handle_info(uint8_t slot_id, HttpReq *req)
{
    (void)req;
    char buf[160];
    JsonWriter w(buf, sizeof(buf)); // writes into this fixed buffer; tracks overflow
    w.begin_object();
    w.kv_str("lib", "ProtoCore");
    w.kv_uint("uptime_ms", (unsigned long)millis());
    w.kv_uint("free_heap", (unsigned long)ESP.getFreeHeap());
    w.key("features");                 // a key whose value is a nested array
    w.begin_array();
    w.str("json");
    w.str("chunked");
    w.str("middleware");
    w.end_array();
    w.end_object();

    if (w.ok())                        // false if the buffer overflowed
        server.send(slot_id, 200, "application/json", w.c_str());
    else
        server.send(slot_id, 500, "text/plain", "json buffer overflow");
}

// POST /api/echo - read top-level fields from a JSON body and reflect them.
void handle_echo(uint8_t slot_id, HttpReq *req)
{
    char name[32];
    long age = 0;
    bool admin = false;
    // Each reader scans the body in place; returns whether the key was present.
    bool have_name = json_get_str((const char *)req->body, "name", name, sizeof(name));
    json_get_int((const char *)req->body, "age", &age);
    json_get_bool((const char *)req->body, "admin", &admin);

    char buf[128];
    JsonWriter w(buf, sizeof(buf));
    w.begin_object();
    w.kv_str("name", have_name ? name : "");
    w.kv_int("age", age);
    w.kv_bool("admin", admin);
    w.end_object();
    server.send(slot_id, 200, "application/json", w.c_str());
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


    server.on("/api/info", HttpMethod::HTTP_GET, handle_info);
    server.on("/api/echo", HttpMethod::HTTP_POST, handle_echo);

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
