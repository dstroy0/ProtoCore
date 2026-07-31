# ZWaveGateway - a Z-Wave mesh bridged to the gateway

**Layer:** Foundation · **Build flags:** `PC_ENABLE_ZWAVE`, `PC_ENABLE_GATEWAY`

## What this example teaches

A **Z-Wave** mesh plugged into the [gateway](../RadioGateway/README.md) through a
Silicon Labs 500 / 700-series controller speaking its **Serial API** over UART. When a node
reports, we pull the source node id + payload and publish it northbound.

```
Z-Wave mesh --UART--> pc_zwave_parse_frame() --> node + payload -> pc_gateway_uplink()
                                                                     |
                                              envelope + topic  zwave/0/<node>
                                                                     |
                                                      northbound publish (MQTT/HTTP/WS)
```

A Serial API **data frame** is `SOF | LEN | Type | Command | Data | Checksum`, and each is
acknowledged by a single **ACK** byte (or NAK / CAN). `services/zwave` frames + verifies
them:

```cpp
uint8_t type, cmd, pdlen; const uint8_t *pd;
int n = pc_zwave_parse_frame(buf, len, &type, &cmd, &pd, &pdlen);  // >0 / need-more / -1
if (n > 0) {
    Serial2.write((uint8_t)ZWAVE_ACK);              // acknowledge the frame
    if (cmd == 0x04 /* ApplicationCommandHandler */)
        pc_gateway_uplink(0, pd[1] /* node */, &pd[3], pdlen - 3, 0);
}
```

`pc_zwave_build_frame()` assembles a command the same way (this sketch sends GetVersion at
boot). The checksum is `0xFF` XOR-folded over LEN..last-data. The codec is host-tested
against the documented GetVersion frame (`01 03 00 15 E9`) in `test/test_zwave`.

## Wiring (ESP32 <-> Z-Wave controller)

| Controller | ESP32        |
| ---------- | ------------ |
| TX         | GPIO 16 (RX) |
| RX         | GPIO 17 (TX) |
| VCC        | 3V3          |
| GND        | GND          |

115200 8N1 (a ZM5304 / EFR32 module or a UZB stick's UART).

## Build-flag note

The flags must reach the library build, so pass them as build flags:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_ZWAVE=1 -DPC_ENABLE_GATEWAY=1" \
  --lib="." examples/Drivers/ZWaveGateway/ZWaveGateway.ino
```
