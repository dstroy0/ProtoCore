# Rtc - give your ESP32 a battery-backed clock

An ESP32 forgets the date and time every time it loses power. This example adds a **real-time
clock (RTC)** - a tiny chip with its own coin-cell battery that keeps ticking for years - so
the board knows the correct time **the instant it boots, with no internet**. It is written for
a beginner, and it is a nice first soldering/wiring project.

## Why bother?

Without a clock, a freshly powered ESP32 thinks it is 1970 until it can reach a time server.
That breaks anything time-sensitive: log timestamps, TLS certificate checks, scheduled tasks.
On an offline network there may be no time server at all. A ~$2 RTC module (a **DS3231** is the
accurate, popular choice; a **DS1307** also works) solves it for good.

## What you will need

- An **ESP32 board** + your WiFi details.
- A **DS3231** (or DS1307) RTC module and a **CR2032** coin cell for it.
- Four jumper wires (the module talks over **I2C**, a simple 2-wire bus).

## Part 1 - Wire it up (4 wires)

| RTC module pin | ESP32 pin   |
| -------------- | ----------- |
| `VCC`          | `3V3`       |
| `GND`          | `GND`       |
| `SDA`          | GPIO **21** |
| `SCL`          | GPIO **22** |

(21/22 are the ESP32's default I2C pins. If your board uses different ones, change
`Wire.begin(SDA, SCL)` - the driver calls plain `Wire.begin()`.) Pop the coin cell into the
module so it keeps time when unplugged.

## Part 2 - Flash and watch it learn the time

Open [Rtc.ino](Rtc.ino), set your `SSID`/`PASSWORD`, upload, and open the Serial Monitor
at **115200**. On a **brand-new** module you will see:

```
RTC at boot: 0 (not set yet)
IP: 192.168.1.174
RTC set from NTP: 1783197559     <- it fetched the real time once and wrote it to the chip
[time] now=1783197559 source=rtc
```

The clever part: the first time the board reaches the internet it reads the correct time from
NTP and **writes it into the RTC chip**. From then on the RTC is the source of truth.

## Part 3 - Pull the plug (the whole point)

Unplug the ESP32, wait a bit, and plug it back in **with WiFi turned off or unavailable**. This
time it already knows the time - from the battery-backed chip:

```
RTC at boot: 1783197612 (battery-backed time!)
```

No network needed. That is what an RTC buys you.

## Where this fits

`pc_time_now()` asks your registered time sources best-first. Here the RTC is source #1, so
your whole app just calls `pc_time_now()` and gets good time offline. Chain it with GPS
(example 58) and upstream NTP for the full picture - **GPS (best) -> RTC (offline) -> NTP** -
and feed it to the NTP server (example 58) to serve time to your entire LAN.

## Troubleshooting

- **`RTC at boot: 0` every time, even after setting.** The coin cell is missing, dead, or in
  backwards - the chip cannot keep time without it. Also check the SDA/SCL wires are not
  swapped.
- **Nothing about the RTC prints / it never sets.** The board cannot see the chip on I2C.
  Double-check `VCC`/`GND` and that SDA->21, SCL->22. A quick I2C scanner sketch should find a
  device at address `0x68`.
- **Time is off by whole hours.** RTCs (like NTP) keep **UTC**; apply your timezone in your own
  code when you display it.

## Build and run (PlatformIO)

The feature lives in the library, so its flags must reach the whole build:

```bash
pio ci examples/Drivers/Rtc \
  --board esp32dev --lib "." \
  --project-option="build_flags=-DPC_ENABLE_RTC=1 -DPC_ENABLE_TIME_SOURCE=1 -DPC_ENABLE_NTP=1"
```

(The Arduino IDE reads the flags from `build_opt.h` beside the sketch automatically.)

## How it works (for the curious)

The DS1307/DS3231 store the time in seven **BCD** registers (each nibble is one decimal digit)
at I2C address `0x68`. `pc_rtc_read_epoch()` reads those seven bytes and `pc_rtc_regs_to_epoch()`
converts them - handling 12/24-hour encoding, leap years, and the chip's clock-halt/century
bits - into a Unix timestamp; `pc_rtc_set_epoch()` does the reverse to set the chip. All the date
math is fixed-point and heap-free, and the conversions are unit-tested on a PC across the
2000-2099 range (that round-trip test caught a real 32-bit overflow past 2038 - see
`test/test_rtc`). Only the register read/write touches I2C.
