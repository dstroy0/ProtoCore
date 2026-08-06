# ModbusTcp - a Modbus TCP slave/server

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_MODBUS`

## What this example teaches

Modbus TCP is the lingua franca of industrial automation. This serves a small
Modbus data model (coils, discrete inputs, holding and input registers) on TCP/502
so a SCADA system, PLC, or a tool like `mbpoll` can read and write it. It also
shows the second listener pattern: a non-HTTP protocol bound to its own port on the
same server.

**A second protocol listener on its own port:**

```cpp
pc_modbus_server_init();
pc_modbus_set_holding_reg(0, 0x1234);  // client-writable registers
pc_modbus_set_input_reg(0, 0);         // application-published (read-only to client)
pc_modbus_on_write(on_write);

server.listen(502, ConnProto::PROTO_MODBUS);   // bind a Modbus listener
server.begin();
```

**Who owns what.** The application writes input registers / discrete inputs (which
are read-only to the client) to publish sensor state, reads holding registers /
coils the client has written, and is notified of client writes through
`pc_modbus_on_write(fc, start, count)`. The loop publishes a live uptime value into
an input register the client can poll.

**Security note.** Modbus has no authentication or encryption: run it only on a
trusted control network, fronted by the per-IP accept throttle
([L4-Transport/PerIpThrottle](../../L4-Transport/PerIpThrottle)).

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_MODBUS=1" \
  --lib="." examples/L7-Application/ModbusTcp/ModbusTcp.ino
```

```sh
mbpoll -m tcp -t 4:hex -r 1 -c 2 <ip>   # read holding regs 0..1
mbpoll -m tcp -t 0 -r 1 1 <ip>          # write coil 0 = 1
```

## Annotated source

The complete sketch ([ModbusTcp.ino](ModbusTcp.ino)), reproduced verbatim
with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_MODBUS 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/fieldbus/modbus/modbus.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

// Notified whenever a client writes a coil or holding register.
static void on_write(uint8_t fc, uint16_t start, uint16_t count)
{
    Serial.printf("client write: fc=0x%02X start=%u count=%u\n", fc, start, count);
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

    pc_modbus_server_init();
    pc_modbus_set_holding_reg(0, 0x1234); // client-writable registers
    pc_modbus_set_input_reg(0, 0);        // application-published (read-only to client)
    pc_modbus_on_write(on_write);

    server.listen(502, ConnProto::PROTO_MODBUS); // Modbus listener on its own port
    server.begin();
    Serial.println("Modbus TCP slave on :502");
}

void loop()
{
    server.handle();

    // Publish a live value into an input register the client can poll.
    static uint32_t last = 0;
    if (millis() - last >= 1000)
    {
        last = millis();
        pc_modbus_set_input_reg(0, (uint16_t)(millis() / 1000)); // uptime seconds
    }
}
```
