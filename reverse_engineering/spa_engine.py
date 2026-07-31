"""Single-trace Simple Power Analysis (SPA): structural pattern recognition on ONE capture,
as opposed to dpa_cpa_engine.py's statistical correlation across many aligned traces.

## Why this is a different, and often easier, attack than CPA/TVLA

CPA and TVLA both ask "does the value at sample position t correlate with / differ by a
hypothesis, averaged over hundreds of traces?" - they need many traces because the leakage
they target is small relative to noise. SPA asks a structurally different question: "does
this ONE trace's shape - the sequence of active bursts and idle gaps - itself encode the
secret?" When a crypto implementation's *control flow* depends on secret data (the textbook
case: RSA/ECC square-and-multiply, where a "multiply" step runs only for a 1-bit, never for a
0-bit), the presence or **absence** of that step's characteristic burst is the secret, visible
directly in the trace's shape - no statistics, no key hypothesis, sometimes no averaging at
all. This is the original attack Kocher's 1996 "Differential Power Analysis" paper describes
before it gets to DPA proper, and it is exactly why constant-time implementations exist: SPA
is what a naive fixed/branching implementation is trading away.

## Techniques implemented

  - **Activity-envelope segmentation** - a moving-energy envelope thresholded (via Otsu's
    method, so no hand-tuned magic number) into alternating burst/gap runs. Otsu, "A
    Threshold Selection Method from Gray-Level Histograms" (IEEE SMC, 1979) - a 1-D
    image-processing technique applied here to a power/EM envelope instead of pixel
    intensities; the bimodal "on vs off" structure is the same problem.
  - **Periodic burst-count decoding** - the square-and-multiply case: given the known (or
    autocorrelation-estimated) period of the always-present operation, count bursts per
    period. One burst per period = a 0 bit (no multiply - the *silence* IS the signal); two
    or more = a 1 bit. Single trace, no key hypothesis.
  - **Zero-value scan** - among the detected bursts, flag ones with anomalously low energy
    relative to the population: multiplying or adding by zero characteristically switches
    fewer bits than a random operand, and stands out even without knowing which operation it
    was. See Akishita & Takagi, "Zero-Value Point Attacks on Elliptic Curve Cryptosystem"
    (ISC 2003), for the ECC case this generalizes from.
"""

import numpy as np


def activity_envelope(trace: np.ndarray, window: int) -> np.ndarray:
    """Moving-energy envelope: high during switching activity, low during idle/quiescent
    stretches. `window` should span a few periods of the trace's own noise/ripple but stay
    short relative to the shortest burst you want to resolve."""
    energy = trace.astype(np.float64) ** 2
    kernel = np.ones(window, dtype=np.float64) / window
    return np.convolve(energy, kernel, mode="same")


def otsu_threshold(values: np.ndarray, bins: int = 256) -> float:
    """Otsu's method: the threshold that maximizes between-class variance of a bimodal
    (here: idle vs active) histogram - no hand-picked magic number."""
    hist, edges = np.histogram(values, bins=bins)
    hist = hist.astype(np.float64)
    centers = (edges[:-1] + edges[1:]) / 2.0
    total = hist.sum()
    if total == 0:
        return float(centers[0])
    sum_all = np.sum(hist * centers)
    weight_bg, sum_bg = 0.0, 0.0
    best_var, best_t = -1.0, float(centers[0])
    for i in range(bins):
        weight_bg += hist[i]
        if weight_bg == 0:
            continue
        weight_fg = total - weight_bg
        if weight_fg == 0:
            break
        sum_bg += hist[i] * centers[i]
        mean_bg = sum_bg / weight_bg
        mean_fg = (sum_all - sum_bg) / weight_fg
        between_var = weight_bg * weight_fg * (mean_bg - mean_fg) ** 2
        if between_var > best_var:
            best_var, best_t = between_var, centers[i]
    return float(best_t)


def segment_activity(envelope: np.ndarray, threshold: float = None):
    """Run-length encode envelope > threshold into [(start, end, is_active), ...]. threshold
    defaults to otsu_threshold(envelope)."""
    if threshold is None:
        threshold = otsu_threshold(envelope)
    active = envelope > threshold
    segments = []
    start = 0
    cur = bool(active[0])
    for i in range(1, len(active)):
        if bool(active[i]) != cur:
            segments.append((start, i, cur))
            start = i
            cur = bool(active[i])
    segments.append((start, len(active), cur))
    return segments, threshold


def burst_gap_durations(segments):
    """(burst_lengths, gap_lengths) in samples - the raw material for a duration-based
    decoder when operations are not cleanly periodic."""
    bursts = [e - s for (s, e, a) in segments if a]
    gaps = [e - s for (s, e, a) in segments if not a]
    return bursts, gaps


def estimate_period(envelope: np.ndarray, min_period: int, max_period: int) -> int:
    """Autocorrelation peak in [min_period, max_period] - a period estimate when the
    always-present operation's spacing is not already known."""
    centered = envelope - envelope.mean()
    full = np.correlate(centered, centered, mode="full")
    mid = len(full) // 2
    window = full[mid + min_period: mid + max_period + 1]
    return min_period + int(np.argmax(window))


def decode_periodic_bursts(segments, period_samples: int, n_periods: int, start_sample: int = 0):
    """Count active-segment starts falling in each [start_sample + k*period, ...) window -
    the square-and-multiply structural decoder. Returns a list of n_periods burst counts."""
    counts = [0] * n_periods
    for (s, e, active) in segments:
        if not active:
            continue
        idx = (s - start_sample) // period_samples
        if 0 <= idx < n_periods:
            counts[int(idx)] += 1
    return counts


def counts_to_bits(counts, baseline: int = 1):
    """bit=1 where MORE than `baseline` bursts occurred in a period (an extra operation
    appeared alongside the always-present one); bit=0 where only the baseline ran - the
    *absence* of the extra burst is what encodes the 0. baseline=1 is the classic
    square-(optional multiply) case."""
    return [1 if c > baseline else 0 for c in counts]


def zero_value_scan(trace: np.ndarray, segments, z_threshold: float = -2.0):
    """Among active segments, flag ones whose energy z-score falls below z_threshold -
    candidate "this operand was zero" events (anomalously low switching activity vs. the
    burst population). Returns the flagged (start, end) segments."""
    active_segs = [(s, e) for (s, e, a) in segments if a]
    if len(active_segs) < 2:
        return []
    energies = np.array([np.sum(trace[s:e].astype(np.float64) ** 2) for s, e in active_segs])
    std = energies.std()
    z = (energies - energies.mean()) / (std if std > 0 else 1e-12)
    return [active_segs[i] for i in range(len(active_segs)) if z[i] < z_threshold]


if __name__ == "__main__":
    # Single-trace demo: a synthetic square-and-multiply-shaped signal encoding a known bit
    # sequence, decoded from ONE trace with no averaging and no key hypothesis.
    print("[*] Single-trace SPA demo: square-and-multiply burst-pattern decoding")
    rng = np.random.default_rng(99)

    BITS = [1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 1, 0]
    PERIOD = 200
    SQUARE_OFFSET, SQUARE_WIDTH = 20, 30
    MULT_OFFSET, MULT_WIDTH = 60, 30
    BURST_AMPLITUDE = 3.0
    NOISE_SIGMA = 0.15

    n_samples = PERIOD * len(BITS)
    trace = rng.normal(0, NOISE_SIGMA, size=n_samples)
    for k, bit in enumerate(BITS):
        base = k * PERIOD
        trace[base + SQUARE_OFFSET: base + SQUARE_OFFSET + SQUARE_WIDTH] += BURST_AMPLITUDE
        if bit == 1:
            trace[base + MULT_OFFSET: base + MULT_OFFSET + MULT_WIDTH] += BURST_AMPLITUDE * 0.9

    env = activity_envelope(trace, window=9)
    segments, threshold = segment_activity(env)
    bursts, gaps = burst_gap_durations(segments)
    print(f"    Otsu threshold={threshold:.3f}  bursts found={len(bursts)}  gaps found={len(gaps)}")

    est_period = estimate_period(env, PERIOD - 20, PERIOD + 20)
    print(f"    autocorrelation period estimate: {est_period} (true: {PERIOD})")

    counts = decode_periodic_bursts(segments, PERIOD, len(BITS))
    decoded = counts_to_bits(counts)
    print(f"    per-period burst counts: {counts}")
    print(f"    decoded bits : {decoded}")
    print(f"    true bits    : {BITS}")
    assert decoded == BITS, "SPA burst-pattern decode did not match the injected bit sequence"

    # Zero-value scan sanity check: make one "multiply" operand zero (near-zero activity).
    zv_trace = trace.copy()
    zv_bit_index = 6  # a bit=1 period - its multiply burst becomes a "multiply by zero"
    zv_base = zv_bit_index * PERIOD
    zv_trace[zv_base + MULT_OFFSET: zv_base + MULT_OFFSET + MULT_WIDTH] = rng.normal(0, NOISE_SIGMA, MULT_WIDTH)
    zv_env = activity_envelope(zv_trace, window=9)
    zv_segments, _ = segment_activity(zv_env)
    flagged = zero_value_scan(zv_trace, zv_segments)
    print(f"    zero-value scan flagged {len(flagged)} anomalously quiet burst(s)")
    assert len(flagged) >= 1, "zero-value scan failed to flag the injected zero-operand burst"

    print("\n================ SPA OK ================")
    print("Burst/gap segmentation, periodic bit decoding, and zero-value scanning all verified")
    print("on a SINGLE synthetic trace - no averaging, no key hypothesis.")
    print("=========================================")
