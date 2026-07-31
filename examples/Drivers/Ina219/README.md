# Ina219 - measure current and power

This example uses an **INA219** to measure how much **current** a circuit draws and how much
**power** it uses - not just the voltage. It prints the bus voltage, current, and power once a
second. It is written for a beginner and is the basis of any "how long will my battery last?"
or "why is this so hot?" experiment.

## Voltage vs. current vs. power

- **Voltage** (volts) is the electrical "pressure" - the Ads1115 example measures that.
- **Current** (amps) is how much electricity actually _flows_ through the circuit.
- **Power** (watts) is voltage x current - the actual energy being used.

The INA219 measures current by watching the tiny voltage drop across a small **shunt resistor**
that your circuit's current flows through, and combines it with the bus voltage to report power.

## What you will need

- An **ESP32 board** with a USB cable.
- An **INA219** breakout (Adafruit or generic - most have a 0.1 ohm shunt).
- A small **load** to measure (an LED with a resistor, a small motor, another board).
- Jumper wires.

## Part 1 - Wire it up

Two connections are I2C; the other two put the board **in series** with the thing you are
measuring (all the current flows through it).

| INA219 pin | Connect to          | Why                       |
| ---------- | ------------------- | ------------------------- |
| `VCC`      | `3V3`               | powers the chip           |
| `GND`      | `GND`               | shared ground             |
| `SDA`      | GPIO **21**         | I2C data                  |
| `SCL`      | GPIO **22**         | I2C clock                 |
| `Vin+`     | your supply **(+)** | current enters here       |
| `Vin-`     | your load **(+)**   | ...and leaves to the load |

So the path is: **supply (+) -> Vin+ -> [shunt] -> Vin- -> load (+) -> load (-) -> GND**. The
load's negative side and the supply's negative side share GND with the ESP32.

## Part 2 - Flash and measure

Open [Ina219.ino](Ina219.ino), upload, and open the Serial Monitor at **115200**:

```
INA219 ready
bus=5.008 V  current=42.100 mA  power=210 mW
```

Switch your load on and off (or change it) and watch the current and power change. That is a
real power meter you built.

## Where this fits

`pc_ina219_read_current_ua()` / `pc_ina219_read_power_uw()` (plus `pc_ina219_read_bus_mv()`) give you the
numbers in integer micro/milli units - no floating point. From here you can log a device's power
draw over time, publish it over **MQTT**, chart battery drain over **WebSocket**, or trip an
alert when something pulls too much. The Ads1115 example measures voltage; this one measures the current
and power that go with it.

## Changing the range (shunt and LSB)

`pc_ina219_begin(addr, current_lsb_ua, shunt_mohm)` sets up the math. The defaults - `100`
microamps per bit and a `100` milliohm (0.1 ohm) shunt - give about a 3.2 A range at ~0.1 mA
resolution, which fits most breakouts. If your board has a different shunt, or you want finer
resolution over a smaller range, change those two numbers; the calibration is recomputed for
you.

## Troubleshooting

- **"INA219 not found".** Check `SDA`->21, `SCL`->22, and power. The default address is `0x40`;
  the `A0`/`A1` solder pads change it (so you can monitor several rails at once).
- **Current reads 0 with a load connected.** The load must be _in series_ through `Vin+` ->
  `Vin-`, not wired straight to your supply. Double-check the path above.
- **Numbers look scaled wrong.** Your board's shunt is probably not 0.1 ohm - set `shunt_mohm`
  in `pc_ina219_begin()` to match it.

## Build and run (PlatformIO)

The feature lives in the library, so its flag must reach the whole build:

```bash
pio ci examples/Drivers/Ina219 \
  --board esp32dev --lib "." \
  --project-option="build_flags=-DPC_ENABLE_INA219=1"
```

(The Arduino IDE reads the flag from `build_opt.h` beside the sketch automatically.)

## How it works (for the curious)

The bus-voltage register holds the voltage in its upper 13 bits at 4 mV per bit
(`pc_ina219_bus_mv` shifts and scales it); the shunt-voltage register is signed at 10 µV per bit
(`pc_ina219_shunt_uv`). To read current and power directly, the chip needs a **calibration** value
derived from the shunt resistance and your chosen current LSB: `pc_ina219_calibration()` computes
`40960000 / (current_lsb_ua * shunt_mohm)` (so 100 µA and 0.1 ohm give 4096), and
`pc_ina219_current_ua` / `pc_ina219_power_uw` scale the raw registers (power's LSB is 20x the current
LSB). All of that math is unit-tested on a PC (see `test/test_ina219`); only the register
reads/writes run on the ESP32.
