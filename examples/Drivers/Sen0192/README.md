# 77. SEN0192 microwave motion sensor

Detect motion with a **DFRobot SEN0192** — a 10.525 GHz microwave Doppler sensor (`PC_ENABLE_SEN0192`).
It's a 3-pin part (V / G / digital OUT) whose OUT line asserts while it senses movement within its
adjustable range. Unlike a PIR it works **through thin non-metal enclosures** and is unaffected by ambient
light or temperature.

The sensor has no protocol — just one digital line — so `services/sen0192` tracks it as a **debounced
presence signal**: presence asserts on motion and is held for `PC_SEN0192_HOLD_MS` after the last
motion sample, so brief gaps between Doppler returns don't make presence flap. The presence state machine
is pure and host-tested (`native_sen0192`); only the GPIO read touches hardware.

This sketch lights the onboard LED while motion is present and prints each detection over Serial.

## Wiring

| SEN0192 | ESP32                     |
| ------- | ------------------------- |
| OUT     | GPIO `PC_SEN0192_PIN` (4) |
| VCC     | 5V                        |
| GND     | GND                       |

The input pin, hold time, and OUT polarity are **ServerConfig** knobs, so a driver's pin assignment lives
in one place — override them with build flags, no code change:

- `PC_SEN0192_PIN` (default `4`) — the GPIO the OUT line is on
- `PC_SEN0192_HOLD_MS` (default `2000`) — how long presence is held after the last motion
- `PC_SEN0192_ACTIVE_HIGH` (default `1`) — set `0` if your module's OUT idles high and drops on motion

## Build and flash

Enable the feature for the whole build (already in `build_opt.h` for the Arduino IDE). With PlatformIO:

```sh
pio ci examples/Drivers/Sen0192 --board esp32dev --lib "." \
  --project-option="build_flags=-DPC_ENABLE_SEN0192=1"
```

Flash, open Serial @ 115200, and walk in front of the sensor:

```
SEN0192 microwave motion ready on GPIO4 - walk in front of it
[motion] DETECTED  (event #1)
[motion] clear
[motion] DETECTED  (event #2)
```

Adjust the module's range with its onboard potentiometer (MIN shortens, MAX lengthens, 2–16 m).
