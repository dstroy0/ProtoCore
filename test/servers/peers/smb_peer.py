# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""SMB2 client interop: drive the device as an SMB2 client against a real samba (stdlib + smbclient).

Role: the *device is the client*. This peer needs a real SMB2 server reachable on the LAN (e.g. the RPi
test samba, dperson/samba, share `programs`, user `cnc`). It first reads the target file with a genuine
third-party SMB client (`smbclient`) to get the authoritative bytes, then triggers the device (via the
rig's GET /smb/probe route) to run the full SMB2 dialogue - NEGOTIATE -> NTLMv2 SESSION_SETUP ->
TREE_CONNECT -> CREATE -> READ -> CLOSE - and confirms the device read the *same content* (FNV-1a match).
That closes the loop: the device's SMB2 + NTLMv2 + SPNEGO wire encoders/parsers interoperate with a real
Windows-compatible server, byte-for-byte.

  1. smbclient reads //server/share/path  -> authoritative bytes + FNV
  2. trigger GET /smb/probe on the device  -> {connected, open, read, size, fnv, close}
  3. the device's FNV == smbclient's FNV (the device read the genuine file over real SMB2)

Start a server first, e.g. (the RPi test rig):
  docker run -it -p 4445:445 -v /srv/smb:/share dperson/samba -u "cnc;secretpassword" \\
      -s "programs;/share;yes;no;no;cnc;cnc"
"""

from __future__ import annotations

import http.client
import json
import os
import socket
import subprocess
import tempfile

from ._common import Probe

NAME = "smb"
HELP = "drive the device as an SMB2 client against a real samba server (device-as-client)"


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname (the rig)")
    p.add_argument("--port", type=int, default=80, help="device HTTP port (default 80)")
    p.add_argument("--smb-host", help="samba address the device dials (default: this machine's IP)")
    p.add_argument("--smb-port", type=int, default=4445, help="samba port (default 4445, the RPi test samba)")
    p.add_argument("--user", default="cnc", help="samba user (default cnc, the dperson/samba test account)")
    p.add_argument("--password", default="secretpassword", help="samba password (throwaway test cred)")
    p.add_argument("--share", default="programs", help="share name (default programs)")
    p.add_argument("--path", default="test.txt", help="file to read from the share (default test.txt)")
    p.add_argument("--timeout", type=float, default=20.0, help="timeout seconds")


def _fnv1a(data: bytes) -> int:
    """FNV-1a 32-bit, matching the rig's /smb/probe checksum of the read bytes."""
    h = 2166136261
    for byte in data:
        h = ((h ^ byte) * 16777619) & 0xFFFFFFFF
    return h


def _smbclient_read(args, smb_host: str) -> bytes | None:
    """Independently read the same file with a genuine third-party SMB client (smbclient)."""
    try:
        with tempfile.TemporaryDirectory() as td:
            out = os.path.join(td, "f")
            r = subprocess.run(
                [
                    "smbclient",
                    f"//{smb_host}/{args.share}",
                    "-U",
                    f"{args.user}%{args.password}",
                    "-p",
                    str(args.smb_port),
                    "-c",
                    f"get {args.path} {out}",
                ],
                capture_output=True,
                timeout=args.timeout,
            )
            if r.returncode == 0 and os.path.exists(out):
                with open(out, "rb") as fh:
                    return fh.read()
    except (FileNotFoundError, subprocess.SubprocessError, OSError):
        pass
    return None


def run(args) -> bool:
    # Figure out the samba address the device should dial (this machine's IP on the route to the device).
    smb_host = args.smb_host
    if not smb_host:
        try:
            s = socket.create_connection((args.host, args.port), timeout=args.timeout)
            smb_host = s.getsockname()[0]
            s.close()
        except OSError as exc:  # noqa: BLE001
            pr = Probe(f"smb device={args.host}")
            pr.check("reach the device", False, str(exc))
            return pr.summary()

    pr = Probe(f"smb device={args.host} server={smb_host}:{args.smb_port}/{args.share}")

    # 1. the authoritative third-party read: smbclient pulls the same file over real SMB2.
    expected = _smbclient_read(args, smb_host)
    pr.check(
        "samba reachable + file read (smbclient)",
        expected is not None,
        f"{len(expected)}B" if expected is not None else "smbclient failed - is the samba up + file present?",
    )
    if expected is None:
        return pr.summary()
    exp_fnv = _fnv1a(expected)

    # 2. trigger the device to run the full SMB2 dialogue and read the same file.
    try:
        c = http.client.HTTPConnection(args.host, args.port, timeout=args.timeout)
        q = (
            f"/smb/probe?host={smb_host}&port={args.smb_port}&user={args.user}"
            f"&pass={args.password}&share={args.share}&path={args.path}"
        )
        c.request("GET", q)
        r = c.getresponse()
        body = r.read()
        c.close()
        rep = json.loads(body)
    except Exception as exc:  # noqa: BLE001
        pr.check("device /smb/probe completed", False, str(exc))
        return pr.summary()

    pr.check("device connected to samba", rep.get("connected") == 1, json.dumps(rep))
    pr.check("NEGOTIATE+SESSION_SETUP+TREE+CREATE ok (open=0)", rep.get("open") == 0, f"open={rep.get('open')}")
    pr.check(
        "device READ full file",
        rep.get("read") == len(expected) and rep.get("size") == len(expected),
        f"read={rep.get('read')} size={rep.get('size')} expected={len(expected)}",
    )
    pr.check("READ status ok (read_rc=0)", rep.get("read_rc") == 0, f"read_rc={rep.get('read_rc')}")
    # 3. byte-verified interop: the device's read content matches the third-party smbclient read exactly.
    pr.check(
        "device content == smbclient content (FNV-1a)",
        rep.get("fnv") == exp_fnv,
        f"device={rep.get('fnv')} smbclient={exp_fnv}",
    )
    pr.check("CLOSE ok (close=0)", rep.get("close") == 0, f"close={rep.get('close')}")

    return pr.summary()
