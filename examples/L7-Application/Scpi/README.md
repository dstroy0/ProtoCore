# Scpi - drive a bench instrument over SCPI / IEEE 488.2

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_SCPI`

## What this example teaches

SCPI (Standard Commands for Programmable Instruments) is the text command language
nearly every modern bench instrument speaks - DMMs, oscilloscopes, power supplies,
function/arbitrary generators, SMUs, spectrum/network analyzers, electronic loads -
over a raw TCP socket on **port 5025** (`PC_SCPI_PORT`). `services/scpi` is a pure,
transport-agnostic codec: it builds command lines and parses replies; the sketch owns
the socket (a plain `WiFiClient`) and runs a small session against a real instrument.

`pc_scpi_build` joins a `:`-hierarchy header with comma-separated parameters and the
newline terminator, and `pc_scpi_common` renders the 13 mandatory IEEE 488.2 common
commands:

```cpp
size_t n = pc_scpi_build(cmd, sizeof(cmd),
                          pc_scpi_common(ScpiCommon::SCPI_IDN_Q), nullptr, 0); // "*IDN?\n"
n = pc_scpi_build(cmd, sizeof(cmd), "MEASure:VOLTage:DC?", nullptr, 0);        // "MEASure:VOLTage:DC?\n"
const char *args[] = {"1.5"};
n = pc_scpi_build(cmd, sizeof(cmd), "SOURce:VOLTage", args, 1);                // "SOURce:VOLTage 1.5\n"
```

Replies are parsed without the standard library: `pc_scpi_parse_number` (NR1/NR2/NR3),
`pc_scpi_parse_bool` (`1`/`0`/`ON`/`OFF`), `pc_scpi_parse_string` (quote-stripping),
and `pc_scpi_parse_block` (the arbitrary block `#<n><len><data>` used for waveform
captures):

```cpp
double volts = 0.0;
if (pc_scpi_parse_number(resp, strlen(resp), &volts))
    Serial.printf("DC voltage = %.6f V\n", volts);
```

The codec is symmetric - it also carries the **instrument** side: the IEEE 488.2 status
model (`ScpiStatus` - the Status Byte, Standard Event Status Register + enable masks, and
the FIFO error/event queue via `pc_scpi_push_error` / `pc_scpi_pop_error` / `pc_scpi_stb`)
and `pc_scpi_match`, a SCPI short/long-form + numeric-suffix header matcher for dispatching
an incoming command against a pattern like `"SYSTem:ERRor?"`. That side turns the device into
a SCPI instrument itself; this sketch demonstrates the controller half.

## Prerequisites (an instrument or a simulator)

Point `INSTRUMENT_IP` at anything that answers SCPI on TCP 5025. To dry-run without
hardware, fake one on a host with a canned responder:

```sh
# a trivial SCPI stub: answers *IDN? and a voltage query, then closes
while true; do printf 'PC,SIM,0,1.0\n1.234567\n0,"No error"\n' | nc -l -p 5025 -q1; done
```

or use a Python `pyvisa` sim / any vendor SCPI simulator.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_SCPI=1" \
  --lib="." examples/L7-Application/Scpi/Scpi.ino
```

Flash and watch Serial @ 115200:

```
IP: 192.168.1.42
[scpi] *IDN? -> KEYSIGHT TECHNOLOGIES,34470A,MY12345678,A.02.16
[scpi] DC voltage = 1.234567 V
[scpi] SYST:ERR? -> 0,"No error"
[scpi] done
```

## Annotated source

The complete sketch is [Scpi.ino](Scpi.ino). The codec itself is in
[src/services/instrumentation/scpi/scpi.h](../../../src/services/instrumentation/scpi/scpi.h); the register bits, error-number
classes, and exact standard error strings are verified against the SCPI-1999 standard and
IEEE 488.2-1992.
