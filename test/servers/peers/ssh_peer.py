# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""SSH interop: complete a full SSH-2 session against the device's server with the reference
implementation, OpenSSH, and prove the modern suite negotiates and data flows encrypted.

Role: the *device is the server* (ConnProto::PROTO_SSH on :22). This peer drives the genuine OpenSSH
client (the authoritative SSH-2 stack) forced onto the device's preferred modern suite -
curve25519-sha256 KEX, ssh-ed25519 host key, chacha20-poly1305@openssh.com record layer - then RFC
4252 password auth and an RFC 4254 shell channel. It proves the device's binary-packet emit path
works end to end, not just the crypto primitives: the KEX-stall bug fixed in v5.95.13 passed every
host unit test because they wired the emit callback themselves; only a real client catches "the server
completes KEX but never actually replies".

With ``--matrix`` it goes further: it reads the algorithms the server actually advertises (from the
OpenSSH ``-vv`` trace) and then forces the client onto **each** one in turn - every KEX, host-key,
cipher and MAC the device offers - so a whole-suite regression (e.g. the v7.23 host-key name-list that
truncated ``rsa-sha2-256`` when all three host keys were loaded) is caught, not just the default tuple.
Testing only what the server advertises means the matrix adapts to how the device is provisioned
(ed25519-only, or all three host keys) instead of false-failing on an algorithm it never offered.

OpenSSH is used rather than a pip client because it is *the* reference implementation and interops
with every cipher/MAC the device offers. mlkem768x25519-sha256 (the PQC hybrid) needs OpenSSH >= 9.9;
older clients simply will not list it as an offered KEX and the matrix skips it. Needs ``ssh`` and
``sshpass`` on PATH (apt install openssh-client sshpass / brew install openssh sshpass). The rig host
key is a throwaway demo key, so it is not checked against known_hosts - the check is the SSH-2 protocol
+ auth + channel, not host trust.
"""

from __future__ import annotations

import shutil
import subprocess
import sys

from ._common import Probe

# Force the device's preferred modern suite so the core check is deterministic (not "whatever OpenSSH
# and the device happen to agree on this release"). The device advertises exactly these first.
_KEX = "curve25519-sha256"
_HOSTKEY = "ssh-ed25519"
_CIPHER = "chacha20-poly1305@openssh.com"
_MARKER = "pc-interop-echo-42"


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname (the SSH server)")
    p.add_argument("--port", type=int, default=22, help="device SSH port (default 22)")
    p.add_argument("--user", default="admin", help="username (default admin)")
    p.add_argument("--password", default="s3cret", help="password (default s3cret)")
    p.add_argument("--timeout", type=float, default=15.0, help="timeout seconds")
    p.add_argument(
        "--matrix",
        action="store_true",
        help="force every KEX/host-key/cipher/MAC the server advertises, one at a time (full interop matrix)",
    )
    p.add_argument(
        "--ssh", default="ssh", help="path to the ssh client (e.g. a locally built OpenSSH >= 9.9 for mlkem)"
    )


NAME = "ssh"
HELP = "complete a full SSH-2 session via OpenSSH (device-as-server); --matrix forces every advertised algorithm"


def _session(ssh, sshpass, args, *, kex=None, hostkey=None, cipher=None, mac=None, verbose="-v"):
    """Run one OpenSSH session forcing the given algorithms; return (trace, stdout, timed_out)."""
    cmd = [sshpass, "-p", args.password, ssh, verbose, "-tt", "-p", str(args.port),
           "-o", "StrictHostKeyChecking=no", "-o", "UserKnownHostsFile=/dev/null",
           "-o", "PubkeyAuthentication=no", "-o", "PreferredAuthentications=password",
           "-o", f"ConnectTimeout={int(args.timeout)}"]  # fmt: skip
    if kex:
        cmd += ["-o", f"KexAlgorithms={kex}"]
    if hostkey:
        cmd += ["-o", f"HostKeyAlgorithms={hostkey}"]
    if cipher:
        cmd += ["-o", f"Ciphers={cipher}"]
    if mac:
        cmd += ["-o", f"MACs={mac}"]
    cmd.append(f"{args.user}@{args.host}")
    try:
        proc = subprocess.run(
            cmd, input=(_MARKER + "\n").encode(), stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=args.timeout
        )
    except subprocess.TimeoutExpired:
        return "", "", True
    return proc.stderr.decode("latin1"), proc.stdout.decode("latin1"), False


def run(args) -> bool:
    pr = Probe(f"ssh device={args.host}:{args.port}")
    ssh = shutil.which(args.ssh) or shutil.which("ssh")
    sshpass = shutil.which("sshpass")
    if not ssh or not sshpass:
        missing = ", ".join(n for n, v in (("ssh", ssh), ("sshpass", sshpass)) if not v)
        sys.stderr.write(
            f"error: missing native dependency for the ssh peer ({missing}).\n"
            f"       install: apt install openssh-client sshpass  (or brew install openssh sshpass)\n"
        )
        sys.exit(2)

    # --- core check: the default modern tuple completes a full session ------------------------------
    trace, out, timed_out = _session(ssh, sshpass, args, kex=_KEX, hostkey=_HOSTKEY, cipher=_CIPHER, verbose="-vv")
    if timed_out:
        pr.check("SSH session completes within the timeout", False, f"timed out after {args.timeout}s")
        return pr.summary()

    def traced(needle: str) -> bool:
        return needle in trace

    pr.check(f"KEX negotiated {_KEX}", traced(f"kex: algorithm: {_KEX}"), _grep1(trace, "kex: algorithm:"))
    pr.check(f"host key is {_HOSTKEY}", traced(f"kex: host key algorithm: {_HOSTKEY}"),
             _grep1(trace, "kex: host key algorithm:"))  # fmt: skip
    pr.check(f"server->client cipher is {_CIPHER}", traced(f"server->client cipher: {_CIPHER}"),
             _grep1(trace, "server->client cipher:"))  # fmt: skip
    pr.check("NEWKEYS: encryption activated both directions", traced("SSH2_MSG_NEWKEYS received"), "")
    pr.check("password auth accepted (RFC 4252)",
             "Authenticated to " in trace and 'using "password"' in trace, _grep1(trace, "Authenticated to"))  # fmt: skip
    pr.check("channel data echoes back over the encrypted session (RFC 4254 CHANNEL_DATA)",
             _MARKER in out, out.replace("\n", " ")[:48].strip())  # fmt: skip

    # --- optional full matrix: force every algorithm the server advertises --------------------------
    if args.matrix:
        offer = _server_offer(trace)
        pr.info("server advertises: kex=[{kex}] hostkey=[{hostkey}] cipher=[{cipher}] mac=[{mac}]".format(**offer))
        client_kex = set(_client_supported(ssh))
        for kex in offer["kex"].split(","):
            if kex in ("ext-info-s", "kex-strict-s-v00@openssh.com") or not kex:
                continue
            if kex not in client_kex:
                pr.info(f"skip kex {kex} (this OpenSSH client does not implement it)")
                continue
            _matrix_check(pr, ssh, sshpass, args, f"KEX {kex}", "kex: algorithm: " + kex, kex=kex, hostkey=_HOSTKEY,
                          cipher=_CIPHER)  # fmt: skip
        for hk in offer["hostkey"].split(","):
            if hk:
                _matrix_check(pr, ssh, sshpass, args, f"host key {hk}", "kex: host key algorithm: " + hk,
                              kex=_KEX, hostkey=hk, cipher="aes256-ctr")  # fmt: skip
        for c in offer["cipher"].split(","):
            if c:
                _matrix_check(pr, ssh, sshpass, args, f"cipher {c}", "server->client cipher: " + c,
                              kex=_KEX, hostkey=_HOSTKEY, cipher=c)  # fmt: skip
        for m in offer["mac"].split(","):
            if m:  # MACs only apply to the non-AEAD cipher
                _matrix_check(pr, ssh, sshpass, args, f"mac {m}", None, kex=_KEX, hostkey=_HOSTKEY,
                              cipher="aes256-ctr", mac=m)  # fmt: skip

    return pr.summary()


def _matrix_check(pr, ssh, sshpass, args, label, neg_needle, **algos) -> None:
    """Force one algorithm, require the session to complete (KEX+auth+echo) and, if given, that the
    requested algorithm was the one actually negotiated."""
    trace, out, timed_out = _session(ssh, sshpass, args, **algos)
    if timed_out:
        pr.check(label, False, "timed out")
        return
    ok = (_MARKER in out) and ("Authenticated to " in trace)
    if neg_needle:
        ok = ok and (neg_needle in trace)
    detail = "echo+auth" if ok else _grep1(trace, "Unable to negotiate") or "no echo"
    pr.check(label, ok, detail)


def _server_offer(trace: str) -> dict:
    """Extract the server's advertised algorithm lists from an OpenSSH -vv trace. After the line
    'peer server KEXINIT proposal' OpenSSH prints the server's KEX/host-key/cipher/MAC lists."""
    out = {"kex": "", "hostkey": "", "cipher": "", "mac": ""}
    lines = trace.splitlines()
    for i, line in enumerate(lines):
        if "peer server KEXINIT proposal" not in line:
            continue
        for nxt in lines[i + 1 : i + 12]:
            body = nxt.split("debug2: ")[-1]
            if body.startswith("KEX algorithms:"):
                out["kex"] = body.split(":", 1)[1].strip()
            elif body.startswith("host key algorithms:"):
                out["hostkey"] = body.split(":", 1)[1].strip()
            elif body.startswith("ciphers stoc:") and not out["cipher"]:
                out["cipher"] = body.split(":", 1)[1].strip()
            elif body.startswith("MACs stoc:") and not out["mac"]:
                out["mac"] = body.split(":", 1)[1].strip()
        break
    return out


def _client_supported(ssh: str) -> list:
    """KEX algorithms this OpenSSH client implements (so the matrix skips e.g. mlkem on ssh < 9.9)."""
    try:
        r = subprocess.run([ssh, "-Q", "kex"], stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, timeout=10)
        return r.stdout.decode("latin1").split()
    except (subprocess.SubprocessError, FileNotFoundError):
        return []


def _grep1(trace: str, needle: str) -> str:
    for line in trace.splitlines():
        if needle in line:
            return line.split("debug1: ")[-1].split("debug2: ")[-1].strip()
    return ""
