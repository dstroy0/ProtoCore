#!/usr/bin/env python3
# SSH KEX handshake wall-clock probe (perf / FEATURE_PERFORMANCE "SSH KEX handshake wall-clock").
#
# Times a full curve25519-sha256 key exchange from a real OpenSSH client against the PC SSH server, and
# pairs it with the device-side compute spans the rig prints (flash rig_s3_ssh, which defines
# PC_SSH_KEX_BENCH: ssh_transport records the ephemeral-keygen span and the reply span in microseconds and
# main_ssh.cpp prints "KEXBENCH #n gen_us=.. reply_us=.. device_us=.." per completed KEX over its serial).
#
#   python3 performance_benching/ssh/ssh_kex_time.py <server-ip> [runs] [kex-name]
#
# BatchMode=yes: after the KEX the server's auth fails fast (no password hang) - the KEX still completes, so
# we get its timing without authenticating. ssh -vv stderr is timestamped with a monotonic clock and the KEX
# phase boundaries are extracted from the debug markers.
import subprocess, sys, time, statistics

HOST = sys.argv[1] if len(sys.argv) > 1 else "192.168.1.163"
N = int(sys.argv[2]) if len(sys.argv) > 2 else 6
KEX = sys.argv[3] if len(sys.argv) > 3 else "curve25519-sha256"

ARGS = [
    "ssh",
    "-vv",
    "-o",
    "KexAlgorithms=" + KEX,
    "-o",
    "HostKeyAlgorithms=ssh-ed25519",
    "-o",
    "StrictHostKeyChecking=no",
    "-o",
    "UserKnownHostsFile=/dev/null",
    "-o",
    "BatchMode=yes",
    "-o",
    "ConnectTimeout=10",
    "admin@" + HOST,
    "true",
]

MARKS = {
    "Connection established": "tcp",
    "SSH2_MSG_KEXINIT sent": "ki_sent",
    "SSH2_MSG_KEXINIT received": "ki_recv",
    "expecting SSH2_MSG_KEX_ECDH_REPLY": "ecdh_init_sent",
    "SSH2_MSG_NEWKEYS sent": "nk_sent",
    "SSH2_MSG_NEWKEYS received": "nk_recv",
}


def stamp(ts, line):
    for key, lbl in MARKS.items():
        if key in line and lbl not in ts:
            ts[lbl] = time.monotonic()


rows = []
for it in range(N):
    ts = {}
    t0 = time.monotonic()
    p = subprocess.Popen(ARGS, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True, bufsize=1)
    for line in p.stderr:
        stamp(ts, line)
    p.wait()
    t_end = time.monotonic()

    def d(a, b):
        return (ts[b] - ts[a]) * 1000.0 if a in ts and b in ts else float("nan")

    row = {
        "tcp_to_newkeys_ms": d("tcp", "nk_recv"),
        "kexinit_to_newkeys_ms": d("ki_sent", "nk_recv"),  # client-observed full KEX
        "ecdh_reply_wait_ms": d("ecdh_init_sent", "nk_recv"),  # device reply compute + one WiFi round-trip
        "total_proc_ms": (t_end - t0) * 1000.0,
    }
    rows.append(row)
    print(
        "run %d: tcp->newkeys=%.1f  kexinit->newkeys=%.1f  ecdh_reply_wait=%.1f  proc=%.1f"
        % (
            it + 1,
            row["tcp_to_newkeys_ms"],
            row["kexinit_to_newkeys_ms"],
            row["ecdh_reply_wait_ms"],
            row["total_proc_ms"],
        )
    )
    time.sleep(1.0)

print("\n=== aggregate over %d runs (ms) ===" % N)
for k in ("tcp_to_newkeys_ms", "kexinit_to_newkeys_ms", "ecdh_reply_wait_ms", "total_proc_ms"):
    vals = [r[k] for r in rows if r[k] == r[k]]  # drop nan
    if vals:
        print(
            "%-24s min=%.1f  median=%.1f  mean=%.1f  max=%.1f"
            % (k, min(vals), statistics.median(vals), statistics.mean(vals), max(vals))
        )
