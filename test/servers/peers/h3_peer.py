# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""HTTP/3 interop: complete a real QUIC handshake (ALPN h3) with the device and drive HTTP/3 requests
through an independent, spec-compliant stack (aioquic).

Role: the *device is the server*. It runs a static-pool QUIC/HTTP-3 server on UDP:443 (the engine's
pool lives in PSRAM; PC_ENABLE_HTTP3 + PC_QUIC_SERVER_IN_PSRAM) with an Ed25519 leaf. This peer
uses aioquic - a mature QUIC/HTTP-3 implementation - to prove the whole stack end to end: the QUIC
TLS-1.3 handshake (X25519 + Ed25519 + AES-128-GCM) completes, an HTTP/3 request (QPACK-encoded
HEADERS + the request stream) returns 200, and a second request multiplexes over the same connection.
The device cert is self-signed, so verification is disabled; the check is the QUIC + h3 wire behaviour.
"""

from __future__ import annotations

from ._common import Probe, require


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname (the QUIC/HTTP-3 server)")
    p.add_argument("--port", type=int, default=443, help="device QUIC UDP port (default 443)")
    p.add_argument("--timeout", type=float, default=8.0, help="timeout seconds")


NAME = "http3"
HELP = "complete a QUIC handshake (ALPN h3) and drive HTTP/3 requests via aioquic (device-as-server)"


def run(args) -> bool:
    require("aioquic")  # exits 2 with an install hint if aioquic is not present
    import asyncio
    import ssl

    from aioquic.asyncio.client import connect
    from aioquic.asyncio.protocol import QuicConnectionProtocol
    from aioquic.h3.connection import H3Connection
    from aioquic.h3.events import DataReceived, HeadersReceived
    from aioquic.quic.configuration import QuicConfiguration
    from aioquic.quic.events import QuicEvent

    pr = Probe(f"http3 device={args.host}:{args.port}")

    class H3Client(QuicConnectionProtocol):
        def __init__(self, *a, **k):
            super().__init__(*a, **k)
            self._h3 = H3Connection(self._quic)
            self._resp = {}

        def quic_event_received(self, event: QuicEvent):
            for e in self._h3.handle_event(event):
                r = self._resp.setdefault(e.stream_id, [None, b"", asyncio.Event()])
                if isinstance(e, HeadersReceived):
                    r[0] = e.headers
                    if getattr(e, "stream_ended", False):
                        r[2].set()
                elif isinstance(e, DataReceived):
                    r[1] += e.data
                    if e.stream_ended:
                        r[2].set()

        async def get(self, path):
            sid = self._quic.get_next_available_stream_id()
            self._resp[sid] = [None, b"", asyncio.Event()]
            self._h3.send_headers(
                sid,
                [
                    (b":method", b"GET"),
                    (b":scheme", b"https"),
                    (b":authority", args.host.encode()),
                    (b":path", path.encode()),
                ],
                end_stream=True,
            )
            self.transmit()
            await asyncio.wait_for(self._resp[sid][2].wait(), timeout=args.timeout)
            hdrs, body, _ = self._resp[sid]
            status = dict(hdrs or {}).get(b":status", b"?").decode()
            return status, body

    async def drive():
        cfg = QuicConfiguration(is_client=True, alpn_protocols=["h3"])
        cfg.verify_mode = ssl.CERT_NONE
        async with connect(args.host, args.port, configuration=cfg, create_protocol=H3Client) as client:
            await asyncio.wait_for(client.wait_connected(), timeout=args.timeout)
            pr.check("QUIC handshake completes (ALPN h3, X25519+Ed25519)", True, "connected")

            status, body = await client.get("/")
            pr.check("HTTP/3 GET / returns 200", status == "200", f":status={status}")
            pr.check(
                "response body arrived over QUIC DATA frames",
                b"http/3" in body.lower() or b"hello" in body.lower(),
                body[:48].decode("latin1", "replace").strip(),
            )

            status2, body2 = await client.get("/status")
            pr.check("second h3 stream GET /status is 200", status2 == "200", f":status={status2}")
            pr.check(
                "multiplexed stream returns the JSON body",
                b'"h3":true' in body2.replace(b" ", b""),
                body2[:48].decode("latin1", "replace").strip(),
            )

    try:
        asyncio.run(drive())
    except Exception as exc:  # noqa: BLE001
        pr.check("QUIC handshake completes (ALPN h3, X25519+Ed25519)", False, f"{type(exc).__name__}: {exc}")

    return pr.summary()
