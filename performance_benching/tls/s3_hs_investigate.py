#!/usr/bin/env python3
# TLS handshake curve investigation vs a PC HTTPS rig. For each ECDHE group, times the handshake with
# DEFINITIVE curve control (openssl -groups) and reports the group the server actually used, plus a TCP
# baseline to separate crypto from network. Pairs with the server-side PC_TLS_HS_BENCH probe (device CPU
# vs wall) - see docs/FEATURE_PERFORMANCE.md "Device-CPU breakdown".
#
#   python3 performance_benching/tls/s3_hs_investigate.py <server-ip>
#
# Note: a P-256 ECDSA leaf constrains the client's supported_groups in TLS 1.2 (the group list gates the
# cert curve too), so a run that offers no P-256 fails outright - that is expected, not a bug.
import subprocess, socket, time, statistics, sys

HOST = sys.argv[1] if len(sys.argv) > 1 else "192.168.1.163"
N = 15


def openssl_hs(group):
    stk, times = "", []
    for _ in range(N):
        t0 = time.monotonic()
        p = subprocess.run(
            ["openssl", "s_client", "-connect", "%s:443" % HOST, "-tls1_2", "-groups", group],
            input=b"",
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            timeout=15,
        )
        times.append((time.monotonic() - t0) * 1000)
        if not stk:
            for line in p.stdout.decode(errors="ignore").splitlines():
                if "Server Temp Key" in line:
                    stk = line.strip()
    return min(times), statistics.median(times), stk


def tcp_baseline():
    times = []
    for _ in range(N):
        t0 = time.monotonic()
        s = socket.create_connection((HOST, 443), timeout=10)
        times.append((time.monotonic() - t0) * 1000)
        s.close()
    return min(times), statistics.median(times)


print("host:", HOST)
b_min, b_med = tcp_baseline()
print("TCP connect baseline:      min=%.1f  median=%.1f ms" % (b_min, b_med))
print("(openssl times include ~process-startup + TCP; the min and the CURVE DELTA are what matter)\n")
for g in ("X25519", "P-256", "P-521"):
    mn, md, stk = openssl_hs(g)
    print("openssl -groups %-7s min=%6.1f  median=%6.1f ms   [%s]" % (g, mn, md, stk or "handshake did not complete"))
