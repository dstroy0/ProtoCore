# WifiSniffer - channel-hopping 802.11 traffic analyzer + roaming decision

**Layer:** Peripherals · **Build flags:** `PC_ENABLE_WIFI_SNIFFER`, `PC_ENABLE_PROMISC`

## What this example teaches

A passive **RF-diagnostics panel**. It sweeps the 2.4 GHz channels, decodes every 802.11 MAC
header the radio hears, tallies frames by type, and keeps a **per-channel survey** of the
strongest AP on each channel. That survey is exactly what a **channel-agility roam** needs: "is
another channel enough stronger than mine to be worth moving?"

```
Wi-Fi radio --pc_promisc_begin--> sink --pc_wifi_parse--> stats tally
                                                         \-> per-channel survey -> roam decision
                  ^                                                                     |
                  +---------------- pc_wifi_sniffer_tick() hops on the dwell ----------+
```

Note the split: the radio itself is owned by [`services/promisc`](../WifiCapture/README.md) (one
owner for promiscuous capture), and `services/wifi_sniffer` adds the decode, the tally, the
channel-hop schedule, and the survey on top. This example does **not** install a second radio
callback.

## The pieces

| Call                                              | Does                                                     |
| ------------------------------------------------- | -------------------------------------------------------- |
| `pc_wifi_sniffer_begin(first, last, dwell_ms)`    | start capture + reset the tally/survey                   |
| `pc_wifi_sniffer_tick()`                          | hop to the next channel once the dwell elapses           |
| `pc_wifi_sniffer_stats()`                         | frames by type (mgmt / ctrl / data / other / total)      |
| `pc_wifi_survey_get(sv, ch)`                      | one channel's frame count + strongest RSSI and its BSSID |
| `pc_wifi_survey_best(sv, exclude_ch, &ch, &rssi)` | the strongest **other** channel - the roam candidate     |
| `pc_wifi_should_roam(cur, cand, hysteresis_db)`   | the RSSI-hysteresis decision                             |

The schedule and survey are pure and wrap-safe against a `pc_millis()` rollover; only the thin
radio binding is device-side. All of it is host-tested in `test/test_wifi_sniffer`.

## Verified on hardware

**HW-verified (2026-07-19)** on an **ESP32-S3** (N16R8) against live 2.4 GHz traffic. In 28 s it
decoded **490 frames** (369 management, 121 data), completed **10 channel sweeps**, and surveyed
nine channels with real APs:

```
-- ch 10, sweep 10 -- frames 490 (mgmt 369, ctrl 0, data 121, other 0)
  ch  1:     57 frames, best -64 dBm from DC:4B:A1:E6:5A:0F
  ch  5:     41 frames, best -61 dBm from 7C:10:C9:61:6A:B0
  ch  6:    204 frames, best -31 dBm from 80:D0:4A:85:4F:1C
  ch  9:     56 frames, best -64 dBm from 42:ED:00:27:5D:A8
  ch 11:     35 frames, best -80 dBm from 08:27:A8:31:EF:99
  roam? ch 10 (-66 dBm) -> ch 6 (-31 dBm): YES
```

The roam decision is the point: the strongest AP (-31 dBm on channel 6) clears the current
channel (-66 dBm) by 35 dB, far past the 8 dB hysteresis, so the answer is YES.

`ctrl 0` is normal - short control frames (ACK/RTS/CTS) are largely filtered out of the
promiscuous path, so a survey is dominated by beacons and data.

## Reading a report

The stats and survey are updated from the Wi-Fi driver's callback while your loop reads them.
They are deliberately **lock-free**: a report is a live snapshot that can be a frame or two newer
than a value you read a moment earlier (you may even see the roam line name a channel that had no
frames when the table above it printed). Counters are whole-word, so you never see a torn value -
but do not treat one report as an instantaneous consistent cut across all channels. Locking in the
radio callback would risk stalling capture, which is the wrong trade for a diagnostics panel.

## Build footprint

| Board    | Flash           | RAM            |
| -------- | --------------- | -------------- |
| ESP32-S3 | 889,463 B (67%) | 55,784 B (17%) |
| ESP32    | 864,064 B (65%) | 54,908 B (16%) |

## Legal note

Capture is strictly passive - no injection, no association. Monitoring a network you do not
administer may be unlawful where you live. Point it at your own.

## Build-flag note

The flags must reach the library build, so pass them as build flags:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_WIFI_SNIFFER=1 -DPC_ENABLE_PROMISC=1" \
  --lib="." examples/Peripherals/WifiSniffer/WifiSniffer.ino
```
