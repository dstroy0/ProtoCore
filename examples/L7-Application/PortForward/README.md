# PortForward - publish an internal service through the ESP32 (TCP relay / DNAT)

This sketch turns the board into a **reverse port forwarder**: anything that
connects to the ESP32 on a chosen front port is relayed to an internal
`host:port` the board can reach, and replies come back automatically. It is the
DNAT / "publish a service" pattern - expose a device that sits on a segment you
can only reach through the board.

You can try it with no special hardware: Part 1 stands up a throwaway origin
service and a client with one command each.

## What is going on here? (the big picture)

The board listens on a **front port** (say 8080). When a client connects there,
the relay opens a second connection out to the **origin** (say an internal web
server on `192.168.1.60:80`) and shovels bytes between the two in both
directions until either side closes. The client thinks it is talking to the
board; it is really talking to the origin behind it.

Wiring is two calls:

```cpp
int32_t li = server.listen(8080, ConnProto::PROTO_RELAY);   // open the front port
pc_relay_publish((uint8_t)li, "192.168.1.60", 80); // bind it to the origin
```

The server's normal `handle()` loop pumps the relay - no extra task.

> **Security.** The relay forwards to whatever origin you publish and does not
> authenticate the inbound side, so it is an open door to that origin for anyone
> who can reach the front port. Only publish trusted internal targets, and keep
> the front port off untrusted networks (put an ACL or VPN in front if needed).

## What you will need

- An ESP32 board.
- Something to be the "origin" service, and something to connect as a client.
  Part 1 uses any PC / Raspberry Pi for both.

## Part 1 - Try it with no special hardware

On a machine on your network (note its IP with `hostname -I` / `ipconfig`), run a
tiny origin web server:

```bash
python3 -m http.server 8000      # serves the current folder on port 8000
```

Point the sketch's origin at it: set `ORIGIN_HOST` to that machine's IP and
`ORIGIN_PORT` to `8000` (see Part 2). Flash the board, then from any machine
connect **to the board** on the front port and you reach that web server:

```bash
curl http://<BOARD_IP>:8080/      # goes ESP32:8080 -> origin:8000
```

You should get the directory listing the Python server is serving - fetched
through the ESP32.

## Part 2 - Configure the sketch

Open `PortForward.ino` and edit the lines marked **CHANGE ME**:

| Line          | Set it to                                                 |
| ------------- | --------------------------------------------------------- |
| `SSID`        | your WiFi network name                                    |
| `PASSWORD`    | your WiFi password                                        |
| `FRONT_PORT`  | the port to open on the board (e.g. `8080`)               |
| `ORIGIN_HOST` | the internal host to forward to (the Part 1 machine's IP) |
| `ORIGIN_PORT` | that service's port (e.g. `8000`)                         |

Flash and open Serial Monitor @ **115200**; you should see:

```
IP: 192.168.1.77
relaying 192.168.1.77:8080  ->  192.168.1.60:8000
connect to this board on port 8080 and you reach the origin
```

## Troubleshooting

- **The connection hangs / resets immediately.** The origin is unreachable from
  the board (wrong `ORIGIN_HOST`/`ORIGIN_PORT`, or a firewall). The board dials
  the origin when the inbound connection arrives; if that fails it drops the
  inbound.
- **"relay publish failed".** The front port could not be opened (already in use)
  or more than `PC_RELAY_MAX_PUBLISH` ports were published.
- **Only a few connections work at once.** Concurrent relays are capped by
  `PC_RELAY_MAX_CONNS` (raise it in `protocore_config.h`).

## Going further

- **Publish several ports.** Call `server.listen` + `pc_relay_publish` once per
  port (up to `PC_RELAY_MAX_PUBLISH`), each to a different origin.
- **Throughput.** Each relay carries up to `PC_RELAY_BUF` bytes per pump step;
  raise it for bulk transfers, at the cost of RAM per connection. **HW-verified**
  on an ESP32-S3 over a W5500 wired link: a 1 MB file pulled through the front port
  came back byte-exact (SHA256 matched the origin). Expect ~44 KB/s on a **single-NIC
  relay** - every byte crosses the one interface twice (origin->board, board->client)
  and the rate is round-trip-latency bound, so the relay suits publishing a service /
  small control files rather than bulk transfer.

## Build and run (PlatformIO)

The relay lives inside the library, so the flag must reach the whole build:

```bash
pio ci examples/L7-Application/PortForward \
  --board esp32dev \
  --lib "." \
  --project-option="build_flags=-DPC_ENABLE_RELAY=1"
```

(The Arduino IDE reads the flag from `build_opt.h` beside the sketch automatically.)

---

## How it works under the hood (for the curious)

`server.listen(port, PROTO_RELAY)` registers a listener whose accepted
connections are handled by the relay's `ProtoHandler`. On accept it dials the
origin through `pc_client` (the shared outbound TCP transport) and pairs the two
sockets in a `pc_relay`. Each `server.handle()` tick calls `pc_relay_step`,
which moves whatever bytes are ready in each direction, carrying anything the far
side is too busy to accept yet (backpressure). When either side closes, the relay
tears the other down. The byte-pump engine is transport-agnostic and unit-tested
on its own; this listener is the thin glue that binds its seams to the server's
inbound connection and an outbound `pc_client`.
