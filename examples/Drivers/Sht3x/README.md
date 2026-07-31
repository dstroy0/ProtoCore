# Sht3x - measure temperature and humidity

This example reads a **Sensirion SHT3x** sensor (SHT30 / SHT31 / SHT35) and prints the
temperature and humidity once a second. It is written for a beginner and is a nice first
soldering / wiring project - this is the sensor most weather stations and room monitors start
from, because it is small, accurate, and cheap.

## Why this sensor?

Lots of cheap sensors drift or lie. The SHT3x is factory-calibrated and, importantly, sends a
**checksum** (a CRC-8) with every reading. This example checks it, so a loose wire shows up
honestly as "checksum failed" instead of a plausible-looking wrong number - a great habit to
learn early.

## What you will need

- An **ESP32 board** with a USB cable.
- An **SHT3x** breakout (often sold as **GY-SHT31** or on a Qwiic/STEMMA board).
- Four jumper wires.

## Part 1 - Wire it up (4 wires)

The sensor talks over **I2C** (two shared data wires).

| SHT3x pin   | ESP32 pin   | Why       |
| ----------- | ----------- | --------- |
| `VIN`/`3V3` | `3V3`       | power     |
| `GND`       | `GND`       | ground    |
| `SDA`       | GPIO **21** | I2C data  |
| `SCL`       | GPIO **22** | I2C clock |

Most breakouts fix the address at **0x44**. If yours ties the `ADDR` pin to `3V3`, it is `0x45`
instead - change the `pc_sht3x_begin(0x44)` call.

## Part 2 - Flash and read

Open [Sht3x.ino](Sht3x.ino), upload, and open the Serial Monitor at **115200**:

```
SHT3x ready
temp=22.417 C   humidity=41.882 %
temp=22.430 C   humidity=41.905 %
```

Breathe on the sensor and watch the humidity jump; pinch it between your fingers and watch the
temperature climb. Those numbers are computed from the sensor's raw 16-bit readings on the ESP32
after the checksum passes.

## Where this fits

`pc_sht3x_read(&temp_mc, &rh_mpct)` gives you the temperature and humidity in **milli-units**
(thousandths - so `22417` means 22.417 C, `41882` means 41.882 %RH). Integers keep it heap-free
and avoid floating-point formatting. From here it is a short hop to a real project: serve the
reading on a web page, publish it over **MQTT**, chart it over **WebSocket**, or push it to
**StatsD** (example 59). This is the same "read a cheap breakout, bridge it onto the network"
pattern the library uses for the RTC, the radar (example 62), and the touch pad (example 63).

## Troubleshooting

- **"SHT3x not found".** Check `SDA`->21, `SCL`->22, power, and whether your board is at `0x44`
  or `0x45`. An I2C scanner sketch should find it.
- **"read failed (checksum or bus error)".** Usually a loose or too-long wire. That message is
  the CRC doing its job - the reading was rejected rather than trusted.
- **Humidity reads high / low near walls or breath.** That is real; the sensor responds to the
  air right at its surface. Give it a few seconds to settle after handling it.

## Build and run (PlatformIO)

The feature lives in the library, so its flag must reach the whole build:

```bash
pio ci examples/Drivers/Sht3x \
  --board esp32dev --lib "." \
  --project-option="build_flags=-DPC_ENABLE_SHT3X=1"
```

(The Arduino IDE reads the flag from `build_opt.h` beside the sketch automatically.)

## How it works (for the curious)

A single-shot measurement command (`0x2400`) tells the SHT3x to take a reading; ~15 ms later it
returns six bytes: a 16-bit temperature word and its CRC, then a 16-bit humidity word and its
CRC. `pc_sht3x_crc8()` recomputes each checksum (the Sensirion CRC-8: polynomial 0x31, starting at
0xFF - its documented check value `0xBEEF` -> `0x92` is one of the unit tests), and
`pc_sht3x_parse()` rejects the whole reading if either fails. The raw ticks convert linearly -
`T = -45 + 175 * raw / 65535`, `RH = 100 * raw / 65535` - done in fixed-point milli-units so
there is no floating point. All of that is unit-tested on a PC (see `test/test_sht3x`); only the
command write and the six-byte read run on the ESP32.
