# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""OPC UA interop with asyncua (the reference Python OPC UA stack).

Two roles:
  opcua-client : the *device is the server*; this peer connects, browses the
                 address space and reads a node.
  opcua-server : the *device is the client*; this peer serves a known variable
                 the device can read/subscribe.
"""

from __future__ import annotations

import asyncio

from ._common import Probe, require


class Client:
    NAME = "opcua-client"
    HELP = "connect to the device OPC UA server, browse + read with asyncua"

    @staticmethod
    def add_args(p) -> None:
        p.add_argument("--host", required=True, help="device IP / hostname")
        p.add_argument("--port", type=int, default=4840, help="OPC UA port (default 4840)")
        p.add_argument("--node", help="optional NodeId to read, e.g. 'ns=1;i=2'")
        p.add_argument("--timeout", type=float, default=5.0, help="connect/read timeout seconds")

    @staticmethod
    def run(args) -> bool:
        require("asyncua")
        from asyncua import Client as UaClient

        pr = Probe(f"opcua-client {args.host}:{args.port}")
        url = f"opc.tcp://{args.host}:{args.port}/"

        async def go():
            cli = UaClient(url=url, timeout=args.timeout)
            try:
                await cli.connect()
                pr.check("connect + create session", True, url)
                # Browse the Objects folder (ns=0;i=85), where a server organizes its
                # user variables, rather than Root - so the check finds real content.
                children = await cli.nodes.objects.get_children()
                pr.check("browse Objects folder", len(children) >= 1, f"{len(children)} children")
                if args.node:
                    val = await cli.get_node(args.node).read_value()
                    pr.check(f"read {args.node}", True, repr(val))
            except Exception as exc:  # noqa: BLE001
                pr.check("session completed", False, str(exc))
            finally:
                try:
                    await cli.disconnect()
                except Exception:  # noqa: BLE001
                    pass

        asyncio.run(go())
        return pr.summary()


class Server:
    NAME = "opcua-server"
    HELP = "serve a known OPC UA variable for the device client to read"

    @staticmethod
    def add_args(p) -> None:
        p.add_argument("--bind", default="0.0.0.0", help="listen address (default 0.0.0.0)")
        p.add_argument("--port", type=int, default=4840, help="OPC UA port (default 4840)")

    @staticmethod
    def run(args) -> bool:
        require("asyncua")
        from asyncua import Server as UaServer, ua

        async def go():
            srv = UaServer()
            await srv.init()
            srv.set_endpoint(f"opc.tcp://{args.bind}:{args.port}/")
            idx = await srv.register_namespace("https://dstroy0.interop")
            obj = await srv.nodes.objects.add_object(idx, "InteropObject")
            var = await obj.add_variable(idx, "InteropValue", 42)
            await var.set_writable()
            print(f"opcua-server: opc.tcp://{args.bind}:{args.port}/ (Ctrl-C to stop)")
            print(f"  node ns={idx};s=InteropValue = 42 (writable)")
            async with srv:
                while True:
                    await asyncio.sleep(1.0)

        try:
            asyncio.run(go())
        except KeyboardInterrupt:
            print("\nopcua-server: stopped")
        return True


PEERS = [Client, Server]
