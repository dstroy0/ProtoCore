# CanCapture - listen-only CAN capture, forwarded to Ethernet

**Layer:** Foundation · **Build flags:** `PC_ENABLE_BUS_CAPTURE`, `PC_ENABLE_FORWARD`,
`PC_ENABLE_ETHERNET`, `ETH_PHY_*`

## What this example teaches

The wired sibling of [WifiCapture](../WifiCapture/). Put the ESP32's CAN (TWAI) controller
in **listen-only** mode - it receives and decodes every frame on the bus but never sends an ACK
or a frame, so it stays completely invisible to the other nodes - and forward each frame out
Ethernet to a collector:

```
CAN bus ─bus_capture_poll()→ sink ─pc_forward_ingress()→ ETH send cb ─UDP→ collector
```

- **`services/bus_capture`** installs TWAI in `TWAI_MODE_LISTEN_ONLY` (`bus_capture_begin(tx, rx,
bitrate, sink)`) and hands each decoded `CanFrame` to your sink; `bus_capture_poll()` drains the
  RX queue from `loop()`.
- **`can_to_socketcan()`** (pure) formats a frame as a 16-byte Linux **SocketCAN** frame -
  big-endian `can_id` with the EFF (extended) / RTR flags, length, and data.
- **`services/forward`** bridges `CAN → ETH`; the Ethernet egress wraps each SocketCAN frame in a
  libpcap record (`PC_DLT_CAN_SOCKETCAN`) and `Udp.client->sendto()`s it to the collector.

On the collector, prepend one `pc_pcap_global_header(..., PC_DLT_CAN_SOCKETCAN)` and **Wireshark**
decodes the stream as CAN.

The framing (`can_to_socketcan`, the PCAP link type) is pure and host-tested
(`pio test -e native_bus_capture`); the TWAI bring-up is ESP32-only and needs a CAN transceiver
(e.g. SN65HVD230) on the `CAN_TX_PIN` / `CAN_RX_PIN` GPIOs.

## Build-flag note

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_BUS_CAPTURE=1 -DPC_ENABLE_FORWARD=1 -DPC_ENABLE_ETHERNET=1 -DETH_PHY_TYPE=ETH_PHY_LAN8720 -DETH_PHY_ADDR=1 -DETH_PHY_POWER=-1 -DETH_PHY_MDC=23 -DETH_PHY_MDIO=18 -DETH_CLK_MODE=ETH_CLOCK_GPIO0_IN" \
  --lib="." examples/Peripherals/CanCapture/CanCapture.ino
```

In the Arduino IDE the `build_opt.h` beside this sketch sets the flags for you.
