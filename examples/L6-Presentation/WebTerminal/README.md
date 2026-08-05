# WebTerminal - a browser "web serial" terminal

**Layer:** L6 Presentation · **Build flags:** `PC_ENABLE_WEB_TERMINAL` (requires `PC_ENABLE_WEBSOCKET`)

## What this example teaches

This serves a self-contained green-phosphor terminal page (the docs-site CRT
theme) plus a WebSocket endpoint, giving you a remote console in the browser:
device output streams to every open page, and each line you type is delivered to
a command callback. It rides the library's existing WebSocket layer, so it is
zero-heap, and the page auto-selects `ws://` or `wss://` (works unchanged once
TLS is on).

**One call mounts the whole thing.** `pc_web_terminal_begin(server, path)`
serves the page and the WebSocket; `..._on_command` registers your handler:

```cpp
pc_web_terminal_begin(server, "/terminal");
pc_web_terminal_on_command(on_command);
```

**Browser to device.** The command callback receives a typed line; respond with
`pc_web_terminal_println` / `..._printf`, which broadcast to all open
terminals:

```cpp
void on_command(const char *line, uint8_t client_id) {
    char out[96];
    if (strcmp(line, "heap") == 0)
        pc_frame_build(out, sizeof(out), REPLY_HEAP, (uint32_t)ESP.getFreeHeap());
    else
        pc_frame_build(out, sizeof(out), REPLY_ECHO, line);
    pc_web_terminal_print(out);
}
```

**Device to browser.** From `loop()` you can push output any time; gate it on
`pc_web_terminal_client_count()` so you only format when someone is watching:

```cpp
if (pc_web_terminal_client_count() > 0) {
    char out[96];
    pc_frame_build(out, sizeof(out), HEARTBEAT, (uint32_t)millis(), (uint32_t)ESP.getFreeHeap());
    pc_web_terminal_print(out);
}
```

A plaintext alternative is [Telnet](../../L5-Session/Telnet); the raw
WebSocket primitive is [WebSocket](../WebSocket).

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_WEB_TERMINAL=1 -DPC_ENABLE_WEBSOCKET=1" \
  --lib="." examples/L6-Presentation/WebTerminal/WebTerminal.ino
```

Flash, then browse to `http://<ip>/terminal` and type `help`.

## Annotated source

The complete sketch ([WebTerminal.ino](WebTerminal.ino)), reproduced
verbatim with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_WEB_TERMINAL 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/web_terminal.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

// Browser -> device: handle a typed command line (broadcast replies to all pages).
void on_command(const char *line, uint8_t client_id)
{
    (void)client_id;
    if (strcmp(line, "help") == 0)
    {
        pc_web_terminal_println("commands: help, heap, uptime, <echo>");
        return;
    }

    char out[96];
    if (strcmp(line, "heap") == 0)
        pc_frame_build(out, sizeof(out), REPLY_HEAP, (uint32_t)ESP.getFreeHeap());
    else if (strcmp(line, "uptime") == 0)
        pc_frame_build(out, sizeof(out), REPLY_UPTIME, (uint32_t)millis());
    else
        pc_frame_build(out, sizeof(out), REPLY_ECHO, line);
    pc_web_terminal_print(out);
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
    Serial.println("Open http://<ip>/terminal in a browser");


    pc_web_terminal_begin("/terminal"); // serves the page + the WebSocket
    pc_web_terminal_on_command(on_command);

    int32_t result = begin_http(80);
    if (result < 0)
    {
        Serial.printf("begin() failed (error %d)\n", result);
        return;
    }
    Serial.println("Server started on port 80");
}

void loop()
{
    handle();

    // Device -> browsers: heartbeat every 3 s (only sent when someone's watching).
    static unsigned long last = 0;
    if (millis() - last >= 3000)
    {
        last = millis();
        if (pc_web_terminal_client_count() > 0)
        {
            char out[96];
            pc_frame_build(out, sizeof(out), HEARTBEAT, (uint32_t)millis(), (uint32_t)ESP.getFreeHeap());
            pc_web_terminal_print(out);
        }
    }
}
```
