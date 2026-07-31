# LoRaGateway - a real LoRa radio bridged to the gateway

**Layer:** Foundation · **Build flags:** `PC_ENABLE_LORA`, `PC_ENABLE_GATEWAY`

## What this example teaches

The radio-plugin half of [RadioGateway](../RadioGateway/README.md): instead of a
simulated feed, this drives an actual **Semtech SX127x / RFM95-96** over SPI and bridges
its frames to the [gateway](../RadioGateway/README.md).

```
RFM95 RX --SPI--> pc_lora_recv() --> pc_lora_frame_parse() --> pc_gateway_uplink()
                                                              |
                                           envelope + topic  lora/0/<from>
                                                              |
                                                     northbound publish (MQTT/HTTP/WS)
```

The `services/lora` module has two layers, and only the first is hardware-specific:

- **Driver** - the SX127x register protocol over a `pc_lora_bus` (two callbacks that read /
  write a chip register). Here they are a few SPI transfers; that is the _only_ code tied
  to your board.
- **Codec** - `pc_lora_frame_parse` / `pc_lora_frame_build` handle the RadioHead 4-byte header
  (`to` / `from` / `id` / `flags`) that sits on top of the header-less LoRa PHY.

```cpp
pc_lora_config cfg = {}; cfg.freq_hz = 915000000; cfg.spreading = 7; cfg.bandwidth = 7;
cfg.coding_rate = 1; cfg.sync_word = 0x12; cfg.tx_power = 17;
pc_lora_init(&bus, &cfg);           // verifies the chip id, applies the config
pc_lora_set_rx(&bus);               // listen

int n = pc_lora_recv(&bus, buf, sizeof(buf), &rssi);   // -> a frame, or -1
pc_lora_header h; const uint8_t *payload; uint16_t plen;
pc_lora_frame_parse(buf, n, &h, &payload, &plen);
pc_gateway_uplink(0, h.from, payload, plen, rssi);      // bridge northbound
```

A **downlink** (a northbound command) is the mirror: `pc_lora_frame_build()` then
`pc_lora_send()`, driven by the gateway port's transmit callback.

## Wiring (ESP32 <-> RFM95)

| RFM95 | ESP32   |
| ----- | ------- |
| SCK   | GPIO 18 |
| MISO  | GPIO 19 |
| MOSI  | GPIO 23 |
| NSS   | GPIO 5  |
| RST   | GPIO 14 |
| DIO0  | GPIO 26 |
| VCC   | 3V3     |
| GND   | GND     |

Change the `PIN_*` constants for your board. A production build triggers RX off the DIO0
interrupt and rides the DMA + FORWARD-lane path (RadioGateway); this sketch polls to
stay simple. It needs the module wired to actually receive - the codec and the register
protocol are host-tested in `test/test_lora`.

## Build-flag note

The flags must reach the library build, so pass them as build flags:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_LORA=1 -DPC_ENABLE_GATEWAY=1" \
  --lib="." examples/Drivers/LoRaGateway/LoRaGateway.ino
```
