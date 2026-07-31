# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""HTTP interop: drive the device's HTTP server with a real client (stdlib).

Role: the *device is the server*; this peer is the client. Uses only the Python
standard library (http.client) so it runs with no extra dependencies - the
authoritative third party here is CPython's own HTTP/1.1 client.
"""

from __future__ import annotations

import http.client

from ._common import Probe

NAME = "http"
HELP = "probe the device HTTP/1.1 server with the stdlib client"


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname")
    p.add_argument("--port", type=int, default=80, help="HTTP port (default 80)")
    p.add_argument("--path", default="/", help="path to GET/HEAD (default /)")
    p.add_argument("--timeout", type=float, default=5.0, help="socket timeout seconds")


def run(args) -> bool:
    pr = Probe(f"http {args.host}:{args.port}{args.path}")

    def conn():
        return http.client.HTTPConnection(args.host, args.port, timeout=args.timeout)

    # GET: a successful status and a body that matches Content-Length (when given).
    try:
        c = conn()
        c.request("GET", args.path)
        r = c.getresponse()
        body = r.read()
        clen = r.getheader("Content-Length")
        pr.check("GET status is 2xx/3xx", 200 <= r.status < 400, f"{r.status} {r.reason}")
        if clen is not None:
            pr.check("Content-Length matches body", int(clen) == len(body), f"hdr={clen} body={len(body)}")
        else:
            pr.info("no Content-Length (chunked or close-delimited) - skipping length check")
        c.close()
    except Exception as exc:  # noqa: BLE001 - report any client error as a FAIL
        pr.check("GET completed", False, str(exc))

    # HEAD: same headers as GET, no body (RFC 9110 9.3.2).
    try:
        c = conn()
        c.request("HEAD", args.path)
        r = c.getresponse()
        body = r.read()
        pr.check("HEAD returns empty body", len(body) == 0, f"{len(body)} bytes")
        c.close()
    except Exception as exc:  # noqa: BLE001
        pr.check("HEAD completed", False, str(exc))

    # Unknown path: a well-formed 404 (or 4xx) rather than a hang or malformed reply.
    try:
        c = conn()
        c.request("GET", "/_interop_does_not_exist_42")
        r = c.getresponse()
        r.read()
        pr.check("unknown path -> 4xx", 400 <= r.status < 500, f"{r.status} {r.reason}")
        c.close()
    except Exception as exc:  # noqa: BLE001
        pr.check("unknown-path request completed", False, str(exc))

    return pr.summary()
