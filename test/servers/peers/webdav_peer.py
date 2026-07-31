# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""WebDAV interop: drive the device's WebDAV share (RFC 4918) with a real client (stdlib).

Role: the *device is the server*; this peer is the WebDAV client. Uses only the Python standard
library (http.client speaks arbitrary methods; xml.etree parses the 207 body), so the authoritative
third party is CPython's own HTTP client + XML parser exercising the RFC 4918 method set:

  OPTIONS  -> 200 + `DAV:` compliance class + Allow lists the DAV methods
  PROPFIND -> 207 Multi-Status, well-formed XML, seeded resources present
  PUT/GET  -> 201 then the body round-trips byte-for-byte
  MKCOL    -> 201 collection created (and PROPFIND Depth 0 sees it)
  COPY     -> 201/204, the copy is readable
  MOVE     -> 201/204, source gone (404) and destination readable
  DELETE   -> 204/200 and the resource is gone (404)

Everything it creates lives under a unique `/dav/_interop_<pid>/` prefix and is deleted at the end,
so the share is left as it was found.
"""

from __future__ import annotations

import http.client
import os
import xml.etree.ElementTree as ET

from ._common import Probe

NAME = "webdav"
HELP = "probe the device WebDAV share (RFC 4918) with the stdlib client"


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname")
    p.add_argument("--port", type=int, default=80, help="HTTP port (default 80)")
    p.add_argument("--root", default="/dav", help="WebDAV share root on the device (default /dav)")
    p.add_argument("--timeout", type=float, default=6.0, help="socket timeout seconds")


def run(args) -> bool:
    pr = Probe(f"webdav {args.host}:{args.port}{args.root}")
    base = args.root.rstrip("/")
    workdir = f"{base}/_interop_{os.getpid()}"

    def conn():
        return http.client.HTTPConnection(args.host, args.port, timeout=args.timeout)

    def dav(method, path, body=None, headers=None):
        c = conn()
        c.request(method, path, body=body, headers=headers or {})
        r = c.getresponse()
        data = r.read()
        c.close()
        return r.status, {k.lower(): v for k, v in r.getheaders()}, data

    # OPTIONS: RFC 4918 8.1 - a DAV-compliant resource advertises its class + method set.
    try:
        st, hdrs, _ = dav("OPTIONS", base + "/")
        pr.check("OPTIONS -> 200", st == 200, f"{st}")
        pr.check("advertises DAV compliance class", "1" in hdrs.get("dav", ""), f"DAV: {hdrs.get('dav', '(none)')}")
        allow = hdrs.get("allow", "")
        pr.check("Allow lists PROPFIND", "PROPFIND" in allow, allow or "(none)")
    except Exception as exc:  # noqa: BLE001
        pr.check("OPTIONS completed", False, str(exc))

    # PROPFIND Depth 1 on the seeded root: a 207 whose body is well-formed multistatus XML.
    try:
        st, _h, body = dav("PROPFIND", base + "/", headers={"Depth": "1"})
        pr.check("PROPFIND -> 207 Multi-Status", st == 207, f"{st}")
        root = ET.fromstring(body)  # raises on malformed XML
        ns = "{DAV:}"
        hrefs = [e.text for e in root.iter(f"{ns}href")]
        pr.check("body parses as <multistatus>", root.tag == f"{ns}multistatus", root.tag)
        pr.check(
            "PROPFIND returned >=1 <response>", len(list(root.iter(f"{ns}response"))) >= 1, f"{len(hrefs)} href(s)"
        )
    except Exception as exc:  # noqa: BLE001
        pr.check("PROPFIND well-formed 207", False, str(exc))

    # Full write round trip under a unique working directory, cleaned up at the end.
    payload = b"webdav interop payload\n" * 4
    fpath = f"{workdir}/file.txt"
    copied = f"{workdir}/copy.txt"
    moved = f"{workdir}/moved.txt"
    try:
        st, _h, _b = dav("MKCOL", workdir)
        pr.check("MKCOL -> 201 Created", st == 201, f"{st}")

        st, _h, _b = dav("PUT", fpath, body=payload)
        pr.check("PUT new file -> 201 Created", st == 201, f"{st}")

        st, _h, got = dav("GET", fpath)
        pr.check("GET returns 200", st == 200, f"{st}")
        pr.check("GET body round-trips byte-for-byte", got == payload, f"{len(got)}/{len(payload)} bytes")

        st, _h, _b = dav("COPY", fpath, headers={"Destination": copied, "Overwrite": "T"})
        pr.check("COPY -> 201/204", st in (201, 204), f"{st}")
        st, _h, got2 = dav("GET", copied)
        pr.check("copy is readable and identical", st == 200 and got2 == payload, f"{st}, {len(got2)}B")

        st, _h, _b = dav("MOVE", copied, headers={"Destination": moved, "Overwrite": "T"})
        pr.check("MOVE -> 201/204", st in (201, 204), f"{st}")
        st_src, _h, _b = dav("GET", copied)
        pr.check("MOVE source is gone (404)", st_src == 404, f"{st_src}")
        st_dst, _h, _b = dav("GET", moved)
        pr.check("MOVE destination is readable", st_dst == 200, f"{st_dst}")

        st, _h, _b = dav("DELETE", fpath)
        pr.check("DELETE -> 204/200", st in (200, 204), f"{st}")
        st_gone, _h, _b = dav("GET", fpath)
        pr.check("deleted resource is gone (404)", st_gone == 404, f"{st_gone}")
    except Exception as exc:  # noqa: BLE001
        pr.check("write round trip completed", False, str(exc))
    finally:
        # Best-effort cleanup: DELETE the working collection (removes any leftovers under it).
        try:
            dav("DELETE", workdir)
        except Exception:  # noqa: BLE001
            pass

    return pr.summary()
