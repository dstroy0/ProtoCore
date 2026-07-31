# NtpServer - make your ESP32 the clock for your whole network

This turns a cheap ESP32 into an **NTP time server**: other computers, cameras, PLCs, and
gadgets on your network can ask it "what time is it?" and set their own clocks from it -
even if your network has **no internet at all**. It is written for a complete beginner.

Time matters more than people expect: log timestamps line up, TLS certificates validate,
scheduled jobs fire on time, and security logs can be trusted. On an isolated or offline
network there is normally nothing to sync to. This example fixes that.

---

## Where does the ESP32 get the time? (a fallback chain)

The ESP32 can't invent the time - it has to learn it from somewhere. This example tries two
sources, best first, and serves whichever is available:

1. **A GPS receiver (best).** GPS satellites carry atomic-clock time, for free, anywhere
   under open sky - no internet needed. This is the ideal source for an offline site. When a
   GPS module is connected and has a fix, the ESP32 serves **stratum 1** (NTP-speak for "I am
   a primary reference clock").
2. **The internet NTP pool (fallback).** If there is no GPS fix yet but the device _does_
   have internet, it falls back to `pool.ntp.org`.

`pc_time_now()` asks the sources in priority order and returns the first good answer, so
you get GPS when it is locked and the internet pool otherwise, seamlessly.

```
  GPS module ──(NMEA)──▶  ESP32  ◀──(fallback)── pool.ntp.org
                           │
                           ▼  serves NTP on UDP/123
        your PCs, cameras, PLCs, other ESP32s ...
```

---

## What you will need

- An **ESP32 board** + USB cable, the Arduino IDE or PlatformIO, and your WiFi details.
- **Optional but recommended: a GPS module** (any NMEA GPS - NEO-6M/7M/8M, etc., ~`$8`).
  Without one, the example still works using the internet fallback.

---

## Part 1 - (Optional) Wire up the GPS module

A GPS module speaks a simple text protocol called **NMEA 0183** over a serial wire. You only
need three wires:

| GPS module pin | connect to ESP32                         |
| -------------- | ---------------------------------------- |
| `VCC`          | `3V3` (or `5V`)                          |
| `GND`          | `GND`                                    |
| `TX`           | GPIO **16** (`GPS_RX_PIN` in the sketch) |

That is it - we only listen to the GPS, so its `RX` can be left unconnected. Give the module
a clear view of the sky; the first fix can take a minute or two (a blinking LED usually means
"fix acquired"). If you use different pins, edit `GPS_RX_PIN` at the top of the sketch.

No GPS? Skip this part - the internet fallback covers you.

---

## Part 2 - Flash it

Open [NtpServer.ino](NtpServer.ino), set your `SSID` and `PASSWORD`, upload, and open
the Serial Monitor at **115200**. You will see:

```
IP: 192.168.1.174
NTP server listening on UDP/123 (point your devices at this IP)
[ntp] epoch=1783197559 source=ntp
```

`source=ntp` means it is using the internet fallback; once your GPS gets a fix it flips to
`source=gps`. `epoch` is the current time in seconds since 1970 (a big number that ticks up).

---

## Part 3 - Point another device at it and check

Use the ESP32's IP from the Serial Monitor. From another computer on the same network:

- **Linux / macOS:** `sntp -d 192.168.1.174` (or `ntpdate -q 192.168.1.174`)
- **Windows:** `w32tm /stripchart /computer:192.168.1.174 /dataonly /samples:3`

You should get a time back that matches the wall clock. To actually _sync_ a Linux box to it,
add `server 192.168.1.174 iburst` to `/etc/chrony/chrony.conf` (or `/etc/ntp.conf`) and
restart the service; on Windows, `w32tm /config /manualpeerlist:192.168.1.174 /syncfromflags:manual /update`.

---

## Troubleshooting

- **`source=none-yet` forever.** No GPS fix _and_ no internet. Give the GPS a sky view, or
  connect the ESP32 to a network that can reach `pool.ntp.org`.
- **The other computer times out.** Many home routers **isolate wireless devices** from each
  other ("AP/client isolation"); a client then can't reach the ESP32. Turn that off, or put
  the querying device on a wired port. (UDP port 123 itself is rarely blocked, unlike email.)
- **GPS shows the wrong time by a whole number of hours.** GPS/NTP work in **UTC**; that is
  correct. Each client applies its own timezone. The server always serves UTC, as it should.

---

## Build and run (PlatformIO)

The feature lives in the library, so its flags must reach the whole build:

```bash
pio ci examples/L7-Application/NtpServer \
  --board esp32dev --lib "." \
  --project-option="build_flags=-DPC_ENABLE_NTP_SERVER=1 -DPC_ENABLE_TIME_SOURCE=1 -DPC_ENABLE_NMEA0183=1 -DPC_ENABLE_NTP=1"
```

(The Arduino IDE reads the flags from `build_opt.h` beside the sketch automatically.)

---

## How it works (for the curious)

`pc_ntp_server_begin(stratum, refid)` binds UDP/123 through the library's transport UDP service
and answers each 48-byte NTP request from `pc_time_now()`, echoing the client's transmit
timestamp so it can measure the round-trip delay. The time comes from **time sources** you
register with `pc_time_source_add(name, priority, fn)` - here a GPS parser (priority 1)
and the SNTP client (priority 2). The GPS parser reads standard `$GPRMC` sentences with the
zero-heap `nmea0183` codec, pulls out the UTC time and date, and converts them to a Unix
timestamp. Everything is fixed-buffer and heap-free, and the response builder is unit-tested
on a PC against the RFC 5905 wire format (see `test/test_ntp_server`).
