# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""FANUC Stream Motion (option J519) interop reference.

No FANUC software is used or required: this speaks the J519 UDP wire protocol directly, from the
layout published by the Wireshark dissector fanuc-stream-motion/packet-fanuc-stream-motion-j519.
It has two uses:

  j519-robot : a mock FANUC robot controller on UDP 60015 that the device (the streaming
               controller) sends to. It accepts Start, answers every Motion Command with a Robot
               Status echoing the sequence number and reporting a pose, answers a Request with an
               Ack carrying the threshold tables, and stops on Stop - so the device's packet
               framing and its Status/Ack parsing are exercised end to end on the rig.

  selftest   : build and round-trip every packet with no network and no device, printing the exact
               hex so it can be diffed against the C byte-exact vectors in test/test_fanuc_j519.
               Run: `python fanuc_j519_peer.py selftest`.

This module is the independent oracle for src/services/fanuc_j519. The C codec packs fields by
hand-written offset arithmetic; here the same packets are expressed as `struct` format strings, so
the two are genuinely different expressions of one layout and agreement between them is real
cross-checking (an offset typo on either side shows up immediately).
"""

from __future__ import annotations

import argparse
import socket
import struct
import sys

# --- wire constants (LITTLE-endian throughout, unlike FOCAS) ---------------- #
UDP_PORT = 60015
AXES = 9
THRESHOLDS = 20

# packet type word - the numeric space is reused per direction
T_START_OR_STATUS = 0  # PC->robot Start; robot->PC Robot Status
T_MOTION = 1  # PC->robot Motion Command
T_STOP = 2  # PC->robot Stop
T_REQUEST_OR_ACK = 3  # PC->robot Request; robot->PC Ack

# struct layouts - the byte offsets are implied by field order under '<' (no padding)
F_HDR = "<II"  # type, version                                        -> 8
F_MOTION = "<IIIBBHHBBHHHH9f"  # + seq, last, rIO(t,i,m), style, wIO(t,i,m,v), pad, 9 setpoints -> 64
F_STATUS = "<IIIBBHHHI9f9f9f"  # + seq, status, rIO(t,i,m,v), stamp, cart/joint/current -> 132
F_REQUEST = "<IIII"  # + axis, threshold type                         -> 16
F_ACK = "<IIIIII20f20f"  # + axis, thr type, max cart speed, unk, tables -> 184

LEN_START = LEN_STOP = 8
LEN_MOTION = 64
LEN_STATUS = 132
LEN_REQUEST = 16
LEN_ACK = 184

# status bits
ST_READY = 0x01
ST_CMD_RECEIVED = 0x02
ST_SYSRDY = 0x04
ST_IN_MOTION = 0x08


# --------------------------------------------------------------------------- #
# builders / parsers (the oracle)
# --------------------------------------------------------------------------- #
def build_start(version=1) -> bytes:
    return struct.pack(F_HDR, T_START_OR_STATUS, version)


def build_stop(version=1) -> bytes:
    return struct.pack(F_HDR, T_STOP, version)


def build_motion(
    seq,
    joints,
    version=1,
    last_data=0,
    read_io=(0, 0, 0),
    style=1,
    write_io=(0, 0, 0, 0),
) -> bytes:
    rt, ri, rm = read_io
    wt, wi, wm, wv = write_io
    j = list(joints) + [0.0] * (AXES - len(joints))
    return struct.pack(
        F_MOTION, T_MOTION, version, seq, last_data, rt, ri, rm, style, wt, wi, wm, wv, 0, *j
    )


def build_status(
    seq, cart, joint, current, version=1, status=ST_READY | ST_SYSRDY, read_io=(0, 0, 0, 0), stamp=0
) -> bytes:
    rt, ri, rm, rv = read_io
    pad = lambda v: list(v) + [0.0] * (AXES - len(v))  # noqa: E731
    return struct.pack(
        F_STATUS,
        T_START_OR_STATUS,
        version,
        seq,
        status,
        rt,
        ri,
        rm,
        rv,
        stamp,
        *pad(cart),
        *pad(joint),
        *pad(current),
    )


def build_request(axis, threshold_type, version=1) -> bytes:
    return struct.pack(F_REQUEST, T_REQUEST_OR_ACK, version, axis, threshold_type)


def build_ack(axis, threshold_type, no_load, max_load, version=1, max_cart_speed=2000, unk=0) -> bytes:
    pad = lambda v: list(v) + [0.0] * (THRESHOLDS - len(v))  # noqa: E731
    return struct.pack(
        F_ACK,
        T_REQUEST_OR_ACK,
        version,
        axis,
        threshold_type,
        max_cart_speed,
        unk,
        *pad(no_load),
        *pad(max_load),
    )


def parse_motion(b: bytes) -> dict:
    if len(b) != LEN_MOTION:
        raise ValueError(f"motion must be {LEN_MOTION} octets, got {len(b)}")
    f = struct.unpack(F_MOTION, b)
    if f[0] != T_MOTION:
        raise ValueError(f"not a motion command (type {f[0]})")
    return dict(
        version=f[1],
        seq=f[2],
        last_data=f[3],
        read_io=(f[4], f[5], f[6]),
        style=f[7],
        write_io=(f[8], f[9], f[10], f[11]),
        joints=list(f[13:]),
    )


def parse_status(b: bytes) -> dict:
    if len(b) != LEN_STATUS:
        raise ValueError(f"status must be {LEN_STATUS} octets, got {len(b)}")
    f = struct.unpack(F_STATUS, b)
    if f[0] != T_START_OR_STATUS:
        raise ValueError(f"not a robot status (type {f[0]})")
    return dict(
        version=f[1],
        seq=f[2],
        status=f[3],
        read_io=(f[4], f[5], f[6], f[7]),
        stamp=f[8],
        cart=list(f[9 : 9 + AXES]),
        joint=list(f[9 + AXES : 9 + 2 * AXES]),
        current=list(f[9 + 2 * AXES :]),
    )


def parse_request(b: bytes) -> dict:
    if len(b) != LEN_REQUEST:
        raise ValueError(f"request must be {LEN_REQUEST} octets, got {len(b)}")
    f = struct.unpack(F_REQUEST, b)
    if f[0] != T_REQUEST_OR_ACK:
        raise ValueError(f"not a request (type {f[0]})")
    return dict(version=f[1], axis=f[2], threshold_type=f[3])


def parse_ack(b: bytes) -> dict:
    if len(b) != LEN_ACK:
        raise ValueError(f"ack must be {LEN_ACK} octets, got {len(b)}")
    f = struct.unpack(F_ACK, b)
    if f[0] != T_REQUEST_OR_ACK:
        raise ValueError(f"not an ack (type {f[0]})")
    return dict(
        version=f[1],
        axis=f[2],
        threshold_type=f[3],
        max_cart_speed=f[4],
        unknown0=f[5],
        no_load=list(f[6 : 6 + THRESHOLDS]),
        max_load=list(f[6 + THRESHOLDS :]),
    )


def peek(b: bytes):
    if len(b) < 8:
        raise ValueError("short packet")
    return struct.unpack(F_HDR, b[:8])


# --------------------------------------------------------------------------- #
# selftest (no network, no device)
# --------------------------------------------------------------------------- #
def selftest() -> bool:
    ok = True

    def check(label, cond, detail=""):
        nonlocal ok
        print(f"  [{'PASS' if cond else 'FAIL'}] {label}" + (f" ({detail})" if detail else ""))
        ok = ok and cond

    # exact sizes
    check("start is 8 octets", len(build_start()) == LEN_START)
    check("stop is 8 octets", len(build_stop()) == LEN_STOP)
    check("motion is 64 octets", len(build_motion(1, [0.0] * AXES)) == LEN_MOTION)
    check("status is 132 octets", len(build_status(1, [], [], [])) == LEN_STATUS)
    check("request is 16 octets", len(build_request(1, 0)) == LEN_REQUEST)
    check("ack is 184 octets", len(build_ack(1, 0, [], [])) == LEN_ACK)

    # the IEEE-754 binary32 little-endian encoding the C vectors pin
    m = build_motion(0x11223344, [1.0, -2.5] + [0.0] * (AXES - 2), last_data=1, style=1)
    check("1.0f encodes 00 00 80 3f at offset 28", m[28:32] == bytes.fromhex("0000803f"), m[28:32].hex())
    check("-2.5f encodes 00 00 20 c0 at offset 32", m[32:36] == bytes.fromhex("000020c0"), m[32:36].hex())
    check("sequence is little-endian at 8", m[8:12] == bytes.fromhex("44332211"), m[8:12].hex())
    check("unused octets 26..27 are zero", m[26:28] == b"\x00\x00")

    # round trips
    got = parse_motion(m)
    check("motion round-trips", got["seq"] == 0x11223344 and got["joints"][1] == -2.5)
    s = build_status(7, [1.0], [-2.5], [0.5], stamp=0x99AABBCC)
    got = parse_status(s)
    check(
        "status round-trips",
        got["seq"] == 7 and got["cart"][0] == 1.0 and got["joint"][0] == -2.5 and got["stamp"] == 0x99AABBCC,
    )
    check("status blocks sit at 24 / 60 / 96", s[24:28] == bytes.fromhex("0000803f") and s[60:64] == bytes.fromhex("000020c0"))
    a = build_ack(6, 2, [float(i) for i in range(THRESHOLDS)], [float(1000 + i) for i in range(THRESHOLDS)])
    got = parse_ack(a)
    check("ack round-trips", got["axis"] == 6 and got["no_load"][1] == 1.0 and got["max_load"][0] == 1000.0)
    check("ack tables sit at 24 / 104", a[24 + 4 : 24 + 8] == bytes.fromhex("0000803f"))

    # the shared type codes are separated by length, not by the type word
    check("start and status share type 0", peek(build_start())[0] == peek(build_status(0, [], [], []))[0] == 0)
    check("request and ack share type 3", peek(build_request(0, 0))[0] == peek(build_ack(0, 0, [], []))[0] == 3)

    print("\n  hex vectors (diff against test/test_fanuc_j519):")
    print(f"    start  : {build_start().hex()}")
    print(f"    stop   : {build_stop().hex()}")
    print(f"    motion : {m.hex()}")
    print(f"    request: {build_request(3, 1).hex()}")
    print(f"\nfanuc_j519 selftest: {'OK' if ok else 'FAILED'}")
    return ok


# --------------------------------------------------------------------------- #
# mock robot controller (device is the streaming controller -> we serve)
# --------------------------------------------------------------------------- #
def serve(bind: str, port: int, max_packets: int = 0) -> bool:
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind((bind, port))
    print(f"j519-robot: listening on {bind}:{port} (UDP)")
    seen = 0
    streaming = False
    try:
        while True:
            data, addr = sock.recvfrom(2048)
            seen += 1
            if len(data) < 8:
                print(f"  short packet ({len(data)} octets) from {addr} - ignored")
                continue
            ptype, _ver = peek(data)

            if ptype == T_START_OR_STATUS and len(data) == LEN_START:
                streaming = True
                print(f"  START from {addr}")
            elif ptype == T_STOP and len(data) == LEN_STOP:
                streaming = False
                print(f"  STOP from {addr}")
            elif ptype == T_MOTION and len(data) == LEN_MOTION:
                m = parse_motion(data)
                # echo the sequence number and report the commanded joints as measured
                reply = build_status(
                    m["seq"],
                    cart=[float(i) for i in range(AXES)],
                    joint=m["joints"],
                    current=[0.5] * AXES,
                    status=(ST_READY | ST_SYSRDY | (ST_IN_MOTION if streaming else 0) | ST_CMD_RECEIVED),
                    stamp=seen,
                )
                sock.sendto(reply, addr)
                if m["seq"] % 100 == 0 or m["last_data"]:
                    print(f"  MOTION seq={m['seq']} last={m['last_data']} -> status sent")
            elif ptype == T_REQUEST_OR_ACK and len(data) == LEN_REQUEST:
                q = parse_request(data)
                sock.sendto(
                    build_ack(
                        q["axis"],
                        q["threshold_type"],
                        no_load=[float(i) for i in range(THRESHOLDS)],
                        max_load=[float(1000 + i) for i in range(THRESHOLDS)],
                    ),
                    addr,
                )
                print(f"  REQUEST axis={q['axis']} type={q['threshold_type']} -> ack sent")
            else:
                print(f"  unrecognised packet type={ptype} len={len(data)} from {addr}")

            if max_packets and seen >= max_packets:
                return True
    except KeyboardInterrupt:
        return True
    finally:
        sock.close()


# --------------------------------------------------------------------------- #
# harness peer
# --------------------------------------------------------------------------- #
class Server:
    NAME = "j519-robot"
    HELP = "mock FANUC robot (Stream Motion J519) for the device to stream setpoints to"

    @staticmethod
    def add_args(p) -> None:
        p.add_argument("--bind", default="0.0.0.0", help="listen address (default 0.0.0.0)")
        p.add_argument("--port", type=int, default=UDP_PORT, help=f"J519 UDP port (default {UDP_PORT})")

    @staticmethod
    def run(args) -> bool:
        return serve(args.bind, args.port)


PEERS = [Server]


# --------------------------------------------------------------------------- #
# standalone entry point (no _common dependency, plain python3)
# --------------------------------------------------------------------------- #
def _main(argv=None) -> int:
    ap = argparse.ArgumentParser(description="FANUC Stream Motion (J519) interop reference")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("selftest", help="build + round-trip every packet (no network)")
    sp = sub.add_parser("server", help="run the mock FANUC robot controller")
    sp.add_argument("--bind", default="0.0.0.0")
    sp.add_argument("--port", type=int, default=UDP_PORT)
    args = ap.parse_args(argv)
    if args.cmd == "selftest":
        return 0 if selftest() else 1
    return 0 if serve(args.bind, args.port) else 1


if __name__ == "__main__":
    sys.exit(_main())
