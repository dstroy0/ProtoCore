# PowerGovernor - clock the SoC to what the work, the die, and the supply allow

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_POWER_MGMT`

## What this example teaches

`services/radio_power` owns the radio and `services/sleep_sched` decides how long to sleep. Neither
owns the SoC itself, which is where the rest of the power budget goes. `services/power_mgmt` answers
one question every tick: what should the CPU clock be right now.

| Input              | Effect                                                                     |
| ------------------ | -------------------------------------------------------------------------- |
| load               | busy work gets the ceiling; an idle server drops to the floor              |
| die temperature    | a hot part clocks down, with a lower threshold to restore                  |
| brownout reset     | come up at the floor for a settle window instead of re-collapsing the rail |
| unused peripherals | a build with no BLE hands back the Bluetooth power domain                  |

Precedence is deliberate: **brownout beats thermal beats load**. A board that cannot hold its supply
must not be clocked up merely because it is busy, and neither must a hot one.

## The feedback loop is the API

The previous tick's throttle flag goes back into the next call:

```cpp
PowerPlan p = pc_power_plan(&cfg, load_pct, temp_c, brownout, now, g_plan.throttled);
pc_power_apply(&p);
g_plan = p;                       // <- this is what gives the thermal decision its hysteresis
```

Pass `false` there and you get a single-threshold governor, which oscillates.

## Verified on hardware

**HW-verified (2026-07-19)** on an **ESP32-S3** with its real internal die sensor.

Boot and idle scaling:

```
released the Bluetooth power domain
boot clock: 240 MHz, die 36 C
clock -> 80 MHz (throttled=0 recovering=0 die=37 C)     <- idle server drops itself 240 -> 80
```

Load scaling over HTTP, with the die responding physically:

```
idle          {"cpu_mhz":80, "throttled":false,"recovering":false,"temp_c":36}
GET /busy     {"cpu_mhz":240,"throttled":false,"recovering":false,"temp_c":38}   <- +2 C under load
after         {"cpu_mhz":80, "throttled":false,"recovering":false,"temp_c":37}
```

## Thermal: what the hardware taught us

The die idles near 35 C, so a test build with the thresholds moved to `hot=37 / cool=35` was used to
make the real sensor actually cross them. The per-tick trace:

```
clock -> 240 MHz (throttled=0 die=35 C)
clock ->  80 MHz (throttled=1 die=37 C)     <- engages at exactly temp_hot_c
clock -> 240 MHz (throttled=0 die=35 C)     <- releases at exactly temp_cool_c
clock ->  80 MHz (throttled=1 die=37 C)
```

The hysteresis is exact - it engages at 37 and releases at 35, and never releases at 36. But it
still oscillates, and the reason is worth knowing:

> **The hysteresis band must be wider than the temperature swing the clock change itself causes.**
> On this S3, dropping 240 -> 80 MHz cools the die ~2 C within one 500 ms tick, and going back up
> reheats it by the same amount. With a 2 C band the throttle's own effect carries the die back
> across the release threshold, so it self-sustains no matter how correct the comparison is.

That is why the shipped default is `hot=80 / cool=70` - a 10 C band, comfortably wider than the
swing. Tune the pair together, not individually.

### What is not covered here

Brownout recovery is host-tested but not hardware-forced: triggering a real brownout needs a bench
supply that can sag the rail on demand. The detection side is live (`esp_reset_reason()` is read and
latched at boot); the policy it feeds is covered by `native_power_mgmt`.

## Routes

| HttpRoute | What it does                                                           |
| --------- | ---------------------------------------------------------------------- |
| `/power`  | `{"cpu_mhz":80,"throttled":false,"recovering":false,"temp_c":36}`      |
| `/busy`   | reports full load for 5 s so the clock can be watched climb and settle |

## Tunables

| Flag                   | Default | Meaning                                           |
| ---------------------- | ------- | ------------------------------------------------- |
| `PC_POWER_MHZ_MAX`     | 240     | clock when there is work to do                    |
| `PC_POWER_MHZ_MIN`     | 80      | clock when idle, throttled, or recovering         |
| `PC_POWER_BUSY_PCT`    | 40      | load at/above which the ceiling is used           |
| `PC_POWER_TEMP_HOT_C`  | 80      | throttle at/above this die temperature            |
| `PC_POWER_TEMP_COOL_C` | 70      | release at/below - keep the band wide (see above) |
| `PC_POWER_RECOVER_MS`  | 10000   | floor-clock hold after a brownout reset           |

A part with no usable internal sensor (classic ESP32) reports `INT16_MIN`, which the governor treats
as "no reading" rather than ice-cold - so it never throttles and never silently releases one.

## Build footprint

| Board    | Flash           |
| -------- | --------------- |
| ESP32-S3 | 911,323 B (69%) |

## Build-flag note

The flags must reach the library build, so pass them as build flags:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_POWER_MGMT=1" \
  --lib="." examples/L7-Application/PowerGovernor/PowerGovernor.ino
```
