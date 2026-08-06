# EthernetW5500 - run the server over a W5500 SPI Ethernet module

**Layer:** Foundation · **Build flags:** `PC_ENABLE_ETHERNET`, `PC_ETH_W5500`, `PC_ETH_W5500_*`

## What this example teaches

The RMII path ([Ethernet](../Ethernet)) needs a chip with an on-chip Ethernet MAC. The
**ESP32-S3 has no RMII MAC**, so a wired link there uses an **SPI Ethernet controller** - the
WIZnet **W5500** - over the HSPI bus. With `PC_ETH_W5500=1`, `Physical.eth->init()` calls the
arduino-esp32 3.x ETH SPI API (`ETH.begin(ETH_PHY_W5500, ...)`); once the link has a DHCP IP the
server accepts on it with no other change:

```cpp
Physical.eth->init();          // ETH.begin(ETH_PHY_W5500, ...) with the PC_ETH_W5500_* pins
while (!Physical.eth->ready()) delay(250);
// ... server.begin(80) - now serving over W5500 Ethernet
```

Nothing else changes: the egress reporting already classifies the wired route as
`pc_if_kind::PC_IF_ETH`, so `Physical.link->egress()`, per-route STA/AP/ETH interface filters, and every
protocol work over the link the moment it has an IP.

## Wiring (ESP32-S3-DevKitC, HSPI)

The pins come from the `PC_ETH_W5500_*` build flags in [build_opt.h](build_opt.h):

| Signal | GPIO | Flag                |
| ------ | ---- | ------------------- |
| `CS`   | `7`  | `PC_ETH_W5500_CS`   |
| `RST`  | `6`  | `PC_ETH_W5500_RST`  |
| `INT`  | `5`  | `PC_ETH_W5500_INT`  |
| `SCLK` | `12` | `PC_ETH_W5500_SCK`  |
| `MOSI` | `11` | `PC_ETH_W5500_MOSI` |
| `MISO` | `13` | `PC_ETH_W5500_MISO` |

Plus `VCC 3V3` and `GND`. Set the flags for your own wiring; see the
[hardware hookup guide](../../../docs/HARDWARE_HOOKUP.md) for the full pinout.

## SPI clock and throughput

`PC_ETH_W5500_SPI_MHZ` sets the SPI clock (default `20`). Throughput is **SPI-bound, not
PHY-bound**: measured on an ESP32-S3 it is ~7.2 Mbit/s at 20 MHz and ~8.2 Mbit/s at 24 MHz,
plateauing near the W5500's internal ~8.3 Mbit/s ceiling around 30 MHz. Higher clocks need clean,
short wiring - on breadboard jumpers, sustained transfers stay reliable to ~24 MHz and the SPI reads
corrupt above ~33 MHz. Keep the default unless the W5500 is on a short, clean PCB trace. A 200 MB
download completes byte-exact at the 20 MHz default. For near-100-Mbit speed use an RMII PHY
(LAN8720, [Ethernet](../Ethernet)) - the W5500 trades speed for needing no built-in MAC. See
[FEATURE_PERFORMANCE.md](../../../docs/FEATURE_PERFORMANCE.md) for the full clock-vs-throughput sweep.

## Core requirement

W5500 SPI Ethernet is **arduino-esp32 3.x only** - the 2.x ETH library has no W5500 support
(`ETH_PHY_W5500` is undefined there). Build this with the arduino-cli / IDF-5.x core.

## Build-flag note

The flags must reach the library build, so pass them as build flags:

```sh
pio ci --board=esp32-s3-devkitc-1 --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_ETHERNET=1 -DPC_ETH_W5500=1 -DPC_ETH_W5500_CS=7 -DPC_ETH_W5500_RST=6 -DPC_ETH_W5500_INT=5 -DPC_ETH_W5500_SCK=12 -DPC_ETH_W5500_MISO=13 -DPC_ETH_W5500_MOSI=11" \
  --lib="." examples/Peripherals/EthernetW5500/EthernetW5500.ino
```

(The Arduino IDE reads the flags from `build_opt.h` beside the sketch automatically.)
