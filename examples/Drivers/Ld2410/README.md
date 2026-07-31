# Ld2410 - sense people with a mmWave radar

This example turns an **HLK-LD2410** radar module into a people-detector: the onboard LED
lights whenever someone is nearby, and the Serial Monitor prints how far away they are. It is
written for a beginner and is a great first soldering / wiring project.

## What makes this radar special?

A PIR ("passive infrared") motion sensor only sees you when you _move_, and not through
anything. The LD2410 is a **24 GHz mmWave radar**: it bounces radio waves off you and measures
the echo, so it can tell:

- **Moving** targets (you walking past), and
- **Stationary** targets (you sitting perfectly still - it picks up the tiny motion of
  breathing).

It works in total darkness, is not a camera (nothing to feel spied on about), and sees through
thin plastic or drywall. Modules cost only a couple of dollars.

## What you will need

- An **ESP32 board** with a USB cable.
- An **HLK-LD2410** (or LD2410B / LD2410C) radar module.
- Four jumper wires.

## Part 1 - Wire it up (4 wires)

The module talks over a **UART** (a simple 2-wire serial link: one wire each way).

| LD2410 pin   | ESP32 pin    | Why                                     |
| ------------ | ------------ | --------------------------------------- |
| `VCC` / `5V` | `5V` / `VIN` | power (the module regulates it to 3.3V) |
| `GND`        | `GND`        | ground                                  |
| `TX` / `OUT` | GPIO **16**  | radar talks -> ESP32 listens (RX)       |
| `RX`         | GPIO **17**  | ESP32 talks -> radar listens (TX)       |

The two data wires **cross over**: the module's TX goes to the ESP32's RX, and vice-versa. If
your board's UART2 is on different pins, change `RADAR_RX` / `RADAR_TX` at the top of
[Ld2410.ino](Ld2410.ino).

## Part 2 - Flash and wave your hand

Open the sketch, upload, and open the Serial Monitor at **115200**. Wave your hand or walk in
front of the module and you will see the onboard LED turn on and lines like:

```
LD2410 radar ready - wave a hand in front of it
[radar] moving            distance= 84cm  moving= 84cm/72  static=  0cm/0
[radar] stationary        distance=112cm  moving=  0cm/0   static=112cm/38
[radar] clear             distance=  0cm  moving=  0cm/0   static=  0cm/0
```

- **distance** is how far away the detected person is, in centimeters.
- The number after each distance (`/72`, `/38`) is the **signal energy** (0-100) - how strong
  and confident the detection is.
- The line only prints when the state **changes**, so the monitor stays readable; the LED
  updates continuously.

Now hold completely still. A cheap PIR sensor would decide you left; the LD2410 keeps seeing you
as a **stationary** target. That is the whole point.

## Where this fits

`pc_ld2410_poll()` decodes each radar frame; `pc_ld2410_present()` and `pc_ld2410_distance_cm()` give you
a yes/no and a distance to act on. From here it is a short hop to a real project: publish
presence over **MQTT** (example with the MQTT feature), push it to a live web page over
**WebSocket**, or feed it into the **preempting queue** (Foundation examples) so a presence
event preempts other work. This is the same "read a cheap breakout, bridge it onto the network"
pattern the library uses for GPS, the RTC, and the field-bus sensors.

## Engineering mode (per-gate energies)

The sketch calls `pc_ld2410_set_engineering(true)`, which asks the module for extra detail: the
radar splits its range into nine **gates** (distance bins) and reports the energy in each. Those
land in `pc_ld2410_last()->moving_gate_energy[0..8]` and `static_gate_energy[0..8]` - useful if you
want to tune sensitivity per distance or draw a little bar chart. Pass `false` for the simpler
report.

## Troubleshooting

- **Nothing prints / always "clear".** Check the two data wires are **crossed** (module TX ->
  GPIO 16, module RX -> GPIO 17) and that the module has power. The LD2410's UART runs at
  **256000 baud** - that is set for you; you do not need to change the Serial Monitor baud
  (115200), which is only for the printed messages.
- **It detects "ghosts" with no one there.** Radar reflects off moving fans, curtains, and
  metal. Mount it facing the area you care about and away from moving objects.
- **The LED is inverted (on when clear).** Some boards wire the onboard LED active-low. Swap
  `HIGH` / `LOW` in the sketch, or use a different GPIO.

## Build and run (PlatformIO)

The feature lives in the library, so its flag must reach the whole build:

```bash
pio ci examples/Drivers/Ld2410 \
  --board esp32dev --lib "." \
  --project-option="build_flags=-DPC_ENABLE_LD2410=1"
```

(The Arduino IDE reads the flag from `build_opt.h` beside the sketch automatically.)

## How it works (for the curious)

The LD2410 sends a framed report ~10 times a second: a fixed **header** (`F4 F3 F2 F1`), a
length, the data, and a **footer** (`F8 F7 F6 F5`). Serial data can arrive split across reads or
with noise, so the library's `Ld2410Stream` reassembles frames one byte at a time - locking onto
the header, reading the length, collecting exactly that many bytes, and checking the footer -
then `pc_ld2410_parse_report()` pulls out the state, distances, energies, and (in engineering mode)
the per-gate values. It is a fixed-size buffer with **no heap** and it **resyncs** cleanly if a
byte is dropped. The frame decoder and reassembler are unit-tested on a PC (see
`test/test_ld2410`); only the UART read/write runs on the ESP32.
