# Real-protocol interop harness

This directory holds the **interop peers**: small launchers that test the device
(running this library) against _genuine third-party reference implementations_ of
each protocol, not just against our own round-trip. It turns the project's
standing rule - _verify against an authoritative independent implementation_ -
into one easy command.

Why bother? A test that encodes a message with our code and decodes it with our
code can be self-consistently wrong. Driving the device with `net-snmp`,
`mosquitto`, `pymodbus`, `aiocoap`, `asyncua`, or the `websockets` library proves
real-world interop: if a real manager/broker/client is happy, the wire format is
right.

## What "client" and "server" mean here

A protocol has two ends. For each peer, **one end is the device** (your ESP32
running this library) and **the other end is the reference tool** this harness
runs. Two shapes:

- **The device is the server** (HTTP, WebSocket, SNMP agent, Modbus slave, CoAP
  server, OPC-UA server). The harness runs a reference **client** that connects
  to the device, exercises it, and prints PASS/FAIL. You give it `--host` (the
  device IP).
- **The device is the client** (MQTT client, Modbus master, OPC-UA client). The
  harness runs a reference **server/broker** and waits; you point the device at
  this machine's IP. These run until you press Ctrl-C.

```
 device-as-server:   [ ESP32 server ] <--- interop client (this harness) --- you
 device-as-client:   [ ESP32 client ] ---> interop server/broker (this harness)
```

## Install

```bash
pip install -r test/servers/requirements.txt
```

Two peers prefer a native binary on PATH (they are not Python packages):

| peer          | needs              | install                                             |
| ------------- | ------------------ | --------------------------------------------------- |
| `snmp`        | net-snmp CLI tools | `apt install snmp` / `choco install net-snmp`       |
| `mqtt-broker` | `mosquitto` broker | `apt install mosquitto` / `choco install mosquitto` |

`http` uses only the Python standard library. `snmp` falls back to the `pysnmp`
package and `mqtt-broker` to the `amqtt` broker (both in requirements.txt) when the
native tools are not on PATH, so those peers also work pip-only.

### Verified against hardware

Driven against the device on real third-party stacks - all seven families:
`http` (stdlib) 4/4, `ws` (`websockets`) 3/3, `coap` (`aiocoap`) 2/2,
`modbus-client` (`pymodbus`) 6/6, `snmp` (`pysnmp`) 3/3, MQTT (device client ->
real `mosquitto`), and `opcua-client` (`asyncua`) 3/3 (connect+session, browse
Objects, read node). The MQTT check used the public `test.mosquitto.org` broker
because this dev box blocks inbound 1883; on a host that accepts inbound,
`mqtt-broker --seconds N` does the same check locally.

## Usage

```bash
python test/servers/interop.py --list             # list every peer
python test/servers/interop.py <protocol> --help  # options for one peer
```

### Device-as-server peers (give them the device IP)

```bash
python test/servers/interop.py http  --host 192.168.1.85
python test/servers/interop.py ws    --host 192.168.1.85 --path /ws --echo
python test/servers/interop.py snmp  --host 192.168.1.85 --community public
python test/servers/interop.py coap  --host 192.168.1.85 --path /sensor
python test/servers/interop.py modbus-client --host 192.168.1.85 --unit 1
python test/servers/interop.py opcua-client  --host 192.168.1.85
```

Each prints a check list and exits `0` when all checks pass, `1` on an interop
FAIL, `2` if a dependency is missing.

### Device-as-client peers (point the device at this machine)

```bash
python test/servers/interop.py mqtt-broker   --port 1883     # device connects + publishes
python test/servers/interop.py modbus-server --port 502      # device master reads/writes
python test/servers/interop.py opcua-server  --port 4840     # device client reads
```

These run until Ctrl-C and print what the device does (e.g. the broker monitor
echoes every topic the device publishes).

## Pointing the device at a peer

For the device-as-client peers, set the device's target host to this machine's
LAN IP (the rig in `C:\Users\Douglas\pio_fp` hard-codes test endpoints; edit its
`main.cpp`). For device-as-server peers, flash the relevant feature
(`PC_ENABLE_*`), note the printed IP, and pass it as `--host`.

## Adding a peer

Drop a module in [peers/](peers/) exposing either module-level
`NAME` / `HELP` / `add_args(parser)` / `run(args) -> bool`, or a `PEERS` list of
objects with those attributes (for a client+server pair). Add it to `_MODULES`
in [interop.py](interop.py). Use the shared `Probe` from
[peers/\_common.py](peers/_common.py) so the output and exit codes stay uniform,
and `require(...)` to fail with an install hint instead of a traceback when an
optional dependency is absent.
