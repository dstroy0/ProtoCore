# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""SMTP (RFC 5321) interop: drive the device as an SMTP client against a real aiosmtpd server.

Role: the *device is the client*. This peer stands up a genuine `aiosmtpd` SMTP server (the reference
implementation) with a capturing handler, then triggers the device (via the rig's GET
/smtp/probe?host=&port=&from=&to=&subject= route) to run the full RFC 5321 dialogue - greeting, EHLO,
MAIL FROM, RCPT TO, DATA, the message with dot-stuffing, and QUIT. It then confirms - as the authoritative
third party, by reading what the server actually received - that the envelope and the message the device
built are correct. That closes the loop: the device's command builder + multiline-reply parser + message
builder interoperate with a genuine ESMTP server.

  1. start aiosmtpd on 0.0.0.0 with a capturing handler
  2. trigger GET /smtp/probe on the device -> {result:0, ok:1}
  3. the captured message has the device's MAIL FROM / RCPT TO / Subject / body

No external server needed - the reference peer is self-contained (pip install aiosmtpd).
"""

from __future__ import annotations

import http.client
import json
import socket
import time

from ._common import Probe, require

NAME = "smtp"
HELP = "drive the device as an SMTP client against a real aiosmtpd server (device-as-client)"

# The rig's h_smtp_probe body (pentesting/rig_firmware/src/main.cpp) - checked server-side.
_BODY_MARK = b"automated alert from the pc-s3 rig"


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname (the rig)")
    p.add_argument("--port", type=int, default=80, help="device HTTP port (default 80)")
    p.add_argument("--smtp-host", help="address the device dials for SMTP (default: this machine's route IP)")
    p.add_argument("--smtp-port", type=int, default=2525, help="SMTP port to serve (default 2525)")
    p.add_argument("--timeout", type=float, default=15.0, help="timeout seconds")


def run(args) -> bool:
    pr = Probe(f"smtp device={args.host} server=:{args.smtp_port}")
    controller_mod = require("aiosmtpd.controller")

    # Figure out the address the device should dial (this machine's IP on the route to the device).
    smtp_host = args.smtp_host
    if not smtp_host:
        try:
            s = socket.create_connection((args.host, args.port), timeout=args.timeout)
            smtp_host = s.getsockname()[0]
            s.close()
        except OSError as exc:  # noqa: BLE001
            pr.check("reach the device", False, str(exc))
            return pr.summary()

    captured = []

    class _Capture:
        async def handle_DATA(self, server, session, envelope):  # aiosmtpd handler hook
            captured.append(
                {
                    "from": envelope.mail_from,
                    "to": list(envelope.rcpt_tos),
                    "data": bytes(envelope.content),
                }
            )
            return "250 Message accepted for delivery"

    controller = controller_mod.Controller(_Capture(), hostname="0.0.0.0", port=args.smtp_port)
    try:
        controller.start()
    except Exception as exc:  # noqa: BLE001 - e.g. port already bound
        pr.check("start aiosmtpd server", False, str(exc))
        return pr.summary()

    try:
        pr.info(f"aiosmtpd serving {smtp_host}:{args.smtp_port}")
        frm = "rig@pc.local"
        to = "ops@pc.local"
        subject = f"pc-interop-{int(time.time())}"

        # 2. trigger the device to send an alert email through that server.
        try:
            q = f"/smtp/probe?host={smtp_host}&port={args.smtp_port}" f"&from={frm}&to={to}&subject={subject}"
            c = http.client.HTTPConnection(args.host, args.port, timeout=args.timeout)
            c.request("GET", q)
            r = c.getresponse()
            body = r.read()
            c.close()
            rep = json.loads(body)
        except Exception as exc:  # noqa: BLE001
            pr.check("device /smtp/probe completed", False, str(exc))
            return pr.summary()

        pr.check("device delivered the message (result 0)", rep.get("ok") == 1, json.dumps(rep))

        # 3. the authoritative check: the server received exactly what the device built.
        got = captured[0] if captured else None
        pr.check("server received a message", got is not None, f"{len(captured)} captured")
        if got:
            pr.check("MAIL FROM matches", got["from"] == frm, str(got["from"]))
            pr.check("RCPT TO matches", to in got["to"], str(got["to"]))
            pr.check("Subject header present", subject.encode() in got["data"], subject)
            pr.check("device body delivered", _BODY_MARK in got["data"], f"{len(got['data'])}B")
    finally:
        controller.stop()

    return pr.summary()
