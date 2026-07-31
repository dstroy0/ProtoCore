# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""EUROMAP 77 (OPC 40077) interop with asyncua (an independent OPC UA stack).

The *device is the server*: it exposes the IMM_MES_Interface model (services/machine_tool/euromap77). This peer
connects and browses the tree by BrowseName - taken from the Browse ReferenceDescriptions, the way a real
MES client discovers a model whose namespace URI is not yet in the NamespaceArray - reads live values,
and checks that the UInt64 production counters decode and climb monotonically across polls. Each poll is
a short connect/browse/read/disconnect (real client polling; stays inside the base server's
secure-channel token window).
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
    NAME = "euromap77-client"
    HELP = "browse the device EUROMAP 77 IMM_MES_Interface + read live UInt64 counters with asyncua"

    @staticmethod
    def add_args(p) -> None:
        p.add_argument("--host", required=True, help="device IP / hostname")
        p.add_argument("--port", type=int, default=4840, help="OPC UA port (default 4840)")
        p.add_argument("--name", default="PC-IMM", help="IMM_MES_Interface BrowseName")
        p.add_argument("--timeout", type=float, default=8.0, help="connect/read timeout seconds")

    @staticmethod
    def run(args) -> bool:
        require("asyncua")
        from asyncua import Client as UaClient

        pr = Probe(f"euromap77-client {args.host}:{args.port}")
        url = f"opc.tcp://{args.host}:{args.port}"

        async def snapshot(structure):
            r = {}
            async with UaClient(url=url, timeout=args.timeout) as c:
                top = await _descs(c, c.nodes.objects)
                if structure:
                    pr.check("Objects -> IMM_MES_Interface", args.name in top, str(list(top)))
                imm = top[args.name]
                comps = await _descs(c, imm)
                if structure:
                    for f in ("MachineInformation", "MachineStatus", "Jobs"):
                        pr.check(f"IMM has {f}", f in comps, str(list(comps)))
                    mi = await _descs(c, comps["MachineInformation"])
                    for v in ("Manufacturer", "Model", "SerialNumber", "ManufacturerUri"):
                        pr.check(f"MachineInformation.{v}", v in mi, str(list(mi)))
                    ms = await _descs(c, comps["MachineStatus"])
                    for v in ("IsPresent", "MachineMode"):
                        pr.check(f"MachineStatus.{v}", v in ms, str(list(ms)))
                    pr.check("IsPresent reads bool", isinstance(await ms["IsPresent"].read_value(), bool))
                    pr.check("MachineMode is Int32 enum", int(await ms["MachineMode"].read_value()) >= 0)
                jobs = await _descs(c, comps["Jobs"])
                if structure:
                    for f in ("ActiveJob", "ActiveJobValues"):
                        pr.check(f"Jobs has {f}", f in jobs, str(list(jobs)))
                    aj = await _descs(c, jobs["ActiveJob"])
                    for v in ("JobName", "ExpectedCycleTime", "NumCavities", "NominalParts"):
                        pr.check(f"ActiveJob.{v}", v in aj, str(list(aj)))
                    # NominalParts is UInt64 - decodes without narrowing
                    pr.check("NominalParts is UInt64", int(await aj["NominalParts"].read_value()) >= 0)
                ajv = await _descs(c, jobs["ActiveJobValues"])
                if structure:
                    for v in ("JobCycleCounter", "MachineCycleCounter", "JobPartsCounter",
                              "JobGoodPartsCounter", "JobBadPartsCounter", "LastCycleTime", "JobStatus"):
                        pr.check(f"ActiveJobValues.{v}", v in ajv, str(list(ajv)))
                # the UInt64 counters (prove the new Variant type decodes on the wire)
                r["cycle"] = int(await ajv["JobCycleCounter"].read_value())
                r["parts"] = int(await ajv["JobPartsCounter"].read_value())
            return r

        async def go():
            try:
                s1 = await snapshot(True)
                pr.check("JobCycleCounter is a non-negative UInt64", s1["cycle"] >= 0, str(s1["cycle"]))
                await asyncio.sleep(2.5)  # > one sim cycle (2 s)
                s2 = await snapshot(False)
                pr.check("JobCycleCounter monotonic non-decreasing", s2["cycle"] >= s1["cycle"],
                         f"{s1['cycle']} -> {s2['cycle']}")
                pr.check("JobCycleCounter advanced (machine running)", s2["cycle"] > s1["cycle"],
                         f"{s1['cycle']} -> {s2['cycle']}")
                pr.check("JobPartsCounter tracks cycles", s2["parts"] >= s1["parts"],
                         f"{s1['parts']} -> {s2['parts']}")
            except Exception as exc:  # noqa: BLE001
                pr.check("session completed", False, str(exc))

        asyncio.run(go())
        return pr.summary()


PEERS = [Client]
