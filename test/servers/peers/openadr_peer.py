# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""OpenADR 3.0 interop: read the device's demand-response objects as a spec consumer.

Role: the *device is the server* (an OpenADR 3.0 VEN, per openadr.h). The rig serves the two core OpenADR 3.0
JSON objects over HTTP: an EVENT at /openadr/event (a program + named event + an `intervals` array of typed
payload points - a demand-response signal) and a REPORT at /openadr/report (a resource reading answering an
event - the VEN's telemetry). This peer acts as an independent OpenADR 3.0 consumer: it GETs each object with
the stdlib JSON parser and validates the 3.0 object model - `objectType`, `programID`, the intervals /
interval / payloads / values nesting, and the report's `resources` array - plus the semantic values. That
validates the device's OpenADR 3.0 codec against a spec-conformant reader, written fresh and independent of
the library.

The OpenADR 3.0 OpenAPI (the VTN/VEN REST contract) + openleadr are the production references; this peer
encodes the same 3.0 object model. Served over plain HTTP here; production OpenADR 3.0 rides HTTPS + OAuth2.
"""

from __future__ import annotations

import http.client
import json

from ._common import Probe

NAME = "openadr"
HELP = "read the device's OpenADR 3.0 event/report objects as a spec consumer (device-as-server)"


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname (the rig)")
    p.add_argument("--port", type=int, default=80, help="device HTTP port (default 80)")
    p.add_argument("--timeout", type=float, default=8.0, help="timeout seconds")


def _get(host, port, path, timeout):
    """GET a resource -> (status, content_type, parsed JSON or None)."""
    c = http.client.HTTPConnection(host, port, timeout=timeout)
    try:
        c.request("GET", path)
        r = c.getresponse()
        body = r.read()
        ctype = r.getheader("Content-Type", "")
        try:
            obj = json.loads(body)
        except json.JSONDecodeError:
            obj = None
        return r.status, ctype, obj
    finally:
        c.close()


def run(args) -> bool:
    pr = Probe(f"openadr device={args.host}:{args.port}")

    # 1. EVENT object (VTN -> VEN demand-response signal).
    try:
        status, ctype, ev = _get(args.host, args.port, "/openadr/event", args.timeout)
    except Exception as exc:  # noqa: BLE001
        pr.check("GET /openadr/event", False, str(exc))
        return pr.summary()
    pr.check("GET /openadr/event -> 200", status == 200, f"status={status}")
    pr.check("event Content-Type is application/json", "json" in ctype.lower(), ctype)
    pr.check("event parses as JSON", isinstance(ev, dict), "")
    if isinstance(ev, dict):
        pr.check("event objectType == EVENT", ev.get("objectType") == "EVENT", f"objectType={ev.get('objectType')!r}")
        pr.check("event has a programID", bool(ev.get("programID")), f"programID={ev.get('programID')!r}")
        pr.check("event has an eventName", bool(ev.get("eventName")), f"eventName={ev.get('eventName')!r}")
        ivs = ev.get("intervals")
        ok_ivs = isinstance(ivs, list) and len(ivs) >= 1
        pr.check("event has an intervals array", ok_ivs, f"intervals={type(ivs).__name__}")
        if ok_ivs:
            iv0 = ivs[0]
            period = iv0.get("interval", {})
            pr.check("interval carries start + duration", "start" in period and "duration" in period, f"{period}")
            payloads = iv0.get("payloads")
            ok_pl = isinstance(payloads, list) and payloads and "type" in payloads[0] and "values" in payloads[0]
            pr.check("interval carries a typed payload with values", bool(ok_pl), f"payloads={payloads}")
            if ok_pl:
                vals = payloads[0]["values"]
                pr.check(
                    "payload values is a numeric array",
                    isinstance(vals, list) and all(isinstance(v, (int, float)) for v in vals),
                    f"values={vals}",
                )

    # 2. REPORT object (VEN -> VTN telemetry).
    try:
        status, ctype, rep = _get(args.host, args.port, "/openadr/report", args.timeout)
    except Exception as exc:  # noqa: BLE001
        pr.check("GET /openadr/report", False, str(exc))
        rep = None
    if isinstance(rep, dict):
        pr.check("GET /openadr/report -> 200", status == 200, f"status={status}")
        pr.check(
            "report objectType == REPORT", rep.get("objectType") == "REPORT", f"objectType={rep.get('objectType')!r}"
        )
        pr.check("report has a programID", bool(rep.get("programID")), f"programID={rep.get('programID')!r}")
        pr.check("report has an eventID", bool(rep.get("eventID")), f"eventID={rep.get('eventID')!r}")
        resources = rep.get("resources")
        ok_res = isinstance(resources, list) and resources and bool(resources[0].get("resourceName"))
        pr.check("report has a resources array with a resourceName", bool(ok_res), f"resources={resources}")
        if ok_res:
            r_ivs = resources[0].get("intervals")
            ok_riv = isinstance(r_ivs, list) and r_ivs and isinstance(r_ivs[0].get("payloads"), list)
            pr.check("resource carries an interval reading (READING payload)", bool(ok_riv), f"intervals={r_ivs}")
            if ok_riv:
                pl = r_ivs[0]["payloads"][0]
                vals = pl.get("values")
                pr.check(
                    "reading value is numeric",
                    isinstance(vals, list) and vals and isinstance(vals[0], (int, float)),
                    f"values={vals}",
                )
    else:
        pr.check("GET /openadr/report parses as JSON", False, f"status={status}")

    return pr.summary()
