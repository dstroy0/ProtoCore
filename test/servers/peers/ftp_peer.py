# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""FTP (RFC 959 + RFC 2428/3659) interop: drive the device as an FTP client against a real pyftpdlib server.

Role: the *device is the client*. This peer stands up a genuine `pyftpdlib` FTP server (the reference
implementation) in a background thread with a writable home directory, then triggers the device (via the
rig's GET /ftp/probe?host=&port=&user=&pass= route) to log in, negotiate passive mode, and STOR a file.
It then confirms - as the authoritative third party, by reading the server's home directory - that the
device's upload actually landed byte-for-byte. That closes the loop: the device's command builder + reply
parser + PASV decoder interoperate with the genuine FTP wire protocol.

  1. start pyftpdlib on 0.0.0.0 (user pc / pc123, writable home)
  2. trigger GET /ftp/probe on the device -> {greet, user, pass, type, pasv, stor, size}
  3. the uploaded file exists in the server home and its bytes match what the device sent

No external server needed - the reference peer is self-contained (pip install pyftpdlib).
"""

from __future__ import annotations

import http.client
import json
import logging
import os
import socket
import tempfile
import threading

from ._common import Probe, require, wait_until

NAME = "ftp"
HELP = "drive the device as an FTP client against a real pyftpdlib server (device-as-client)"

# Must match the rig's h_ftp_probe upload (penetration_testing/rig_firmware/src/main.cpp: FTP_UPLOAD).
_EXPECT = b"pc-ftp-rig-upload\n"
_UPLOAD_NAME = "pc_rig.txt"


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname (the rig)")
    p.add_argument("--port", type=int, default=80, help="device HTTP port (default 80)")
    p.add_argument("--ftp-host", help="address the device dials for FTP (default: this machine's route IP)")
    p.add_argument("--ftp-port", type=int, default=2121, help="FTP control port to serve (default 2121)")
    p.add_argument("--user", default="pc", help="FTP username (default pc)")
    p.add_argument("--passwd", default="pc123", help="FTP password (default pc123)")
    p.add_argument("--timeout", type=float, default=10.0, help="timeout seconds")


def _start_server(homedir: str, port: int, user: str, passwd: str):
    """Start a pyftpdlib FTPServer in a daemon thread; return (server, thread)."""
    authorizers = require("pyftpdlib.authorizers")
    handlers = require("pyftpdlib.handlers")
    servers = require("pyftpdlib.servers")

    logging.getLogger("pyftpdlib").setLevel(logging.ERROR)  # keep the interop output clean

    auth = authorizers.DummyAuthorizer()
    auth.add_user(user, passwd, homedir=homedir, perm="elradfmw")  # full read/write/store
    handler = handlers.FTPHandler
    handler.authorizer = auth
    handler.passive_ports = range(60000, 60020)  # a small deterministic PASV range (flat LAN, no masquerade)
    server = servers.FTPServer(("0.0.0.0", port), handler)
    thread = threading.Thread(target=server.serve_forever, kwargs={"timeout": 0.2}, daemon=True)
    thread.start()
    return server, thread


def run(args) -> bool:
    pr = Probe(f"ftp device={args.host} server=:{args.ftp_port} user={args.user}")

    # Figure out the address the device should dial (this machine's IP on the route to the device).
    ftp_host = args.ftp_host
    if not ftp_host:
        try:
            s = socket.create_connection((args.host, args.port), timeout=args.timeout)
            ftp_host = s.getsockname()[0]
            s.close()
        except OSError as exc:  # noqa: BLE001
            pr.check("reach the device", False, str(exc))
            return pr.summary()

    homedir = tempfile.mkdtemp(prefix="pc-ftp-")
    try:
        server, _thread = _start_server(homedir, args.ftp_port, args.user, args.passwd)
    except OSError as exc:  # noqa: BLE001 - e.g. port already bound
        pr.check("start pyftpdlib server", False, str(exc))
        return pr.summary()

    try:
        pr.info(f"pyftpdlib serving {ftp_host}:{args.ftp_port} home={homedir}")

        # 2. trigger the device to log in + PASV + STOR against that server.
        try:
            q = f"/ftp/probe?host={ftp_host}&port={args.ftp_port}&user={args.user}&pass={args.passwd}"
            c = http.client.HTTPConnection(args.host, args.port, timeout=args.timeout)
            c.request("GET", q)
            r = c.getresponse()
            body = r.read()
            c.close()
            rep = json.loads(body)
        except Exception as exc:  # noqa: BLE001
            pr.check("device /ftp/probe completed", False, str(exc))
            return pr.summary()

        pr.check("device connected to FTP", rep.get("connected") == 1, json.dumps(rep))
        pr.check("device got 220 greeting", rep.get("greet") == 220, f"greet={rep.get('greet')}")
        pr.check("device USER accepted", rep.get("user") == 1, f"user={rep.get('user')}")
        pr.check("device PASS -> logged in", rep.get("pass") == 1, f"pass={rep.get('pass')}")
        pr.check("device TYPE I accepted", rep.get("type") == 1, f"type={rep.get('type')}")
        pr.check("device parsed 227 PASV address", rep.get("pasv") == 1, f"pasv={rep.get('pasv')}")
        pr.check("device STOR -> 226 complete", rep.get("stor") == 1, f"stor={rep.get('stor')}")
        pr.check("device sent the payload", rep.get("sent") == len(_EXPECT), f"sent={rep.get('sent')}")
        pr.check("server SIZE confirms the file (213)", rep.get("size") == 1, f"size={rep.get('size')}")

        # 3. the authoritative check: the upload is present in the server home with the exact bytes.
        path = os.path.join(homedir, _UPLOAD_NAME)
        landed = wait_until(lambda: os.path.exists(path), 3.0)
        pr.check("upload landed in the server home", landed, path)
        if landed:
            with open(path, "rb") as f:
                got = f.read()
            pr.check("uploaded bytes match", got == _EXPECT, f"{len(got)}B {got[:24]!r}")
    finally:
        server.close_all()
        try:
            os.remove(os.path.join(homedir, _UPLOAD_NAME))
        except OSError:
            pass
        try:
            os.rmdir(homedir)
        except OSError:
            pass

    return pr.summary()
