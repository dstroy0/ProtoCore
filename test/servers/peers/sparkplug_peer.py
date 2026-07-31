# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Sparkplug B interop: receive + decode the device's NBIRTH over a real MQTT broker.

Role: the *device is the client* (a Sparkplug edge node). This peer runs a reference MQTT broker (mosquitto),
subscribes to `spBv1.0/#`, triggers the device's `GET /sparkplug/probe` (which connects to the broker and
publishes an NBIRTH to `spBv1.0/<group>/NBIRTH/<node>`), receives the retained-less publish, and decodes the
payload as an Eclipse-Tahu Sparkplug B `Payload` protobuf - INDEPENDENTLY of the device (a stdlib decoder of
the documented Tahu wire format: Payload{timestamp 1, metrics 2, seq 3}; Metric{name 1, datatype 4, value
oneof 10..15}). It asserts the topic namespace and the metric names / datatypes / values (a float, a uint32,
a string). That validates the device's Sparkplug payload + topic codec against an independent decoder over a
genuine broker hop.
"""

from __future__ import annotations

import http.client
import os
import shutil
import socket
import struct
import subprocess
import tempfile
import time

from ._common import Probe, require, wait_until


def _find_mosquitto():
    """mosquitto is often in sbin (not on a login PATH); check the usual broker locations."""
    return shutil.which("mosquitto") or next(
        (p for p in ("/usr/sbin/mosquitto", "/usr/local/sbin/mosquitto", "/sbin/mosquitto") if os.path.exists(p)),
        None,
    )


NAME = "sparkplug"
HELP = "receive + decode the device's Sparkplug B NBIRTH over a real MQTT broker (device-as-client)"


def add_args(p) -> None:
    p.add_argument("--host", required=True, help="device IP / hostname (triggers GET /sparkplug/probe)")
    p.add_argument("--port", type=int, default=80, help="device HTTP port (default 80)")
    p.add_argument("--broker-host", default=None, help="IP the device dials for MQTT (default: auto LAN IP)")
    p.add_argument("--mqtt-port", type=int, default=1883, help="MQTT broker port (default 1883)")
    p.add_argument("--timeout", type=float, default=10.0, help="seconds to wait for the publish")


# ---- protobuf wire decode (independent stdlib decoder of the Tahu schema) ----
def _read_varint(buf: bytes, i: int):
    val = shift = 0
    while True:
        b = buf[i]
        i += 1
        val |= (b & 0x7F) << shift
        shift += 7
        if not b & 0x80:
            return val, i


def _iter_fields(body: bytes):
    i = 0
    while i < len(body):
        tag, i = _read_varint(body, i)
        fn, wt = tag >> 3, tag & 7
        if wt == 0:
            v, i = _read_varint(body, i)
            yield fn, wt, v
        elif wt == 2:
            ln, i = _read_varint(body, i)
            yield fn, wt, body[i : i + ln]
            i += ln
        elif wt == 5:
            yield fn, wt, body[i : i + 4]
            i += 4
        elif wt == 1:
            yield fn, wt, body[i : i + 8]
            i += 8
        else:
            return


def _decode_metric(body: bytes) -> dict:
    m: dict = {}
    for fn, wt, v in _iter_fields(body):
        if fn == 1 and wt == 2:
            m["name"] = v.decode(errors="replace")
        elif fn == 4 and wt == 0:
            m["datatype"] = v
        elif fn == 10 and wt == 0:  # int_value
            m["value"] = v
        elif fn == 11 and wt == 0:  # long_value
            m["value"] = v
        elif fn == 12 and wt == 5:  # float_value
            m["value"] = struct.unpack("<f", v)[0]
        elif fn == 13 and wt == 1:  # double_value
            m["value"] = struct.unpack("<d", v)[0]
        elif fn == 14 and wt == 0:  # boolean_value
            m["value"] = bool(v)
        elif fn == 15 and wt == 2:  # string_value
            m["value"] = v.decode(errors="replace")
    return m


def _decode_payload(body: bytes) -> dict:
    out: dict = {"timestamp": None, "seq": None, "metrics": []}
    for fn, wt, v in _iter_fields(body):
        if fn == 1 and wt == 0:
            out["timestamp"] = v
        elif fn == 2 and wt == 2:
            out["metrics"].append(_decode_metric(v))
        elif fn == 3 and wt == 0:
            out["seq"] = v
    return out


def _lan_ip_toward(host: str) -> str:
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        s.connect((host, 9))  # UDP connect sends nothing; just selects the egress interface
        return s.getsockname()[0]
    except OSError:
        return "127.0.0.1"
    finally:
        s.close()


def run(args) -> bool:
    pr = Probe(f"sparkplug device={args.host}:{args.port}")
    mqtt = require("paho.mqtt.client")
    broker_host = args.broker_host or _lan_ip_toward(args.host)

    # Start mosquitto (the reference broker) bound to all interfaces so the device can reach it. If mosquitto
    # is not on PATH, fall back to whatever broker is already running at broker_host:mqtt_port.
    proc = None
    conf_path = None
    mosq = _find_mosquitto()
    if mosq:
        # mosquitto 2.x requires a real config file (rejects -c /dev/stdin), so write one to a temp file.
        conf = f"listener {args.mqtt_port} 0.0.0.0\nallow_anonymous true\n"
        fd, conf_path = tempfile.mkstemp(suffix=".conf", prefix="spb_mosq_")
        with os.fdopen(fd, "w") as f:
            f.write(conf)
        proc = subprocess.Popen([mosq, "-c", conf_path])
        time.sleep(0.7)
        pr.check("broker started (mosquitto)", proc.poll() is None, f"pid {proc.pid}")
    else:
        pr.info("mosquitto not found - using an existing broker at the broker host")

    received: list = []
    client = mqtt.Client(client_id="sparkplug-monitor")
    client.on_connect = lambda c, *_: c.subscribe("spBv1.0/#")
    client.on_message = lambda c, u, msg: received.append((msg.topic, bytes(msg.payload)))
    try:
        client.connect("127.0.0.1" if proc else broker_host, args.mqtt_port, keepalive=30)
        client.loop_start()
        time.sleep(0.4)
        pr.check("subscriber connected to broker", True, f"{broker_host}:{args.mqtt_port}")

        # Trigger the device: it dials broker_host:mqtt_port and publishes the NBIRTH.
        try:
            c = http.client.HTTPConnection(args.host, args.port, timeout=args.timeout)
            c.request("GET", f"/sparkplug/probe?host={broker_host}&port={args.mqtt_port}&group=pc&node=rig")
            rep = c.getresponse().read().decode(errors="replace")
            c.close()
        except Exception as exc:  # noqa: BLE001
            pr.check("device /sparkplug/probe completed", False, str(exc))
            return pr.summary()
        pr.check("device connected + published", '"connected":1' in rep and '"published":1' in rep, rep[:120])

        got = wait_until(lambda: len(received) > 0, args.timeout)
        pr.check("broker received a Sparkplug publish", got, f"{len(received)} messages")
        if not got:
            return pr.summary()
    finally:
        client.loop_stop()
        if proc:
            proc.terminate()
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()
        if conf_path:
            try:
                os.remove(conf_path)
            except OSError:
                pass

    topic, payload = received[0]
    pr.check("topic == spBv1.0/pc/NBIRTH/rig", topic == "spBv1.0/pc/NBIRTH/rig", topic)
    pl = _decode_payload(payload)
    by_name = {m.get("name"): m for m in pl["metrics"]}
    pr.check(
        "payload decodes to 3 metrics", len(pl["metrics"]) == 3, f"metrics={[m.get('name') for m in pl['metrics']]}"
    )

    temp = by_name.get("Node Control/Temperature", {})
    pr.check(
        "Temperature: float 23.5 (datatype 9)",
        temp.get("datatype") == 9 and abs(float(temp.get("value", 0)) - 23.5) < 1e-3,
        f"{temp}",
    )
    up = by_name.get("Node Control/Uptime", {})
    pr.check("Uptime: uint32 (datatype 7)", up.get("datatype") == 7 and isinstance(up.get("value"), int), f"{up}")
    fw = by_name.get("Node Control/Firmware", {})
    pr.check(
        "Firmware: string 'pc-rig' (datatype 12)",
        fw.get("datatype") == 12 and fw.get("value") == "pc-rig",
        f"{fw}",
    )

    return pr.summary()
