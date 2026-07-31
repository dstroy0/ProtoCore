"""Template attacks: the third pillar of side-channel analysis, alongside dpa_cpa_engine.py's
statistical (many-trace) CPA/TVLA and spa_engine.py's structural (single-trace) SPA.

## Where this fits

CPA asks "does a linear model of the leakage correlate with the trace, averaged over many
traces?" Template attacks ask a different question in two phases:

  1. **Profile** (attacker owns a clone device, or the profiling traces come from the same
     target with a known key): characterize the trace's *statistical distribution* - not just
     its mean - for every value the leaking intermediate can take. A Gaussian template per
     class: a mean vector over a handful of points of interest, and a covariance matrix
     capturing how those points vary and co-vary together.
  2. **Attack** (unknown key): score an unknown trace against every class's template via its
     likelihood, and for CPA-style key recovery, sum the log-likelihood each key hypothesis
     predicts across a handful of attack traces - the hypothesis whose predicted classes best
     explain what was actually observed wins.

This is the standard **worst-case SCA evaluation model**: it assumes the attacker did the
expensive characterization work up front, so the live attack phase needs only a handful of
traces - often single digits - which is the whole point of profiling and the reason
evaluation labs treat it as the bar an implementation has to clear. Chari, Rao, Rohatgi,
"Template Attacks" (CHES 2002) is the original; the pooled-covariance simplification here
(one shared covariance matrix across all classes, only the means differ) follows Choudary &
Kuhn, "Efficient Template Attacks" (CARDIS 2013) - far more numerically stable with a modest
profiling set than a full per-class covariance.

Classes are the Hamming weight of the Hamming-distance intermediate (0-8, not the full 0-255
byte value) - the same model dpa_cpa_engine.py's CPA uses, and a far smaller profiling set
suffices to characterize 9 classes than 256.
"""

import numpy as np

from dpa_cpa_engine import AES_SBOX, HAMMING_WEIGHT


class TemplateAttack:
    """Profile on a device with a known key (or known intermediate labels), then attack an
    unknown key with as few traces as the templates' quality allows."""

    def __init__(self, poi_indices):
        self.poi_indices = np.asarray(poi_indices)
        self.class_means = {}
        self.pooled_cov = None
        self.classes = None
        self._cov_inv_cache = None

    def profile(self, traces: np.ndarray, labels: np.ndarray):
        """traces: (n, n_samples); labels: (n,) the Hamming-weight class (0-8) of the
        intermediate value each trace was captured under."""
        poi = traces[:, self.poi_indices].astype(np.float64)
        self.classes = np.unique(labels)
        n_poi = len(self.poi_indices)
        pooled = np.zeros((n_poi, n_poi))
        total_dof = 0
        for c in self.classes:
            x = poi[labels == c]
            self.class_means[c] = x.mean(axis=0)
            if len(x) > 1:
                pooled += (len(x) - 1) * np.cov(x, rowvar=False).reshape(n_poi, n_poi)
                total_dof += len(x) - 1
        self.pooled_cov = pooled / max(total_dof, 1)
        # Tiny diagonal regularization: keeps the inverse well-conditioned when a profiling
        # set is thin relative to n_poi, without meaningfully perturbing a well-conditioned one.
        trace_scale = np.trace(self.pooled_cov) / n_poi if n_poi else 1.0
        self.pooled_cov += np.eye(n_poi) * 1e-6 * max(trace_scale, 1e-12)
        self._cov_inv_cache = None

    def _cov_inv(self):
        if self._cov_inv_cache is None:
            self._cov_inv_cache = np.linalg.inv(self.pooled_cov)
        return self._cov_inv_cache

    def _mahalanobis_sq(self, diffs: np.ndarray) -> np.ndarray:
        cov_inv = self._cov_inv()
        return np.einsum("ij,jk,ik->i", diffs, cov_inv, diffs)

    def class_log_likelihoods(self, trace: np.ndarray) -> dict:
        """Per-class log-likelihood of a single trace's POI vector (dropping the additive
        normalization constant shared by every class, which never changes an argmax)."""
        poi = trace[self.poi_indices].astype(np.float64)
        out = {}
        for c in self.classes:
            diff = (poi - self.class_means[c])[None, :]
            out[c] = float(-0.5 * self._mahalanobis_sq(diff)[0])
        return out

    def attack_key_byte(self, traces: np.ndarray, plaintexts: np.ndarray, byte_index: int):
        """Sum each key hypothesis's predicted-class log-likelihood across every attack trace;
        the hypothesis that best explains what was actually observed wins.
        Returns (best_key_guess, scores[256])."""
        poi = traces[:, self.poi_indices].astype(np.float64)
        scores = np.zeros(256)
        for k in range(256):
            intermediate = AES_SBOX[plaintexts[:, byte_index] ^ k] ^ plaintexts[:, byte_index]
            pred_class = HAMMING_WEIGHT[intermediate].astype(int)
            for c in self.classes:
                mask = pred_class == c
                if not np.any(mask):
                    continue
                diffs = poi[mask] - self.class_means[c]
                scores[k] += float(np.sum(-0.5 * self._mahalanobis_sq(diffs)))
        return int(np.argmax(scores)), scores


def select_poi(traces: np.ndarray, labels: np.ndarray, n_poi: int = 6):
    """Points of interest via SNR (dpa_cpa_engine.signal_to_noise_ratio) - the highest-SNR
    samples are where the per-class means separate the most relative to their spread, the
    same property a template needs to discriminate classes at all."""
    from dpa_cpa_engine import signal_to_noise_ratio

    snr = signal_to_noise_ratio(traces, labels)
    return np.argsort(snr)[-n_poi:][::-1]


if __name__ == "__main__":
    print("[*] Template attack demo: profile on a KNOWN key, attack an UNKNOWN key with few traces")
    rng = np.random.default_rng(4242)

    TIME_SAMPLES = 200
    LEAK_SAMPLE = 90
    PROFILE_KEY = 0x11  # the clone device's key - known during profiling
    ATTACK_KEY = 0xD3   # the target device's key - unknown to the attack phase
    NOISE_SIGMA = 0.35

    def make_traces(n, key, rng):
        plaintexts = rng.integers(0, 256, size=(n, 16), dtype=np.uint8)
        intermediate = AES_SBOX[plaintexts[:, 0] ^ key] ^ plaintexts[:, 0]
        hw = HAMMING_WEIGHT[intermediate]
        traces = rng.normal(0, NOISE_SIGMA, size=(n, TIME_SAMPLES))
        traces[:, LEAK_SAMPLE] += hw * 0.5
        return traces, plaintexts, hw.astype(int)

    print("[*] Profiling phase: 3000 traces from a clone device, key known...")
    profile_traces, _, profile_labels = make_traces(3000, PROFILE_KEY, rng)
    poi = select_poi(profile_traces, profile_labels, n_poi=5)
    print(f"    points of interest: {sorted(poi.tolist())} (true leak sample: {LEAK_SAMPLE})")

    tmpl = TemplateAttack(poi)
    tmpl.profile(profile_traces, profile_labels)

    for n_attack in (5, 10, 20):
        attack_traces, attack_plaintexts, _ = make_traces(n_attack, ATTACK_KEY, rng)
        guess, scores = tmpl.attack_key_byte(attack_traces, attack_plaintexts, byte_index=0)
        margin = np.sort(scores)[-1] - np.sort(scores)[-2]
        print(f"    {n_attack:>2} attack traces: recovered={hex(guess)} expected={hex(ATTACK_KEY)} "
              f"margin={margin:.2f}")

    guess, _ = tmpl.attack_key_byte(*make_traces(20, ATTACK_KEY, rng)[:2], byte_index=0)
    print(f"\n================ RESULTS ================")
    print(f"Profiling traces : 3000 (known key {hex(PROFILE_KEY)})")
    print(f"Attack traces    : 20 (unknown key)")
    print(f"Recovered byte   : {hex(guess)}")
    print(f"Expected byte    : {hex(ATTACK_KEY)}")
    print("==========================================")
    assert guess == ATTACK_KEY, "template attack failed to recover the key byte"
    print("[+] OK - a handful of attack traces was enough once the templates were built")
