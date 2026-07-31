# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""StatsD interop: drive the device as a StatsD client against a UDP collector (stdlib).

Role: the *device is the client*. This peer binds a UDP socket (a StatsD collector), triggers the device
(via the rig's GET /statsd/probe?host=&port=&name=&value=&type= route) to format one metric line and
pc_udp_sendto it, then receives the datagram and validates it against the StatsD line grammar as an
independent receiver:

  name:value|type[|@rate][|#tags]

  1. bind a UDP collector on 0.0.0.0
  2. trigger GET /statsd/probe on the device -> {sent:1}
  3. the received datagram parses as StatsD with the metric name / value / type we asked it to emit

The collector is stdlib (a raw UDP socket + a StatsD line parser); no external server is needed (the
independent check is the wire-format validation - Graphite/StatsD, Telegraf, Datadog are production peers).
"""

from __future__ import annotations

import http.client
import socket

from ._common import Probe

NAME = "statsd"
HELP = "drive the device as a StatsD client against a UDP collector (device-as-client)"


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname (the rig)")
    p.add_argument("--port", type=int, default=80, help="device HTTP port (default 80)")
    p.add_argument("--statsd-host", help="collector address the device dials (default: this machine's route IP)")
    p.add_argument("--statsd-port", type=int, default=8125, help="UDP collector port to bind (default 8125)")
    p.add_argument("--timeout", type=float, default=8.0, help="timeout seconds")


def _parse_statsd(dgram: bytes):
    """Return (name, value, type, extras) for one StatsD line, or None if it is not name:value|type."""
    line = dgram.split(b"\n", 1)[0]  # a packet may carry several \n-separated metrics; take the first
    if b":" not in line or b"|" not in line:
        return None
    name, _, rest = line.partition(b":")
    fields = rest.split(b"|")
    if len(fields) < 2:
        return None
    return name, fields[0], fields[1], fields[2:]


def run(args) -> bool:
    pr = Probe(f"statsd device={args.host} collector=:{args.statsd_port}")

    statsd_host = args.statsd_host
    if not statsd_host:
        try:
            s = socket.create_connection((args.host, args.port), timeout=args.timeout)
            statsd_host = s.getsockname()[0]
            s.close()
        except OSError as exc:  # noqa: BLE001
            pr.check("reach the device", False, str(exc))
            return pr.summary()

    coll = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    coll.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        coll.bind(("0.0.0.0", args.statsd_port))
    except OSError as exc:  # noqa: BLE001
        pr.check("bind UDP collector", False, str(exc))
        coll.close()
        return pr.summary()
    coll.settimeout(args.timeout)

    try:
        pr.info(f"collector on {statsd_host}:{args.statsd_port}")
        want_name, want_value, want_type = "pc.rig.gauge", "4242", "g"

        # 2. trigger the device to emit one gauge metric to the collector.
        try:
            q = f"/statsd/probe?host={statsd_host}&port={args.statsd_port}&name={want_name}&value={want_value}&type={want_type}"
            c = http.client.HTTPConnection(args.host, args.port, timeout=args.timeout)
            c.request("GET", q)
            c.getresponse().read()
            c.close()
        except Exception as exc:  # noqa: BLE001
            pr.check("device /statsd/probe completed", False, str(exc))
            return pr.summary()

        # 3. receive + validate the datagram as an independent StatsD receiver.
        try:
            dgram, _addr = coll.recvfrom(2048)
        except socket.timeout:
            pr.check("collector received a datagram", False, "no UDP datagram arrived")
            return pr.summary()

        pr.check("collector received a datagram", True, f"{len(dgram)}B {dgram[:40]!r}")
        parsed = _parse_statsd(dgram)
        pr.check(
            "datagram is valid StatsD (name:value|type)", parsed is not None, dgram[:40].decode("latin1", "replace")
        )
        if parsed:
            name, value, mtype, _extras = parsed
            pr.check("metric name matches", name == want_name.encode(), name.decode("latin1", "replace"))
            pr.check("value matches", value == want_value.encode(), value.decode("latin1", "replace"))
            pr.check("type is gauge (g)", mtype == want_type.encode(), mtype.decode("latin1", "replace"))
    finally:
        coll.close()

    return pr.summary()
