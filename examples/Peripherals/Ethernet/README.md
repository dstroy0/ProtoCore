# Ethernet - run the server over a wired Ethernet PHY

**Layer:** Foundation · **Build flags:** `PC_ENABLE_ETHERNET`, `ETH_PHY_*`

## What this example teaches

Some deployments want a **wired** uplink - PoE cameras, panel-mount controllers, RF-noisy
factory floors, anything that can't rely on Wi-Fi. With `PC_ENABLE_ETHERNET` the physical
layer gains `Physical.eth->init()` alongside `Physical.wifi->init()`:

```cpp
Physical.eth->init();          // ETH.begin() with the ETH_PHY_* build-flag config
while (!Physical.eth->ready()) delay(250);
// ... server.begin(80) - now serving over Ethernet
```

It is a thin wrapper over the Arduino **ETH** library for an RMII PHY (LAN8720 / TLK110 /
RTL8201 / DP83848). Nothing else changes: the egress reporting already classifies a wired
route as `PC_IF_ETH`, so `Physical.link->egress()`, per-route STA/AP/ETH interface filters, and
every protocol work over the wired link the moment it has an IP. Wi-Fi and Ethernet can also
run together (dual-homed) - the stack picks the default route.

## PHY configuration

The PHY pins, address, type, and clock source come from the standard `ETH_PHY_*` build
flags (they vary by board). This example is tuned for a generic LAN8720:

| Flag            | Value                |
| --------------- | -------------------- |
| `ETH_PHY_TYPE`  | `ETH_PHY_LAN8720`    |
| `ETH_PHY_ADDR`  | `1`                  |
| `ETH_PHY_POWER` | `-1` (none)          |
| `ETH_PHY_MDC`   | `23`                 |
| `ETH_PHY_MDIO`  | `18`                 |
| `ETH_CLK_MODE`  | `ETH_CLOCK_GPIO0_IN` |

Common boards differ - e.g. **WT32-ETH01** uses `ETH_PHY_POWER=16`, **Olimex ESP32-POE**
uses `ETH_PHY_ADDR=0 ETH_PHY_POWER=12 ETH_CLK_MODE=ETH_CLOCK_GPIO17_OUT`. Set the flags for
your hardware.

## ESP32-P4 (verified)

On the **ESP32-P4** the RMII PHY drives the P4's built-in EMAC (no external SPI chip). A board
whose arduino-esp32 variant already defines the `ETH_PHY_*` pins needs **no** `ETH_PHY_*` flags -
`Physical.eth->init()`'s no-arg `ETH.begin()` picks them up. **HW-verified (2026-07-19)** on a
**Waveshare ESP32-P4-POE-ETH** (onboard IP101 PHY: `MDC=31 MDIO=52 POWER=51`, `EMAC_CLK_EXT_IN`,
phy addr 0) built under arduino-esp32 3.x - link came up 100M full-duplex + DHCP and the server
answered HTTP over pure wired Ethernet:

```sh
# arduino-esp32 3.x; the waveshare_p4_poe_eth variant supplies the IP101 pins, so only the library
# flag is needed:
arduino-cli compile --fqbn esp32:esp32:waveshare_p4_poe_eth \
  --build-property "compiler.cpp.extra_flags=-DPC_ENABLE_ETHERNET=1" \
  examples/Peripherals/Ethernet/Ethernet.ino
```

For a P4 board without a ready-made variant, pass the pins explicitly (from the board's schematic),
e.g. `-DPC_ENABLE_ETHERNET=1 -DETH_PHY_TYPE=ETH_PHY_IP101 -DETH_PHY_ADDR=0 -DETH_PHY_MDC=31 -DETH_PHY_MDIO=52 -DETH_PHY_POWER=51 -DETH_CLK_MODE=EMAC_CLK_EXT_IN`.

## Build-flag note

The flags must reach the library build, so pass them as build flags:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_ETHERNET=1 -DETH_PHY_TYPE=ETH_PHY_LAN8720 -DETH_PHY_ADDR=1 -DETH_PHY_POWER=-1 -DETH_PHY_MDC=23 -DETH_PHY_MDIO=18 -DETH_CLK_MODE=ETH_CLOCK_GPIO0_IN" \
  --lib="." examples/Peripherals/Ethernet/Ethernet.ino
```
