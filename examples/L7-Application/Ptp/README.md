# Ptp - keep the device's clock in step with a PTP master (IEEE 1588)

This example makes the ESP32 a **PTP slave**: it listens for a **Precision Time Protocol** master on
the LAN and measures how far its own clock is from the master's, plus the network path delay. PTP
(IEEE 1588) is how professional audio, industrial automation, telecom, and test gear keep many
devices on the _same_ clock - far tighter than plain NTP.

## How PTP measures the offset

The master and slave trade four timestamped messages each cycle:

1. Master sends **Sync**; the slave notes when it arrived (**t2**).
2. Master sends **Follow_Up** with the exact time it sent Sync (**t1**).
3. Slave sends **Delay_Req** and notes when it left (**t3**).
4. Master replies **Delay_Resp** with the time it arrived there (**t4**).

From those four numbers:

- **offset** (how wrong the slave clock is) = `((t2 - t1) - (t4 - t3)) / 2`
- **path delay** (one-way network latency) = `((t2 - t1) + (t4 - t3)) / 2`

`pc_ptp_compute()` does that arithmetic; the codec builds and parses every message.

## What you will need

- An **ESP32 board** on your Wi-Fi.
- A computer on the **same LAN** to run a PTP master. On Linux, `linuxptp` provides `ptp4l`.

## Part 1 - Run a master

On a Linux box on the same network (replace `eth0` with your interface):

```bash
sudo apt install linuxptp
sudo ptp4l -i eth0 -m -S --masterOnly=1
```

`-S` uses software timestamping (no special NIC needed); `--masterOnly=1` makes it the grandmaster so
your ESP32 always plays slave.

## Part 2 - Flash the slave

Put your Wi-Fi name/password in `SSID` / `PASSWORD` at the top of [Ptp.ino](Ptp.ino), upload, and open
the Serial Monitor at **115200**. You will see the master's Announce, then per-cycle offset and delay:

```
=== PC PTP slave (IEEE 1588 ordinary clock) ===
IP: 192.168.1.87
PTP slave on 319/320 (event bind=1 general bind=1) - run a master (ptp4l)
PTP master: clockClass=6 priority1=128 stepsRemoved=0 utcOffset=37
PTP seq=41  offset=1500388922100000 ns  path_delay=310000 ns
PTP seq=42  offset=1500388922230000 ns  path_delay=290000 ns
```

- **path_delay** is the real number to watch here: a few hundred microseconds on a quiet LAN. It is
  measured correctly regardless of what the two clocks read.
- **offset** is _local clock minus master clock_. It is huge at first because the ESP32's software
  clock counts from boot while the master reports real wall-clock time - the offset is exactly the
  amount you would subtract to set the ESP32 to the master's time. Disciplining the clock (stepping
  it by the offset, then slewing) is the natural next step.

## Accuracy - read this

Timestamps here are taken **in software**, inside the UDP receive callback, so jitter is
millisecond-class. That is enough to prove the exchange and to measure LAN path delay, but it is
**not** the sub-microsecond accuracy PTP is famous for. That requires **hardware timestamping** at the
MAC - which this chip's **Ethernet** MAC supports (see the Ethernet examples) but Wi-Fi does not. Over
Wi-Fi, treat this as "NTP-class accuracy using the PTP protocol."

## Where this fits

Pair the offset with the **Time Source** feature to serve corrected time, or feed it to the **NTP
server** example so the whole LAN can sync to a PTP grandmaster through this device. For a GPS-
disciplined grandmaster, combine with the **UbloxGnss** example.

## Troubleshooting

- **No `PTP master:` line.** The master is not reachable: check both devices are on the same subnet,
  multicast/IGMP is not blocked by the AP, and `ptp4l` is running with `-m` (so you can see it work).
- **`event bind=0`.** Another process holds UDP 319, or the Wi-Fi did not come up.
- **Offset jumps around by milliseconds.** Expected over Wi-Fi (software timestamps). Use Ethernet +
  HW timestamping for stability.

## Build and run (PlatformIO)

```bash
pio ci examples/L7-Application/Ptp \
  --board esp32dev --lib "." \
  --project-option="build_flags=-DPC_ENABLE_PTP=1"
```

(The Arduino IDE reads the flag from `build_opt.h` beside the sketch automatically.)

## How it works (for the curious)

Every PTP message starts with the same 34-octet header (message type, a version, length, domain, a
64-bit correction field, the sender's 8-octet clock identity + port, a sequence id, and a control
byte), all **big-endian**. Timestamps are 10 octets: 6 for seconds, 4 for nanoseconds.
`pc_ptp_parse_header` decodes the header; `pc_ptp_parse_timestamp_msg` / `_parse_delay_resp` /
`_parse_announce` decode the bodies; the `pc_ptp_build_*` functions do the reverse and stamp the
right message-type and control values for you. It is fixed-size with **no heap**, unit-tested on a PC
(`test/test_ptp`) against the IEEE 1588-2008 wire format. Only the UDP send/receive and the
timestamping run on the ESP32. See [`src/services/timing_position/ptp/ptp.h`](../../../src/services/timing_position/ptp/ptp.h).
