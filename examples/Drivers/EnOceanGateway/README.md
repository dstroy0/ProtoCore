# EnOceanGateway - an EnOcean (ESP3) radio bridged to the gateway

**Layer:** Foundation · **Build flags:** `PC_ENABLE_ENOCEAN`, `PC_ENABLE_GATEWAY`

## What this example teaches

A **UART** radio plugged into the [gateway](../RadioGateway/README.md) - EnOcean's
energy-harvesting 868 MHz switches and sensors, reached through a serial gateway module
(TCM 310 / USB 300) that streams **ESP3** telegrams at 57600 baud. Unlike the SPI radios
([LoRa](../LoRaGateway/README.md), [nRF24](../Nrf24Gateway/README.md)) there is no
chip driver here - the module does the RF, so the "driver" is purely the **ESP3 framing
codec**.

```
TCM310 --UART--> pc_esp3_parse() --> RADIO_ERP1 sender + payload -> pc_gateway_uplink()
                                                                       |
                                                envelope + topic  enocean/0/<sender>
                                                                       |
                                                        northbound publish (MQTT/HTTP/WS)
```

An ESP3 telegram is `0x55 | data-len(2) | opt-len(1) | type(1) | CRC8H | data | opt | CRC8D`.
The codec accumulates the byte stream and frames one telegram at a time, verifying both
CRC-8s and resynchronizing on garbage:

```cpp
pc_esp3_packet pkt;
int n = pc_esp3_parse(buf, len, &pkt);   // >0 = a telegram, 0 = need more, -1 = drop a byte
if (n > 0 && pkt.type == pc_esp3_type::ESP3_RADIO_ERP1) {
    const uint8_t *sender = pkt.data + pkt.data_len - 5;  // 4-byte id + status
    pc_gateway_uplink(0, (sender[2] << 8) | sender[3], pkt.data, pkt.data_len - 5, 0);
}
```

`pc_esp3_build()` assembles a telegram the same way (for sending a common command / a
teach-in reply). The codec is pure - you feed it the UART bytes - and fully host-tested
(CRC-8 known answers, round trip, malformed framing, resync) in `test/test_enocean`.

## Wiring (ESP32 <-> EnOcean serial module)

| Module | ESP32        |
| ------ | ------------ |
| TX     | GPIO 16 (RX) |
| RX     | GPIO 17 (TX) |
| VCC    | 3V3          |
| GND    | GND          |

57600 8N1. Change `PIN_RX` / `PIN_TX` for your board.

## Build-flag note

The flags must reach the library build, so pass them as build flags:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_ENOCEAN=1 -DPC_ENABLE_GATEWAY=1" \
  --lib="." examples/Drivers/EnOceanGateway/EnOceanGateway.ino
```
