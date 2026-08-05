#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""
pid_tune.py - offline PID tuner / plotter for services/control.

Takes a CSV log captured from a live PID run on the device (the columns
`services/control` emits: t_s,setpoint,measurement,output,dt_s), identifies a
discrete plant model from the logged command->measurement data, then RE-SIMULATES
the closed loop with a Python PID that matches control.cpp exactly - so you can
sweep Kp/Ki/Kd, auto-tune, and see the predicted step response without touching
hardware.

    # log format (matches CONTROL_LOG_HEADER):
    #   t_s,setpoint,measurement,output,dt_s

    python tools/pid_tune.py run.csv --sweep                 # sweep gains around the logged ones
    python tools/pid_tune.py run.csv --autotune              # search for better Kp/Ki/Kd
    python tools/pid_tune.py run.csv --kp 2 --ki 8 --kd 0.1  # simulate one gain set
    python tools/pid_tune.py run.csv --out-min 0 --out-max 255 --autotune --png tune.png

Requires numpy (identification + sim) and, for plots, matplotlib. scipy is used
for --autotune if present, else a coarse grid search is used.
"""

import argparse
import csv
import sys

try:
    import numpy as np
except ImportError:
    sys.exit("pid_tune.py needs numpy:  python -m pip install numpy matplotlib scipy")


# --------------------------------------------------------------------------------------
# PID - a faithful re-implementation of src/services/system/control/control.cpp (same law, so
# the simulation matches what the device will actually do).
# --------------------------------------------------------------------------------------
class Pid:
    def __init__(self, kp, ki, kd, kff=0.0, out_min=-1e30, out_max=1e30, integ_min=-1e30, integ_max=1e30, d_alpha=0.0):
        self.kp, self.ki, self.kd, self.kff = kp, ki, kd, kff
        self.out_min, self.out_max = out_min, out_max
        self.integ_min, self.integ_max = integ_min, integ_max
        self.d_alpha = d_alpha
        self.reset()

    def reset(self):
        self.integ = 0.0
        self.prev_meas = 0.0
        self.d_filt = 0.0
        self.primed = False

    @staticmethod
    def _clamp(v, lo, hi):
        return lo if v < lo else (hi if v > hi else v)

    def update(self, setpoint, measurement, dt):
        if dt <= 0.0:
            return 0.0
        error = setpoint - measurement
        deriv = 0.0
        if self.primed:
            deriv = -(measurement - self.prev_meas) / dt
            self.d_filt = self.d_filt + self.d_alpha * (deriv - self.d_filt) if self.d_alpha > 0.0 else deriv
        self.prev_meas = measurement
        self.primed = True

        integ_next = self._clamp(self.integ + self.ki * error * dt, self.integ_min, self.integ_max)
        unclamped = self.kp * error + integ_next + self.kd * self.d_filt + self.kff * setpoint
        out = self._clamp(unclamped, self.out_min, self.out_max)
        worsen_high = unclamped > self.out_max and error > 0.0
        worsen_low = unclamped < self.out_min and error < 0.0
        if not worsen_high and not worsen_low:
            self.integ = integ_next
        return out


# --------------------------------------------------------------------------------------
# Plant identification: fit a discrete ARX model  y[k] = -a.y_past + b.u_past  by least
# squares over the logged (output u -> measurement y) pairs. Captures the plant so a
# different controller can be simulated against it.
# --------------------------------------------------------------------------------------
class ArxPlant:
    def __init__(self, a, b, nk, y_hist, u_hist):
        self.a = np.asarray(a, float)  # y[k-1..k-na] coefficients (without the leading 1)
        self.b = np.asarray(b, float)  # u[k-nk..k-nk-nb+1] coefficients
        self.nk = nk
        self.na = len(a)
        self.nb = len(b)
        self.y = list(y_hist)  # seed history
        self.u = list(u_hist)

    @classmethod
    def identify(cls, y, u, na=2, nb=2, nk=1):
        y = np.asarray(y, float)
        u = np.asarray(u, float)
        n = len(y)
        start = max(na, nk + nb - 1)
        rows, targ = [], []
        for k in range(start, n):
            row = [-y[k - i] for i in range(1, na + 1)] + [u[k - nk - j] for j in range(nb)]
            rows.append(row)
            targ.append(y[k])
        phi = np.asarray(rows, float)
        theta, *_ = np.linalg.lstsq(phi, np.asarray(targ, float), rcond=None)
        a = theta[:na]
        b = theta[na:]
        return cls(a, b, nk, y[:start], u[:start])

    def fit_quality(self, y, u):
        """R^2 of the one-step-ahead prediction over the log (identification validation)."""
        y = np.asarray(y, float)
        pred = self.predict_series(u, y0=y)
        m = min(len(pred), len(y))
        ss_res = np.sum((y[:m] - pred[:m]) ** 2)
        ss_tot = np.sum((y[:m] - np.mean(y[:m])) ** 2) + 1e-12
        return 1.0 - ss_res / ss_tot

    def predict_series(self, u, y0):
        """Free-run prediction of y from the u series, seeded by the first samples of y0."""
        u = np.asarray(u, float)
        n = len(u)
        start = max(self.na, self.nk + self.nb - 1)
        y = np.zeros(n)
        y[:start] = np.asarray(y0, float)[:start]
        for k in range(start, n):
            acc = 0.0
            for i in range(1, self.na + 1):
                acc += -self.a[i - 1] * y[k - i]
            for j in range(self.nb):
                acc += self.b[j] * u[k - self.nk - j]
            y[k] = acc
        return y

    def step(self, u_new):
        """Advance the plant one sample with command u_new; return the new measurement."""
        self.u.append(u_new)
        start = max(self.na, self.nk + self.nb - 1)
        if len(self.y) < start:
            self.y.append(self.y[-1] if self.y else 0.0)
            return self.y[-1]
        acc = 0.0
        for i in range(1, self.na + 1):
            acc += -self.a[i - 1] * self.y[-i]
        for j in range(self.nb):
            acc += self.b[j] * self.u[-(self.nk + j)]
        self.y.append(acc)
        return acc


# --------------------------------------------------------------------------------------
# Closed-loop simulation + tuning cost
# --------------------------------------------------------------------------------------
def simulate(plant_ctor, pid, setpoint, dt):
    """Run the PID against a fresh plant over the setpoint series. Returns (y, u)."""
    plant = plant_ctor()
    n = len(setpoint)
    y = np.zeros(n)
    u = np.zeros(n)
    y[0] = plant.y[-1] if plant.y else 0.0
    for k in range(n):
        u[k] = pid.update(setpoint[k], y[k], dt)
        nxt = plant.step(u[k])
        if k + 1 < n:
            y[k + 1] = nxt
    return y, u


def cost(y, u, setpoint, dt):
    """ISE + overshoot penalty + control-effort penalty (lower is better)."""
    err = setpoint - y
    ise = float(np.sum(err * err) * dt)
    final = setpoint[-1]
    span = max(abs(final), 1e-6)
    overshoot = max(0.0, (np.max(y) - final) / span) if final >= 0 else max(0.0, (final - np.min(y)) / span)
    effort = float(np.sum(np.diff(u) ** 2)) * 1e-4
    return ise + 50.0 * overshoot**2 + effort


# --------------------------------------------------------------------------------------
def load_log(path):
    """Load a PID-run log. Returns (t, setpoint, measurement, output, dt_per_sample, meta, saturated)
    where meta carries the gains/limits/dt the run used (from the dense-binary header or a CSV
    `# pid ...` comment, else {}), and saturated is a bool mask (or None)."""
    with open(path, "rb") as f:
        head = f.read(4)
    if head == b"DPID":
        return _load_binary(path)
    return _load_csv(path)


def _load_binary(path):
    import struct

    with open(path, "rb") as f:
        raw = f.read()
    magic, ver, flags, _resv, dt, kp, ki, kd, kff, omin, omax = struct.unpack_from("<4sBBH7f", raw, 0)
    meta = {"kp": kp, "ki": ki, "kd": kd, "kff": kff, "out_min": omin, "out_max": omax, "dt": dt}
    recs = raw[36:]
    n = len(recs) // 16
    sp = np.zeros(n)
    meas = np.zeros(n)
    out = np.zeros(n)
    sat = np.zeros(n, bool)
    for i in range(n):
        s, m, o, status = struct.unpack_from("<3fI", recs, i * 16)
        sp[i], meas[i], out[i], sat[i] = s, m, o, bool(status & 0x1)
    t = np.arange(n) * dt
    dts = np.full(n, dt)
    return t, sp, meas, out, dts, meta, sat


def _load_csv(path):
    t, sp, meas, out, dts = [], [], [], [], []
    meta = {}
    with open(path, newline="") as f:
        for line in f:
            s = line.strip()
            if s.startswith("#"):  # optional "# pid kp=.. ki=.. kd=.. out_min=.. out_max=.. dt=.." line
                for tok in s.lstrip("#").split():
                    if "=" in tok:
                        k, v = tok.split("=", 1)
                        try:
                            meta[k] = float(v)
                        except ValueError:
                            pass
                continue
            if not s or s.startswith("t_s"):
                continue
            vals = [float(x) for x in s.split(",")[:5]]
            t.append(vals[0])
            sp.append(vals[1])
            meas.append(vals[2])
            out.append(vals[3])
            dts.append(vals[4] if len(vals) > 4 else 0.0)
    arrs = tuple(np.asarray(v, float) for v in (t, sp, meas, out, dts))
    return arrs + (meta, None)


def autotune(plant_ctor, setpoint, dt, base, out_min, out_max):
    x0 = np.array([base[0], base[1], base[2]], float)

    def obj(x):
        kp, ki, kd = np.maximum(x, 0.0)
        pid = Pid(kp, ki, kd, out_min=out_min, out_max=out_max)
        y, u = simulate(plant_ctor, pid, setpoint, dt)
        return cost(y, u, setpoint, dt)

    try:
        from scipy.optimize import minimize

        res = minimize(obj, x0, method="Nelder-Mead", options={"xatol": 1e-3, "fatol": 1e-4, "maxiter": 400})
        best = np.maximum(res.x, 0.0)
    except ImportError:
        # coarse grid fallback around the base gains
        best, best_c = x0, obj(x0)
        for kp in np.linspace(0.2 * x0[0] + 1e-3, 3 * x0[0] + 1e-3, 7):
            for ki in np.linspace(0.0, 3 * x0[1] + 1e-3, 7):
                for kd in np.linspace(0.0, 3 * x0[2] + 1e-3, 5):
                    c = obj([kp, ki, kd])
                    if c < best_c:
                        best, best_c = np.array([kp, ki, kd]), c
    return tuple(float(v) for v in best)


def main():
    ap = argparse.ArgumentParser(description="Offline PID tuner for services/system/control logs.")
    ap.add_argument("log", help="CSV log: t_s,setpoint,measurement,output,dt_s")
    ap.add_argument("--kp", type=float, help="proportional gain (default: from the log header, else 1)")
    ap.add_argument("--ki", type=float, help="integral gain (default: from the log header, else 0)")
    ap.add_argument("--kd", type=float, help="derivative gain (default: from the log header, else 0)")
    ap.add_argument("--sweep", action="store_true", help="sweep gains around the base set")
    ap.add_argument("--autotune", action="store_true", help="search for better gains")
    ap.add_argument("--out-min", type=float, help="output lower clamp (default: from the log header)")
    ap.add_argument("--out-max", type=float, help="output upper clamp (default: from the log header)")
    ap.add_argument("--na", type=int, default=2, help="ARX denominator order")
    ap.add_argument("--nb", type=int, default=2, help="ARX numerator order")
    ap.add_argument("--png", help="save the plot to this PNG instead of showing it")
    args = ap.parse_args()

    t, sp, meas, out, dts, meta, sat = load_log(args.log)
    if len(t) < 10:
        sys.exit("log too short to identify a plant (need >= 10 rows)")
    if "dt" in meta and meta["dt"] > 0:
        dt = float(meta["dt"])
    elif np.any(dts > 0):
        dt = float(np.median(dts[dts > 0]))
    else:
        dt = float(np.median(np.diff(t)))
    if meta:
        print("log header: " + "  ".join(f"{k}={v:g}" for k, v in meta.items()))
    if sat is not None and np.any(sat):
        print(
            f"  {100.0 * np.mean(sat):.1f}% of samples had a saturated output "
            f"(kept for plant ID - the logged output is the true plant input)."
        )

    plant0 = ArxPlant.identify(meas, out, na=args.na, nb=args.nb)
    r2 = plant0.fit_quality(meas, out)
    print(
        f"identified ARX plant  na={args.na} nb={args.nb}  a={np.round(plant0.a,4)} "
        f"b={np.round(plant0.b,4)}  fit R^2={r2:.3f}  dt={dt:.4g}s"
    )
    if r2 < 0.5:
        print("  ! low fit quality - log a run with more excitation (a step or chirp on the setpoint).")

    def plant_ctor():
        return ArxPlant(
            plant0.a,
            plant0.b,
            plant0.nk,
            [meas[0]] * max(args.na, plant0.nk + args.nb - 1),
            [out[0]] * max(args.na, plant0.nk + args.nb - 1),
        )

    # baseline gains + output limits: CLI overrides, else the log header, else sane defaults.
    kp0 = args.kp if args.kp is not None else meta.get("kp", 1.0)
    ki0 = args.ki if args.ki is not None else meta.get("ki", 0.0)
    kd0 = args.kd if args.kd is not None else meta.get("kd", 0.0)
    out_min = args.out_min if args.out_min is not None else meta.get("out_min", -1e30)
    out_max = args.out_max if args.out_max is not None else meta.get("out_max", 1e30)
    base = (kp0, ki0, kd0)
    runs = {}  # label -> (y, u, gains)

    if args.autotune:
        g = autotune(plant_ctor, sp, dt, base, out_min, out_max)
        print(f"autotuned gains:  Kp={g[0]:.4g}  Ki={g[1]:.4g}  Kd={g[2]:.4g}")
        y, u = simulate(plant_ctor, Pid(*g, out_min=out_min, out_max=out_max), sp, dt)
        runs[f"autotuned Kp={g[0]:.3g} Ki={g[1]:.3g} Kd={g[2]:.3g}"] = (y, u, g)
    elif args.sweep:
        for scale in (0.5, 1.0, 2.0):
            g = (kp0 * scale, ki0 * scale, kd0)
            y, u = simulate(plant_ctor, Pid(*g, out_min=out_min, out_max=out_max), sp, dt)
            runs[f"Kp={g[0]:.3g} Ki={g[1]:.3g} Kd={g[2]:.3g}"] = (y, u, g)
    else:
        g = base
        y, u = simulate(plant_ctor, Pid(*g, out_min=out_min, out_max=out_max), sp, dt)
        runs[f"Kp={g[0]:.3g} Ki={g[1]:.3g} Kd={g[2]:.3g}"] = (y, u, g)

    for label, (y, u, g) in runs.items():
        print(f"  {label:40s}  cost={cost(y, u, sp, dt):.4g}")

    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("(matplotlib not installed - skipping plot; numeric results above)")
        return

    fig, (ax1, ax2) = plt.subplots(2, 1, figsize=(10, 7), sharex=True)
    ax1.plot(t, sp, "k--", lw=1.2, label="setpoint")
    ax1.plot(t, meas, color="0.5", lw=1.0, label="logged measurement")
    for label, (y, u, g) in runs.items():
        ax1.plot(t, y, lw=1.4, label="sim " + label)
        ax2.plot(t, u, lw=1.0, label="u " + label)
    ax1.set_ylabel("process value")
    ax1.set_title(f"PID tuning - identified plant fit R^2={r2:.3f}")
    ax1.legend(fontsize=8, loc="best")
    ax1.grid(alpha=0.3)
    ax2.plot(t, out, color="0.5", lw=1.0, label="logged output")
    ax2.set_ylabel("control output")
    ax2.set_xlabel("time (s)")
    ax2.legend(fontsize=8, loc="best")
    ax2.grid(alpha=0.3)
    fig.tight_layout()
    if args.png:
        fig.savefig(args.png, dpi=110)
        print(f"wrote {args.png}")
    else:
        plt.show()


if __name__ == "__main__":
    main()
