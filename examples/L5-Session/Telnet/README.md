# Telnet - a line-oriented Telnet console

**Layer:** L5 Session · **Build flags:** `PC_ENABLE_TELNET`

## What this example teaches

This opens a Telnet console (RFC 854) on port 23 alongside the HTTP server. The
library negotiates echo + character mode, edits the input line for you
(backspace works), and hands each completed line to a command callback - so you
write a tiny command interpreter, not a byte pump.

> **Plaintext warning.** Telnet has no auth and no encryption. Use it only on a
> trusted LAN; prefer [SSH](../SSH) or the WebSocket terminal for anything
> exposed.

**Opening the port.** As with SSH, you add a non-HTTP listener and then
`begin()` (which activates all listeners, here HTTP on 80 too):

```cpp
server.listen(23, ConnProto::PROTO_TELNET); // open the Telnet port
pc_telnet_on_command(on_command);   // register the line handler
server.begin(80);                // also start HTTP; begin() activates every listener
```

**A line-at-a-time command handler.** The callback receives a fully-edited line
and the connection id; reply with `pc_telnet_print` / `pc_telnet_println` /
`pc_telnet_frame`. This example is a minimal shell with `help`, `heap`, `uptime`,
and an echo fallback:

```cpp
void on_command(const char *line, uint8_t conn_id) {
    if (strcmp(line, "heap") == 0)
        pc_telnet_frame(REPLY_HEAP, (uint32_t)ESP.getFreeHeap());
    else if (line[0])
        pc_telnet_frame(REPLY_ECHO, line);
}
```

**Build-flag note.** `PC_ENABLE_TELNET` must be set for the whole build; an
in-sketch `#define` does not reach the separately compiled library, so pass it as
a `build_flag` (see the [build_flags gotcha](../../../docs/EXAMPLES.md)).

`WiFi.setSleep(false)` is called so the radio stays responsive for an interactive
session.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_TELNET=1" \
  --lib="." examples/L5-Session/Telnet/Telnet.ino
```

Flash, read the IP on Serial, then:

```sh
telnet <ip> 23        # type "help"
```

## Annotated source

The complete sketch ([Telnet.ino](Telnet.ino)), reproduced verbatim with
added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_TELNET 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "network_drivers/presentation/telnet/telnet.h" // pc_telnet_on_command / pc_telnet_frame

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// Called once per completed line (the library handles echo + line editing).
void on_command(const char *line, uint8_t conn_id)
{
    (void)conn_id;
    if (strcmp(line, "help") == 0)
        pc_telnet_println("commands: help, heap, uptime, <echo>");
    else if (strcmp(line, "heap") == 0)
        pc_telnet_frame(REPLY_HEAP, (uint32_t)ESP.getFreeHeap());
    else if (strcmp(line, "uptime") == 0)
        pc_telnet_frame(REPLY_UPTIME, (uint32_t)millis());
    else if (line[0])                       // non-empty -> echo it
        pc_telnet_frame(REPLY_ECHO, line);
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

    server.listen(23, ConnProto::PROTO_TELNET);         // open the Telnet port
    pc_telnet_on_command(on_command);

    server.begin(80);                        // also start HTTP (begin() activates all listeners)
    Serial.println("Telnet on port 23 (try: telnet <ip>)");
}

void loop()
{
    server.handle();
}
```
