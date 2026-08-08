# ModbusScan - the Modbus master codec + register scanner

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_MODBUS`, `PC_ENABLE_MODBUS_MASTER`

## What this example teaches

[ModbusTcp](../ModbusTcp) was the slave; this is the **master/client** side:
building read-request ADUs and parsing the responses back into register values. To
keep the example self-contained it self-scans - it feeds the request through the
on-board slave (`pc_modbus_process_adu`) so the build/parse codec is demonstrated
end-to-end with no external device. Against a real slave you would send the ADU over
a TCP client instead. `GET /scan` returns the discovered holding registers as JSON.

**Build a request, process it, parse the response.**

```cpp
uint8_t req[16], resp[MODBUS_ADU_MAX];
size_t rn = pc_modbus_build_read(ModbusFunction::MODBUS_FC_READ_HOLDING_REGS, 1, 1, 0, 3, req, sizeof(req)); // unit 1, regs 0..2
size_t pn = pc_modbus_process_adu(req, rn, resp, sizeof(resp));   // self-scan via the local slave
uint16_t regs[3];
uint8_t ex = 0;
int n = pc_modbus_parse_response(resp, pn, regs, 3, &ex);         // n>0 -> regs filled; else ex = exception
```

`pc_modbus_build_read(fc, tid, unit, start, count, ...)` encodes a read request;
`pc_modbus_parse_response()` returns the count on success or sets the Modbus exception
code on a failure response. Swapping `pc_modbus_process_adu` for a TCP send/receive is
the only change needed to scan a real device.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_MODBUS=1 -DPC_ENABLE_MODBUS_MASTER=1" \
  --lib="." examples/L7-Application/ModbusScan/ModbusScan.ino
```

```sh
curl http://<ip>/scan   # {"regs":[1234,5678,4095]}
```

## Annotated source

The complete sketch ([ModbusScan.ino](ModbusScan.ino)), reproduced verbatim
with added explanatory comments:

```cpp
// Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
// SPDX-License-Identifier: AGPL-3.0-or-later

#define PC_ENABLE_MODBUS 1
#define PC_ENABLE_MODBUS_MASTER 1

#include "protocore.h"
#include "network_drivers/physical/physical.h"
#include "services/fieldbus/modbus/modbus.h"
#include "services/fieldbus/modbus/modbus_master.h"

static const char *SSID = "YOUR_SSID";
static const char *PASSWORD = "YOUR_PASSWORD";

PC server;

void setup()
{
    Serial.begin(115200);
    Physical.wifi->init(SSID, PASSWORD);
    while (!Physical.wifi->ready())
        delay(250);
    Serial.print("IP: ");
    uint32_t ip = Physical.link->egress_ip(); // library egress IP (network byte order), no Arduino WiFi
    Serial.printf("IP: %u.%u.%u.%u\n", (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                  (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF));

    // Seed a few holding registers (the "slave" data model).
    pc_modbus_server_init();
    pc_modbus_set_holding_reg(0, 1234);
    pc_modbus_set_holding_reg(1, 5678);
    pc_modbus_set_holding_reg(2, 4095);
    server.listen(502, ConnProto::PROTO_MODBUS); // real Modbus TCP slave on :502

    // /scan: read holding registers 0..3 via the master codec (self-scan).
    server.on("/scan", HttpMethod::HTTP_GET, [](uint8_t id, HttpReq *) {
        uint8_t req[16], resp[MODBUS_ADU_MAX];
        size_t rn = pc_modbus_build_read(ModbusFunction::MODBUS_FC_READ_HOLDING_REGS, 1, 1, 0, 3, req, sizeof(req));
        size_t pn = pc_modbus_process_adu(req, rn, resp, sizeof(resp));
        uint16_t regs[3];
        uint8_t ex = 0;
        int n = pc_modbus_parse_response(resp, pn, regs, 3, &ex);
        char b[96];
        if (n > 0)
            snprintf(b, sizeof(b), "{\"regs\":[%u,%u,%u]}", regs[0], regs[1], regs[2]);
        else
            snprintf(b, sizeof(b), "{\"exception\":%u}", ex);
        server.send(id, 200, "application/json", b);
    });
    server.begin(80);
}

void loop()
{
    server.handle();
}
```
