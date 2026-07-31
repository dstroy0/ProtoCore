# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""syslog (RFC 5424) interop: drive the device as a syslog client against a UDP collector (stdlib).

Role: the *device is the client*. This peer binds a UDP socket (a syslog collector), triggers the device
(via the rig's GET /syslog/probe?host=&port=&msg=&sev= route) to format one RFC 5424 line and pc_udp_sendto
it, then receives the datagram and validates it against the RFC 5424 grammar as an independent receiver:

  <PRI>1 SP TIMESTAMP SP HOSTNAME SP APP-NAME SP PROCID SP MSGID SP STRUCTURED-DATA SP MSG

  1. bind a UDP collector on 0.0.0.0
  2. trigger GET /syslog/probe on the device -> {sent:1}
  3. the received datagram parses as RFC 5424 with PRI = LOCAL0*8+INFO, version 1, the device's
     HOSTNAME/APP-NAME, and the message we asked it to log

The collector is stdlib (a raw UDP socket + a strict RFC 5424 parser), so no external server is needed; the
independent check is the wire-format validation. (rsyslog is the production reference receiver.)
"""

from __future__ import annotations

import http.client
import socket

from ._common import Probe

NAME = "syslog"
HELP = "drive the device as a syslog client against a UDP collector (device-as-client, RFC 5424)"

_LOCAL0 = 16
_INFO = 6


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname (the rig)")
    p.add_argument("--port", type=int, default=80, help="device HTTP port (default 80)")
    p.add_argument("--syslog-host", help="collector address the device dials (default: this machine's route IP)")
    p.add_argument("--syslog-port", type=int, default=5514, help="UDP collector port to bind (default 5514)")
    p.add_argument("--timeout", type=float, default=8.0, help="timeout seconds")


def _parse_rfc5424(dgram: bytes):
    """Return (pri, version, hostname, appname, msg) or None if the head is not RFC 5424."""
    if not dgram.startswith(b"<"):
        return None
    end = dgram.find(b">")
    if end < 0:
        return None
    try:
        pri = int(dgram[1:end])
    except ValueError:
        return None
    rest = dgram[end + 1 :]
    # VERSION SP TIMESTAMP SP HOSTNAME SP APP-NAME SP PROCID SP MSGID SP SD SP MSG
    # Split into the 7 header tokens (space-separated) + the MSG remainder.
    parts = rest.split(b" ", 7)
    if len(parts) < 7:
        return None
    version, _ts, hostname, appname = parts[0], parts[1], parts[2], parts[3]
    msg = parts[7] if len(parts) == 8 else b""
    return pri, version, hostname, appname, msg


def run(args) -> bool:
    pr = Probe(f"syslog device={args.host} collector=:{args.syslog_port}")

    syslog_host = args.syslog_host
    if not syslog_host:
        try:
            s = socket.create_connection((args.host, args.port), timeout=args.timeout)
            syslog_host = s.getsockname()[0]
            s.close()
        except OSError as exc:  # noqa: BLE001
            pr.check("reach the device", False, str(exc))
            return pr.summary()

    coll = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    coll.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        coll.bind(("0.0.0.0", args.syslog_port))
    except OSError as exc:  # noqa: BLE001
        pr.check("bind UDP collector", False, str(exc))
        coll.close()
        return pr.summary()
    coll.settimeout(args.timeout)

    try:
        pr.info(f"collector on {syslog_host}:{args.syslog_port}")
        want_msg = "pc-syslog-interop-check"

        # 2. trigger the device to ship one RFC 5424 line to the collector.
        try:
            q = f"/syslog/probe?host={syslog_host}&port={args.syslog_port}&msg={want_msg}&sev={_INFO}"
            c = http.client.HTTPConnection(args.host, args.port, timeout=args.timeout)
            c.request("GET", q)
            r = c.getresponse()
            r.read()
            c.close()
        except Exception as exc:  # noqa: BLE001
            pr.check("device /syslog/probe completed", False, str(exc))
            return pr.summary()

        # 3. receive + validate the datagram as an independent RFC 5424 receiver.
        try:
            dgram, _addr = coll.recvfrom(2048)
        except socket.timeout:
            pr.check("collector received a datagram", False, "no UDP datagram arrived")
            return pr.summary()

        pr.check("collector received a datagram", True, f"{len(dgram)}B")
        parsed = _parse_rfc5424(dgram)
        pr.check("datagram is valid RFC 5424", parsed is not None, dgram[:40].decode("latin1", "replace"))
        if parsed:
            pri, version, hostname, appname, msg = parsed
            pr.check("PRI = LOCAL0*8 + INFO", pri == _LOCAL0 * 8 + _INFO, f"pri={pri}")
            pr.check("VERSION = 1", version == b"1", version.decode("latin1", "replace"))
            pr.check("HOSTNAME matches", hostname == b"pc-rig", hostname.decode("latin1", "replace"))
            pr.check("APP-NAME matches", appname == b"rig-app", appname.decode("latin1", "replace"))
            pr.check("MSG delivered", want_msg.encode() in msg, msg[:40].decode("latin1", "replace"))
    finally:
        coll.close()

    return pr.summary()
