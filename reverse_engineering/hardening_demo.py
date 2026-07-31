"""Vulnerable vs. hardened: two concrete before/after stories showing a countermeasure
change the outcome of the SAME attack, using nothing but the analysis primitives already in
dpa_cpa_engine.py. This is the payoff the rest of the toolbox builds up to - not "here is a
technique" but "here is why an implementation choice matters," end to end.

## Story 1: Boolean masking defeats first-order CPA - second-order CPA defeats masking

A first-order Boolean-masked implementation splits the sensitive intermediate value into two
shares whose XOR reconstructs it: `masked = intermediate ^ mask`, with `mask` fresh and
uniform random every trace. Individually, neither share's Hamming weight correlates with the
real intermediate - that IS the security property masking buys (a first-order CPA reads as
pure noise). But the shares still leak *together*: their **centered product**
`(leak(masked) - mean)(leak(mask) - mean)` does correlate with the real intermediate, because
XOR-ing two independent random variables does not erase the correlation of their *product*
the way it erases each one's individual correlation. This is Messerges, "Using Second-Order
Power Analysis to Attack DPA Resistant Software" (CHES 2000) - the textbook counter to
first-order masking, and dpa_cpa_engine.py's `compute_second_order_cpa` implements exactly
this combiner.

## Story 2: Timing desynchronization defeats naive (fixed-position) CPA - realignment defeats it

A far cheaper "hiding" countermeasure than masking: run the sensitive operation at a randomly
jittered offset each execution (a random delay, or a shuffled instruction order with similar
effect) instead of a fixed time after the trigger. A CPA that assumes every trace's leak sits
at the SAME sample index sees the correlation smeared across however many sample positions the
jitter can land on - with enough spread, the peak drops into the noise floor and the wrong key
byte wins. `align_trace_jitter`'s windowed cross-correlation resynchronizes each trace against
a shared reference feature (here: a strong, always-present marker at a fixed OFFSET from the
leak, standing in for a real trigger-adjacent clock artifact) before CPA runs - undoing a
SIMPLE, single-shift-per-trace desynchronization. Real hiding countermeasures use jitter deep
enough, or shaped enough (non-uniform, non-single-shift), that this stops working - the point
of this story is that hiding has to be strong enough to survive realignment, not that hiding
is worthless.

    python hardening_demo.py
"""

import numpy as np

from dpa_cpa_engine import (
    AES_SBOX,
    HAMMING_WEIGHT,
    align_trace_jitter,
    compute_cpa_matrix,
    compute_second_order_cpa,
    generate_hamming_distance_hypotheses,
)

KEY = 0xD3
TARGET_BYTE = 0


def _recover(traces, plaintexts):
    hyp = generate_hamming_distance_hypotheses(plaintexts, TARGET_BYTE)
    cpa = np.abs(compute_cpa_matrix(traces, hyp))
    guess = int(np.argmax(np.max(cpa, axis=1)))
    return guess, float(cpa.max())


def masking_story():
    print("[*] Story 1: Boolean masking vs. first/second-order CPA")
    rng = np.random.default_rng(55)
    n_samples = 200
    share1_pos, share2_pos = 60, 140
    num_traces = 4000
    gain, noise_sigma = 0.4, 0.3

    plaintexts = rng.integers(0, 256, size=(num_traces, 16), dtype=np.uint8)
    intermediate = AES_SBOX[plaintexts[:, TARGET_BYTE] ^ KEY] ^ plaintexts[:, TARGET_BYTE]

    print("    Unmasked (vulnerable) target: the raw intermediate leaks directly...")
    unmasked_traces = rng.normal(0, noise_sigma, size=(num_traces, n_samples))
    unmasked_traces[:, share1_pos] += HAMMING_WEIGHT[intermediate] * gain
    guess, corr = _recover(unmasked_traces, plaintexts)
    print(f"    first-order CPA: recovered={hex(guess)} expected={hex(KEY)} |corr|={corr:.4f}")
    assert guess == KEY, "first-order CPA should recover the key from an unmasked target"

    print("    Masked (hardened) target: intermediate = share1 ^ mask, mask fresh every trace...")
    mask = rng.integers(0, 256, size=num_traces).astype(np.uint8)
    masked_intermediate = intermediate ^ mask
    masked_traces = rng.normal(0, noise_sigma, size=(num_traces, n_samples))
    masked_traces[:, share1_pos] += HAMMING_WEIGHT[masked_intermediate] * gain
    masked_traces[:, share2_pos] += HAMMING_WEIGHT[mask] * gain

    guess1, corr1 = _recover(masked_traces, plaintexts)
    print(f"    first-order CPA on masked traces: recovered={hex(guess1)} expected={hex(KEY)} "
          f"|corr|={corr1:.4f} (masking should defeat this - low correlation, likely wrong key)")

    hyp = generate_hamming_distance_hypotheses(plaintexts, TARGET_BYTE)
    second = np.abs(compute_second_order_cpa(masked_traces, hyp, poi_indices=[share1_pos, share2_pos]))
    guess2 = int(np.argmax(np.max(second, axis=1)))
    corr2 = float(second.max())
    print(f"    second-order CPA (centered product of both shares): recovered={hex(guess2)} "
          f"expected={hex(KEY)} |corr|={corr2:.4f}")
    assert guess2 == KEY, "second-order CPA should recover the key despite the masking"
    assert corr2 > 3 * corr1, "second-order CPA should clearly outperform first-order CPA against masking"
    print("    OK - masking defeated first-order CPA; second-order CPA defeated masking\n")


def desync_story():
    print("[*] Story 2: timing desynchronization vs. naive CPA + realignment")
    rng = np.random.default_rng(2020)
    n_samples = 400
    marker_pos, leak_pos = 50, 250
    num_traces = 1200
    shift_range = 10  # per-trace; relative shift between any two traces can reach 2x this

    x = np.arange(n_samples)
    marker = 2.0 * np.exp(-0.5 * ((x - marker_pos) / 8.0) ** 2)  # a strong, always-present
    # reference feature (stands in for a real trigger-adjacent clock artifact) that moves WITH
    # the trace under jitter, so aligning on it also re-aligns the leak sitting elsewhere in it.

    plaintexts = rng.integers(0, 256, size=(num_traces, 16), dtype=np.uint8)
    intermediate = AES_SBOX[plaintexts[:, TARGET_BYTE] ^ KEY] ^ plaintexts[:, TARGET_BYTE]

    base = rng.normal(0, 0.15, size=(num_traces, n_samples)) + marker
    base[:, leak_pos] += HAMMING_WEIGHT[intermediate] * 0.4
    shifts = rng.integers(-shift_range, shift_range + 1, size=num_traces)
    jittered = np.array([np.roll(base[i], int(shifts[i])) for i in range(num_traces)])

    guess_naive, corr_naive = _recover(jittered, plaintexts)
    print(f"    naive (fixed-position) CPA on jittered traces: recovered={hex(guess_naive)} "
          f"expected={hex(KEY)} |corr|={corr_naive:.4f}")
    assert guess_naive != KEY, "desync should be strong enough to defeat a naive fixed-position CPA"

    realigned = align_trace_jitter(jittered, jittered[0], max_shift=2 * shift_range + 5)
    guess_aligned, corr_aligned = _recover(realigned, plaintexts)
    print(f"    after align_trace_jitter (locks onto the marker): recovered={hex(guess_aligned)} "
          f"expected={hex(KEY)} |corr|={corr_aligned:.4f}")
    assert guess_aligned == KEY, "realignment should recover the key after undoing the desync"
    print("    OK - desync defeated naive CPA; realignment (on a strong shared marker) defeated the desync\n")


if __name__ == "__main__":
    print("[*] Vulnerable vs. hardened: two countermeasures, two attacks that adapt to them\n")
    masking_story()
    desync_story()
    print("================ HARDENING DEMO OK ================")
    print("Both before/after stories verified: a countermeasure changed the outcome, and a")
    print("stronger technique changed it back - the same escalation real evaluation labs run.")
    print("=====================================================")
