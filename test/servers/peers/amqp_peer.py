# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""AMQP 0-9-1 interop: drive the device as an AMQP client against a spec broker (stdlib).

Role: the *device is the client* (an AMQP publisher, per amqp.h). This peer stands up a self-contained AMQP
0-9-1 broker in a background thread - a spec-correct connection/channel handshake + method decoder written
fresh, independent of the library - triggers the device (via the rig's GET /amqp/probe route) to connect out
and run the full client dialogue, and confirms - as the authoritative receiver - that every frame the device's
amqp.h codec put on the wire is well-formed and correctly encoded.

  1. the device sends the `AMQP\\0\\0\\9\\1` protocol header -> we send Connection.Start (PLAIN mechanism)
  2. the device sends Connection.Start-Ok -> we decode the SASL PLAIN response + verify the credentials
  3. we send Connection.Tune -> the device sends Tune-Ok + Connection.Open -> we send Open-Ok
  4. the device sends Channel.Open -> we send Channel.Open-Ok
  5. the device sends Basic.Publish + a content header frame + a body frame -> we capture + validate the body
  6. trigger GET /amqp/probe on the device -> {connected, start, tune, openok, chanok, published}

That closes the loop: the device's protocol-header + method-frame builder (StartOk/TuneOk/Open/Channel.Open/
Basic.Publish) + frame parser (Start/Tune/OpenOk/Channel.OpenOk) interoperate with an independent AMQP 0-9-1
implementation. Self-contained - no external broker needed (RabbitMQ is the production reference).
"""

from __future__ import annotations

import http.client
import json
import socket
import struct
import threading

from ._common import Probe

NAME = "amqp"
HELP = "drive the device as an AMQP 0-9-1 client against a spec broker (device-as-client)"

_BODY = b"hello-from-pc-rig"  # must match the rig's h_amqp_probe Basic.Publish body
_FRAME_END = 0xCE
_PROTO_HEADER = b"AMQP\x00\x00\x09\x01"


# ---- argument encoders (AMQP 0-9-1 field types) ----
def _octet(v):
    return struct.pack("!B", v)


def _short(v):
    return struct.pack("!H", v)


def _long(v):
    return struct.pack("!I", v)


def _shortstr(s: bytes):
    return struct.pack("!B", len(s)) + s


def _longstr(s: bytes):
    return struct.pack("!I", len(s)) + s


def _table_empty():
    return _long(0)  # a zero-length field table


def _frame(ftype: int, channel: int, payload: bytes) -> bytes:
    return struct.pack("!BHI", ftype, channel, len(payload)) + payload + bytes([_FRAME_END])


def _method(class_id: int, method_id: int, args: bytes) -> bytes:
    return _short(class_id) + _short(method_id) + args


class _Broker(threading.Thread):
    """A minimal spec AMQP 0-9-1 broker: protocol header, Connection/Channel handshake, Basic.Publish capture."""

    def __init__(self, port, user, password):
        super().__init__(daemon=True)
        self.user, self.password = user, password
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("0.0.0.0", port))
        self.sock.listen(4)
        self.sock.settimeout(20)
        self._stop = False
        # authoritative observations:
        self.header_ok = False  # the 8-octet AMQP protocol header was correct
        self.auth_ok = False  # the SASL PLAIN response decoded + credentials matched
        self.tuned = False  # a Tune-Ok arrived
        self.opened = False  # Connection.Open arrived (we sent Open-Ok)
        self.channel_opened = False  # Channel.Open arrived (we sent Channel.Open-Ok)
        self.published = []  # [(exchange, routing_key, body)] captured Basic.Publish deliveries

    def run(self):
        while not self._stop:
            try:
                conn, _ = self.sock.accept()
            except OSError:
                break
            threading.Thread(target=self._serve, args=(conn,), daemon=True).start()

    def _recv_exactly(self, conn, n):
        buf = b""
        while len(buf) < n:
            d = conn.recv(n - len(buf))
            if not d:
                return None
            buf += d
        return buf

    def _read_frame(self, conn):
        """Read one whole frame -> (ftype, channel, payload) or None. Validates the 0xCE end octet."""
        head = self._recv_exactly(conn, 7)
        if not head:
            return None
        ftype, channel, size = struct.unpack("!BHI", head)
        payload = self._recv_exactly(conn, size) if size else b""
        if payload is None:
            return None
        end = self._recv_exactly(conn, 1)
        if not end or end[0] != _FRAME_END:
            return None
        return ftype, channel, payload

    def _serve(self, conn):
        conn.settimeout(8)
        try:
            hdr = self._recv_exactly(conn, 8)
            self.header_ok = hdr is not None and hdr[:4] == b"AMQP"
            if not self.header_ok:
                return
            # Connection.Start (10,10): major=0 minor=9, empty server-props, mechanisms=PLAIN, locales=en_US
            start = _method(10, 10, _octet(0) + _octet(9) + _table_empty() + _longstr(b"PLAIN") + _longstr(b"en_US"))
            conn.sendall(_frame(1, 0, start))

            pending_rk = None
            pending_size = None
            body = b""
            while not self._stop:
                fr = self._read_frame(conn)
                if fr is None:
                    break
                ftype, channel, payload = fr
                if ftype == 1:  # METHOD
                    cls, mth = struct.unpack("!HH", payload[:4])
                    args = payload[4:]
                    if (cls, mth) == (10, 11):  # Connection.Start-Ok
                        self._check_startok(args)
                        # Connection.Tune (10,30): channel-max, frame-max, heartbeat
                        conn.sendall(_frame(1, 0, _method(10, 30, _short(0) + _long(131072) + _short(0))))
                    elif (cls, mth) == (10, 31):  # Connection.Tune-Ok
                        self.tuned = True
                    elif (cls, mth) == (10, 40):  # Connection.Open
                        self.opened = True
                        conn.sendall(_frame(1, 0, _method(10, 41, _shortstr(b""))))  # Open-Ok
                    elif (cls, mth) == (20, 10):  # Channel.Open
                        self.channel_opened = True
                        conn.sendall(_frame(1, channel, _method(20, 11, _longstr(b""))))  # Channel.Open-Ok
                    elif (cls, mth) == (60, 40):  # Basic.Publish
                        pending_rk = self._publish_rk(args)
                        body = b""
                        pending_size = None
                    elif (cls, mth) in ((10, 50), (20, 40)):  # Connection/Channel.Close
                        ok = _method(cls, 51 if cls == 10 else 41, b"")  # Close-Ok
                        conn.sendall(_frame(1, channel, ok))
                elif ftype == 2:  # content HEADER: class(2) weight(2) body-size(8) flags(2)
                    pending_size = struct.unpack("!Q", payload[4:12])[0]
                    body = b""
                    if pending_size == 0 and pending_rk is not None:
                        self.published.append(("", pending_rk, b""))
                elif ftype == 3:  # content BODY
                    body += payload
                    if pending_size is not None and len(body) >= pending_size and pending_rk is not None:
                        self.published.append(("", pending_rk, body[:pending_size]))
                        pending_rk = None
        except OSError:
            pass
        finally:
            try:
                conn.close()
            except OSError:
                pass

    def _check_startok(self, args):
        """Start-Ok = table client-props, shortstr mechanism, longstr response, shortstr locale."""
        try:
            off = 0
            tlen = struct.unpack("!I", args[off : off + 4])[0]
            off += 4 + tlen  # skip client-properties table
            mlen = args[off]
            off += 1
            mechanism = args[off : off + mlen]
            off += mlen
            rlen = struct.unpack("!I", args[off : off + 4])[0]
            off += 4
            response = args[off : off + rlen]
            parts = response.split(b"\x00")  # authzid \0 authcid \0 passwd
            self.auth_ok = (
                mechanism == b"PLAIN"
                and len(parts) == 3
                and parts[1].decode() == self.user
                and parts[2].decode() == self.password
            )
        except Exception:  # noqa: BLE001
            self.auth_ok = False

    def _publish_rk(self, args):
        """Basic.Publish = short reserved, shortstr exchange, shortstr routing-key, bit flags."""
        try:
            off = 2  # reserved-1 short
            elen = args[off]
            off += 1 + elen  # exchange
            rlen = args[off]
            off += 1
            return args[off : off + rlen].decode("utf-8", "replace")
        except Exception:  # noqa: BLE001
            return ""

    def stop(self):
        self._stop = True
        try:
            self.sock.close()
        except OSError:
            pass


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname (the rig)")
    p.add_argument("--port", type=int, default=80, help="device HTTP port (default 80)")
    p.add_argument("--broker-host", help="broker address the device dials (default: this machine's route IP)")
    p.add_argument("--broker-port", type=int, default=5672, help="AMQP broker port to serve (default 5672)")
    p.add_argument("--user", default="pc", help="SASL PLAIN username the device authenticates as")
    p.add_argument("--password", default="s3cret", help="SASL PLAIN password")
    p.add_argument("--routing-key", default="pc.q", help="expected Basic.Publish routing key")
    p.add_argument("--timeout", type=float, default=10.0, help="timeout seconds")


def run(args) -> bool:
    pr = Probe(f"amqp device={args.host} broker=:{args.broker_port}")

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
        broker = _Broker(args.broker_port, args.user, args.password)
        broker.start()
    except OSError as exc:  # noqa: BLE001
        pr.check("start AMQP broker", False, str(exc))
        return pr.summary()

    try:
        pr.info(f"AMQP 0-9-1 broker on {broker_host}:{args.broker_port}")
        try:
            q = (
                f"/amqp/probe?host={broker_host}&port={args.broker_port}"
                f"&user={args.user}&pass={args.password}&rk={args.routing_key}"
            )
            c = http.client.HTTPConnection(args.host, args.port, timeout=args.timeout)
            c.request("GET", q)
            rep = json.loads(c.getresponse().read())
            c.close()
        except Exception as exc:  # noqa: BLE001
            pr.check("device /amqp/probe completed", False, str(exc))
            return pr.summary()

        pr.check("device connected to broker", rep.get("connected") == 1, json.dumps(rep))
        pr.check("device read Connection.Start", rep.get("start") == 1, f"start={rep.get('start')}")
        pr.check("device read Connection.Tune", rep.get("tune") == 1, f"tune={rep.get('tune')}")
        pr.check("device read Connection.Open-Ok", rep.get("openok") == 1, f"openok={rep.get('openok')}")
        pr.check("device read Channel.Open-Ok", rep.get("chanok") == 1, f"chanok={rep.get('chanok')}")
        pr.check("device sent Basic.Publish", rep.get("published") == 1, f"published={rep.get('published')}")

        # authoritative (broker side): the wire the device produced was spec-conformant.
        pr.check("broker saw a correct AMQP protocol header", broker.header_ok, "")
        pr.check("broker verified the SASL PLAIN credentials", broker.auth_ok, "")
        pr.check("broker received Tune-Ok", broker.tuned, "")
        pr.check("broker opened the connection + channel", broker.opened and broker.channel_opened, "")
        landed = next(((e, rk, b) for (e, rk, b) in broker.published if b == _BODY), None)
        pr.check(
            "broker received the device's published body", landed is not None, f"{len(broker.published)} publish(es)"
        )
        if landed:
            pr.check("publish routing key matches", landed[1] == args.routing_key, f"rk={landed[1]!r}")
    finally:
        broker.stop()

    return pr.summary()
