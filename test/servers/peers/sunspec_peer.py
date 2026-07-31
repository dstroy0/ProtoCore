# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""SunSpec interop: read the device's SunSpec Common model over Modbus TCP with pysunspec2.

Role: the *device is the server* (a SunSpec Modbus slave). The rig seeds a SunSpec device-information map
into its Modbus holding registers - the "SunS" marker, the Common model (ID 1, 66-register body: Mn / Md /
Opt / Vr / SN / DA), and the end model - at a base register clear of the plain-Modbus interop's low regs.
This peer uses **pysunspec2** (the reference SunSpec Alliance Python implementation) to connect over Modbus
TCP, scan the model chain, and read the Common model, asserting the manufacturer / model / serial / version.
That validates the device's SunSpec model codec + register layout against the reference SunSpec client.
"""

from __future__ import annotations

from ._common import Probe, require

NAME = "sunspec"
HELP = "read the device's SunSpec Common model over Modbus TCP with pysunspec2 (device-as-server)"


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP (the Modbus TCP slave)")
    p.add_argument("--port", type=int, default=502, help="Modbus TCP port (default 502)")
    p.add_argument("--unit", type=int, default=1, help="Modbus unit / slave id (default 1)")
    p.add_argument("--base", type=int, default=100, help="SunSpec base register (the rig seeds it at 100)")
    p.add_argument("--timeout", type=float, default=8.0, help="timeout seconds")


def _point(model, name):
    """Read a SunSpec point value across pysunspec2 access styles (attr or points dict)."""
    pt = getattr(model, name, None)
    if pt is not None and hasattr(pt, "value"):
        return pt.value
    try:
        return model.points[name].value
    except Exception:  # noqa: BLE001
        return None


def run(args) -> bool:
    pr = Probe(f"sunspec device={args.host}:{args.port}")
    cli = require("sunspec2.modbus.client")  # pysunspec2

    d = cli.SunSpecModbusClientDeviceTCP(slave_id=args.unit, ipaddr=args.host, ipport=args.port, timeout=args.timeout)
    d.base_addr_list = [args.base]  # the rig seeds the "SunS" map at reg 100, not a standard base
    try:
        d.connect()
    except Exception as exc:  # noqa: BLE001
        pr.check("connect to the Modbus TCP slave", False, str(exc))
        return pr.summary()

    try:
        d.scan()
    except Exception as exc:  # noqa: BLE001
        pr.check("scan the SunSpec model chain (SunS marker + models)", False, str(exc))
        d.disconnect()
        return pr.summary()

    pr.check("scan found a SunSpec model chain", bool(getattr(d, "models", None)), f"models={list(d.models.keys())}")

    common = None
    if getattr(d, "common", None):
        common = d.common[0]
    elif getattr(d, "models", None) and 1 in d.models:
        common = d.models[1][0]
    pr.check("Common model (id 1) present", common is not None, "")
    if common is None:
        d.disconnect()
        return pr.summary()

    mn, md, sn, vr = (_point(common, "Mn"), _point(common, "Md"), _point(common, "SN"), _point(common, "Vr"))
    pr.check("Common.Mn (manufacturer) == 'PROTOCORE'", mn == "PROTOCORE", f"Mn={mn!r}")
    pr.check("Common.Md (model) == 'RIG-S3'", md == "RIG-S3", f"Md={md!r}")
    pr.check("Common.SN (serial) == 'SN-000001'", sn == "SN-000001", f"SN={sn!r}")
    pr.check("Common.Vr (version) == '1.0.0'", vr == "1.0.0", f"Vr={vr!r}")

    d.disconnect()
    return pr.summary()
