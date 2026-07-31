# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""SNMP interop: query the device agent with a real SNMP manager.

Role: the *device is the agent*; this peer is the manager. Prefers the net-snmp
command-line tools (the canonical reference manager); if they are not on PATH it
falls back to the pysnmp library so the peer also works pip-only. Either way a
pass proves the device's agent interops with an independent SNMP stack.
"""

from __future__ import annotations

import asyncio
import shutil
import subprocess

from ._common import Probe, have

NAME = "snmp"
HELP = "query the device SNMP agent with net-snmp (or pysnmp fallback)"

# sysDescr.0 - the first scalar every MIB-II agent answers; system - the group.
SYSDESCR = "1.3.6.1.2.1.1.1.0"
SYSTEM = "1.3.6.1.2.1.1"
SYSDESCR_OBJECT = "1.3.6.1.2.1.1.1"  # the object without its .0 instance


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname")
    p.add_argument("--port", type=int, default=161, help="agent UDP port (default 161)")
    p.add_argument("--community", default="public", help="v2c community (default public)")
    p.add_argument("--version", default="2c", choices=["1", "2c"], help="SNMP version")
    p.add_argument("--timeout", type=float, default=3.0, help="per-request timeout seconds")


def run(args) -> bool:
    pr = Probe(f"snmp {args.host}:{args.port} v{args.version}")
    if shutil.which("snmpget") and shutil.which("snmpwalk"):
        return _run_netsnmp(args, pr)
    if have("pysnmp"):
        pr.info("net-snmp not on PATH - using the pysnmp fallback")
        return _run_pysnmp(args, pr)
    pr.check("an SNMP manager is available", False, "install net-snmp, or `pip install pysnmp`")
    return pr.summary()


# --------------------------------------------------------------------------- #
# net-snmp backend (reference)
# --------------------------------------------------------------------------- #
def _invoke(args, tool: str, *oid_and_extra) -> tuple[int, str]:
    target = f"{args.host}:{args.port}"
    cmd = [tool, "-v", args.version, "-c", args.community, "-t", str(args.timeout), "-r", "1", target, *oid_and_extra]
    try:
        cp = subprocess.run(cmd, capture_output=True, text=True, timeout=args.timeout * 4 + 5)
        return cp.returncode, (cp.stdout + cp.stderr).strip()
    except subprocess.TimeoutExpired:
        return 124, "timed out"


def _run_netsnmp(args, pr: Probe) -> bool:
    snmpget, snmpwalk = shutil.which("snmpget"), shutil.which("snmpwalk")

    rc, out = _invoke(args, snmpget, SYSDESCR)
    pr.check("GET sysDescr.0", rc == 0 and "=" in out, out.splitlines()[0] if out else "no output")

    rc, out = _invoke(args, snmpwalk, SYSTEM)
    rows = [ln for ln in out.splitlines() if "=" in ln]
    pr.check("WALK system group returns rows", rc == 0 and len(rows) >= 1, f"{len(rows)} varbinds")

    rc, out = _invoke(args, snmpget, SYSDESCR_OBJECT)  # object, no instance
    pr.check("GET non-instance answered (no hang/abort)", rc in (0, 1, 2), out.splitlines()[0] if out else "")
    return pr.summary()


# --------------------------------------------------------------------------- #
# pysnmp backend (fallback, pip-only)
# --------------------------------------------------------------------------- #
def _run_pysnmp(args, pr: Probe) -> bool:
    from pysnmp.hlapi.asyncio import (
        CommunityData,
        ContextData,
        ObjectIdentity,
        ObjectType,
        SnmpEngine,
        UdpTransportTarget,
        get_cmd,
        walk_cmd,
    )

    mp_model = 0 if args.version == "1" else 1  # 0 = v1, 1 = v2c

    async def go():
        engine = SnmpEngine()
        comm = CommunityData(args.community, mpModel=mp_model)
        ctx = ContextData()
        target = await UdpTransportTarget.create((args.host, args.port), timeout=args.timeout, retries=1)

        err_ind, err_stat, _idx, vbs = await get_cmd(engine, comm, target, ctx, ObjectType(ObjectIdentity(SYSDESCR)))
        ok = err_ind is None and not err_stat and len(vbs) == 1
        pr.check("GET sysDescr.0", ok, str(vbs[0][1]) if vbs else str(err_ind or err_stat))

        rows = 0
        async for err_ind, err_stat, _idx, vbs in walk_cmd(
            engine, comm, target, ctx, ObjectType(ObjectIdentity(SYSTEM)), lexicographicMode=False
        ):
            if err_ind or err_stat:
                break
            rows += len(vbs)
        pr.check("WALK system group returns rows", rows >= 1, f"{rows} varbinds")

        err_ind, err_stat, _idx, vbs = await get_cmd(
            engine, comm, target, ctx, ObjectType(ObjectIdentity(SYSDESCR_OBJECT))
        )
        # A non-instance GET must be answered (noSuchInstance varbind), not error out.
        pr.check("GET non-instance answered (no hang/abort)", err_ind is None, str(vbs[0][1]) if vbs else str(err_ind))

    try:
        asyncio.run(go())
    except Exception as exc:  # noqa: BLE001
        pr.check("session completed", False, str(exc))
    return pr.summary()
