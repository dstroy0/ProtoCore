# EthernetDnc - drip-feed a G-code program to a CNC controller over TCP

This sketch streams a machine-tool program to a CNC controller the classic "DNC"
way - one block (line) at a time, with XON/XOFF flow control - but over an
Ethernet TCP connection instead of an RS-232 cable. Many controllers expose a
raw TCP "program port" that accepts exactly this stream.

You do not need a CNC machine to try it: Part 1 shows how to capture the
drip-feed with a one-line listener on any PC or Raspberry Pi and confirm the
program arrived intact.

## What is going on here? (the big picture)

**DNC** (Distributed Numerical Control) is how a program too big for a
controller's memory is fed to it live: the sender transmits a block, the
controller executes/buffers it, and when its small input buffer fills it sends
**XOFF** (`Ctrl-S`, byte `0x13`) to say "pause"; when there is room again it
sends **XON** (`Ctrl-Q`, byte `0x11`) to say "resume". The program is wrapped
with a `%` "rewind stop" marker at the start and end.

`dnc_stream()` does all of that for you: it sends the leader, the `%` start
marker, each line of your program as a block, the `%` end marker, and the
trailer - pausing whenever the controller sends XOFF and resuming on XON. It is
**transport-agnostic**: it moves bytes only through a send/recv seam, so the same
engine works over TCP (this example) or a UART (classic RS-232). This sketch
supplies the glue - `cl_send` / `cl_recv` over `pc_client`, the library's shared
outbound TCP transport.

Two "tape codes" are supported, matching what the controller expects:

- `DNC_CODE_ISO` - ISO 7-bit / ASCII (the common modern default).
- `DNC_CODE_EIA` - the older EIA RS-244 punched-tape code (uppercase only).

## What you will need

- An ESP32 board.
- Either a CNC controller with a raw TCP program port, **or** any PC / Raspberry
  Pi to play the part of one (Part 1).

## Part 1 - Stand in for a controller (test without a machine)

On a PC or Raspberry Pi on the same network, listen on a port and capture what
the board sends:

```bash
# netcat: accept one connection on port 5000 and save the stream to a file
nc -l 5000 > received.nc
```

(On some systems the flag is `nc -l -p 5000`.) Note that machine's IP address
with `hostname -I` (Linux) or `ipconfig` (Windows). Leave the listener running;
it will exit when the board closes the connection, leaving `received.nc`.

## Part 2 - Tell the ESP32 where to send

Open `EthernetDnc.ino` and edit the lines marked **CHANGE ME**:

| Line       | Set it to                                                 |
| ---------- | --------------------------------------------------------- |
| `SSID`     | your WiFi network name                                    |
| `PASSWORD` | your WiFi password                                        |
| `CNC_HOST` | the controller's (or listener's) IP address               |
| `CNC_PORT` | the raw program port (`5000` to match the listener above) |

If your controller uses the EIA tape code, change `cfg.code` to `DNC_CODE_EIA`.
`cfg.crlf` sends CR before each LF (common); `cfg.leader_len` is the blank
runout length.

## Part 3 - Flash it and watch

Flash the sketch and open Serial Monitor @ **115200**. You should see:

```
Connecting to WiFi....
IP: 192.168.1.77
program sent - the controller has the full drip-feed
```

Then look at `received.nc` on the listener machine - it holds the framed
program, `%` markers and all:

```
%
O0001 (DEMO)
N10 G21 G90
N20 G0 X0 Y0
N30 G1 X10 Y5 F100
N40 M30
%
```

(The leading/trailing blank runout bytes are invisible NULs.)

> **Verified on hardware.** On an ESP32-S3 over a W5500 wired Ethernet link, the
> drip to a TCP capture sink was **byte-exact** against an independently computed
> reference (16-NUL leader, `%`CRLF, each block with CR before LF, `%`CRLF,
> 16-NUL trailer), and the flow control holds: with the sink asserting XOFF,
> **zero** program bytes were sent during a 3 s hold, and the full identical
> program arrived the moment XON was sent.

## Troubleshooting

The sketch prints `DncStreamResult` codes.

| Code | Name                    | Likely cause                                                                                                                              |
| ---- | ----------------------- | ----------------------------------------------------------------------------------------------------------------------------------------- |
| `-1` | `DNC_STREAM_ERR_ARG`    | a null argument - check `PROGRAM` is set                                                                                                  |
| `-2` | `DNC_STREAM_ERR_IO`     | the connection dropped, or the controller held XOFF too long                                                                              |
| `-3` | `DNC_STREAM_ERR_ENCODE` | a character can't be sent in the chosen tape code (e.g. a lowercase letter in `DNC_CODE_EIA`), or a line is longer than `PC_DNC_LINE_MAX` |

"connect failed" (before any result code) means the TCP connection never opened -
the host/port is wrong or the listener/controller is not accepting connections.

## Going further

- **Stream a real file.** Pull the program from an SD card, HTTP, or an SMB share
  (see `SmbFileClient`) into a buffer and pass it to `dnc_stream`.
- **EIA controllers.** Set `cfg.code = DNC_CODE_EIA` - the engine translates every
  character to the EIA RS-244 tape code on the fly.
- **Slow controllers.** If a controller holds XOFF for a long time, raise
  `PC_DNC_XOFF_MAX_POLLS` in `protocore_config.h`.

## Build and run (PlatformIO)

DNC lives inside the library, so the flag must reach the whole build:

```bash
pio ci examples/L7-Application/EthernetDnc \
  --board esp32dev \
  --lib "." \
  --project-option="build_flags=-DPC_ENABLE_DNC=1"
```

(The Arduino IDE reads the flag from `build_opt.h` beside the sketch automatically.)

---

## How it works under the hood (for the curious)

`dnc_stream` never touches a socket directly - it calls the two function pointers
you pass. `cl_send` writes every byte with `pc_client_send`. `cl_recv` does a
**non-blocking** read of the reverse channel: it returns any XON/XOFF bytes
immediately, returns `0` (after a 1 ms nap) when nothing is waiting so the
engine's XOFF pause loop does not busy-spin, and returns `-1` if the controller
closed the link. The engine feeds whatever `cl_recv` returns into its flow-control
state (`DncFlow`), so a byte-perfect `0x13` in the middle of your G-code is never
mistaken for XOFF - flow control is only read from the reverse channel, never
from the program you are sending. Point the same engine at a UART's read/write
and you have classic RS-232 DNC with no other changes.
