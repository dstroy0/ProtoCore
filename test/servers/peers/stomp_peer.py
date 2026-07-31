# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""STOMP 1.2 interop: drive the device as a STOMP client against a spec broker (stdlib).

Role: the *device is the client*. This peer stands up a self-contained STOMP 1.2 broker in a background
thread (a spec-correct frame parser/encoder written fresh, independent of the library), triggers the device
(via the rig's GET /stomp/probe route) to connect out and run CONNECT/SUBSCRIBE/SEND, routes the device's
SEND back to its own SUBSCRIBE as a MESSAGE, and confirms - as the authoritative receiver - that the frame
the device built is well-formed and carries the expected body. That closes the loop: the device's
CONNECT/SUBSCRIBE/SEND builder + CONNECTED/MESSAGE parser interoperate with an independent STOMP 1.2 impl.

  1. start the broker; the device connects and CONNECT -> we reply CONNECTED
  2. trigger GET /stomp/probe on the device -> {stomp, sub, send, msg}
  3. the broker received a well-formed SEND to the destination with the expected body

Self-contained - no external broker needed (coilmq / RabbitMQ-STOMP are production references).
"""

from __future__ import annotations

import http.client
import json
import socket
import threading
import time

from ._common import Probe

NAME = "stomp"
HELP = "drive the device as a STOMP client against a spec STOMP 1.2 broker (device-as-client)"

_PAYLOAD = b"hello-from-pc-rig"  # must match the rig's h_stomp_probe SEND body


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname (the rig)")
    p.add_argument("--port", type=int, default=80, help="device HTTP port (default 80)")
    p.add_argument("--broker-host", help="broker address the device dials (default: this machine's route IP)")
    p.add_argument("--broker-port", type=int, default=61613, help="STOMP broker port to serve (default 61613)")
    p.add_argument("--timeout", type=float, default=8.0, help="timeout seconds")


def _parse_frame(raw: bytes):
    """Parse one STOMP frame (bytes before its NUL) -> (command, headers dict, body bytes)."""
    head, _, body = raw.partition(b"\n\n")
    lines = head.split(b"\n")
    command = lines[0].decode("latin1", "replace").strip()
    headers = {}
    for ln in lines[1:]:
        if b":" in ln:
            k, _, v = ln.partition(b":")
            headers.setdefault(k.decode("latin1", "replace"), v.decode("latin1", "replace"))
    return command, headers, body


class _Broker(threading.Thread):
    """A minimal spec STOMP 1.2 broker: CONNECT->CONNECTED, SUBSCRIBE registers, SEND routes back to the
    subscribers of the destination as MESSAGE (so the device receives its own SEND). Captures SEND frames."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("0.0.0.0", port))
        self.sock.listen(8)
        self.sock.settimeout(20)
        self.sent = []  # captured (destination, body) from SEND frames
        self._stop = False
        self._mid = 0

    def run(self):
        while not self._stop:
            try:
                conn, _ = self.sock.accept()
            except OSError:
                break
            threading.Thread(target=self._serve, args=(conn,), daemon=True).start()

    def _serve(self, conn):
        subs = {}  # destination -> subscription id
        buf = b""
        conn.settimeout(8)
        try:
            while not self._stop:
                try:
                    d = conn.recv(4096)
                except OSError:
                    break
                if not d:
                    break
                buf += d
                while b"\x00" in buf:
                    raw, _, buf = buf.partition(b"\x00")
                    raw = raw.lstrip(b"\r\n")  # skip inter-frame EOLs / heart-beats
                    if not raw:
                        continue
                    command, headers, body = _parse_frame(raw)
                    if command == "CONNECT" or command == "STOMP":
                        conn.sendall(b"CONNECTED\nversion:1.2\nheart-beat:0,0\n\n\x00")
                    elif command == "SUBSCRIBE":
                        subs[headers.get("destination", "")] = headers.get("id", "0")
                    elif command == "SEND":
                        dest = headers.get("destination", "")
                        clen = headers.get("content-length")
                        payload = body[: int(clen)] if (clen and clen.isdigit()) else body
                        self.sent.append((dest, payload))
                        if dest in subs:  # route back to this client's own subscription
                            msg = (
                                b"MESSAGE\ndestination:%s\nmessage-id:%d\nsubscription:%s\ncontent-length:%d\n\n%s\x00"
                                % (dest.encode(), self._mid, subs[dest].encode(), len(payload), payload)
                            )
                            self._mid += 1
                            conn.sendall(msg)
                    elif command == "DISCONNECT":
                        break
        except OSError:
            pass
        finally:
            try:
                conn.close()
            except OSError:
                pass

    def stop(self):
        self._stop = True
        try:
            self.sock.close()
        except OSError:
            pass


def run(args) -> bool:
    pr = Probe(f"stomp device={args.host} broker=:{args.broker_port}")

    broker_host = args.broker_host
    if not broker_host:
        try:
            s = socket.create_connection((args.host, args.port), timeout=args.timeout)
            broker_host = s.getsockname()[0]
            s.close()
        except OSError as exc:  # noqa: BLE001
            pr.check("reach the device", False, str(exc))
            return pr.summary()

    try:
        broker = _Broker(args.broker_port)
        broker.start()
    except OSError as exc:  # noqa: BLE001
        pr.check("start STOMP broker", False, str(exc))
        return pr.summary()

    try:
        pr.info(f"STOMP broker on {broker_host}:{args.broker_port}")
        try:
            q = f"/stomp/probe?host={broker_host}&port={args.broker_port}&dest=/topic/pc"
            c = http.client.HTTPConnection(args.host, args.port, timeout=args.timeout)
            c.request("GET", q)
            rep = json.loads(c.getresponse().read())
            c.close()
        except Exception as exc:  # noqa: BLE001
            pr.check("device /stomp/probe completed", False, str(exc))
            return pr.summary()

        pr.check("device connected to broker", rep.get("connected") == 1, json.dumps(rep))
        pr.check("device got CONNECTED", rep.get("stomp") == 1, f"stomp={rep.get('stomp')}")
        pr.check("device SUBSCRIBE accepted", rep.get("sub") == 1, f"sub={rep.get('sub')}")
        pr.check("device SEND accepted", rep.get("send") == 1, f"send={rep.get('send')}")
        pr.check("device received its own MESSAGE back", rep.get("msg") == 1, f"msg={rep.get('msg')}")

        # authoritative: the broker received a well-formed SEND to the destination with the expected body.
        landed = next(((d, b) for (d, b) in broker.sent if b == _PAYLOAD), None)
        pr.check("broker received the device's SEND", landed is not None, f"{len(broker.sent)} SEND frame(s)")
        if landed:
            pr.check("SEND destination is /topic/pc", landed[0] == "/topic/pc", landed[0])
    finally:
        broker.stop()

    return pr.summary()
