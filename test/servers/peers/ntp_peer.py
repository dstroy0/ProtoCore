# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""NTP (RFC 5905 server mode) interop: query the device's NTP server with a real ntplib client.

Role: the *device is the server*. The device answers NTP requests on UDP/123 from its own clock
(`ntp_server_begin`); this peer uses the third-party `ntplib` client to query it and validate the reply.
ntplib builds the 48-octet request, sends it, and parses + sanity-checks the response (it verifies the
origin timestamp echoes the transmit stamp it sent), so a clean return already proves wire-level interop.
We additionally assert the server semantics:

  1. ntplib.request() succeeds (a well-formed 48-octet mode-4 reply that echoed our transmit stamp)
  2. mode = 4 (server), leap = 0 (in sync), the advertised stratum, and a plausible epoch (>= 2021)

The device is seeded with a synthetic clock on the rig (base epoch + uptime), so the absolute time is not
real - the interop check is protocol correctness, not clock accuracy.
"""

from __future__ import annotations

import datetime

from ._common import Probe, require

NAME = "ntp"
HELP = "query the device's NTP server (UDP/123) with a real ntplib client (device-as-server)"


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname (the NTP server)")
    p.add_argument("--ntp-port", type=int, default=123, help="device NTP UDP port (default 123)")
    p.add_argument("--stratum", type=int, default=2, help="stratum the rig advertises (default 2)")
    p.add_argument("--timeout", type=float, default=5.0, help="timeout seconds")


def run(args) -> bool:
    pr = Probe(f"ntp device={args.host}:{args.ntp_port}")
    ntplib = require("ntplib")

    client = ntplib.NTPClient()
    try:
        r = client.request(args.host, version=4, port=args.ntp_port, timeout=args.timeout)
    except Exception as exc:  # noqa: BLE001 - ntplib raises NTPException on a malformed/absent reply
        pr.check("ntplib got a valid NTP reply", False, str(exc))
        return pr.summary()

    pr.check("ntplib got a valid NTP reply (origin echoed)", True, f"tx_time={r.tx_time:.0f}")
    pr.check("mode = 4 (server)", r.mode == 4, f"mode={r.mode}")
    pr.check("leap = 0 (in sync)", r.leap == 0, f"leap={r.leap}")
    pr.check("stratum matches", r.stratum == args.stratum, f"stratum={r.stratum}")
    pr.check("version = 4 (echoed)", r.version == 4, f"version={r.version}")
    # A plausible wall-clock epoch (the rig serves a synthetic 2026 base + uptime).
    plausible = r.tx_time >= 1609459200  # 2021-01-01
    when = datetime.datetime.fromtimestamp(r.tx_time, datetime.timezone.utc).isoformat()
    pr.check("served a plausible epoch (>= 2021)", plausible, when)
    ref = getattr(r, "ref_id", None)
    pr.info(f"ref_id={ref} root_delay={r.root_delay:.4f} root_disp={r.root_dispersion:.4f}")

    return pr.summary()
