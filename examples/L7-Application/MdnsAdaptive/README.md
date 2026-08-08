# MdnsAdaptive - stay discoverable on a crowded channel without adding to the noise

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_MDNS`, `PC_ENABLE_PROMISC`, `PC_ENABLE_WIFI_SNIFFER`, `PC_ENABLE_MDNS_ADAPTIVE`

## What this example teaches

An mDNS record lapses from network caches at its TTL, so a device has to re-announce to stay
discoverable. But hammering announces on a busy 2.4 GHz channel just adds collisions. This ties three
shipped pieces together so the announce cadence tracks the air:

```
promiscuous capture (services/radio/promisc)     -> a live frame count = RF contention
beacon scheduler   (network_drivers/application/mdns_adaptive) -> backs the interval off when busy, recovers when quiet
mDNS TXT re-apply  (network_drivers/application/mdns_service)  -> re-announces with no goodbye (a refresh, not an evict)
```

## Why the contention signal is promiscuous mode, not a socket

This was tried once with a second socket bound to `224.0.0.251:5353` to count announcements, and it
**broke the responder**: bound after the ESP-IDF mdns responder the join fails; bound before it, the
responder goes announce-only - its service still appears in `avahi-browse` but every SRV/TXT/A query
times out, so the device silently stops resolving. lwIP hands a datagram to the first matching PCB,
and a raw PCB sharing the port with the responder's socket-layer one does not get a copy.

Promiscuous mode is a **radio-layer callback**, not a socket. It never touches the responder's
sockets, so the record keeps resolving while the count runs - which this example verifies on
hardware. The cost is that it owns the radio in promiscuous mode, pinned to the station's own
channel, so it cannot run at the same time as the wifi_sniffer live channel-hop binding.

## The adaptive range is bounded by the TTL

You cannot back off past the TTL - the record would lapse from caches before the next refresh, making
the device undiscoverable, which is the exact opposite of the point. So:

- the base cadence is **TTL/2** (two refreshes per TTL, comfortable headroom),
- the backoff ceiling is **capped at ~7/8 of the TTL** by `pc_mdns_adaptive_begin()`, whatever
  `max_interval_ms` you request.

The adaptive range is therefore fundamentally `[TTL/2, ~TTL)`. A short TTL is a narrow range; a longer
TTL buys more absolute room to back off. This example uses a 30 s TTL so the cadence is easy to watch.

## Verified on hardware

**HW-verified (2026-07-19)** on an **ESP32-S3**, with `avahi` on a LAN peer.

**Coexistence** - the thing that broke before. While promiscuous capture and adaptive announcing run:

```
$ avahi-resolve -n adaptive.local
adaptive.local  192.168.1.163

$ avahi-browse -rt _http._tcp
=  wlan0 IPv4 adaptive   Web Site   local
   hostname = [adaptive.local]
   address  = [192.168.1.163]
   port     = [80]
   txt      = ["role=sensor"]
```

A, SRV, and TXT all resolve - the responder is fully healthy with the capture running.

**Backoff from real contention** - a build with the threshold lowered below the ambient traffic on
the connected channel, so real frames cross it:

```
interval=15000ms contention=0  announces=0   <- base cadence (TTL/2)
interval=25000ms contention=4  announces=0   <- real ambient frames drove the backoff...
interval=25000ms contention=12 announces=0   <- ...and it capped at 25 s, below the 30 s TTL
interval=25000ms contention=10 announces=1   <- refresher fired on the adaptive schedule
```

The interval climbed from the real per-window frame count crossing the threshold, and **stopped at
the safe ceiling** rather than running past the TTL. Recovery (a fully silent window halves the
interval back toward the base) is host-tested - a live channel will not reliably go silent for a
whole window, so it is not shown here.

## Routes

| HttpRoute | What it does                                                 |
| --------- | ------------------------------------------------------------ |
| `/mdns`   | `{"interval_ms":N,"contention":N,"announces":N,"channel":N}` |

## Config

```cpp
MdnsAdaptiveCfg cfg;
cfg.key = "role";            // an existing TXT key, re-applied unchanged to re-announce
cfg.value = "sensor";
cfg.ttl_s = 30;              // base cadence = TTL/2 = 15 s
cfg.max_interval_ms = 25000; // requested ceiling (capped at ~7/8 of the TTL regardless)
cfg.hi_contention = 40;      // >= 40 frames/window counts as "busy"
cfg.window_ms = 1000;        // contention sampling window
pc_mdns_adaptive_begin(&cfg);
// then pc_mdns_adaptive_tick() every loop
```

## Build footprint

| Board    | Flash           |
| -------- | --------------- |
| ESP32    | 780,361 B (59%) |
| ESP32-S3 | 939,995 B (71%) |

## Build-flag note

The flags must reach the library build, so pass them as build flags:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_MDNS=1 -DPC_ENABLE_PROMISC=1 -DPC_ENABLE_WIFI_SNIFFER=1 -DPC_ENABLE_MDNS_ADAPTIVE=1" \
  --lib="." examples/L7-Application/MdnsAdaptive/MdnsAdaptive.ino
```
