"""End-to-end verification: synthesize DaqPacketHeader-framed packets exactly as the
firmware (main.cpp) would emit them, ship them over a real loopback TCP socket into
SideChannelStreamReceiver, and run the full DSP -> TVLA -> SNR -> streaming-CPA ->
full-key-recovery pipeline (dpa_cpa_engine.py) against the result. Exercises the wire
protocol's framing/resync/CRC code paths for real, not just the analysis math.

    python -m tests.simulate_pipeline      (run from the reverse_engineering/ directory)
"""

import socket
import struct
import time

import numpy as np

from dpa_cpa_engine import (
    AES_SBOX,
    HAMMING_WEIGHT,
    TVLA_THRESHOLD,
    IncrementalCPA,
    bandpass_filter,
    generate_hamming_distance_hypotheses,
    recover_full_key,
    signal_to_noise_ratio,
    tvla_t_test,
)
from dpa_cpa_network_engine import (
    HEADER_FORMAT,
    MAGIC,
    MSG_WINDOW,
    PROTO_VERSION,
    SideChannelStreamReceiver,
    crc16_ccitt_false,
    windows_to_volts_matrix,
    windows_with_plaintext,
)
from spa_engine import (
    activity_envelope,
    counts_to_bits,
    decode_periodic_bursts,
    estimate_period,
    segment_activity,
    zero_value_scan,
)

SAMPLE_RATE_HZ = 65_000_000.0  # AD9238-class front end
VREF = 2.0
FULL_SCALE_CODES = 4096  # 12-bit
Y_INCREMENT = VREF / FULL_SCALE_CODES
TIME_SAMPLES = 320
SECRET_KEY = bytes([0xD3, 0x2A, 0x91, 0x5C, 0x7E, 0x08, 0xF1, 0x44, 0xB6, 0x1D, 0x9A, 0x3F, 0x60, 0xC7, 0x27, 0xE5])
LEAK_SAMPLE_BASE = 40  # each of the 16 key bytes leaks at LEAK_SAMPLE_BASE + 16*byte_index


def build_packet(trace_id, codes_u16, y_increment=Y_INCREMENT, y_origin=0.0,
                  pretrigger_samples=0, sample_rate_hz=SAMPLE_RATE_HZ, wall_clock_us=0):
    payload = codes_u16.astype("<u2").tobytes()
    header_no_crc = struct.pack(
        HEADER_FORMAT, MAGIC, PROTO_VERSION, MSG_WINDOW, 0, trace_id, len(codes_u16),
        pretrigger_samples, 2, 1, 0.0, sample_rate_hz, y_increment, y_origin, 0, 1234,
        wall_clock_us, len(payload), 0,
    )
    header_crc = crc16_ccitt_false(header_no_crc[:-2] + b"\x00\x00")
    header = header_no_crc[:-2] + struct.pack("<H", header_crc)
    payload_crc = crc16_ccitt_false(payload)
    return header + payload + struct.pack("<H", payload_crc)


def synthesize_traces(rng, num_traces, secret_key, plaintexts=None, noise_sigma=0.02, leak_gain=0.0035):
    """12-bit ADC codes with an AES-128 Hamming-distance leak for every key byte, buried in
    Gaussian noise - the same synthetic model the original one-shot script used, extended to
    all 16 SubBytes bytes. Pass plaintexts explicitly (e.g. a fixed/random TVLA split) or
    leave it None for a fully random batch (the CPA case)."""
    if plaintexts is None:
        plaintexts = rng.integers(0, 256, size=(num_traces, 16), dtype=np.uint8)
    base_code = FULL_SCALE_CODES / 2
    noise = rng.normal(0, noise_sigma * FULL_SCALE_CODES, size=(num_traces, TIME_SAMPLES))
    for byte_index in range(16):
        intermediate = AES_SBOX[plaintexts[:, byte_index] ^ secret_key[byte_index]] ^ plaintexts[:, byte_index]
        leak = HAMMING_WEIGHT[intermediate] * leak_gain * FULL_SCALE_CODES
        noise[:, LEAK_SAMPLE_BASE + 16 * byte_index] += leak
    # No synthetic jitter injected: align_trace_jitter() is a real, independently-verified
    # cross-correlation aligner (see dpa_cpa_engine.py's smoke test comment), but like any such
    # aligner it needs a common deterministic waveform shape to dominate the noise floor - a
    # bare noise-plus-per-byte-leak model has none, so this models a well-triggered capture
    # instead of exercising alignment against an input it was never meant to handle.
    codes = np.clip(base_code + noise, 0, FULL_SCALE_CODES - 1).astype(np.uint16)
    return codes, plaintexts


def synthesize_spa_trace(rng, bits, period=200, square_offset=20, square_width=30,
                          mult_offset=60, mult_width=30, amplitude_codes=500, noise_sigma=0.01):
    """A single square-and-multiply-shaped trace in 12-bit ADC code space: a fixed 'square'
    burst every period, plus a 'multiply' burst iff the corresponding bit is 1 - the absence
    of that second burst (silence where a multiply would have run) is what encodes a 0."""
    n_samples = period * len(bits)
    base_code = FULL_SCALE_CODES / 2
    trace = rng.normal(0, noise_sigma * FULL_SCALE_CODES, size=n_samples)
    for k, bit in enumerate(bits):
        base = k * period
        trace[base + square_offset: base + square_offset + square_width] += amplitude_codes
        if bit == 1:
            trace[base + mult_offset: base + mult_offset + mult_width] += amplitude_codes * 0.9
    codes = np.clip(base_code + trace, 0, FULL_SCALE_CODES - 1).astype(np.uint16)
    return codes


def run():
    print("[*] reverse_engineering end-to-end pipeline simulation")
    rng = np.random.default_rng(2026)

    NUM_TRACES = 1400
    codes, plaintexts = synthesize_traces(rng, NUM_TRACES, SECRET_KEY)

    plaintext_by_trace = {}
    receiver = SideChannelStreamReceiver(host="127.0.0.1", port=18080,
                                          plaintext_source=lambda tid: plaintext_by_trace.get(tid))
    receiver.start()
    time.sleep(0.2)  # let the listener thread's bind()/listen() land before we connect

    print(f"[*] Streaming {NUM_TRACES} synthetic windows over a real loopback TCP socket...")
    client = socket.create_connection(("127.0.0.1", 18080), timeout=5)
    client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    for i in range(NUM_TRACES):
        plaintext_by_trace[i] = plaintexts[i].tobytes()
        client.sendall(build_packet(i, codes[i]))
    client.close()

    windows = receiver.gather_batch(NUM_TRACES, timeout=10)
    print(f"[+] Received {len(windows)} windows (CRC rejects: {receiver.frames_rejected_crc})")
    assert receiver.frames_rejected_crc == 0, "loopback transport should never corrupt a frame"

    print("[*] Single-trace SPA: one more window, over the same wire protocol, decoded alone...")
    SPA_BITS = [1, 0, 1, 1, 0, 0, 1, 0, 1, 1, 1, 0]
    SPA_PERIOD = 200
    spa_codes = synthesize_spa_trace(rng, SPA_BITS, period=SPA_PERIOD)
    spa_client = socket.create_connection(("127.0.0.1", 18080), timeout=5)
    spa_client.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    SPA_TRACE_ID = NUM_TRACES  # one past the CPA batch's ids; no plaintext_source entry needed
    spa_client.sendall(build_packet(SPA_TRACE_ID, spa_codes))
    spa_client.close()
    (spa_window,) = receiver.gather_batch(1, timeout=10)
    receiver.stop()
    spa_volts = spa_window.volts.astype(np.float64)

    env = activity_envelope(spa_volts, window=9)
    segments, _ = segment_activity(env)
    est_period = estimate_period(env, SPA_PERIOD - 20, SPA_PERIOD + 20)
    counts = decode_periodic_bursts(segments, SPA_PERIOD, len(SPA_BITS))
    decoded_bits = counts_to_bits(counts)
    print(f"    period estimate={est_period} (true {SPA_PERIOD})  decoded={decoded_bits}  true={SPA_BITS}")
    assert decoded_bits == SPA_BITS, "single-trace SPA failed to decode the burst pattern off the wire"

    zero_trace = spa_codes.astype(np.float64).copy()
    zv_base = 6 * SPA_PERIOD  # SPA_BITS[6] == 1: its multiply burst becomes "multiply by zero"
    zero_trace[zv_base + 60: zv_base + 90] = FULL_SCALE_CODES / 2 + rng.normal(0, 5, 30)
    zv_env = activity_envelope(zero_trace, window=9)
    zv_segments, _ = segment_activity(zv_env)
    flagged = zero_value_scan(zero_trace, zv_segments)
    print(f"    zero-value scan flagged {len(flagged)} anomalously quiet burst(s) (expected >= 1)")
    assert len(flagged) >= 1, "zero-value scan failed on the wire-received trace"

    volts, recv_plaintexts = windows_with_plaintext(windows)
    assert volts is not None, "plaintext_source must have resolved every window in this sim"
    print(f"[+] Matrix: {volts.shape[0]} traces x {volts.shape[1]} samples, scaled to volts")

    print("[*] DSP: bandpass filter...")
    filtered = bandpass_filter(volts, SAMPLE_RATE_HZ, 1e6, 20e6)

    print("[*] TVLA (fixed-vs-random leakage detection)...")
    # A real, correctly-labeled fixed-vs-random split (not a slice of the random-plaintext CPA
    # batch above - TVLA needs a genuine "fixed" class: the same plaintext captured repeatedly).
    TVLA_N = 1000
    fixed_pt = np.zeros(16, dtype=np.uint8)
    tvla_plaintexts = np.empty((TVLA_N, 16), dtype=np.uint8)
    tvla_plaintexts[: TVLA_N // 2] = fixed_pt
    tvla_plaintexts[TVLA_N // 2:] = rng.integers(0, 256, size=(TVLA_N - TVLA_N // 2, 16), dtype=np.uint8)
    # A stronger leak_gain than the main CPA batch: TVLA here is a dedicated, independent
    # illustration of leakage *detection* (no key hypothesis, no per-guess correlation to
    # average over), not a rerun of the CPA scenario, so it does not need to match its SNR.
    tvla_codes, tvla_plaintexts = synthesize_traces(rng, TVLA_N, SECRET_KEY, plaintexts=tvla_plaintexts,
                                                      leak_gain=0.02)
    tvla_volts = bandpass_filter(tvla_codes.astype(np.float64) * Y_INCREMENT, SAMPLE_RATE_HZ, 1e6, 20e6)
    t_trace = tvla_t_test(tvla_volts[: TVLA_N // 2], tvla_volts[TVLA_N // 2:])
    print(f"    peak |t| = {np.max(np.abs(t_trace)):.2f} (>4.5 => detectable leakage present)")
    assert np.max(np.abs(t_trace)) > TVLA_THRESHOLD, "TVLA failed to flag the injected leakage"

    print("[*] SNR against byte-0's intermediate state...")
    intermediate0 = AES_SBOX[plaintexts[:, 0] ^ SECRET_KEY[0]] ^ plaintexts[:, 0]
    snr = signal_to_noise_ratio(filtered, intermediate0)
    print(f"    peak SNR = {np.max(snr):.4f} at sample {int(np.argmax(snr))} "
          f"(expected near {LEAK_SAMPLE_BASE})")

    print("[*] Streaming CPA sanity check (byte 0, 4 batches)...")
    cpa = IncrementalCPA(n_samples=filtered.shape[1])
    hyp = generate_hamming_distance_hypotheses(recv_plaintexts, 0)
    batch = len(filtered) // 4
    for b in range(4):
        cpa.update(filtered[b * batch:(b + 1) * batch], hyp[b * batch:(b + 1) * batch])
    guess, sample, corr = cpa.best_guess()
    print(f"    byte 0: recovered={hex(guess)} expected={hex(SECRET_KEY[0])} "
          f"sample={sample} |corr|={corr:.4f}")
    assert guess == SECRET_KEY[0], "streaming CPA failed to recover byte 0"

    print("[*] Full AES-128 key recovery (all 16 bytes)...")
    recovered_key, confidence = recover_full_key(filtered, recv_plaintexts)
    print(f"    recovered : {recovered_key.hex()}")
    print(f"    expected  : {SECRET_KEY.hex()}")
    print(f"    min confidence across bytes: {min(confidence):.4f}")
    assert recovered_key == SECRET_KEY, "full key recovery did not match the injected key"

    print("\n================ PIPELINE OK ================")
    print("Wire protocol, DSP, TVLA, SNR, streaming CPA, full 16-byte key recovery, and single-")
    print("trace SPA (burst-pattern decode + zero-value scan) all verified.")
    print("===============================================")


if __name__ == "__main__":
    run()
