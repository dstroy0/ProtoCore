# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""TLS interop: complete a real TLS handshake with the device's HTTPS listener and fetch a page (stdlib).

Role: the *device is the server*. The device terminates TLS on :443 (static-pool mbedTLS, the arena in PSRAM
on the rig) and serves the same routes over the encrypted channel. This peer uses Python's stdlib `ssl` to
run a genuine TLS handshake against it and asserts the server side works end to end:

  1. the TLS handshake completes (ClientHello -> ... -> Finished) against the device's cert
  2. a modern protocol is negotiated (TLS 1.2 or 1.3) with a real cipher suite
  3. the leaf certificate is the device's (CN=esp32-pc)
  4. an HTTP GET over the encrypted record returns 200 (the request rode inside TLS, not plaintext)

The device cert is self-signed (a throwaway ECDSA P-256), so verification is disabled - the check is that
the handshake + encrypted request/response cycle works, not the PKI trust chain.
"""

from __future__ import annotations

import socket
import ssl

from ._common import Probe


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname (the HTTPS server)")
    p.add_argument("--tls-port", type=int, default=443, help="device TLS port (default 443)")
    p.add_argument("--path", default="/", help="path to GET over TLS (default /)")
    p.add_argument("--timeout", type=float, default=8.0, help="timeout seconds")


NAME = "tls"
HELP = "complete a real TLS handshake with the device's HTTPS listener and fetch a page (device-as-server)"


def run(args) -> bool:
    pr = Probe(f"tls device={args.host}:{args.tls_port}")

    ctx = ssl.create_default_context()
    ctx.check_hostname = False
    ctx.verify_mode = ssl.CERT_NONE  # device cert is self-signed / pinned, not CA-issued

    try:
        raw = socket.create_connection((args.host, args.tls_port), timeout=args.timeout)
    except OSError as exc:  # noqa: BLE001
        pr.check("reach the device TLS port", False, str(exc))
        return pr.summary()

    try:
        raw.settimeout(args.timeout)
        tls = ctx.wrap_socket(raw, server_hostname=args.host)
    except (ssl.SSLError, OSError) as exc:  # noqa: BLE001
        pr.check("TLS handshake completes", False, str(exc))
        raw.close()
        return pr.summary()

    try:
        version = tls.version()
        cipher = tls.cipher()  # (name, protocol, secret_bits)
        cert = tls.getpeercert(binary_form=False) or {}
        pr.check("TLS handshake completes", True, f"{version} {cipher[0] if cipher else '?'}")
        pr.check("modern protocol negotiated (TLS 1.2/1.3)", version in ("TLSv1.2", "TLSv1.3"), str(version))
        pr.check("a real cipher suite was chosen", bool(cipher and cipher[2] and cipher[2] >= 128), str(cipher))

        # The device cert (self-signed) - getpeercert() returns {} with CERT_NONE, so read the DER + parse CN.
        der = tls.getpeercert(binary_form=True)
        pr.check("device presented a certificate", bool(der), f"{len(der) if der else 0}B DER")
        if der:
            pr.check("cert is the device's (CN esp32-pc)", b"esp32-pc" in der, "CN in DER")

        # 4. an HTTP request over the encrypted record returns 200.
        req = f"GET {args.path} HTTP/1.1\r\nHost: {args.host}\r\nConnection: close\r\n\r\n".encode()
        tls.sendall(req)
        resp = b""
        while len(resp) < 4096:
            try:
                chunk = tls.recv(2048)
            except (ssl.SSLError, OSError):
                break
            if not chunk:
                break
            resp += chunk
        status = 0
        if resp.startswith(b"HTTP/1."):
            try:
                status = int(resp.split(b" ", 2)[1])
            except (ValueError, IndexError):
                status = 0
        pr.check("HTTP GET over TLS returns 200", status == 200, f"status={status} {resp[:32]!r}")
        pr.check(
            "response came back over the encrypted channel",
            b"pc" in resp,
            resp.split(b"\r\n\r\n", 1)[-1][:32].decode("latin1", "replace"),
        )
    finally:
        try:
            tls.close()
        except OSError:
            pass

    return pr.summary()
