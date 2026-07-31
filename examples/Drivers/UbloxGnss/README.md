# UbloxGnss - read a GPS as text (NMEA) and binary (UBX) on one wire

This example reads a **u-blox GNSS module** (the popular **GT-U7**, or a NEO-6/7/8/M8) and prints
your position, satellite count, and the receiver's firmware version to the Serial Monitor. It shows
the two languages a u-blox receiver speaks on the same UART - human-readable **NMEA** sentences and
compact **UBX** binary frames - and how the library reads both and _talks back_ in UBX.

## Two languages on one wire

Every u-blox GPS streams two things at once over its serial pin:

- **NMEA 0183** - ASCII lines like `$GPGGA,123519,4807.038,N,...` that any GPS emits. Easy to read,
  a bit wasteful.
- **UBX** - u-blox's own **binary** format: `B5 62 <class> <id> <length> <payload> <checksum>`.
  Compact, and the only way to **configure** the receiver or get the all-in-one **NAV-PVT** fix
  (position + velocity + time in a single frame).

They are interleaved byte-by-byte on the same wire. This sketch's demultiplexer (`pc_ubx_stream_feed`)
untangles them: it hands complete, checksum-checked UBX frames to the UBX decoder and every other
byte to a normal NMEA line reader.

## "Accept and send"

The sketch does both directions:

- **Send** (at boot): it asks the receiver its version (`UBX-MON-VER` poll) and turns on the binary
  position fix (`UBX-CFG-MSG` enabling `UBX-NAV-PVT`).
- **Accept** (continuously): it prints the version string, the configuration **ACK**, the binary
  **NAV-PVT** fix, and any NMEA **GGA** line.

## What you will need

- An **ESP32 board** with a USB cable.
- A **u-blox GT-U7** (or NEO-6M/7M/8M) GPS module with its antenna.
- Four or five jumper wires, and a **clear view of the sky** (GPS does not work well indoors).

## Part 1 - Wire it up

The module talks over a **UART** (two data wires, crossed over):

| GPS pin | ESP32 pin   | Why                                   |
| ------- | ----------- | ------------------------------------- |
| `VCC`   | `3V3`       | power                                 |
| `GND`   | `GND`       | ground                                |
| `TX`    | GPIO **16** | GPS talks -> ESP32 listens (RX)       |
| `RX`    | GPIO **17** | ESP32 talks -> GPS listens (TX)       |
| `PPS`   | GPIO **4**  | 1 pulse/second (optional; timing use) |

The data wires **cross**: GPS `TX` -> ESP32 RX (GPIO 16), GPS `RX` -> ESP32 TX (GPIO 17). The `RX`
wire is only needed because this sketch **sends** UBX; a read-only sketch can leave it off. If your
board's second UART is elsewhere, change `GNSS_RX` / `GNSS_TX` at the top of
[UbloxGnss.ino](UbloxGnss.ino).

> **On a dev board with a silk-screened `TX`/`RX` header:** those pins are the chip's UART0, wired to
> the USB serial console - do **not** put the GPS there or it fights the console. Use plain numbered
> GPIOs like 16/17.

## Part 2 - Flash and watch

Upload, open the Serial Monitor at **115200**, and give the antenna a view of the sky. You will see:

```
=== PC u-blox GNSS (NMEA + UBX) ===
sent: UBX-MON-VER poll + UBX-NAV-PVT enable
UBX MON-VER sw="EXT CORE 1.00 (59842)"
UBX ACK-ACK for 06 01
NMEA GGA fix=0 sats=0 lat= lon=
UBX NAV-PVT fix=0 sats=4 lat=0.0000000 lon=0.0000000 alt=0.00m pps=0
...
UBX NAV-PVT fix=3 sats=9 lat=48.1173000 lon=11.5167000 alt=545.40m pps=12
NMEA GGA fix=1 sats=9 lat=4807.038N lon=01131.000E
```

- The **MON-VER** line proves your `send` worked - the receiver replied to a UBX poll.
- The **ACK-ACK** line proves the receiver accepted the `UBX-NAV-PVT` enable.
- `fix=0` means **no satellite fix yet** (cold start / indoors). Once `fix` becomes `2` or `3` the
  latitude/longitude fill in, and the **PPS** pin starts pulsing (the counter climbs).

Getting a first fix outdoors can take from 30 seconds to a few minutes.

## Where this fits

This is the "read a cheap breakout, bridge it onto the network" pattern the library uses everywhere.
From here: feed the fix into the **Time Source** feature (GPS is a stratum-1 clock), serve it to a
web page over **WebSocket**, or become an **NTP server** (see the NtpServer example) using GPS time.
For centimeter-accuracy RTK, the **NtripCaster** example streams RTCM3 corrections.

## Troubleshooting

- **Nothing prints / `parsed` stays 0.** Check the two data wires are **crossed** (GPS TX -> GPIO 16),
  the module has power, and the baud matches (`GNSS_BAUD`, default 9600 - some modules ship at
  38400).
- **NMEA appears but no `UBX MON-VER`.** The `send` (GPS RX -> ESP32 TX GPIO 17) is not connected, or
  the module's UBX protocol is disabled. NMEA-only still proves reception.
- **`fix=0` forever.** GPS needs sky. Move the antenna to a window or outdoors.

## Build and run (PlatformIO)

The codecs live in the library, so the flags must reach the whole build:

```bash
pio ci examples/Drivers/UbloxGnss \
  --board esp32dev --lib "." \
  --project-option="build_flags=-DPC_ENABLE_NMEA0183=1 -DPC_ENABLE_UBX=1"
```

(The Arduino IDE reads the flags from `build_opt.h` beside the sketch automatically.)

## How it works (for the curious)

A UBX frame is two sync bytes (`B5 62`), a class + id that name the message, a little-endian payload
length, the payload, and a two-byte **Fletcher checksum** over everything in between.
`pc_ubx_stream_feed` is a byte-at-a-time state machine: it hunts for the sync bytes, reads the
length, collects exactly that many payload bytes while running the checksum, and only emits a frame
if the checksum matches - otherwise it resyncs. Any byte that is not part of a UBX frame is passed
straight through, so an ordinary NMEA line assembler runs on the leftovers. Building is the reverse:
`pc_ubx_build` frames a payload and appends the checksum; `pc_ubx_build_poll` sends the empty
"please send me this message" request. All of it is fixed-size, **no heap**, and unit-tested on a PC
(`test/test_ubx`, checked against the published u-blox poll frames); only the UART read/write runs on
the ESP32. See [`src/services/timing_position/ubx/ubx.h`](../../../src/services/timing_position/ubx/ubx.h) and
[`src/services/timing_position/nmea0183/nmea0183.h`](../../../src/services/timing_position/nmea0183/nmea0183.h).
