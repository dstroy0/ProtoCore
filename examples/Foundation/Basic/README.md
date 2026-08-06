# Basic - the smallest complete server

**Layer:** Foundation (tutorial) · **Build flags:** none (core features only)

## What this example teaches

This is the smallest complete server in the library and the right place to start.
It brings up WiFi, registers one route, and then services HTTP requests forever.
Nothing here is optional or gated behind a feature flag: HTTP/1.1 parsing,
routing, and the automatic error responses are always compiled in.

Read this one until the shape is obvious, then move to
[Advanced](../Advanced/README.md), which adds more routes, wildcards, and
request parsing on top of exactly this skeleton.

**The shape of every sketch.** A ProtoCore program is three things: one global
server object, a `setup()` that connects WiFi and registers routes before calling
`begin()`, and a `loop()` that pumps the pipeline:

```cpp
PC server; // one global; all state lives in BSS, no heap

void setup() { /* connect WiFi, register routes */ server.begin(80); }
void loop()  { server.handle(); } // the heartbeat: every request flows through here
```

`handle()` is the heartbeat. Each call sweeps idle connections, drains the TCP
event queue, dispatches completed requests to your handlers, and emits the
automatic protocol error responses. No request is processed off this call, so
`loop()` must never block.

**The handler signature.** Every route handler takes the connection's `slot_id`
and the parsed request, and replies with `server.send()`:

```cpp
void handle_root(uint8_t slot_id, HttpReq *req)
{
    server.send(slot_id, 200, "text/plain", "Welcome to ProtoCore!");
}
```

`slot_id` identifies the connection you are answering (pass it back to `send()`);
`req` carries the method, path, version, headers, query parameters, and body.
Every buffer in `req` is fixed-size and bounds-checked at parse time, and
`req->body` is always null-terminated, so handlers never allocate.

**Registering a route.** `on()` binds a path and a method to a handler, and must
be called before `begin()`. The method is an `enum class`, so it carries its
type name:

```cpp
server.on("/", HttpMethod::HTTP_GET, handle_root);
```

**Checking `begin()`.** It returns `1` on success and a negative error code on
failure (listener pool full, or an lwIP error). Because `-1` is "truthy", test
`result < 0`, not `!result`:

```cpp
int32_t result = server.begin(80);
if (result < 0) { Serial.printf("begin() failed (error %d)\n", result); return; }
```

**Getting the device's address.** Use the library's own accessor,
`Physical.link->egress_ip()`, which returns the egress IP in network byte order. The
Arduino `WiFi` classes are not used anywhere in this library or its examples:
all networking goes through the library transport, so the same sketch works
regardless of which interface carries the traffic.

**What the framework does for you.** You write no code for malformed input. The
parser auto-sends `400 Bad Request` for an RFC 7230 character violation,
`413 Payload Too Large` when `Content-Length` exceeds `BODY_BUF_SIZE`,
`414 URI Too Long` when the path exceeds `MAX_PATH_LEN`, and `501 Not Implemented`
when a `Transfer-Encoding` header is present (chunked request bodies are not
accepted). Those limits are the compile-time capacity constants in
[protocore_config.h](../../../src/protocore_config.h).

## Where to go next

Every technique below builds on this skeleton:

| You want to...                       | See                                                       |
| ------------------------------------ | --------------------------------------------------------- |
| Add more routes and read the body    | [Advanced](../Advanced/README.md)                         |
| Match a path prefix with a wildcard  | [Advanced](../Advanced/README.md)                         |
| Read `?query=` parameters            | [Totp](../../L7-Application/Totp/README.md)               |
| Handle a `POST` body                 | [Csrf](../../L7-Application/Csrf/README.md)               |
| Install a custom 404 handler         | [Expert](../Expert/README.md)                             |
| Inspect the connection pool          | [Expert](../Expert/README.md)                             |
| See every tunable flag and constant  | [Configuration](../Configuration/README.md)               |
| Serve files from flash or an SD card | [FileServing](../../L7-Application/FileServing/README.md) |
| Allow cross-origin browser requests  | [CORS](../../L7-Application/CORS/README.md)               |

## Build and run

This example needs no feature flags. Set `SSID` / `PASSWORD` in the sketch first,
then compile for an ESP32 board:

```sh
pio ci --board=esp32dev \
  --project-option="framework=arduino" \
  --lib="." examples/Foundation/Basic/Basic.ino
```

Flash it from a PlatformIO project (`pio run -t upload`), open the serial monitor
at 115200 baud to read the assigned IP, then:

```sh
curl http://<ip>/
```

You should see `Welcome to ProtoCore!`.

If the upload itself fails, or the board never prints an IP, start with
[docs/HARDWARE_HOOKUP.md](../../../docs/HARDWARE_HOOKUP.md#start-here-get-a-board-blinking):
it covers the charge-only USB cable, download mode, and the other failures that
look like code problems and are not.

## Annotated source

The complete sketch ([Basic.ino](Basic.ino)), reproduced with added explanatory
comments:

```cpp
// The main library header. Including it pulls in the PC class, the HttpReq
// type, the HttpMethod / HttpVersion enums, and the accessor helpers
// (http_get_query / http_get_header). Any PC_ENABLE_* override must be
// #defined BEFORE this include to take effect in the sketch.
#include "protocore.h"
// Physical.wifi->init() / Physical.wifi->ready(): the physical-layer (L1) WiFi bring-up
// helpers, plus Physical.link->egress_ip() for the assigned address.
#include "network_drivers/physical/physical.h"

// Credentials. Replace these with your network before flashing.
static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// The server instance. One global object owns all connection state; every
// buffer it uses lives in BSS, sized at compile time. There is no heap use.
PC server;

// GET /
// The simplest possible handler: take the connection's slot_id, send a status,
// content-type, and body. server.send() owns the response framing.
void handle_root(uint8_t slot_id, HttpReq *req)
{
    server.send(slot_id, 200, "text/plain", "Welcome to ProtoCore!");
}

void setup()
{
    Serial.begin(115200);

    // Bring up WiFi (station mode) and poll until the link + IP are ready.
    // Physical.wifi->ready() is non-blocking; this loop is the one place a sketch spins.
    Physical.wifi->init(SSID, PASSWORD);
    while (!Physical.wifi->ready())
        delay(250);

    // The library's own egress-IP accessor, in network byte order. No Arduino
    // WiFi class is involved, so this works whichever interface is carrying
    // the traffic.
    uint32_t ip = Physical.link->egress_ip();
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Register routes BEFORE begin(). The route table is fixed-capacity
    // (MAX_ROUTES) and is immutable once the server is serving. HttpMethod is
    // an enum class, so the member carries its type name.
    server.on("/", HttpMethod::HTTP_GET, handle_root);

    // Start listening on port 80. Returns 1 on success, negative on failure.
    // -1 is truthy, so test "< 0" rather than "!result".
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
    // The single pipeline pump. Every iteration it:
    //   1. sweeps and force-closes idle connections (the timeout sweep),
    //   2. drains the TCP event queue (connect / data / disconnect),
    //   3. dispatches any completed request to its route handler,
    //   4. auto-sends 400 / 413 / 414 / 501 for parser error states.
    // Keep loop() non-blocking so this runs promptly.
    server.handle();
}
```
