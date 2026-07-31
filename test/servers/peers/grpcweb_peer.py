# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""gRPC-web interop: exchange a unary Greeter call with the device using an independent spec client.

Role: the *device is the server*. It runs a gRPC-web "Greeter/SayHello" echo behind POST /grpc: it parses a
gRPC-web message frame (`[flags][len BE32][body]`), decodes HelloRequest.name (protobuf field 1, string), and
replies with HelloReply.message = "hello, <name>" as a gRPC-web message frame followed by a trailers frame
(`grpc-status: 0`). This peer implements the gRPC-web framing AND the protobuf wire format independently (per
spec, stdlib only - the same approach as the syslog / statsd / stomp / smb peers), so a genuine second
implementation frames + protobuf-encodes the request and decodes the device's response. It asserts the reply
message, the trailers status, and - by calling twice with different names - that the device really echoes the
argument (not a constant). That validates the device's grpcweb + protobuf codecs against an independent peer.
"""

from __future__ import annotations

import http.client
import re
import struct

from ._common import Probe

NAME = "grpcweb"
HELP = "exchange a gRPC-web Greeter call with the device via an independent spec client (device-as-server)"


# ---- minimal protobuf wire codec (spec; independent of the device's codec) ----
def _varint(n: int) -> bytes:
    out = bytearray()
    while True:
        b = n & 0x7F
        n >>= 7
        out.append(b | (0x80 if n else 0))
        if not n:
            return bytes(out)


def _read_varint(buf: bytes, i: int):
    val = shift = 0
    while True:
        b = buf[i]
        i += 1
        val |= (b & 0x7F) << shift
        shift += 7
        if not b & 0x80:
            return val, i


def _string_field(field: int, s: str) -> bytes:
    b = s.encode()
    return bytes([(field << 3) | 2]) + _varint(len(b)) + b  # wire type 2 = length-delimited


def _read_string_field(body: bytes, field: int):
    """Return the string value of length-delimited field number `field`, or None."""
    i = 0
    while i < len(body):
        tag, i = _read_varint(body, i)
        fn, wt = tag >> 3, tag & 7
        if wt == 2:
            ln, i = _read_varint(body, i)
            val = body[i : i + ln]
            i += ln
            if fn == field:
                return val.decode(errors="replace")
        elif wt == 0:
            _, i = _read_varint(body, i)
        elif wt == 5:
            i += 4
        elif wt == 1:
            i += 8
        else:
            return None
    return None


# ---- gRPC-web framing (spec) ----
def _frame(body: bytes, flags: int = 0) -> bytes:
    return bytes([flags]) + struct.pack(">I", len(body)) + body


def _iter_frames(buf: bytes):
    i = 0
    while i + 5 <= len(buf):
        flags = buf[i]
        ln = struct.unpack(">I", buf[i + 1 : i + 5])[0]
        yield flags, buf[i + 5 : i + 5 + ln]
        i += 5 + ln


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname (the gRPC-web server)")
    p.add_argument("--port", type=int, default=80, help="device HTTP port (default 80)")
    p.add_argument("--timeout", type=float, default=8.0, help="timeout seconds")


def run(args) -> bool:
    pr = Probe(f"grpcweb device={args.host}:{args.port}")

    # The rig enforces CSRF on POST: fetch a token from GET /csrf and echo it in X-CSRF-Token (rig preamble).
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

    def say_hello(name: str):
        """Frame a HelloRequest, POST it, return (status, content_type, [(flags, body), ...])."""
        headers = {"Content-Type": "application/grpc-web+proto"}
        if csrf:
            headers["X-CSRF-Token"] = csrf
        c = http.client.HTTPConnection(args.host, args.port, timeout=args.timeout)
        c.request("POST", "/grpc", body=_frame(_string_field(1, name)), headers=headers)
        rep = c.getresponse()
        status, ct, raw = rep.status, rep.getheader("Content-Type", ""), rep.read()
        c.close()
        return status, ct, list(_iter_frames(raw))

    try:
        status, ct, frames = say_hello("pc")
    except Exception as exc:  # noqa: BLE001
        pr.check("device POST /grpc completed", False, str(exc))
        return pr.summary()

    pr.check("device 200 + grpc-web content-type", status == 200 and "grpc-web" in ct, f"{status} {ct}")
    pr.check(
        "response = one message frame + one trailers frame",
        len(frames) == 2 and not (frames[0][0] & 0x80) and (frames[1][0] & 0x80),
        f"{len(frames)} frames, flags={[f for f, _ in frames]}",
    )
    if not frames:
        return pr.summary()

    reply = _read_string_field(frames[0][1], 1)
    pr.check("HelloReply.message == 'hello, pc'", reply == "hello, pc", f"reply={reply!r}")

    status_ok = False
    if len(frames) >= 2:
        trailer = frames[1][1].decode(errors="replace")
        m = re.search(r"grpc-status:\s*(\d+)", trailer)
        status_ok = bool(m) and m.group(1) == "0"
    pr.check("trailers frame carries grpc-status: 0 (OK)", status_ok, frames[1][1][:60] if len(frames) >= 2 else "")

    # A second call with a different name proves the device echoes the argument, not a constant.
    _, _, frames2 = say_hello("rig42")
    reply2 = _read_string_field(frames2[0][1], 1) if frames2 else None
    pr.check("echoes a different argument ('hello, rig42')", reply2 == "hello, rig42", f"reply={reply2!r}")

    return pr.summary()
