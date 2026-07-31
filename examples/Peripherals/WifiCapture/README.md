# WifiCapture - capture 802.11 on Wi-Fi, forward to Ethernet

**Layer:** Foundation · **Build flags:** `PC_ENABLE_PROMISC`, `PC_ENABLE_FORWARD`,
`PC_ENABLE_ETHERNET`, `ETH_PHY_*`

## What this example teaches

A **wireless tap**: put the radio in promiscuous (monitor) mode and forward every captured
frame out the wired interface to a collector - "capture on Wi-Fi, forward to Ethernet". It
wires three existing pieces together:

```
Wi-Fi radio ─pc_promisc_begin()→ sink ─pc_forward_ingress()→ ETH send cb ─UDP→ collector
```

- **`services/promisc`** puts the radio in promiscuous mode (`pc_promisc_begin(channel, sink)`) and
  delivers each raw 802.11 frame (plus RSSI + channel) to your sink. Capture is strictly passive
  (no injection).
- **`services/forward`** (the forwarding plane) bridges interfaces: register a Wi-Fi source and
  an Ethernet destination, add a `WIFI → ETH` ALLOW rule (with a rate cap so a busy channel can't
  swamp the uplink), and every ingress frame is handed to the Ethernet interface's send callback.
- The **Ethernet egress** here wraps each frame in a **libpcap record** (`pc_pcap_record_header()`
  from `shared_primitives/pcap.h`, link type `PC_DLT_IEEE802_11`) and UDPs it to the collector
  with `pc_udp_sendto()`, which routes over the wired default route.

On the collector, receive the UDP datagrams, prepend one
`pc_pcap_global_header(..., PC_DLT_IEEE802_11)`, and you have a `.pcap` **Wireshark** opens
directly as 802.11.

## Filtering

Forward only what you want with the plane's ingress **ACL** (`pc_forward_acl_add()`) - e.g.
match on the frame-control byte to forward management frames only, or deny a specific BSSID. The
pure `wifi_frame_parse()` helper decodes the 802.11 header (type/subtype, the to/from-DS
src/dst/bssid layout, sequence number) if you want to filter or annotate in your sink. Both the
parser and the PCAP framing are host-tested (`pio test -e native_promisc`); the radio is
ESP32-only.

## Throughput note

This example calls `pc_forward_ingress()` directly in the capture callback for clarity. On a
busy channel, copy the frame and post it to the **FORWARD lane** of the preempting queue
(`PC_ENABLE_PREEMPT_QUEUE`) instead, so the radio callback stays light and the plane drains on
a dedicated task - the same pattern the DMA ingest path uses.

## Build-flag note

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_PROMISC=1 -DPC_ENABLE_FORWARD=1 -DPC_ENABLE_ETHERNET=1 -DETH_PHY_TYPE=ETH_PHY_LAN8720 -DETH_PHY_ADDR=1 -DETH_PHY_POWER=-1 -DETH_PHY_MDC=23 -DETH_PHY_MDIO=18 -DETH_CLK_MODE=ETH_CLOCK_GPIO0_IN" \
  --lib="." examples/Peripherals/WifiCapture/WifiCapture.ino
```

In the Arduino IDE the `build_opt.h` beside this sketch sets the flags for you. Needs an ESP32
with an Ethernet PHY to run.
