# PidTuning - a PID control loop + the offline tuning workflow

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_CONTROL`

## What this example teaches

`services/control` is a zero-heap, FPU-accelerated PID controller. Tuning a PID by
hand on hardware is slow and risky, so this example shows the **offline** workflow:
the device runs the loop and records it, you pull the run down as a CSV, and
[`tools/pid_tune.py`](../../../tools/pid_tune.py) identifies the plant, simulates
candidate gains, and hands you better `Kp/Ki/Kd` — no trial-and-error on the motor.

**The control loop** runs at a fixed rate and records each step:

```cpp
float sp = setpoint_now(now);      // a square wave to excite the plant
float meas = plant_read();         // your sensor (ADC, encoder, ...)
float u = pid_update(&pid, sp, meas, dt);
plant_write(u, dt);                // your actuator (PWM, dshot, CiA 402 target, ...)
```

So it runs on **any ESP32 with no extra hardware**, `plant_read()`/`plant_write()`
here are a simulated first-order process. To tune a real loop, replace those two
functions with your sensor read and actuator write — nothing else changes.

**The run is served as a self-describing CSV** at `GET /log.csv`: a `# pid ...`
metadata line (the gains + limits + rate the run used, so the tuner needs no flags),
then the `t_s,setpoint,measurement,output,dt_s` columns.

## The tuning loop

```sh
curl http://<ip>/log.csv > run.csv                        # capture a run
python tools/pid_tune.py run.csv --autotune --png tune.png # identify + tune + plot
```

`pid_tune.py` reproduces `control.cpp`'s PID exactly (derivative-on-measurement,
conditional anti-windup, clamping), so the simulation matches what the device will
actually do. It prints the identified plant fit and a suggested gain set, e.g.:

```
log header: kp=1.5  ki=4  kd=0.05  out_min=0  out_max=50  dt=0.02
identified ARX plant  na=2 nb=2  ...  fit R^2=0.998
autotuned gains:  Kp=10.3  Ki=110  Kd=0
```

Edit `KP` / `KI` / `KD` at the top of the sketch to the suggestion, re-flash, then
`GET /reset` to record and re-check a fresh run. Other modes: `--sweep` (compare
0.5x / 1x / 2x the gains), or `--kp N --ki N --kd N` to simulate one set. The tuner
needs `numpy` (+ `matplotlib` for the plot, `scipy` for `--autotune`):
`python -m pip install numpy matplotlib scipy`.

> **Tip.** The best identification comes from a run with real excitation - the
> square-wave setpoint here steps the plant so the fit is well-conditioned. A loop
> that just holds one setpoint gives the tuner little to learn from.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_CONTROL=1" \
  --lib="." examples/L7-Application/PidTuning/PidTuning.ino
```

Flash, open Serial @ 115200 for the IP, then run the tuning loop above. `GET /` shows
the current gains + live state and the exact commands.
