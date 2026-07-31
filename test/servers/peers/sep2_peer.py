# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""IEEE 2030.5 (Smart Energy Profile 2.0) interop: read the device's 2030.5 resources as a spec consumer.

Role: the *device is the server* (a 2030.5 resource server, per sep2.h). The rig serves the core resource
documents as `application/sep+xml`: DeviceCapability at /dcap (the root, with hrefs to the EndDevice list + the
DER-program list), an EndDevice registration at /edev (sFDI/lFDI identity), and a DERControl event at /derc
(interval + an opModFixedW setpoint). This peer acts as an independent IEEE 2030.5 consumer: it walks
DeviceCapability -> the linked resources with the stdlib XML parser and validates the `urn:ieee:std:2030.5:ns`
namespace, the resource element vocabulary, the href graph, and the semantic values (sFDI, the DER interval,
the real-power setpoint). That validates the device's 2030.5 resource codec + link structure against a
spec-conformant reader, written fresh and independent of the library.

The official IEEE 2030.5 XSD (`sep.xsd`) + certified 2030.5 clients (e.g. QualityLogic's test harness) are the
production references; this peer encodes the same standard's namespace + resource model. 2030.5 is served over
plain HTTP here; production 2030.5 mandates TLS with client certificates.
"""

from __future__ import annotations

import http.client
import xml.etree.ElementTree as ET

from ._common import Probe

NAME = "sep2"
HELP = "read the device's IEEE 2030.5 resources as a spec consumer (device-as-server)"

_NS = "urn:ieee:std:2030.5:ns"


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname (the rig)")
    p.add_argument("--port", type=int, default=80, help="device HTTP port (default 80)")
    p.add_argument("--timeout", type=float, default=8.0, help="timeout seconds")


def _q(tag: str) -> str:
    return f"{{{_NS}}}{tag}"


def _get(host, port, path, timeout):
    """GET a resource -> (status, content_type, ElementTree root or None)."""
    c = http.client.HTTPConnection(host, port, timeout=timeout)
    try:
        c.request("GET", path)
        r = c.getresponse()
        body = r.read()
        ctype = r.getheader("Content-Type", "")
        root = None
        try:
            root = ET.fromstring(body)
        except ET.ParseError:
            root = None
        return r.status, ctype, root
    finally:
        c.close()


def _text(root, tag):
    el = root.find(_q(tag))
    return el.text if el is not None else None


def run(args) -> bool:
    pr = Probe(f"sep2 device={args.host}:{args.port}")

    # 1. DeviceCapability (the root resource).
    try:
        status, ctype, dcap = _get(args.host, args.port, "/dcap", args.timeout)
    except Exception as exc:  # noqa: BLE001
        pr.check("GET /dcap", False, str(exc))
        return pr.summary()
    pr.check("GET /dcap -> 200", status == 200, f"status={status}")
    pr.check("/dcap Content-Type is application/sep+xml", "sep+xml" in ctype, ctype)
    pr.check("/dcap parses as XML", dcap is not None, "")
    if dcap is None:
        return pr.summary()
    pr.check("root is DeviceCapability in the 2030.5 namespace", dcap.tag == _q("DeviceCapability"), dcap.tag)
    pr.check(
        "DeviceCapability has a pollRate", dcap.get("pollRate") not in (None, ""), f"pollRate={dcap.get('pollRate')}"
    )
    edev_link = dcap.find(_q("EndDeviceListLink"))
    derp_link = dcap.find(_q("DERProgramListLink"))
    edev_href = edev_link.get("href") if edev_link is not None else None
    pr.check("EndDeviceListLink href present", bool(edev_href), f"href={edev_href!r}")
    pr.check(
        "DERProgramListLink href present",
        derp_link is not None and bool(derp_link.get("href")),
        f"href={derp_link.get('href') if derp_link is not None else None!r}",
    )

    # 2. Follow the EndDeviceListLink to the EndDevice registration.
    if edev_href:
        try:
            status, ctype, edev = _get(args.host, args.port, edev_href, args.timeout)
        except Exception as exc:  # noqa: BLE001
            pr.check(f"GET {edev_href}", False, str(exc))
            edev = None
        if edev is not None:
            pr.check(f"EndDeviceListLink href resolves ({edev_href} -> 200)", status == 200, f"status={status}")
            pr.check("resource is an EndDevice", edev.tag == _q("EndDevice"), edev.tag)
            sfdi = _text(edev, "sFDI")
            pr.check("EndDevice.sFDI is a decimal identifier", bool(sfdi) and sfdi.isdigit(), f"sFDI={sfdi!r}")
            pr.check("EndDevice.lFDI present", bool(_text(edev, "lFDI")), f"lFDI={_text(edev, 'lFDI')!r}")
            pr.check("EndDevice has an href", bool(edev.get("href")), f"href={edev.get('href')!r}")

    # 3. DERControl event (interval + the real-power setpoint).
    try:
        status, ctype, derc = _get(args.host, args.port, "/derc", args.timeout)
    except Exception as exc:  # noqa: BLE001
        pr.check("GET /derc", False, str(exc))
        derc = None
    if derc is not None:
        pr.check("GET /derc -> 200", status == 200, f"status={status}")
        pr.check("resource is a DERControl", derc.tag == _q("DERControl"), derc.tag)
        pr.check("DERControl.mRID present", bool(_text(derc, "mRID")), f"mRID={_text(derc, 'mRID')!r}")
        interval = derc.find(_q("interval"))
        start = (
            interval.find(_q("start")).text if interval is not None and interval.find(_q("start")) is not None else None
        )
        dur = (
            interval.find(_q("duration")).text
            if interval is not None and interval.find(_q("duration")) is not None
            else None
        )
        pr.check("DERControl interval/start is an epoch", bool(start) and start.isdigit(), f"start={start!r}")
        pr.check("DERControl interval/duration is seconds", bool(dur) and dur.isdigit(), f"duration={dur!r}")
        base = derc.find(_q("DERControlBase"))
        opmod = (
            base.find(_q("opModFixedW")).text if base is not None and base.find(_q("opModFixedW")) is not None else None
        )
        ok_w = bool(opmod) and (opmod.lstrip("-").isdigit())
        pr.check("DERControlBase/opModFixedW is a signed setpoint (W)", ok_w, f"opModFixedW={opmod!r}")

    return pr.summary()
