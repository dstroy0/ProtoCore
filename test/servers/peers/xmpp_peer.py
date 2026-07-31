# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""XMPP (RFC 6120) interop: drive the device as an XMPP client against a spec c2s server (stdlib).

Role: the *device is the client* (an IoT XMPP client, per xmpp.h). This peer stands up a self-contained
RFC 6120 client-to-server (c2s) endpoint in a background thread - a spec-correct stream + SASL-PLAIN + resource
bind handshake written fresh, independent of the library - triggers the device (via the rig's GET /xmpp/probe
route) to connect out and run the full client dialogue (stream open -> SASL PLAIN auth -> stream restart ->
resource bind -> presence -> a `<message>` stanza), and confirms - as the authoritative receiver - that every
byte the device's xmpp.h codec put on the wire is a well-formed, correctly-escaped, spec-conformant stream.

  1. start the c2s server; the device connects and opens a stream -> we send stream header + SASL features
  2. the device sends `<auth mechanism='PLAIN'>` -> we b64-decode + verify the credentials -> `<success/>`
  3. the device restarts the stream -> we send stream header + a `<bind>` feature
  4. the device binds a resource with an `<iq type='set'>` -> we reply `<iq type='result'>` with the full JID
  5. the device sends `<presence/>` then a `<message><body>..</body></message>` -> we capture + validate the body
  6. trigger GET /xmpp/probe on the device -> {connected, stream, features, auth, bind, presence, message}

That closes the loop: the device's stream-open / SASL / presence / message / iq builders (+ its stanza + stream
readers) interoperate with an independent RFC 6120 implementation. Self-contained - no external server needed
(prosody / ejabberd are the production references).
"""

from __future__ import annotations

import base64
import http.client
import json
import re
import socket
import threading

from ._common import Probe

NAME = "xmpp"
HELP = "drive the device as an XMPP client against a spec RFC 6120 c2s server (device-as-client)"

_BODY = "hello-from-pc-rig"  # must match the rig's h_xmpp_probe <message> body


def _element(buf: bytes, name: bytes):
    """Find the first complete top-level <name ...>..</name> or <name .../> in buf.

    Returns (start, end, inner_bytes) where [start:end) is the whole element and inner_bytes is the text
    between the open and close tags (empty for a self-closed element), or None if not yet complete.
    """
    i = buf.find(b"<" + name)
    if i < 0:
        return None
    # the char after the name must be a tag delimiter, not part of a longer name (e.g. <message vs <messages)
    after = buf[i + 1 + len(name) : i + 2 + len(name)]
    if after and after not in (b" ", b"\t", b"\r", b"\n", b">", b"/"):
        return None
    gt = buf.find(b">", i)
    if gt < 0:
        return None
    if buf[gt - 1 : gt] == b"/":  # self-closed <name .../>
        return i, gt + 1, b""
    close = buf.find(b"</" + name + b">", gt + 1)
    if close < 0:
        return None
    end = close + len(name) + 3
    return i, end, buf[gt + 1 : close]


def _attr(tag: bytes, name: str):
    """Extract attribute value from an element's start-tag bytes (single or double quoted)."""
    m = re.search(rb"\b" + name.encode() + rb"\s*=\s*(['\"])(.*?)\1", tag)
    return m.group(2).decode("utf-8", "replace") if m else None


class _C2S(threading.Thread):
    """A minimal spec RFC 6120 c2s server: stream negotiation, SASL PLAIN, resource bind, stanza receipt."""

    _SASL_NS = "urn:ietf:params:xml:ns:xmpp-sasl"
    _BIND_NS = "urn:ietf:params:xml:ns:xmpp-bind"

    def __init__(self, port, domain, user, password):
        super().__init__(daemon=True)
        self.domain, self.user, self.password = domain, user, password
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.sock.bind(("0.0.0.0", port))
        self.sock.listen(4)
        self.sock.settimeout(20)
        self._stop = False
        # captured, authoritative observations:
        self.stream_to = None  # the `to` domain on the device's initial stream header
        self.stream_versioned = False  # the initial stream advertised version='1.0'
        self.auth_ok = False  # SASL PLAIN decoded and credentials matched
        self.bound = False  # a resource-bind iq arrived
        self.messages = []  # [(to, body)] captured from <message> stanzas

    def run(self):
        while not self._stop:
            try:
                conn, _ = self.sock.accept()
            except OSError:
                break
            threading.Thread(target=self._serve, args=(conn,), daemon=True).start()

    def _stream_header(self):
        return (
            "<?xml version='1.0'?>"
            f"<stream:stream from='{self.domain}' id='pc-c2s' version='1.0' xml:lang='en' "
            "xmlns='jabber:client' xmlns:stream='http://etherx.jabber.org/streams'>"
        ).encode()

    def _serve(self, conn):
        conn.settimeout(8)
        buf = b""
        state = "init"
        try:
            while not self._stop:
                try:
                    d = conn.recv(4096)
                except OSError:
                    break
                if not d:
                    break
                buf += d
                progressed = True
                while progressed:
                    progressed = False
                    if state in ("init", "restart"):
                        i = buf.find(b"<stream:stream")
                        gt = buf.find(b">", i) if i >= 0 else -1
                        if i >= 0 and gt >= 0:
                            tag = buf[i : gt + 1]
                            if state == "init":
                                self.stream_to = _attr(tag, "to")
                                self.stream_versioned = _attr(tag, "version") == "1.0"
                            buf = buf[gt + 1 :]
                            conn.sendall(self._stream_header())
                            if state == "init":
                                conn.sendall(
                                    f"<stream:features><mechanisms xmlns='{self._SASL_NS}'>"
                                    "<mechanism>PLAIN</mechanism></mechanisms></stream:features>".encode()
                                )
                                state = "auth"
                            else:
                                conn.sendall(
                                    f"<stream:features><bind xmlns='{self._BIND_NS}'/></stream:features>".encode()
                                )
                                state = "bind"
                            progressed = True
                    elif state == "auth":
                        el = _element(buf, b"auth")
                        if el:
                            start, end, inner = el
                            self._do_auth(conn, inner.strip())
                            buf = buf[end:]
                            state = "restart"
                            progressed = True
                    elif state == "bind":
                        el = _element(buf, b"iq")
                        if el:
                            start, end, _inner = el
                            iq = buf[start:end]
                            iq_id = _attr(iq[: iq.find(b">") + 1], "id") or "bind1"
                            resource = "rig"
                            rm = re.search(rb"<resource>(.*?)</resource>", iq)
                            if rm:
                                resource = rm.group(1).decode("utf-8", "replace")
                            self.bound = b"xmpp-bind" in iq
                            jid = f"{self.user}@{self.domain}/{resource}"
                            conn.sendall(
                                f"<iq type='result' id='{iq_id}'><bind xmlns='{self._BIND_NS}'>"
                                f"<jid>{jid}</jid></bind></iq>".encode()
                            )
                            buf = buf[end:]
                            state = "active"
                            progressed = True
                    elif state == "active":
                        el = _element(buf, b"message")
                        if el:
                            start, end, inner = el
                            msg = buf[start:end]
                            to = _attr(msg[: msg.find(b">") + 1], "to")
                            bm = re.search(rb"<body>(.*?)</body>", inner, re.S)
                            body = bm.group(1).decode("utf-8", "replace") if bm else ""
                            self.messages.append((to, body))
                            buf = buf[end:]
                            progressed = True
                        else:
                            # drop a leading <presence.../> so it doesn't wedge the scan for <message>
                            pel = _element(buf, b"presence")
                            if pel:
                                buf = buf[pel[1] :]
                                progressed = True
        except OSError:
            pass
        finally:
            try:
                conn.close()
            except OSError:
                pass

    def _do_auth(self, conn, b64):
        try:
            raw = base64.b64decode(b64)
            parts = raw.split(b"\x00")  # authzid \0 authcid \0 passwd
            ok = len(parts) == 3 and parts[1].decode() == self.user and parts[2].decode() == self.password
        except Exception:  # noqa: BLE001
            ok = False
        self.auth_ok = ok
        if ok:
            conn.sendall(f"<success xmlns='{self._SASL_NS}'/>".encode())
        else:
            conn.sendall(f"<failure xmlns='{self._SASL_NS}'><not-authorized/></failure>".encode())

    def stop(self):
        self._stop = True
        try:
            self.sock.close()
        except OSError:
            pass


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname (the rig)")
    p.add_argument("--port", type=int, default=80, help="device HTTP port (default 80)")
    p.add_argument("--server-host", help="c2s address the device dials (default: this machine's route IP)")
    p.add_argument("--server-port", type=int, default=5222, help="XMPP c2s port to serve (default 5222)")
    p.add_argument("--domain", default="pc.local", help="XMPP domain (stream to / JID host)")
    p.add_argument("--user", default="rig", help="SASL PLAIN username the device authenticates as")
    p.add_argument("--password", default="s3cret", help="SASL PLAIN password")
    p.add_argument("--timeout", type=float, default=10.0, help="timeout seconds")


def run(args) -> bool:
    pr = Probe(f"xmpp device={args.host} c2s=:{args.server_port} domain={args.domain}")

    server_host = args.server_host
    if not server_host:
        try:
            s = socket.create_connection((args.host, args.port), timeout=args.timeout)
            server_host = s.getsockname()[0]
            s.close()
        except OSError as exc:  # noqa: BLE001
            pr.check("reach the device", False, str(exc))
            return pr.summary()

    try:
        srv = _C2S(args.server_port, args.domain, args.user, args.password)
        srv.start()
    except OSError as exc:  # noqa: BLE001
        pr.check("start XMPP c2s server", False, str(exc))
        return pr.summary()

    try:
        pr.info(f"XMPP c2s on {server_host}:{args.server_port} (domain {args.domain})")
        to = f"sink@{args.domain}"
        try:
            q = (
                f"/xmpp/probe?host={server_host}&port={args.server_port}"
                f"&domain={args.domain}&user={args.user}&pass={args.password}&to={to}"
            )
            c = http.client.HTTPConnection(args.host, args.port, timeout=args.timeout)
            c.request("GET", q)
            rep = json.loads(c.getresponse().read())
            c.close()
        except Exception as exc:  # noqa: BLE001
            pr.check("device /xmpp/probe completed", False, str(exc))
            return pr.summary()

        pr.check("device connected to c2s", rep.get("connected") == 1, json.dumps(rep))
        pr.check("device opened a stream + read features", rep.get("features") == 1, f"features={rep.get('features')}")
        pr.check("device SASL PLAIN authenticated", rep.get("auth") == 1, f"auth={rep.get('auth')}")
        pr.check("device bound a resource", rep.get("bind") == 1, f"bind={rep.get('bind')}")
        pr.check("device sent a message stanza", rep.get("message") == 1, f"message={rep.get('message')}")

        # authoritative (server side): the wire the device produced was spec-conformant.
        pr.check("initial stream advertised version='1.0'", srv.stream_versioned, "")
        pr.check("initial stream targeted the domain", srv.stream_to == args.domain, f"to={srv.stream_to!r}")
        pr.check("c2s verified the SASL PLAIN credentials", srv.auth_ok, "")
        pr.check("c2s bound the device's resource", srv.bound, "")
        landed = next(((t, b) for (t, b) in srv.messages if b == _BODY), None)
        pr.check("c2s received the device's <message> body", landed is not None, f"{len(srv.messages)} message(s)")
        if landed:
            pr.check("message addressed to the sink JID", landed[0] == to, f"to={landed[0]!r}")
    finally:
        srv.stop()

    return pr.summary()
