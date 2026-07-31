# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""WebSocket interop: drive the device WS endpoint with the `websockets` library.

Role: the *device is the server*; this peer is a real RFC 6455 client. The
`websockets` package performs the handshake (Sec-WebSocket-Key/Accept), framing,
masking, ping/pong and close-code handling, so a pass means the device interops
with an independent implementation, not just our own round-trip.
"""

from __future__ import annotations

import asyncio

from ._common import Probe, require

NAME = "ws"
HELP = "probe the device WebSocket server with the websockets client"


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname")
    p.add_argument("--port", type=int, default=80, help="WS port (default 80)")
    p.add_argument("--path", default="/ws", help="WS resource path (default /ws)")
    p.add_argument("--echo", action="store_true", help="expect the server to echo sent frames")
    p.add_argument("--timeout", type=float, default=5.0, help="per-op timeout seconds")


def run(args) -> bool:
    websockets = require("websockets")
    pr = Probe(f"ws {args.host}:{args.port}{args.path}")
    uri = f"ws://{args.host}:{args.port}{args.path}"

    async def go():
        try:
            async with websockets.connect(uri, open_timeout=args.timeout) as ws:
                pr.check("handshake (101 Switching Protocols)", True, uri)
                # A protocol-level ping must draw a pong (RFC 6455 5.5.2/5.5.3).
                pong = await asyncio.wait_for(ws.ping(), timeout=args.timeout)
                await asyncio.wait_for(pong, timeout=args.timeout)
                pr.check("ping -> pong", True)
                if args.echo:
                    await ws.send("interop-text")
                    got = await asyncio.wait_for(ws.recv(), timeout=args.timeout)
                    pr.check("text frame echoed", got == "interop-text", repr(got))
                    payload = b"\x00\x01\x02\xff"
                    await ws.send(payload)
                    got = await asyncio.wait_for(ws.recv(), timeout=args.timeout)
                    pr.check("binary frame echoed", got == payload, repr(got))
                await ws.close(code=1000)
                pr.check("clean close (1000)", ws.close_code in (1000, None), str(ws.close_code))
        except Exception as exc:  # noqa: BLE001
            pr.check("session completed", False, str(exc))

    asyncio.run(go())
    return pr.summary()
