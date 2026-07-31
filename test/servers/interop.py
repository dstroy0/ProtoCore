#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Real-protocol interop harness - one CLI to drive the device against genuine
reference implementations (the standing "verify against a third-party impl" rule
turned into a reusable tool).

Usage:
    python test/servers/interop.py <protocol> [options]
    python test/servers/interop.py --list
    python test/servers/interop.py http  --host 192.168.1.85
    python test/servers/interop.py snmp  --host 192.168.1.85 --community public
    python test/servers/interop.py modbus-client --host 192.168.1.85
    python test/servers/interop.py mqtt-broker --port 1883      # device connects in
    python test/servers/interop.py opcua-server                 # device connects in

Exit code: 0 if every check passed (or a server peer ran), 1 on an interop FAIL,
2 on a missing dependency (install test/servers/requirements.txt). Each protocol
note says whether the *device* plays server or client for that peer.
"""

from __future__ import annotations

import argparse
import sys

from peers import (
    amqp_peer,
    coap_peer,
    dns_peer,
    ftp_peer,
    graphql_peer,
    grpcweb_peer,
    h2_peer,
    h3_peer,
    http_peer,
    jwt_peer,
    modbus_peer,
    mqtt_peer,
    mtconnect_peer,
    nats_peer,
    ntp_peer,
    opcua_peer,
    openadr_peer,
    robotics_peer,
    euromap77_peer,
    redis_peer,
    sep2_peer,
    smb_peer,
    smtp_peer,
    snmp_peer,
    sparkplug_peer,
    sse_peer,
    sunspec_peer,
    ssh_peer,
    statsd_peer,
    stomp_peer,
    syslog_peer,
    tls_peer,
    wamp_peer,
    webdav_peer,
    ws_peer,
    xmpp_peer,
)

# Module list; each contributes one or more peers (module-level or via PEERS).
_MODULES = [
    amqp_peer,
    http_peer,
    h2_peer,
    h3_peer,
    ws_peer,
    sse_peer,
    webdav_peer,
    mtconnect_peer,
    snmp_peer,
    modbus_peer,
    coap_peer,
    mqtt_peer,
    opcua_peer,
    redis_peer,
    ftp_peer,
    smb_peer,
    smtp_peer,
    syslog_peer,
    ntp_peer,
    dns_peer,
    nats_peer,
    stomp_peer,
    statsd_peer,
    jwt_peer,
    tls_peer,
    ssh_peer,
    graphql_peer,
    grpcweb_peer,
    sparkplug_peer,
    sunspec_peer,
    xmpp_peer,
    wamp_peer,
    sep2_peer,
    openadr_peer,
    robotics_peer,
    euromap77_peer,
]


def _peers():
    """Yield (name, help, add_args, run) for every peer across all modules."""
    for mod in _MODULES:
        for obj in getattr(mod, "PEERS", [mod]):
            yield obj.NAME, obj.HELP, obj.add_args, obj.run


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        prog="interop.py",
        description="Drive the ProtoCore device against real protocol peers.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    ap.add_argument("--list", action="store_true", help="list available protocol peers and exit")
    sub = ap.add_subparsers(dest="protocol", metavar="<protocol>")
    for name, help_text, add_args, run in _peers():
        sp = sub.add_parser(name, help=help_text, description=help_text)
        add_args(sp)
        sp.set_defaults(_run=run)
    return ap


def main(argv=None) -> int:
    ap = build_parser()
    args = ap.parse_args(argv)
    if args.list:
        print("Available interop peers:")
        for name, help_text, _a, _r in _peers():
            print(f"  {name:<16} {help_text}")
        return 0
    if not getattr(args, "protocol", None):
        ap.print_help()
        return 0
    return 0 if args._run(args) else 1


if __name__ == "__main__":
    sys.exit(main())
