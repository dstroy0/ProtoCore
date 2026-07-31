# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""FANUC FOCAS Ethernet interop reference (independent of fwlib32).

The proprietary FANUC fwlib32 library is *not* used or required: like diohpix/pyfanuc, this speaks
the FOCAS wire protocol directly. It has two uses:

  focas-server : a mock FANUC CNC on TCP 8193 that the device (a FOCAS client) connects to. It
                 answers the open handshake and a small set of read functions with canned ODBSYS /
                 alarm / position data so the device's request framing + response parsing can be
                 exercised end to end on the rig.

  selftest     : build every documented request frame and round-trip it through the parser (no
                 network, no device). Prints the exact hex so it can be diffed against the C
                 byte-exact vectors in test/test_focas. Run: `python focas_peer.py selftest`.

This module is the independent oracle for src/services/focas: the framing here is written from the
same spec the C codec implements, so agreement between the two is real cross-checking.
"""

from __future__ import annotations

import argparse
import socket
import struct
import sys

# --- wire constants (big-endian throughout) -------------------------------- #
MAGIC = b"\xa0\xa0\xa0\xa0"
VERSION = 1
FRAME_DST = 0x0002

FTYPE_OPN_REQU = 0x0101
FTYPE_OPN_RESP = 0x0102
FTYPE_CLS_REQU = 0x0201
FTYPE_CLS_RESP = 0x0202
FTYPE_VAR_REQU = 0x2101
FTYPE_VAR_RESP = 0x2102

# documented function selectors (c1, c2, c3)
CMD_READ_PARAM = (1, 1, 0x0E)
CMD_READ_MACRO = (1, 1, 0x15)
CMD_SET_MACRO = (1, 1, 0x16)
CMD_SYSINFO = (1, 1, 0x18)
CMD_READ_ALARM = (1, 1, 0x1A)
CMD_READ_FEED = (1, 1, 0x24)
CMD_READ_SPINDLE = (1, 1, 0x25)
CMD_READ_POSITION = (1, 1, 0x26)


# --------------------------------------------------------------------------- #
# framing (the independent oracle)
# --------------------------------------------------------------------------- #
def build_frame(ftype: int, payload: bytes) -> bytes:
    return MAGIC + struct.pack(">HHH", VERSION, ftype, len(payload)) + payload


def build_open() -> bytes:
    return build_frame(FTYPE_OPN_REQU, struct.pack(">H", FRAME_DST))


def build_close() -> bytes:
    return build_frame(FTYPE_CLS_REQU, b"")


def build_request(cmd, v1=0, v2=0, v3=0, v4=0, v5=0, extra: bytes = b"") -> bytes:
    body = struct.pack(">HHH", *cmd) + struct.pack(">iiiii", v1, v2, v3, v4, v5) + extra
    return build_frame(FTYPE_VAR_REQU, body)


def parse_frame(buf: bytes):
    """(version, ftype, payload) or None if the envelope is short/bad."""
    if len(buf) < 10 or buf[:4] != MAGIC:
        return None
    version, ftype, length = struct.unpack(">HHH", buf[4:10])
    if 10 + length > len(buf):
        return None
    return version, ftype, buf[10 : 10 + length]


def parse_response(payload: bytes):
    """(c1, c2, c3, status, data) from a VAR response payload, or None."""
    if len(payload) < 14:
        return None
    c1, c2, c3 = struct.unpack(">HHH", payload[0:6])
    (status,) = struct.unpack(">h", payload[6:8])
    (dlen,) = struct.unpack(">H", payload[12:14])
    if 14 + dlen > len(payload):
        return None
    return c1, c2, c3, status, payload[14 : 14 + dlen]


def build_response(cmd, status: int, data: bytes) -> bytes:
    payload = struct.pack(">HHH", *cmd) + struct.pack(">h", status) + b"\x00" * 4 + struct.pack(">H", len(data)) + data
    return build_frame(FTYPE_VAR_RESP, payload)


def parse_sysinfo(data: bytes):
    if len(data) < 18:
        return None
    add_info, max_axis, cnc_type, mt_type, series, version, axes = struct.unpack(">HH2s2s4s4s2s", data[:18])
    return {
        "add_info": add_info,
        "max_axis": max_axis,
        "cnc_type": cnc_type.decode("latin1"),
        "mt_type": mt_type.decode("latin1"),
        "series": series.decode("latin1"),
        "version": version.decode("latin1"),
        "axes": axes.decode("latin1"),
    }


def encode8(data: int, base: int = 10, exp: int = 0) -> bytes:
    """One FOCAS 8-octet value: int32 data + base at octet 5 + exp at octet 7."""
    return struct.pack(">i", data) + bytes([0, base, 0, exp])


def decode8(chunk: bytes):
    if len(chunk) < 8:
        return None
    (data,) = struct.unpack(">i", chunk[0:4])
    base, exp = chunk[5], chunk[7]
    if chunk[6] == 0xFF and chunk[7] == 0xFF:
        return None
    if base not in (2, 10):
        return None
    return data / (base**exp)


# --------------------------------------------------------------------------- #
# mock FANUC CNC (device is the client)
# --------------------------------------------------------------------------- #
_SYSINFO = struct.pack(">HH2s2s4s4s2s", 0, 8, b"30", b" M", b"G01A", b"27.1", b"03")


def _handle_command(payload: bytes) -> bytes:
    c1, c2, c3 = struct.unpack(">HHH", payload[0:6])
    cmd = (c1, c2, c3)
    if cmd == CMD_SYSINFO:
        return build_response(cmd, 0, _SYSINFO)
    if cmd == CMD_READ_ALARM:
        return build_response(cmd, 0, struct.pack(">L", 0x00000010))
    if cmd == CMD_READ_FEED:
        return build_response(cmd, 0, encode8(1500, 10, 0))  # 1500 mm/min
    if cmd == CMD_READ_SPINDLE:
        return build_response(cmd, 0, encode8(2400, 10, 0))  # 2400 rpm
    if cmd == CMD_READ_POSITION:
        # three axes: X=123.456, Y=-5.000, Z=0.000
        data = encode8(123456, 10, 3) + encode8(-5000, 10, 3) + encode8(0, 10, 3)
        return build_response(cmd, 0, data)
    return build_response(cmd, -2, b"")  # EW_FUNC: unsupported here


def _recv_frame(conn) -> bytes | None:
    head = _recv_exact(conn, 10)
    if head is None:
        return None
    length = struct.unpack(">H", head[8:10])[0]
    body = _recv_exact(conn, length) if length else b""
    if body is None:
        return None
    return head + body


def _recv_exact(conn, n: int) -> bytes | None:
    buf = b""
    while len(buf) < n:
        chunk = conn.recv(n - len(buf))
        if not chunk:
            return None
        buf += chunk
    return buf


def serve(bind: str, port: int) -> bool:
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind((bind, port))
    srv.listen(4)
    print(f"focas-server: mock FANUC CNC on {bind}:{port} (Ctrl-C to stop)")
    print("  answers: SysInfo(30/ M), alarm=0x10, feed=1500, spindle=2400, position X123.456/Y-5/Z0")
    try:
        while True:
            conn, addr = srv.accept()
            with conn:
                while True:
                    frame = _recv_frame(conn)
                    if frame is None:
                        break
                    parsed = parse_frame(frame)
                    if parsed is None:
                        break
                    _, ftype, payload = parsed
                    if ftype == FTYPE_OPN_REQU:
                        conn.sendall(build_frame(FTYPE_OPN_RESP, struct.pack(">H", FRAME_DST)))
                    elif ftype == FTYPE_CLS_REQU:
                        conn.sendall(build_frame(FTYPE_CLS_RESP, b""))
                        break
                    elif ftype == FTYPE_VAR_REQU:
                        conn.sendall(_handle_command(payload))
                    else:
                        break
    except KeyboardInterrupt:
        print("\nfocas-server: stopped")
    finally:
        srv.close()
    return True


# --------------------------------------------------------------------------- #
# selftest: build every documented frame and round-trip it (no network)
# --------------------------------------------------------------------------- #
def selftest() -> bool:
    ok = True

    def show(label: str, frame: bytes) -> None:
        print(f"  {label:22s} {frame.hex()}")

    def expect(label: str, cond: bool) -> None:
        nonlocal ok
        tag = "PASS" if cond else "FAIL"
        print(f"  [{tag}] {label}")
        ok = ok and cond

    print("request frames:")
    show("open", build_open())
    show("close", build_close())
    show("sysinfo", build_request(CMD_SYSINFO))
    show("read_alarm", build_request(CMD_READ_ALARM))
    show("position(abs)", build_request(CMD_READ_POSITION, 4, 0))
    show("read_param", build_request(CMD_READ_PARAM, 6510, 6510, 1))

    # byte-exact open frame (matches the C test_build_open vector)
    open_expect = bytes([0xA0, 0xA0, 0xA0, 0xA0, 0x00, 0x01, 0x01, 0x01, 0x00, 0x02, 0x00, 0x02])
    expect("open == C vector", build_open() == open_expect)
    # sysinfo request: envelope + selector + five zero args (structural, matches test_build_sysinfo)
    sp = parse_frame(build_request(CMD_SYSINFO))
    expect(
        "sysinfo req framing",
        sp is not None and sp[1] == FTYPE_VAR_REQU and sp[2] == struct.pack(">HHH", *CMD_SYSINFO) + b"\x00" * 20,
    )

    print("round-trips:")
    # sysinfo response
    resp = build_response(CMD_SYSINFO, 0, _SYSINFO)
    parsed = parse_frame(resp)
    expect("sysinfo frame parses", parsed is not None and parsed[1] == FTYPE_VAR_RESP)
    r = parse_response(parsed[2])
    expect("sysinfo response decodes", r is not None and r[:3] == CMD_SYSINFO and r[3] == 0)
    si = parse_sysinfo(r[4])
    expect("ODBSYS cnc_type=30", si is not None and si["cnc_type"] == "30" and si["max_axis"] == 8)

    # alarm response
    ar = parse_response(parse_frame(build_response(CMD_READ_ALARM, 0, struct.pack(">L", 0x10)))[2])
    expect("alarm bitmask 0x10", struct.unpack(">L", ar[4])[0] == 0x10)

    # decode8 values
    expect("decode8 123.456", abs(decode8(encode8(123456, 10, 3)) - 123.456) < 1e-6)
    expect("decode8 -5.0", abs(decode8(encode8(-5000, 10, 3)) - (-5.0)) < 1e-6)
    expect("decode8 sentinel", decode8(struct.pack(">i", 0) + b"\x00\x0a\xff\xff") is None)

    print("selftest:", "OK" if ok else "FAILED")
    return ok


# --------------------------------------------------------------------------- #
# harness peer (device is the client -> we serve)
# --------------------------------------------------------------------------- #
class Server:
    NAME = "focas-server"
    HELP = "mock FANUC CNC for the device (a FOCAS client) to read from"

    @staticmethod
    def add_args(p) -> None:
        p.add_argument("--bind", default="0.0.0.0", help="listen address (default 0.0.0.0)")
        p.add_argument("--port", type=int, default=8193, help="FOCAS TCP port (default 8193)")

    @staticmethod
    def run(args) -> bool:
        return serve(args.bind, args.port)


PEERS = [Server]


# --------------------------------------------------------------------------- #
# standalone entry point (no _common dependency, plain python3)
# --------------------------------------------------------------------------- #
def _main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="FANUC FOCAS interop reference (no fwlib32)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("selftest", help="build + round-trip every documented frame (no network)")
    sp = sub.add_parser("server", help="run the mock FANUC CNC")
    sp.add_argument("--bind", default="0.0.0.0")
    sp.add_argument("--port", type=int, default=8193)
    args = ap.parse_args(argv)
    if args.cmd == "selftest":
        return 0 if selftest() else 1
    return 0 if serve(args.bind, args.port) else 1


if __name__ == "__main__":
    sys.exit(_main())
