# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Redis (RESP) interop: drive the device as a Redis client against a real redis-server (stdlib).

Role: the *device is the client*. This peer needs a real `redis-server` reachable on the LAN; it then
triggers the device (via the rig's GET /redis/probe?host=&port= route) to connect out and run
PING / SET / GET, and independently confirms - as the authoritative third party, over a raw RESP socket -
that the device's SET actually landed on the server. That closes the loop: the device's RESP encoder +
reply parser interoperate with the genuine Redis wire protocol.

  1. reach the redis-server (raw PING -> +PONG)
  2. trigger GET /redis/probe on the device -> {connected, ping, set, get}
  3. GET pc:rig on the server == "hello" (the device's SET is visible to a real client)

Start a server first, e.g.:  redis-server --bind 0.0.0.0 --protected-mode no
"""

from __future__ import annotations

import http.client
import json
import socket

from ._common import Probe

NAME = "redis"
HELP = "drive the device as a Redis client against a real redis-server (device-as-client)"


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname (the rig)")
    p.add_argument("--port", type=int, default=80, help="device HTTP port (default 80)")
    p.add_argument("--redis-host", help="redis-server address the device dials (default: this machine's IP)")
    p.add_argument("--redis-port", type=int, default=6379, help="redis-server port (default 6379)")
    p.add_argument("--timeout", type=float, default=8.0, help="timeout seconds")


def _resp_cmd(sock, *args) -> bytes:
    cmd = f"*{len(args)}\r\n".encode()
    for a in args:
        b = a.encode() if isinstance(a, str) else a
        cmd += b"$" + str(len(b)).encode() + b"\r\n" + b + b"\r\n"
    sock.sendall(cmd)
    return sock.recv(4096)


def run(args) -> bool:
    pr = Probe(f"redis device={args.host} server={args.redis_host or 'auto'}:{args.redis_port}")

    # Figure out the redis address the device should dial (this machine's IP on the route to the device).
    redis_host = args.redis_host
    if not redis_host:
        try:
            s = socket.create_connection((args.host, args.port), timeout=args.timeout)
            redis_host = s.getsockname()[0]
            s.close()
        except OSError as exc:  # noqa: BLE001
            pr.check("reach the device", False, str(exc))
            return pr.summary()

    # 1. the redis-server must be reachable (this is the external peer).
    try:
        rs = socket.create_connection((redis_host, args.redis_port), timeout=args.timeout)
        rs.settimeout(args.timeout)
        pong = _resp_cmd(rs, "PING")
        pr.check("redis-server reachable (PING)", pong.startswith(b"+PONG"), pong[:16].decode("latin1", "replace"))
        _resp_cmd(rs, "DEL", "pc:rig")  # clean slate so the SET check is meaningful
    except OSError as exc:  # noqa: BLE001
        pr.check("redis-server reachable", False, f"{exc} - start: redis-server --bind 0.0.0.0 --protected-mode no")
        return pr.summary()

    # 2. trigger the device to run PING / SET / GET against that server.
    try:
        c = http.client.HTTPConnection(args.host, args.port, timeout=args.timeout)
        c.request("GET", f"/redis/probe?host={redis_host}&port={args.redis_port}")
        r = c.getresponse()
        body = r.read()
        c.close()
        rep = json.loads(body)
        pr.check("device connected to redis", rep.get("connected") == 1, json.dumps(rep))
        pr.check("device PING -> PONG", rep.get("ping") == 1, f"ping={rep.get('ping')}")
        pr.check("device SET accepted", rep.get("set") == 1, f"set={rep.get('set')}")
        pr.check("device GET returned the value", rep.get("get") == 1, f"get={rep.get('get')}")
    except Exception as exc:  # noqa: BLE001
        pr.check("device /redis/probe completed", False, str(exc))
        rs.close()
        return pr.summary()

    # 3. the authoritative check: the device's SET is visible to a real client on the real server.
    try:
        got = _resp_cmd(rs, "GET", "pc:rig")
        pr.check("device's SET landed on the server", b"hello" in got, got[:24].decode("latin1", "replace"))
    except OSError as exc:  # noqa: BLE001
        pr.check("server-side GET", False, str(exc))
    finally:
        rs.close()

    return pr.summary()
