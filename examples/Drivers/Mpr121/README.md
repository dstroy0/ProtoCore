# Mpr121 - twelve touch buttons from one little chip

This example uses an **MPR121** capacitive-touch chip to turn wires or copper pads into touch
buttons. Touch a pad and the Serial Monitor says which one; hold any pad and the onboard LED
lights. It is written for a beginner and is a fun first soldering / wiring project - you can
even use pieces of aluminum foil, or (famously) fruit, as the buttons.

## How does "capacitive touch" work?

Your body holds a tiny electrical charge. When your finger comes near a pad, it changes how much
charge that pad can hold (its _capacitance_). The MPR121 measures that change on twelve pads at
once and tells the ESP32, over **I2C**, exactly which pads are being touched - no moving parts,
no visible buttons. It is the same idea as the buttons on an elevator or a microwave.

## What you will need

- An **ESP32 board** with a USB cable.
- An **MPR121** breakout board (Adafruit, or any generic MPR121).
- Six jumper wires, plus a wire (or foil, or a coin) for at least one touch pad.

## Part 1 - Wire it up

The chip talks over **I2C** (two shared data wires) and has an address pin.

| MPR121 pin  | ESP32 pin   | Why                          |
| ----------- | ----------- | ---------------------------- |
| `VIN`/`3V3` | `3V3`       | power                        |
| `GND`       | `GND`       | ground                       |
| `SDA`       | GPIO **21** | I2C data                     |
| `SCL`       | GPIO **22** | I2C clock                    |
| `ADDR`      | `GND`       | sets the address to **0x5A** |

Then run a wire from any pad labelled **`0` .. `11`** (ELE0..ELE11) to something you will touch -
a bare wire end, a square of foil, a drawing pin, a banana.

## Part 2 - Flash and touch

Open [Mpr121.ino](Mpr121.ino), upload, and open the Serial Monitor at **115200**. You
should see:

```
MPR121 ready - touch a pad (ELE0..ELE11)
electrode 0 touched
electrode 0 released
electrode 3 touched
```

Touch and release a pad and the matching line prints; the onboard LED is on whenever any pad is
held. That is a working 12-button touch keypad.

## Where this fits

`pc_mpr121_read_touched()` gives you a 12-bit mask - one bit per electrode - and
`pc_mpr121_is_touched(mask, e)` tests one. From here you can build a real project: a touch keypad
that submits a code over **HTTP**, a set of light switches published over **MQTT**, or a musical
"fruit piano." This is the same "read a cheap breakout, bridge it onto the network" pattern the
library uses for the RTC, the radar (example 62), and GPS.

## Reading raw values (for tuning)

Besides the on/off mask, `pc_mpr121_read_filtered(e)` returns electrode `e`'s raw 10-bit
capacitance reading. Watching that number climb as your finger approaches is the way to pick
better **touch / release thresholds** (the defaults are 12 / 6). The library builds the whole
register bring-up - reset, the NXP filter defaults, thresholds, and the electrode-enable - and
you can see those exact bytes in `test/test_mpr121`.

## Troubleshooting

- **"MPR121 not found".** Check `SDA`->21, `SCL`->22, power, and the `ADDR` pin. `ADDR` to `GND`
  is address `0x5A`; to `3V3` it is `0x5B`. An I2C scanner sketch should find the chip.
- **Touches trigger by themselves, or never trigger.** Long pad wires pick up noise. Keep them
  short to start, and remember the pad senses a _nearby_ finger, not just a hard press.
- **The LED is inverted (on when idle).** Some boards wire the onboard LED active-low. Swap
  `HIGH` / `LOW` in the sketch or use another GPIO.

## Build and run (PlatformIO)

The feature lives in the library, so its flag must reach the whole build:

```bash
pio ci examples/Drivers/Mpr121 \
  --board esp32dev --lib "." \
  --project-option="build_flags=-DPC_ENABLE_MPR121=1"
```

(The Arduino IDE reads the flag from `build_opt.h` beside the sketch automatically.)

## How it works (for the curious)

The MPR121 reports a 16-bit status word: bits 0-11 are the twelve electrodes, bit 12 is a
combined "proximity" electrode, and bit 15 is an over-current fault flag.
`pc_mpr121_touched(lo, hi)` masks that down to the 12 electrode bits; `pc_mpr121_word10()` unpacks the
chip's 10-bit filtered / baseline readings. Bringing the chip up is a fixed list of register
writes (a soft reset, the NXP AN3944 filter and analog-front-end defaults, a touch and release
threshold per electrode, then the electrode-configuration register that starts it running);
`pc_mpr121_build_init()` produces that whole list as `(register, value)` byte pairs, which are
verified byte-for-byte on a PC. Only the actual register read/write runs on the ESP32.
