# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""NATS interop: drive the device as a NATS client against a real nats-server (stdlib).

Role: the *device is the client*. This peer needs a real `nats-server` reachable on the LAN. It subscribes
(as an independent NATS client, raw text protocol over a socket) to the device's subject, triggers the
device (via the rig's GET /nats/probe route) to connect out and SUB/PUB to that subject, and confirms - as
the authoritative third party through the real broker - that the device's published message is delivered.
That closes the loop: the device's CONNECT/SUB/PUB builder + INFO/MSG parser interoperate with genuine NATS.

  1. reach nats-server (read the INFO greeting), CONNECT + SUB the subject
  2. trigger GET /nats/probe on the device -> {info, connect, sub, pub, msg}
  3. the device's PUB arrives at this peer through the broker (server-side confirmation)

Start a server first, e.g.:  nats-server -a 0.0.0.0 -p 4222   (binary at /usr/sbin/nats-server)
"""

from __future__ import annotations

import http.client
import json
import socket
import time

from ._common import Probe

NAME = "nats"
HELP = "drive the device as a NATS client against a real nats-server (device-as-client)"

_PAYLOAD = b"hello-from-pc-rig"  # must match the rig's h_nats_probe PUB payload


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname (the rig)")
    p.add_argument("--port", type=int, default=80, help="device HTTP port (default 80)")
    p.add_argument("--nats-host", help="nats-server address the device dials (default: this machine's IP)")
    p.add_argument("--nats-port", type=int, default=4222, help="nats-server port (default 4222)")
    p.add_argument("--subject", default="pc.rig", help="subject to bridge (default pc.rig)")
    p.add_argument("--timeout", type=float, default=8.0, help="timeout seconds")


def _recv_until(sock, needle: bytes, deadline: float) -> bytes:
    """Read until `needle` appears or the deadline passes; answer a server PING with PONG."""
    buf = b""
    while time.time() < deadline:
        try:
            d = sock.recv(4096)
        except socket.timeout:
            break
        except OSError:
            break
        if not d:
            break
        buf += d
        if b"PING\r\n" in buf:
            try:
                sock.sendall(b"PONG\r\n")
            except OSError:
                pass
        if needle in buf:
            break
    return buf


def run(args) -> bool:
    pr = Probe(f"nats device={args.host} server={args.nats_host or 'auto'}:{args.nats_port}")

    nats_host = args.nats_host
    if not nats_host:
        try:
            s = socket.create_connection((args.host, args.port), timeout=args.timeout)
            nats_host = s.getsockname()[0]
            s.close()
        except OSError as exc:  # noqa: BLE001
            pr.check("reach the device", False, str(exc))
            return pr.summary()

    # 1. reach the real nats-server and subscribe (raw NATS text protocol).
    try:
        ns = socket.create_connection((nats_host, args.nats_port), timeout=args.timeout)
        ns.settimeout(args.timeout)
        info = ns.recv(4096)
        pr.check("nats-server reachable (INFO)", info.startswith(b"INFO "), info[:24].decode("latin1", "replace"))
        ns.sendall(b'CONNECT {"verbose":false,"name":"pc-interop-peer"}\r\n')
        ns.sendall(f"SUB {args.subject} 99\r\n".encode())
        ns.sendall(b"PING\r\n")
        _recv_until(ns, b"PONG\r\n", time.time() + args.timeout)  # flush the SUB, confirm the session is live
    except OSError as exc:  # noqa: BLE001
        pr.check("nats-server reachable", False, f"{exc} - start: nats-server -a 0.0.0.0 -p {args.nats_port}")
        return pr.summary()

    # 2. trigger the device to connect out + SUB/PUB to the subject.
    try:
        q = f"/nats/probe?host={nats_host}&port={args.nats_port}&subject={args.subject}"
        c = http.client.HTTPConnection(args.host, args.port, timeout=args.timeout)
        c.request("GET", q)
        r = c.getresponse()
        rep = json.loads(r.read())
        c.close()
        pr.check("device connected to NATS", rep.get("connected") == 1, json.dumps(rep))
        pr.check("device read the INFO greeting", rep.get("info") == 1, f"info={rep.get('info')}")
        pr.check("device SUB accepted", rep.get("sub") == 1, f"sub={rep.get('sub')}")
        pr.check("device PUB accepted", rep.get("pub") == 1, f"pub={rep.get('pub')}")
        pr.check("device received its own MSG back", rep.get("msg") == 1, f"msg={rep.get('msg')}")
    except Exception as exc:  # noqa: BLE001
        pr.check("device /nats/probe completed", False, str(exc))
        ns.close()
        return pr.summary()

    # 3. the authoritative check: the device's PUB reached this independent subscriber through the broker.
    try:
        data = _recv_until(ns, _PAYLOAD, time.time() + args.timeout)
        pr.check("device's PUB delivered to the broker", _PAYLOAD in data, data[-48:].decode("latin1", "replace"))
    finally:
        ns.close()

    return pr.summary()
