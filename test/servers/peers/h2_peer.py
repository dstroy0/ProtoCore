# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""HTTP/2 interop: negotiate ALPN h2 with the device's HTTPS listener and drive real HTTP/2 traffic
through an independent, spec-compliant stack (httpx, backed by the `h2` library).

Role: the *device is the server*. It terminates TLS on :443 (static-pool mbedTLS, ALPN offers `h2`)
and serves its routes over HTTP/2. This peer proves the binary framing works end to end against a
mature client - the connection negotiates HTTP/2, requests return 200 over HEADERS/DATA frames with
HPACK-compressed headers, and several streams multiplex over the one connection. The device cert is
self-signed, so verification is disabled; the check is the h2 framing + request/response cycle, not
the PKI chain. HTTP/2 on the device is PSRAM-gated (PC_ENABLE_HTTP2 + PC_H2_POOL_IN_PSRAM).
"""

from __future__ import annotations

from ._common import Probe, require


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname (the HTTPS server)")
    p.add_argument("--tls-port", type=int, default=443, help="device TLS port (default 443)")
    p.add_argument("--path", default="/", help="path to GET over h2 (default /)")
    p.add_argument("--timeout", type=float, default=8.0, help="timeout seconds")


NAME = "http2"
HELP = "drive real HTTP/2 (ALPN h2) requests at the device's HTTPS listener via httpx (device-as-server)"


def run(args) -> bool:
    httpx = require("httpx")  # pulls in the h2 library; exits 2 with an install hint if absent
    pr = Probe(f"http2 device={args.host}:{args.tls_port}")
    base = f"https://{args.host}:{args.tls_port}"

    try:
        client = httpx.Client(http2=True, verify=False, timeout=args.timeout)
    except (httpx.HTTPError, OSError) as exc:  # noqa: BLE001
        pr.check("create an HTTP/2 client", False, str(exc))
        return pr.summary()

    try:
        try:
            r = client.get(base + args.path)
        except (httpx.HTTPError, OSError) as exc:  # noqa: BLE001
            pr.check("HTTP/2 GET completes", False, str(exc))
            return pr.summary()

        pr.check("HTTP/2 GET completes", True, f"{r.http_version} {r.status_code}")
        pr.check("connection negotiated HTTP/2 (ALPN h2)", r.http_version == "HTTP/2", str(r.http_version))
        pr.check("GET / returns 200 over h2", r.status_code == 200, f"status={r.status_code}")
        body = r.text
        pr.check(
            "response body arrived over the h2 DATA frames",
            "mbedtls" in body.lower() or "hello" in body.lower(),
            body[:48].strip(),
        )

        # A second route on the SAME connection: a new stream, HPACK-compressed against the first.
        try:
            rs = client.get(base + "/status")
            ok = rs.status_code == 200 and rs.http_version == "HTTP/2"
            pr.check("second stream GET /status is 200 over h2", ok, f"{rs.http_version} {rs.status_code}")
            pr.check(
                "h2 stream reuse returns the JSON body",
                '"tls":true' in rs.text.replace(" ", ""),
                rs.text[:48].strip(),
            )
        except (httpx.HTTPError, OSError) as exc:  # noqa: BLE001
            pr.check("second stream GET /status", False, str(exc))

        # Multiplex several streams over the one h2 connection - the static engine must serve them all.
        try:
            codes = [client.get(base + args.path).status_code for _ in range(5)]
            pr.check("5 streams on one h2 connection all return 200", all(c == 200 for c in codes), str(codes))
        except (httpx.HTTPError, OSError) as exc:  # noqa: BLE001
            pr.check("multiplex several h2 streams", False, str(exc))
    finally:
        client.close()

    return pr.summary()
