# ZigbeeGateway - a Zigbee (EZSP/ASH) NCP bridged to the gateway

**Layer:** Foundation · **Build flags:** `PC_ENABLE_ZIGBEE`, `PC_ENABLE_GATEWAY`

## What this example teaches

A **Zigbee** network plugged into the [gateway](../RadioGateway/README.md) through a
Silicon Labs **EmberZNet** network co-processor (NCP), which speaks **EZSP** over the
**ASH** data-link protocol on a UART. This resets the NCP and decodes its ASH frames; a
DATA frame carrying an EZSP callback (an incoming Zigbee message) is bridged northbound.

```
Zigbee NCP --UART--> pc_ash_frame_decode() --> EZSP payload -> pc_gateway_uplink()
                                                                 |
                                          envelope + topic  zigbee/0/<node>
                                                                 |
                                                  northbound publish (MQTT/HTTP/WS)
```

ASH wraps each frame as `[control | payload | CRC16]`, byte-stuffs the reserved control
bytes, and terminates with a Flag `0x7E`. `services/zigbee` does the framing:

```cpp
uint8_t control, payload[128]; uint16_t plen;
int n = pc_ash_frame_decode(buf, len, &control, payload, sizeof(payload), &plen);
if (n > 0 && (control & 0x80) == 0 /* HislipMsg::DATA frame */)
    pc_gateway_uplink(0, node, payload, plen, 0);
```

`pc_ash_frame_encode()` builds a frame the same way (this sketch sends an ASH `RST` at boot).
The CRC is CRC-16/CCITT; the codec is host-tested against the documented RST frame
(`C0 38 BC 7E`) and the byte-stuffing in `test/test_zigbee`.

Interpreting the EZSP command _inside_ a DATA frame - version negotiation, the ACK/sequence
numbers, the `incomingMessageHandler` fields - is application work; this sketch bridges the
raw EZSP payload to show the transport path.

## Wiring (ESP32 <-> Zigbee NCP)

| NCP | ESP32        |
| --- | ------------ |
| TX  | GPIO 16 (RX) |
| RX  | GPIO 17 (TX) |
| VCC | 3V3          |
| GND | GND          |

115200 8N1 (an EFR32 / EM357 NCP; RTS/CTS optional).

## Build-flag note

The flags must reach the library build, so pass them as build flags:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_ZIGBEE=1 -DPC_ENABLE_GATEWAY=1" \
  --lib="." examples/Drivers/ZigbeeGateway/ZigbeeGateway.ino
```
