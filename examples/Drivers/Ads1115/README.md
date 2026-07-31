# Ads1115 - measure voltages precisely with a 16-bit ADC

This example reads an analog voltage with an **ADS1115** and prints it once a second. An ADC
("analog-to-digital converter") turns a real-world voltage - from a battery, a knob, a light or
temperature sensor - into a number the ESP32 can use. The ADS1115 does it far more precisely
than the ESP32's own ADC. It is written for a beginner and is a nice first soldering / wiring
project.

## Why not just use the ESP32's ADC?

The ESP32 has a built-in ADC, but it is noisy, not very linear, and only about 12-bit. The
ADS1115 is **16-bit**, much more accurate, and has a **programmable gain** - it can zoom in to
measure tiny signals (down to a ±0.256 V range) or zoom out to ±6.144 V. It sits on the I2C
bus, so it does not use up the ESP32's own analog pins.

## What you will need

- An **ESP32 board** with a USB cable.
- An **ADS1115** breakout (Adafruit or generic).
- Something to measure: a **potentiometer** (a knob) is perfect for a first test, or a battery.
- Jumper wires.

## Part 1 - Wire it up

The board talks over **I2C** and has four analog inputs (AIN0..AIN3).

| ADS1115 pin | ESP32 pin   | Why                          |
| ----------- | ----------- | ---------------------------- |
| `VDD`       | `3V3`       | power                        |
| `GND`       | `GND`       | ground                       |
| `SDA`       | GPIO **21** | I2C data                     |
| `SCL`       | GPIO **22** | I2C clock                    |
| `ADDR`      | `GND`       | sets the address to **0x48** |

For a first test, wire a **potentiometer**: its two outer pins to `3V3` and `GND`, and its
middle (wiper) pin to **`AIN0`**. Now turning the knob sweeps the voltage on AIN0.

> Keep the measured voltage between 0 and about 4 V here (the ±4.096 V gain), and never above
> the board's `VDD`. To measure a higher voltage (like a 12 V battery) use a resistor divider.

## Part 2 - Flash and read

Open [Ads1115.ino](Ads1115.ino), upload, and open the Serial Monitor at **115200**:

```
ADS1115 ready - reading AIN0 at +/-4.096 V full scale
AIN0 = 1.650 V
AIN0 = 2.980 V
```

Turn the knob and watch the voltage track it, smoothly, to the millivolt.

## Where this fits

`pc_ads1115_read_uv(channel, gain, &microvolts)` gives you a channel's voltage in **microvolts** (an
integer, so no floating point). From here you can log a battery's charge, read a sensor that
outputs a voltage, or publish the reading over **MQTT** / a web page - the same "read a cheap
breakout, bridge it onto the network" pattern used by the RTC, the radar (62), the touch pad
(63), and the SHT3x (64). Change `ADS1115_GAIN_1` to a higher gain to zoom in on small signals,
or read `pc_ads1115_read_raw()` for the plain 16-bit count.

## Troubleshooting

- **Always reads 0 or full-scale.** Check `SDA`->21, `SCL`->22, power, and that the thing you are
  measuring shares **GND** with the ESP32. Confirm the input is on AIN0 (channel 0).
- **"read failed".** A bus error - usually a loose wire or the wrong address (`ADDR` to `GND` is
  `0x48`; to `VDD` it is `0x49`). An I2C scanner should find it.
- **The number is jumpy.** Long unshielded wires pick up noise. A potentiometer wired directly is
  the cleanest first test.

## Build and run (PlatformIO)

The feature lives in the library, so its flag must reach the whole build:

```bash
pio ci examples/Drivers/Ads1115 \
  --board esp32dev --lib "." \
  --project-option="build_flags=-DPC_ENABLE_ADS1115=1"
```

(The Arduino IDE reads the flag from `build_opt.h` beside the sketch automatically.)

## How it works (for the curious)

Reading the ADS1115 is two I2C transfers: write the 16-bit **config register** to start a
conversion, then read the 16-bit **conversion register**. `pc_ads1115_config_single(channel, gain,
rate)` builds that config word from the datasheet bit fields - the start bit, the channel
multiplexer, the gain, single-shot mode, the data rate, and the disabled comparator (so
`ch0, ±4.096 V, 128 SPS` becomes `0xC383`). The result is a signed 16-bit number spanning the
full-scale range, and `pc_ads1115_raw_to_uv()` scales it to microvolts (for ±4.096 V that is
125 µV per count). Both are unit-tested on a PC (see `test/test_ads1115`); only the two register
transfers run on the ESP32.
