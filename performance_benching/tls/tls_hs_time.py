#!/usr/bin/env python3
# TLS handshake wall-clock probe vs the PC HTTPS server (rig_s3_tls). Reproduces the number in
# docs/FEATURE_PERFORMANCE.md "TLS handshake": times the TLS handshake alone (TCP connect excluded) from a
# real client, and reads the device-side ECC decomposition the rig already exposes at /bench/tls.
#
#   python3 performance_benching/tls/tls_hs_time.py <server-ip> [runs]
#
# The rig terminates TLS from its 48 KB static arena on the stock (no-PSRAM) core, so no special build is
# needed - flash rig_s3_tls and point this at RIG_IP. The self-signed test cert is not verified here (the
# measurement is of the handshake cost, not the trust decision). Prints the negotiated suite + group.
import socket, ssl, sys, time, statistics, urllib.request, json

HOST = sys.argv[1] if len(sys.argv) > 1 else "192.168.1.163"
N = int(sys.argv[2]) if len(sys.argv) > 2 else 8

# device-side ECC decomposition (CCOUNT us/op) from the rig's /bench/tls endpoint over HTTP/80
try:
    with urllib.request.urlopen("http://%s/bench/tls" % HOST, timeout=10) as r:
        print("device /bench/tls (us/op):", json.dumps(json.load(r)))
except Exception as e:
    print("device /bench/tls failed:", e)

ctx = ssl._create_unverified_context()  # self-signed test cert - measuring handshake cost, not trust
ctx.check_hostname = False
ctx.verify_mode = ssl.CERT_NONE

rows = []
info = None
for it in range(N):
    try:
        raw = socket.create_connection((HOST, 443), timeout=10)
        t1 = time.monotonic()
        s = ctx.wrap_socket(raw, server_hostname=HOST, do_handshake_on_connect=False)
        s.do_handshake()
        t2 = time.monotonic()
        if info is None:
            try:
                grp = s.group()  # py3.10+: negotiated key-exchange group
            except Exception:
                grp = "n/a (use: openssl s_client -connect host:443 | grep 'Server Temp Key')"
            info = (s.version(), s.cipher(), grp)
        s.close()
        rows.append((t2 - t1) * 1000.0)
        print("run %d: tls_handshake=%.1f ms" % (it + 1, (t2 - t1) * 1000.0))
    except Exception as e:
        print("run %d failed: %s" % (it + 1, e))
    time.sleep(1.0)

if info:
    print("\nnegotiated:", info[0], "| cipher:", info[1], "| group:", info[2])
if rows:
    print("=== TLS handshake (ms) over %d runs ===" % len(rows))
    print("min=%.1f  median=%.1f  mean=%.1f  max=%.1f"
          % (min(rows), statistics.median(rows), statistics.mean(rows), max(rows)))
