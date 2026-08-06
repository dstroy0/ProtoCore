# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""WAMP interop: drive the device as a WAMP client against a spec router over WebSocket (stdlib).

Role: the *device is the client* (a WAMP subscriber/publisher/caller, per wamp.h - the codec builds the client
messages HELLO/SUBSCRIBE/PUBLISH/CALL and parses the router's WELCOME/SUBSCRIBED/EVENT/RESULT). This peer stands
up a self-contained WAMP-over-WebSocket router in a background thread - an RFC 6455 server handshake that selects
the `wamp.2.json` subprotocol, plus a spec WAMP broker+dealer written fresh, independent of the library - and
triggers the device (via the rig's GET /wamp/probe route) to connect out and run the full client dialogue:

  1. the device sends the WS Upgrade (Sec-WebSocket-Protocol: wamp.2.json) -> we 101 + select the subprotocol
  2. the device sends HELLO -> we send WELCOME (broker+dealer roles + a session id)
  3. the device sends SUBSCRIBE <topic> -> we send SUBSCRIBED (a subscription id)
  4. the device sends PUBLISH <topic> (acknowledge, exclude_me=false) -> we send PUBLISHED + route an EVENT with
     the payload back to the device's own subscription (so the pub/sub loop closes on one connection)
  5. the device sends CALL <procedure> -> we send RESULT echoing the arguments
  6. trigger GET /wamp/probe on the device -> {connected, welcome, subscribed, published, event, result}

That closes the loop: the device's HELLO/SUBSCRIBE/PUBLISH/CALL builders + WELCOME/SUBSCRIBED/PUBLISHED/EVENT/
RESULT parser interoperate with an independent WAMP router over a real WebSocket. Self-contained - no external
router needed (crossbar / autobahn are the production references).
"""

from __future__ import annotations

import base64
import hashlib
import http.client
import json
import socket
import struct
import threading

from ._common import Probe

NAME = "wamp"
HELP = "drive the device as a WAMP client against a spec router over WebSocket (device-as-client)"

_ARG = "hello-from-pc-rig"  # must match the rig's h_wamp_probe PUBLISH argument
_WS_MAGIC = b"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

# WAMP message type codes.
HELLO, WELCOME, GOODBYE = 1, 2, 6
PUBLISH, PUBLISHED = 16, 17
SUBSCRIBE, SUBSCRIBED = 32, 33
EVENT = 36
CALL, RESULT = 48, 50


def _accept(key: str) -> str:
    return base64.b64encode(hashlib.sha1(key.encode() + _WS_MAGIC).digest()).decode()


class _Router(threading.Thread):
    """A minimal spec WAMP-over-WebSocket router: WS handshake (wamp.2.json), broker (pub/sub) + dealer (rpc)."""

    def __init__(self, port):
        super().__init__(daemon=True)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("0.0.0.0", port))
        self.sock.listen(4)
        self.sock.settimeout(20)
        self._stop = False
        self._id = 1000
        # authoritative observations:
        self.subprotocol_ok = False  # the device offered Sec-WebSocket-Protocol: wamp.2.json
        self.hello_realm = None  # the realm URI from the device's HELLO
        self.subscribed_topic = None  # the topic the device SUBSCRIBEd
        self.published = []  # [(topic, args)] captured PUBLISH deliveries
        self.called = []  # [(procedure, args)] captured CALLs

    def _next_id(self):
        self._id += 1
        return self._id

    def run(self):
        while not self._stop:
            try:
                conn, _ = self.sock.accept()
            except OSError:
                break
            threading.Thread(target=self._serve, args=(conn,), daemon=True).start()

    def _recv_at_least(self, conn, buf, n):
        while len(buf) < n:
            d = conn.recv(4096)
            if not d:
                return None
            buf += d
        return buf

    def _read_ws(self, conn, buf):
        """Read one (masked) client WS frame. Returns (opcode, payload, buf) or (None, None, buf) on close."""
        buf = self._recv_at_least(conn, buf, 2)
        if buf is None:
            return None, None, b""
        b1 = buf[1]
        masked = b1 & 0x80
        ln = b1 & 0x7F
        idx = 2
        if ln == 126:
            buf = self._recv_at_least(conn, buf, 4)
            if buf is None:
                return None, None, b""
            ln = struct.unpack("!H", buf[2:4])[0]
            idx = 4
        elif ln == 127:
            buf = self._recv_at_least(conn, buf, 10)
            if buf is None:
                return None, None, b""
            ln = struct.unpack("!Q", buf[2:10])[0]
            idx = 10
        mask = b""
        if masked:
            buf = self._recv_at_least(conn, buf, idx + 4)
            if buf is None:
                return None, None, b""
            mask = buf[idx : idx + 4]
            idx += 4
        buf = self._recv_at_least(conn, buf, idx + ln)
        if buf is None:
            return None, None, b""
        payload = bytearray(buf[idx : idx + ln])
        if masked:
            for i in range(ln):
                payload[i] ^= mask[i % 4]
        opcode = buf[0] & 0x0F
        return opcode, bytes(payload), buf[idx + ln :]

    @staticmethod
    def _send_ws_text(conn, s: str):
        data = s.encode()
        n = len(data)
        hdr = bytes([0x81])  # FIN + text (server frames are not masked)
        if n < 126:
            hdr += bytes([n])
        elif n < 65536:
            hdr += bytes([126]) + struct.pack("!H", n)
        else:
            hdr += bytes([127]) + struct.pack("!Q", n)
        conn.sendall(hdr + data)

    def _send_wamp(self, conn, msg):
        self._send_ws_text(conn, json.dumps(msg))

    def _serve(self, conn):
        conn.settimeout(8)
        try:
            req = b""
            while b"\r\n\r\n" not in req:
                d = conn.recv(1024)
                if not d:
                    return
                req += d
            key = None
            for line in req.split(b"\r\n"):
                low = line.lower()
                if low.startswith(b"sec-websocket-key:"):
                    key = line.split(b":", 1)[1].strip().decode()
                elif low.startswith(b"sec-websocket-protocol:"):
                    self.subprotocol_ok = b"wamp.2.json" in low
            if not key:
                return
            conn.sendall(
                (
                    "HTTP/1.1 101 Switching Protocols\r\n"
                    "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                    f"Sec-WebSocket-Accept: {_accept(key)}\r\n"
                    "Sec-WebSocket-Protocol: wamp.2.json\r\n\r\n"
                ).encode()
            )

            subs = {}  # topic -> subscription id
            buf = b""
            while not self._stop:
                opcode, payload, buf = self._read_ws(conn, buf)
                if opcode is None or opcode == 0x8:  # closed / CLOSE frame
                    break
                if opcode == 0x9:  # PING -> PONG
                    conn.sendall(bytes([0x8A, 0]))
                    continue
                if opcode != 0x1:  # only text (JSON) carries WAMP
                    continue
                try:
                    msg = json.loads(payload)
                except json.JSONDecodeError:
                    continue
                if not isinstance(msg, list) or not msg:
                    continue
                mtype = msg[0]
                if mtype == HELLO:  # [1, realm, details]
                    self.hello_realm = msg[1] if len(msg) > 1 else None
                    self._send_wamp(conn, [WELCOME, self._next_id(), {"roles": {"broker": {}, "dealer": {}}}])
                elif mtype == SUBSCRIBE:  # [32, request, options, topic]
                    request, topic = msg[1], msg[3]
                    sub_id = self._next_id()
                    subs[topic] = sub_id
                    self.subscribed_topic = topic
                    self._send_wamp(conn, [SUBSCRIBED, request, sub_id])
                elif mtype == PUBLISH:  # [16, request, options, topic, args?, kwargs?]
                    request, options, topic = msg[1], msg[2], msg[3]
                    args = msg[4] if len(msg) > 4 else []
                    self.published.append((topic, args))
                    pub_id = self._next_id()
                    if options.get("acknowledge"):
                        self._send_wamp(conn, [PUBLISHED, request, pub_id])
                    # HttpRoute the event back to the publisher's own subscription when exclude_me is false.
                    if topic in subs and options.get("exclude_me", True) is False:
                        self._send_wamp(conn, [EVENT, subs[topic], pub_id, {}, args])
                elif mtype == CALL:  # [48, request, options, procedure, args?, kwargs?]
                    request, procedure = msg[1], msg[3]
                    args = msg[4] if len(msg) > 4 else []
                    self.called.append((procedure, args))
                    self._send_wamp(conn, [RESULT, request, {}, args])
                elif mtype == GOODBYE:  # [6, details, reason]
                    self._send_wamp(conn, [GOODBYE, {}, "wamp.close.goodbye_and_out"])
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


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname (the rig)")
    p.add_argument("--port", type=int, default=80, help="device HTTP port (default 80)")
    p.add_argument("--router-host", help="router address the device dials (default: this machine's route IP)")
    p.add_argument("--router-port", type=int, default=8080, help="WAMP router WS port to serve (default 8080)")
    p.add_argument("--topic", default="com.pc.topic", help="expected SUBSCRIBE/PUBLISH topic")
    p.add_argument("--timeout", type=float, default=10.0, help="timeout seconds")


def run(args) -> bool:
    pr = Probe(f"wamp device={args.host} router=:{args.router_port}")

    router_host = args.router_host
    if not router_host:
        try:
            s = socket.create_connection((args.host, args.port), timeout=args.timeout)
            router_host = s.getsockname()[0]
            s.close()
        except OSError as exc:  # noqa: BLE001
            pr.check("reach the device", False, str(exc))
            return pr.summary()

    try:
        router = _Router(args.router_port)
        router.start()
    except OSError as exc:  # noqa: BLE001
        pr.check("start WAMP router", False, str(exc))
        return pr.summary()

    try:
        pr.info(f"WAMP-over-WebSocket router on {router_host}:{args.router_port}")
        try:
            q = f"/wamp/probe?host={router_host}&port={args.router_port}&topic={args.topic}"
            c = http.client.HTTPConnection(args.host, args.port, timeout=args.timeout)
            c.request("GET", q)
            rep = json.loads(c.getresponse().read())
            c.close()
        except Exception as exc:  # noqa: BLE001
            pr.check("device /wamp/probe completed", False, str(exc))
            return pr.summary()

        pr.check("device connected + WS upgraded", rep.get("connected") == 1, json.dumps(rep))
        pr.check("device got WELCOME", rep.get("welcome") == 1, f"welcome={rep.get('welcome')}")
        pr.check("device got SUBSCRIBED", rep.get("subscribed") == 1, f"subscribed={rep.get('subscribed')}")
        pr.check("device got PUBLISHED", rep.get("published") == 1, f"published={rep.get('published')}")
        pr.check("device received the routed EVENT", rep.get("event") == 1, f"event={rep.get('event')}")
        pr.check("device got RESULT", rep.get("result") == 1, f"result={rep.get('result')}")

        # authoritative (router side): the wire the device produced was spec-conformant.
        pr.check("router selected the wamp.2.json subprotocol", router.subprotocol_ok, "")
        pr.check("router received HELLO with a realm", router.hello_realm is not None, f"realm={router.hello_realm!r}")
        pr.check(
            "router received SUBSCRIBE to the topic",
            router.subscribed_topic == args.topic,
            f"topic={router.subscribed_topic!r}",
        )
        landed = next(((t, a) for (t, a) in router.published if a and a[0] == _ARG), None)
        pr.check("router received the device's PUBLISH arg", landed is not None, f"{len(router.published)} publish(es)")
        if landed:
            pr.check("PUBLISH went to the topic", landed[0] == args.topic, f"topic={landed[0]!r}")
        pr.check("router received a CALL", len(router.called) >= 1, f"{len(router.called)} call(s)")
    finally:
        router.stop()

    return pr.summary()
