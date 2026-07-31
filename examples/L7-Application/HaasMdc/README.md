# HaasMdc - collect machine data from a Haas CNC control

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_HAAS_MDC`

## What this example teaches

Haas CNC mills and lathes answer a small, fully-documented **Machine Data Collection (MDC)**
query set - the `?Q` commands - over RS-232 or a raw TCP socket (turned on by **Setting 143**,
default port **5051**). It is the cheapest way to pull live machine status (serial, model, mode,
active program, run status, parts counter, any macro/system variable) with no vendor middleware.
`services/haas_mdc` is a pure codec for it; the sketch owns the socket.

You send an uppercase `?Q###` line; the control replies with a comma-delimited payload framed
between **STX (0x02)** and **ETB (0x17)**, followed by CR LF and a `>` prompt:

```
?Q100\r   ->  <STX>SERIAL NUMBER, 1234567<ETB>\r\n>
?Q500\r   ->  <STX>PROGRAM, O00010, Simatic3964State::IDLE, PARTS, 42<ETB>\r\n>   (or  <STX>STATUS, BUSY<ETB>\r\n>)
?Q600 100 ->  <STX>MACRO, 100, 0.000000<ETB>\r\n>
```

The codec builds the queries and decodes the replies - scanning for the STX/ETB delimiters
(never fixed offsets), so `read_frame` just accumulates bytes through the ETB:

```cpp
pc_haas_mdc_build_q(cmd, sizeof(cmd), HAAS_Q_SERIAL);   // "?Q100\r"
pc_haas_mdc_build_var(cmd, sizeof(cmd), 100);           // "?Q600 100\r"
HaasMdcResp r;  pc_haas_mdc_parse(frame, len, &r);      // payload -> trimmed CSV fields
pc_haas_mdc_value(&r, &v, &vl);                         // simple "LABEL, value" reply
pc_haas_mdc_parse_status(&r, &s);                       // Q500: PROGRAM,... vs STATUS, BUSY
pc_haas_mdc_parse_macro(&r, &var, &val, &vl);           // Q600: MACRO, <var>, <value>
```

`Q500` has two branches (running/idle vs `STATUS, BUSY`); an unsupported or lowercase command
returns `UNKNOWN` (`pc_haas_mdc_is_error`). A running G-code program can also push `DPRNT(...)`
lines on the same link - raw CRLF text with no STX/ETB - which `pc_haas_mdc_dprnt_line`
separates from framed Q replies.

## Prerequisites (a Haas control, or a simulator)

Point `HAAS_IP` at a Haas control with MDC enabled (**Setting 143** on, note its port). No machine
handy? A small socket server that echoes the `<STX>...<ETB>\r\n>` frames for each `?Q` (the vectors
above) works for a dry run.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_HAAS_MDC=1" \
  --lib="." examples/L7-Application/HaasMdc/HaasMdc.ino
```

Flash and watch Serial @ 115200:

```
IP: 192.168.1.42
[haas] serial: 1234567
[haas] model: VF-2
[haas] mode: MEM
[haas] program O00010  status Simatic3964State::IDLE  parts 42
[haas] macro #100 = 0.000000
[haas] done
```

## Annotated source

The complete sketch is [HaasMdc.ino](HaasMdc.ino). The codec itself is in
[src/services/machine_tool/haas_mdc/haas_mdc.h](../../../src/services/machine_tool/haas_mdc/haas_mdc.h); the request framing,
the STX/ETB response wrapper, and the Q100/Q104/Q500/Q600 field layouts are verified against the
Haas MDC service manual (Setting 143) and cross-checked byte-for-byte against a production Haas
serial adapter. Pairs with the [Umati](../Umati/README.md) and Fanuc FOCAS machine-tool examples.
