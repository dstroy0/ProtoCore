# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""GraphQL interop: validate the device's query engine with the reference graphql-core library.

Role: the *device is the server*. It runs a zero-heap, schema-free GraphQL *query* engine
(pc_graphql_execute) behind POST /graphql. This peer uses `graphql-core` - the reference Python
GraphQL implementation - to PARSE + VALIDATE each query (which proves it is spec-valid GraphQL and yields
the exact field shape the client asked for), sends the raw query to the device, and asserts the device's
`{"data":{...}}` response MIRRORS that shape. That mirroring is the defining GraphQL property: the client
picks the fields and the server returns exactly those, no more. It also checks the resolved values and that
an argument on the path reaches the leaf resolver (`sensor(id: 7) { value }` -> 71), and that a malformed
query fails closed with errors.
"""

from __future__ import annotations

import http.client
import json
import re

from ._common import Probe, require

NAME = "graphql"
HELP = "validate the device's GraphQL query engine with the reference graphql-core (device-as-server)"


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname (the GraphQL server)")
    p.add_argument("--port", type=int, default=80, help="device HTTP port (default 80)")
    p.add_argument("--timeout", type=float, default=8.0, help="timeout seconds")


def _shape(selection_set):
    """The field shape a selection set asks for: {name: subshape-or-None} (None marks a scalar leaf)."""
    out = {}
    for sel in selection_set.selections:
        out[sel.name.value] = _shape(sel.selection_set) if sel.selection_set else None
    return out


def _keys_match(expected, actual, path=""):
    """Recursively verify the response mirrors the requested shape (keys equal exactly at each level)."""
    if not isinstance(actual, dict):
        return False, f"{path or '<root>'}: expected object, got {type(actual).__name__}"
    if set(expected) != set(actual):
        return False, f"{path or '<root>'}: keys {sorted(actual)} != requested {sorted(expected)}"
    for k, sub in expected.items():
        if sub is not None:
            ok, why = _keys_match(sub, actual[k], f"{path}.{k}" if path else k)
            if not ok:
                return False, why
    return True, ""


def run(args) -> bool:
    pr = Probe(f"graphql device={args.host}:{args.port}")
    gql = require("graphql")  # graphql-core

    # The rig enforces CSRF on POST: fetch a signed token from GET /csrf and echo it in X-CSRF-Token
    # (a no-op when CSRF is disabled). This is the standard rig POST preamble, not part of GraphQL.
    csrf = None
    try:
        c = http.client.HTTPConnection(args.host, args.port, timeout=args.timeout)
        c.request("GET", "/csrf")
        rep = c.getresponse()
        body = rep.read()
        c.close()
        if rep.status == 200:
            m = re.search(rb'"token"\s*:\s*"([^"]+)"', body)
            csrf = m.group(1).decode() if m else None
    except OSError:
        csrf = None

    def execute(query: str):
        headers = {"Content-Type": "application/graphql"}
        if csrf:
            headers["X-CSRF-Token"] = csrf
        c = http.client.HTTPConnection(args.host, args.port, timeout=args.timeout)
        c.request("POST", "/graphql", body=query.encode(), headers=headers)
        rep = c.getresponse()
        status, raw = rep.status, rep.read().decode(errors="replace")
        c.close()
        return status, raw

    def data_of(raw):
        try:
            return json.loads(raw).get("data")
        except ValueError:
            return None

    # 1. graphql-core validates the query is spec-valid GraphQL; the device returns exactly the requested shape.
    q = "{ device { name uptime heap online } }"
    try:
        want = _shape(gql.parse(q).definitions[0].selection_set)  # raises on invalid GraphQL
    except Exception as exc:  # noqa: BLE001
        pr.check("graphql-core parses the query", False, str(exc))
        return pr.summary()
    try:
        status, raw = execute(q)
    except Exception as exc:  # noqa: BLE001
        pr.check("device POST /graphql completed", False, str(exc))
        return pr.summary()
    data = data_of(raw)
    pr.check("valid query: device 200 + JSON data", status == 200 and data is not None, f"{status} {raw[:80]}")
    ok, why = _keys_match(want, data or {})
    pr.check("response shape mirrors the requested fields", ok, why or json.dumps(data))
    dev = (data or {}).get("device", {})
    pr.check("device.name resolved", dev.get("name") == "esp32-pc", f"name={dev.get('name')}")
    pr.check("device.uptime is int", isinstance(dev.get("uptime"), int), f"uptime={dev.get('uptime')}")
    pr.check(
        "device.heap int > 0", isinstance(dev.get("heap"), int) and dev.get("heap", 0) > 0, f"heap={dev.get('heap')}"
    )
    pr.check("device.online true", dev.get("online") is True, f"online={dev.get('online')}")

    # 2. THE GraphQL property: a subset query returns ONLY the requested field, not the whole object.
    q2 = "{ device { name } }"
    gql.parse(q2)
    _, raw2 = execute(q2)
    pr.check(
        "subset query returns only the requested field",
        data_of(raw2) == {"device": {"name": "esp32-pc"}},
        raw2[:100],
    )

    # 3. an argument on the path reaches the leaf resolver: sensor(id: 7) { id value } -> value = id*10+1.
    q3 = "{ sensor(id: 7) { id value } }"
    gql.parse(q3)
    _, raw3 = execute(q3)
    pr.check(
        "argument flows to the leaf resolver (value == id*10+1)",
        (data_of(raw3) or {}).get("sensor") == {"id": 7, "value": 71},
        raw3[:100],
    )

    # 4. a named operation resolves the same as a bare query.
    q4 = "query Status { device { online } }"
    gql.parse(q4)
    _, raw4 = execute(q4)
    pr.check("named operation resolves", data_of(raw4) == {"device": {"online": True}}, raw4[:80])

    # 5. a malformed query fails closed: errors (not data), status 400.
    status5, raw5 = execute("{ device { name ")  # unterminated selection set
    try:
        body5 = json.loads(raw5)
    except ValueError:
        body5 = {}
    pr.check(
        "malformed query fails closed (errors, 400)",
        status5 == 400 and "errors" in body5 and "data" not in body5,
        f"{status5} {raw5[:80]}",
    )

    return pr.summary()
