# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""DNS (RFC 1035) interop: query the device's authoritative DNS server with a real dnspython client.

Role: the *device is the server*. The device answers A/IN queries for its built-in table on UDP/53
(`pc_dns_server_begin`), NXDOMAIN for anything else. This peer uses the third-party `dnspython` client to
build/send real DNS queries and parse the replies, then asserts the server semantics:

  1. A/IN query for a configured name -> NOERROR + an A record with the expected address, AA (authoritative)
  2. a name NOT in the table -> NXDOMAIN (proving it is authoritative-only, not an open resolver)

The rig seeds rig.lan/printer.lan/gateway.lan; dnspython validates the wire format on the way in and out.
"""

from __future__ import annotations

from ._common import Probe, require

NAME = "dns"
HELP = "query the device's authoritative DNS server (UDP/53) with a real dnspython client (device-as-server)"

# The A records the rig seeds (pentesting/rig_firmware/src/main.cpp).
_EXPECT = {"rig.lan": "192.168.1.29", "printer.lan": "192.168.1.5", "gateway.lan": "192.168.1.1"}


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname (the DNS server)")
    p.add_argument("--dns-port", type=int, default=53, help="device DNS UDP port (default 53)")
    p.add_argument("--timeout", type=float, default=5.0, help="timeout seconds")


def run(args) -> bool:
    pr = Probe(f"dns device={args.host}:{args.dns_port}")
    require("dns.message")  # dnspython
    import dns.flags
    import dns.message
    import dns.query
    import dns.rcode
    import dns.rdatatype

    def query(name, rdtype=dns.rdatatype.A):
        q = dns.message.make_query(name, rdtype)
        return dns.query.udp(q, args.host, port=args.dns_port, timeout=args.timeout)

    # 1. each configured A record resolves correctly, authoritatively.
    for name, ip in _EXPECT.items():
        try:
            r = query(name)
        except Exception as exc:  # noqa: BLE001
            pr.check(f"resolve {name}", False, str(exc))
            continue
        answers = [rr.to_text() for rrset in r.answer for rr in rrset]
        pr.check(f"{name} -> NOERROR", r.rcode() == dns.rcode.NOERROR, dns.rcode.to_text(r.rcode()))
        pr.check(f"{name} has A {ip}", ip in answers, ",".join(answers) or "(no answer)")
        pr.check(f"{name} is authoritative (AA)", bool(r.flags & dns.flags.AA), f"flags=0x{r.flags:04x}")

    # 2. an unconfigured name must be NXDOMAIN (authoritative-only, not an open resolver/reflector).
    try:
        r = query("no-such-host.lan")
        pr.check("unknown name -> NXDOMAIN", r.rcode() == dns.rcode.NXDOMAIN, dns.rcode.to_text(r.rcode()))
        pr.check("NXDOMAIN carries no answer", len(r.answer) == 0, f"{len(r.answer)} rrsets")
    except Exception as exc:  # noqa: BLE001
        pr.check("query unknown name", False, str(exc))

    return pr.summary()
