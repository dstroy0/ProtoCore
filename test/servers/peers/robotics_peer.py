# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""OPC UA for Robotics (OPC 40010-1) interop with asyncua (an independent OPC UA stack).

The *device is the server*: it exposes the MotionDeviceSystem model (services/machine_tool/robotics). This peer
connects and browses the full tree by BrowseName - taken from the Browse ReferenceDescriptions, the way
a real client discovers a model whose namespace URI is not yet in the NamespaceArray - and reads live
Values, asserting the OPC 40010-1 structure and that the axes track the moving robot. Each poll is a
short connect/browse/read/disconnect (real client polling; stays inside the base server's secure-channel
token window).
"""

from __future__ import annotations

import asyncio

from ._common import Probe, require


async def _descs(client, node):
    out = {}
    for d in await node.get_children_descriptions():
        out[d.BrowseName.Name] = client.get_node(d.NodeId)
    return out


class Client:
    NAME = "robotics-client"
    HELP = "browse the device OPC UA for Robotics MotionDeviceSystem + read live axes with asyncua"

    @staticmethod
    def add_args(p) -> None:
        p.add_argument("--host", required=True, help="device IP / hostname")
        p.add_argument("--port", type=int, default=4840, help="OPC UA port (default 4840)")
        p.add_argument("--name", default="PC-Robot", help="MotionDeviceSystem BrowseName")
        p.add_argument("--timeout", type=float, default=8.0, help="connect/read timeout seconds")

    @staticmethod
    def run(args) -> bool:
        require("asyncua")
        from asyncua import Client as UaClient

        pr = Probe(f"robotics-client {args.host}:{args.port}")
        url = f"opc.tcp://{args.host}:{args.port}"

        async def snapshot(structure):
            r = {}
            async with UaClient(url=url, timeout=args.timeout) as c:
                top = await _descs(c, c.nodes.objects)
                if structure:
                    pr.check("Objects -> MotionDeviceSystem", args.name in top, str(list(top)))
                mds = top[args.name]
                sysf = await _descs(c, mds)
                if structure:
                    for f in ("MotionDevices", "Controllers", "SafetyStates"):
                        pr.check(f"system folder {f}", f in sysf, str(list(sysf)))
                md = (await _descs(c, sysf["MotionDevices"]))["MotionDevice"]
                mdc = await _descs(c, md)
                if structure:
                    for v in (
                        "Manufacturer",
                        "Model",
                        "ProductCode",
                        "SerialNumber",
                        "MotionDeviceCategory",
                        "ParameterSet",
                        "Axes",
                    ):
                        pr.check(f"MotionDevice.{v}", v in mdc, str(list(mdc)))
                    pr.check(
                        "MotionDeviceCategory is Int32 enum", int(await mdc["MotionDeviceCategory"].read_value()) >= 0
                    )
                axes = await _descs(c, mdc["Axes"])
                if structure:
                    pr.check("Axes browse (>=1 Axis_k)", "Axis_1" in axes, str(list(axes)))
                    ax1 = await _descs(c, axes["Axis_1"])
                    for v in ("ActualPosition", "ActualSpeed", "ActualAcceleration", "MotionProfile"):
                        pr.check(f"Axis_1.{v}", v in ax1, str(list(ax1)))
                    ctrl = (await _descs(c, sysf["Controllers"]))["Controller"]
                    sw = (await _descs(c, ctrl))["Software"]
                    pr.check(
                        "Controller.Software.SoftwareRevision readable",
                        isinstance(await (await _descs(c, sw))["SoftwareRevision"].read_value(), str),
                    )
                    ss = (await _descs(c, sysf["SafetyStates"]))["SafetyState"]
                    ssps = await _descs(c, (await _descs(c, ss))["ParameterSet"])
                    pr.check(
                        "SafetyState.OperationalMode readable", int(await ssps["OperationalMode"].read_value()) >= 0
                    )
                ax1v = await _descs(c, axes["Axis_1"])
                r["ax1"] = round(await ax1v["ActualPosition"].read_value(), 4)
                if "Axis_3" in axes:
                    ax3v = await _descs(c, axes["Axis_3"])
                    r["ax3"] = round(await ax3v["ActualPosition"].read_value(), 4)
            return r

        async def go():
            try:
                s1 = await snapshot(True)
                await asyncio.sleep(1.5)
                s2 = await snapshot(False)
                pr.check("Axis_1.ActualPosition tracks the sim", s1["ax1"] != s2["ax1"], f"{s1['ax1']} -> {s2['ax1']}")
                if "ax3" in s2:
                    pr.check(
                        "Axis_3 distinct from Axis_1 (per-axis decode)",
                        s2["ax3"] != s2["ax1"],
                        f"ax1={s2['ax1']} ax3={s2['ax3']}",
                    )
            except Exception as exc:  # noqa: BLE001
                pr.check("session completed", False, str(exc))

        asyncio.run(go())
        return pr.summary()


PEERS = [Client]
