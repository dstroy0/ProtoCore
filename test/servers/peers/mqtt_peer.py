# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""MQTT interop: run a reference broker for the device's MQTT client.

Role: the *device is the client*; this peer is the broker. Prefers mosquitto (the
reference broker) with a paho monitor; if mosquitto is not on PATH it falls back
to the amqtt broker (a separate, independent MQTT implementation) so the peer also
works pip-only. Point the device's broker host at this machine's LAN IP.

With `--seconds N` the peer runs for N seconds and PASSES if it observed at least
one publish from the device (a real end-to-end interop check); with `--seconds 0`
(default) it monitors until Ctrl-C.
"""

from __future__ import annotations

import asyncio
import shutil
import subprocess
import sys
import time

from ._common import Probe, have

NAME = "mqtt-broker"
HELP = "run a broker (mosquitto, or amqtt fallback) for the device MQTT client"


def add_args(p) -> None:
    p.add_argument("--bind", default="0.0.0.0", help="listen address (default 0.0.0.0)")
    p.add_argument("--port", type=int, default=1883, help="MQTT port (default 1883)")
    p.add_argument("--topic", default="#", help="topic filter to monitor (default # = all)")
    p.add_argument("--publish", nargs=2, metavar=("TOPIC", "PAYLOAD"), help="publish once after start")
    p.add_argument(
        "--seconds", type=int, default=0, help="run N seconds and require >=1 device publish (0 = until Ctrl-C)"
    )


def run(args) -> bool:
    pr = Probe(f"mqtt-broker :{args.port}")
    if shutil.which("mosquitto") and have("paho.mqtt.client"):
        return _run_mosquitto(args, pr)
    if have("amqtt"):
        pr.info("mosquitto not on PATH - using the amqtt broker fallback")
        return _run_amqtt(args, pr)
    pr.check("a broker is available", False, "install mosquitto (+paho), or `pip install amqtt`")
    return pr.summary()


# --------------------------------------------------------------------------- #
# mosquitto backend (reference broker + paho monitor)
# --------------------------------------------------------------------------- #
def _run_mosquitto(args, pr: Probe) -> bool:
    import paho.mqtt.client as mqtt

    mosquitto = shutil.which("mosquitto")
    conf = f"listener {args.port} {args.bind}\nallow_anonymous true\n"
    if sys.platform != "win32":
        proc = subprocess.Popen([mosquitto, "-v", "-c", "/dev/stdin"], stdin=subprocess.PIPE, text=True)
        proc.stdin.write(conf)
        proc.stdin.close()
    else:
        proc = subprocess.Popen([mosquitto, "-v", "-p", str(args.port)])
    time.sleep(0.6)
    pr.check("broker started (mosquitto)", proc.poll() is None, f"pid {proc.pid}")

    seen = {"n": 0}
    client = mqtt.Client(client_id="interop-monitor")
    client.on_connect = lambda c, *_: c.subscribe(args.topic)

    def on_message(c, u, msg):
        seen["n"] += 1
        print(f"  device -> {msg.topic}: {msg.payload!r}")

    client.on_message = on_message
    try:
        client.connect("127.0.0.1", args.port, keepalive=30)
        pr.check("monitor connected to broker", True)
        if args.publish:
            client.publish(args.publish[0], args.publish[1])
        client.loop_start()
        _wait(args, proc)
    except KeyboardInterrupt:
        print("\nmqtt-broker: stopping")
    finally:
        client.loop_stop()
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
    if args.seconds:
        pr.check("observed >=1 device publish", seen["n"] >= 1, f"{seen['n']} messages")
    return pr.summary()


def _wait(args, proc) -> None:
    deadline = time.time() + args.seconds if args.seconds else None
    while proc.poll() is None and (deadline is None or time.time() < deadline):
        time.sleep(0.2)


# --------------------------------------------------------------------------- #
# amqtt backend (independent broker + subscriber, asyncio, pip-only)
# --------------------------------------------------------------------------- #
def _run_amqtt(args, pr: Probe) -> bool:
    from amqtt.broker import Broker
    from amqtt.client import MQTTClient
    from amqtt.mqtt.constants import QOS_0

    # amqtt 0.11 plugins-style config: anonymous auth only (a lab broker), no
    # logging/sys plugins, so it starts without the legacy-config deprecation noise.
    config = {
        "listeners": {"default": {"type": "tcp", "bind": f"{args.bind}:{args.port}"}},
        "plugins": {"amqtt.plugins.authentication.AnonymousAuthPlugin": {"allow_anonymous": True}},
    }
    seen = {"n": 0}

    async def go():
        broker = Broker(config)
        await broker.start()
        pr.check("broker started (amqtt)", True, f"{args.bind}:{args.port}")
        sub = MQTTClient()
        await sub.connect(f"mqtt://127.0.0.1:{args.port}/")
        await sub.subscribe([(args.topic, QOS_0)])
        pr.check("monitor connected to broker", True)
        print(
            f"mqtt-broker: monitoring '{args.topic}' (amqtt){' for %ds' % args.seconds if args.seconds else ', Ctrl-C to stop'}"
        )
        if args.publish:
            await sub.publish(args.publish[0], args.publish[1].encode(), QOS_0)
        deadline = time.time() + args.seconds if args.seconds else None
        try:
            while deadline is None or time.time() < deadline:
                try:
                    msg = await asyncio.wait_for(sub.deliver_message(), timeout=1.0)
                except asyncio.TimeoutError:
                    continue
                seen["n"] += 1
                print(f"  device -> {msg.topic}: {bytes(msg.data)!r}")
        finally:
            await sub.disconnect()
            await broker.shutdown()

    try:
        asyncio.run(go())
    except KeyboardInterrupt:
        print("\nmqtt-broker: stopping")
    except Exception as exc:  # noqa: BLE001
        pr.check("broker ran", False, str(exc))
    if args.seconds:
        pr.check("observed >=1 device publish", seen["n"] >= 1, f"{seen['n']} messages")
    return pr.summary()
