# Pca9685 - drive 16 servos or LEDs from two wires

This example uses a **PCA9685** board to control a servo motor - sweeping it smoothly back and
forth - using only the ESP32's two I2C wires. The same board drives up to **16** servos or LEDs
at once, which is how people build robot arms, walking robots, and big LED displays. It is
written for a beginner and is a nice first soldering / wiring project.

## What does the PCA9685 do?

Making a motor or LED move smoothly needs **PWM** (pulse-width modulation) - switching power on
and off very fast, where the _fraction_ of on-time sets the position or brightness. The ESP32
can do a few PWM channels itself, but the PCA9685 adds **16 dedicated 12-bit PWM channels** with
their own precise timer, freeing the ESP32 and letting you chain boards for even more. A servo,
specifically, reads a pulse about every 20 ms (**50 Hz**); a pulse of ~1.5 ms means "center".

## What you will need

- An **ESP32 board** with a USB cable.
- A **PCA9685** 16-channel servo board (Adafruit or generic).
- A **hobby servo** (SG90 or similar).
- A **separate 5-6 V power supply** for the servo (batteries or a wall adapter) - see the
  warning below.
- Jumper wires.

## Part 1 - Wire it up

The board talks over **I2C** and has a separate power input for the motors.

| PCA9685 pin  | Connect to   | Why                                      |
| ------------ | ------------ | ---------------------------------------- |
| `VCC`        | `3V3`        | powers the chip's logic                  |
| `GND`        | `GND`        | shared ground                            |
| `SDA`        | GPIO **21**  | I2C data                                 |
| `SCL`        | GPIO **22**  | I2C clock                                |
| `V+` (screw) | 5-6 V supply | powers the **servos** (not from the ESP) |

Plug the servo onto the **channel 0** 3-pin header (match the colors: brown/black = GND, red =
V+, yellow/orange = signal).

> **Do not power a servo from the ESP32's 3V3 pin.** A moving servo can pull far more current
> than the board can supply, which browns out and reboots the ESP32. Use the `V+` screw
> terminal with its own supply, and connect the two grounds together.

## Part 2 - Flash and watch it sweep

Open [Pca9685.ino](Pca9685.ino), upload, and open the Serial Monitor at **115200**:

```
PCA9685 ready - sweeping the servo on channel 0
```

The servo should sweep from one end to the other and back, continuously. Move the servo to a
different channel header and change `SERVO_CH` in the sketch to match.

## Where this fits

`pc_pca9685_set_servo_us(channel, microseconds)` positions a servo by pulse width;
`pc_pca9685_set_pwm(channel, on, off)` sets a raw 12-bit duty cycle (great for dimming an LED). From
here you can drive a servo from a web page, a slider over **WebSocket**, or a schedule - the same
"bridge the network to the real world" idea, but as an **output**. Examples 62-64 read sensors;
this one moves things.

## Dimming LEDs instead

An LED does not care about servo pulses - it just wants a duty cycle. Wire an LED (through a
resistor) to a channel and call `pc_pca9685_set_pwm(channel, 0, level)` where `level` is 0
(off) to 4095 (full brightness). `PCA9685_FULL_ON` / `PCA9685_FULL_OFF` switch a channel fully on
or off with no PWM at all.

## Troubleshooting

- **"PCA9685 not found".** Check `SDA`->21, `SCL`->22, and `VCC`/`GND`. The default address is
  `0x40`; the six solder jumpers on the board change it (so you can chain many boards).
- **The ESP32 reboots when the servo moves.** Classic sign of powering the servo from the ESP32.
  Give the servos their own supply on `V+` and share ground.
- **The servo buzzes or does not reach the ends.** Servos vary; widen or narrow the sweep by
  changing the `500` / `2500` microsecond limits in the sketch (some like 1000-2000).

## Build and run (PlatformIO)

The feature lives in the library, so its flag must reach the whole build:

```bash
pio ci examples/Drivers/Pca9685 \
  --board esp32dev --lib "." \
  --project-option="build_flags=-DPC_ENABLE_PCA9685=1"
```

(The Arduino IDE reads the flag from `build_opt.h` beside the sketch automatically.)

## How it works (for the curious)

The PCA9685 makes PWM from a 25 MHz oscillator divided by a **PRESCALE** value;
`pc_pca9685_prescale(freq)` computes it as `round(25e6 / (4096 * freq)) - 1` (so 50 Hz -> 121).
Each channel has a 12-bit **ON** count and **OFF** count at register `0x06 + 4 * channel`; a
servo pulse of _N_ microseconds becomes an OFF count of `N * 4096 * freq / 1e6`
(`pc_pca9685_us_to_count`), and `pc_pca9685_set_pwm_bytes` packs the 5-byte I2C write. All of that math
and the register encoding are unit-tested on a PC (see `test/test_pca9685`), including the
full-on/off flag; only the I2C writes run on the ESP32.
