# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""MTConnect interop: drive the device's MTConnect agent (ANSI/MTC1.4) with a real client (stdlib).

Role: the *device is the server* (an MTConnect HTTP agent); this peer is the client. Uses only the
Python standard library (http.client + xml.etree), so the authoritative third party is CPython's own
HTTP + XML parser validating the agent's documents against the MTConnect schema shape:

  GET /probe   -> MTConnectDevices: root + <Header> + a <Device> with <DataItem>s
  GET /current -> MTConnectStreams: root + <Header> + <DeviceStream>/<ComponentStream>
  GET /sample?from=&count= -> MTConnectStreams with the sequence cursor header
                              (firstSequence / lastSequence / nextSequence) the client resumes from

MTConnect namespaces the elements (xmlns="urn:mtconnect.org:MTConnect*:1.4"); the peer matches on the
local element name so it is version/namespace tolerant.
"""

from __future__ import annotations

import http.client
import xml.etree.ElementTree as ET

from ._common import Probe

NAME = "mtconnect"
HELP = "probe the device MTConnect agent (probe/current/sample) and validate the XML"


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname")
    p.add_argument("--port", type=int, default=80, help="HTTP port (default 80)")
    p.add_argument("--timeout", type=float, default=6.0, help="socket timeout seconds")


def _local(tag: str) -> str:
    return tag.rsplit("}", 1)[-1]  # strip a {namespace} prefix


def run(args) -> bool:
    pr = Probe(f"mtconnect {args.host}:{args.port}")

    def get(path):
        c = http.client.HTTPConnection(args.host, args.port, timeout=args.timeout)
        c.request("GET", path)
        r = c.getresponse()
        body = r.read()
        c.close()
        return r.status, body

    # probe -> MTConnectDevices with a Device model.
    try:
        st, body = get("/probe")
        pr.check("GET /probe -> 200", st == 200, f"{st}")
        root = ET.fromstring(body)
        pr.check("root is MTConnectDevices", _local(root.tag) == "MTConnectDevices", _local(root.tag))
        pr.check("has a <Header>", any(_local(e.tag) == "Header" for e in root.iter()), "header present")
        devs = [e for e in root.iter() if _local(e.tag) == "Device"]
        items = [e for e in root.iter() if _local(e.tag) == "DataItem"]
        pr.check(
            "has a <Device> with <DataItem>s",
            len(devs) >= 1 and len(items) >= 1,
            f"{len(devs)} dev, {len(items)} items",
        )
    except Exception as exc:  # noqa: BLE001
        pr.check("probe well-formed", False, str(exc))

    # current -> MTConnectStreams.
    try:
        st, body = get("/current")
        pr.check("GET /current -> 200", st == 200, f"{st}")
        root = ET.fromstring(body)
        pr.check("root is MTConnectStreams", _local(root.tag) == "MTConnectStreams", _local(root.tag))
    except Exception as exc:  # noqa: BLE001
        pr.check("current well-formed", False, str(exc))

    # sample -> MTConnectStreams with the sequence cursor header.
    try:
        st, body = get("/sample?from=1&count=10")
        pr.check("GET /sample -> 200", st == 200, f"{st}")
        root = ET.fromstring(body)
        pr.check("root is MTConnectStreams", _local(root.tag) == "MTConnectStreams", _local(root.tag))
        hdr = next((e for e in root.iter() if _local(e.tag) == "Header"), None)
        if hdr is not None:
            a = hdr.attrib
            pr.info(
                f"header: firstSequence={a.get('firstSequence')} lastSequence={a.get('lastSequence')} nextSequence={a.get('nextSequence')}"
            )
            pr.check(
                "sample header has the sequence cursor",
                all(k in a for k in ("firstSequence", "lastSequence", "nextSequence")),
                "cursor present",
            )
        else:
            pr.check("sample has a <Header>", False, "no header")
    except Exception as exc:  # noqa: BLE001
        pr.check("sample well-formed", False, str(exc))

    return pr.summary()
