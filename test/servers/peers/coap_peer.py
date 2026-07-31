# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""CoAP interop: drive the device CoAP server with aiocoap (the reference client).

Role: the *device is the server*; this peer is a real RFC 7252 client. aiocoap
builds the UDP message (version/type/token/options/payload) and matches the
response, so a pass proves the device's option encoding and response codes
interop with an independent stack.
"""

from __future__ import annotations

import asyncio

from ._common import Probe, require

NAME = "coap"
HELP = "probe the device CoAP server with the aiocoap client"


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname")
    p.add_argument("--port", type=int, default=5683, help="CoAP UDP port (default 5683)")
    p.add_argument("--path", default="/", help="resource path to GET (default /)")
    p.add_argument("--put", help="optional value to PUT to --path before the GET")
    p.add_argument("--timeout", type=float, default=5.0, help="request timeout seconds")


def run(args) -> bool:
    aiocoap = require("aiocoap")
    from aiocoap import GET, PUT, Code, Context, Message

    pr = Probe(f"coap {args.host}:{args.port}{args.path}")
    uri = f"coap://{args.host}:{args.port}{args.path}"

    async def go():
        ctx = await Context.create_client_context()
        try:
            if args.put is not None:
                req = Message(code=PUT, uri=uri, payload=args.put.encode())
                resp = await asyncio.wait_for(ctx.request(req).response, timeout=args.timeout)
                ok = resp.code in (Code.CHANGED, Code.CREATED, Code.CONTENT)
                pr.check("PUT accepted", ok, str(resp.code))

            req = Message(code=GET, uri=uri)
            resp = await asyncio.wait_for(ctx.request(req).response, timeout=args.timeout)
            pr.check("GET -> 2.05 Content", resp.code == Code.CONTENT, str(resp.code))
            pr.info(f"payload: {bytes(resp.payload)[:64]!r}")

            # RFC 6690 resource discovery: GET /.well-known/core lists the resources.
            disc = Message(code=GET, uri=f"coap://{args.host}:{args.port}/.well-known/core")
            dr = await asyncio.wait_for(ctx.request(disc).response, timeout=args.timeout)
            pr.check(".well-known/core discovery -> 2.05", dr.code == Code.CONTENT, str(dr.code))
            pr.info(f"link-format: {bytes(dr.payload)[:96]!r}")

            # An unknown resource should yield 4.04, not a hang or malformed reply.
            req = Message(code=GET, uri=f"coap://{args.host}:{args.port}/_nope_interop")
            resp = await asyncio.wait_for(ctx.request(req).response, timeout=args.timeout)
            pr.check("unknown resource -> 4.04", resp.code == Code.NOT_FOUND, str(resp.code))
        except Exception as exc:  # noqa: BLE001
            pr.check("session completed", False, str(exc))
        finally:
            await ctx.shutdown()

    asyncio.run(go())
    return pr.summary()
