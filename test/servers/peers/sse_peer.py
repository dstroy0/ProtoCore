# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Server-Sent Events interop: drive the device's SSE endpoint with a spec-faithful
EventSource client (stdlib only).

Role: the *device is the server*; this peer is the EventSource client. There is no
EventSource in the Python standard library, so the peer implements the WHATWG HTML
"interpret an event stream" parsing algorithm itself - that algorithm IS the
authoritative reference (https://html.spec.whatwg.org/multipage/server-sent-events.html),
so parsing the device's stream with it validates the wire format against the standard.

The device's rig firmware pushes an initial burst on connect (welcome/hello/tick
records), so the peer can assert the handshake headers AND that real event records
decode field-by-field. Checks:
  * 200 + Content-Type: text/event-stream (the SSE contract)
  * Cache-Control: no-cache and a long-lived (not close-delimited length) response
  * at least one well-formed event record parses out of the stream
  * event name / id / data fields survive the round trip
"""

from __future__ import annotations

import socket

from ._common import Probe

NAME = "sse"
HELP = "probe the device Server-Sent Events endpoint with a WHATWG EventSource parser"


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname")
    p.add_argument("--port", type=int, default=80, help="HTTP port (default 80)")
    p.add_argument("--path", default="/events", help="SSE path to subscribe (default /events)")
    p.add_argument("--timeout", type=float, default=5.0, help="socket / read-window timeout seconds")


def _read_headers(sock, deadline_buf: bytearray) -> tuple[bytes, dict, bytes]:
    """Read until the CRLFCRLF header terminator; return (status_line, headers, leftover_body)."""
    buf = bytearray()
    while b"\r\n\r\n" not in buf:
        chunk = sock.recv(1024)
        if not chunk:
            break
        buf += chunk
        if len(buf) > 16384:  # a sane cap - SSE headers are tiny
            break
    head, _, rest = bytes(buf).partition(b"\r\n\r\n")
    lines = head.split(b"\r\n")
    status = lines[0] if lines else b""
    headers = {}
    for ln in lines[1:]:
        if b":" in ln:
            k, _, v = ln.partition(b":")
            headers[k.strip().lower().decode("latin1")] = v.strip().decode("latin1")
    return status, headers, rest


def _parse_event_stream(data: bytes):
    """WHATWG "interpret an event stream": yield dicts {event,id,data} per dispatched event.

    Faithful to the algorithm: lines split on \\n / \\r / \\r\\n; a blank line dispatches;
    ':'-prefixed lines are comments; 'data' accumulates with trailing-newline joins; an
    event with an empty data buffer is not dispatched; default event type is 'message'.
    """
    text = data.decode("utf-8", errors="replace")
    # Normalize the three legal line terminators to \n for a single split.
    text = text.replace("\r\n", "\n").replace("\r", "\n")
    lines = text.split("\n")

    event_type = ""
    data_lines: list[str] = []
    last_id = ""

    def dispatch():
        nonlocal event_type, data_lines
        if not data_lines:
            event_type = ""
            return None
        payload = "\n".join(data_lines)
        ev = {"event": event_type or "message", "id": last_id, "data": payload}
        event_type = ""
        data_lines = []
        return ev

    for line in lines:
        if line == "":
            ev = dispatch()
            if ev:
                yield ev
            continue
        if line.startswith(":"):
            continue  # comment / keepalive
        if ":" in line:
            field, _, value = line.partition(":")
            if value.startswith(" "):
                value = value[1:]
        else:
            field, value = line, ""
        if field == "event":
            event_type = value
        elif field == "data":
            data_lines.append(value)
        elif field == "id":
            if "\x00" not in value:
                last_id = value
        elif field == "retry":
            pass  # reconnection time; not asserted here


def run(args) -> bool:
    pr = Probe(f"sse {args.host}:{args.port}{args.path}")

    try:
        sock = socket.create_connection((args.host, args.port), timeout=args.timeout)
    except OSError as exc:  # noqa: BLE001
        pr.check("connected", False, str(exc))
        return pr.summary()

    try:
        req = (
            f"GET {args.path} HTTP/1.1\r\n"
            f"Host: {args.host}\r\n"
            f"Accept: text/event-stream\r\n"
            f"Connection: keep-alive\r\n\r\n"
        ).encode()
        sock.sendall(req)
        sock.settimeout(args.timeout)

        status, headers, leftover = _read_headers(sock, bytearray())
        pr.check("status is 200", status.startswith(b"HTTP/1.1 200"), status.decode("latin1", "replace"))
        ctype = headers.get("content-type", "")
        pr.check("Content-Type is text/event-stream", ctype.startswith("text/event-stream"), ctype or "(none)")
        pr.check(
            "Cache-Control: no-cache",
            "no-cache" in headers.get("cache-control", ""),
            headers.get("cache-control", "(none)"),
        )
        # A long-lived stream must NOT advertise a body length; it is close-delimited.
        pr.check(
            "no Content-Length (long-lived stream)",
            "content-length" not in headers,
            headers.get("content-length", "absent"),
        )

        # Read a short window of the event body (the rig pushes an initial burst on connect).
        body = bytearray(leftover)
        try:
            while len(body) < 8192:
                chunk = sock.recv(1024)
                if not chunk:
                    break
                body += chunk
                if b"\n\n" in bytes(body):  # got at least one complete record; keep briefly draining
                    sock.settimeout(0.4)
        except OSError:
            pass  # read window elapsed - expected for a long-lived stream

        events = list(_parse_event_stream(bytes(body)))
        pr.check("at least one event record parsed", len(events) >= 1, f"{len(events)} event(s)")
        if events:
            names = {e["event"] for e in events}
            datas = [e["data"] for e in events]
            pr.info(f"events: {[(e['event'], e['id'], e['data']) for e in events]}")
            pr.check("a named event was delivered", any(n != "message" for n in names), f"names={sorted(names)}")
            pr.check("event data is non-empty", all(d for d in datas), f"payloads={datas}")
            pr.check("an event carried an id", any(e["id"] for e in events), "resumable id present")
    except Exception as exc:  # noqa: BLE001 - any client error is an interop FAIL
        pr.check("SSE exchange completed", False, str(exc))
    finally:
        try:
            sock.close()
        except OSError:
            pass

    return pr.summary()
