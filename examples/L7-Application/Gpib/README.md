# Gpib - drive a legacy GPIB instrument through a Prologix adapter

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_GPIB`

## What this example teaches

A huge installed base of bench instruments speaks only **IEEE-488 (GPIB)** and will never
talk SCPI-over-TCP directly. A **Prologix GPIB-Ethernet** adapter bridges them onto the LAN:
you open a raw TCP socket (port 1234) and speak the Prologix `++` command language.
`services/gpib` is a pure codec for that command set; the sketch owns the socket.

A line starting with `++` is a controller command; anything else is data forwarded over GPIB
to the addressed instrument. Configure the adapter, then send SCPI as data:

```cpp
pc_gpib_command(cmd, sizeof(cmd), "mode 1");            // controller-in-charge
pc_gpib_addr(cmd, sizeof(cmd), 9, -1);                  // ++addr 9  (instrument address)
pc_gpib_eos(cmd, sizeof(cmd), GpibEos::LF);             // ++eos 2   (append LF over GPIB)
pc_gpib_command(cmd, sizeof(cmd), "eoi 1");             // assert EOI on the last byte
pc_gpib_build_data(data, sizeof(data), (const uint8_t *)"*IDN?", 5);  // send SCPI as data
pc_gpib_read(cmd, sizeof(cmd), GpibRead::UNTIL_EOI, 0); // ++read eoi -> read the reply
```

The data escaping is the subtle part: each CR / LF / ESC / `+` byte in the payload is preceded
by an ESC (27) so it is not eaten as a line terminator or mistaken for a command - verified
byte-exact against the Prologix manual's binary example. Response parsers cover the serial-poll
status byte (`pc_gpib_parse_decimal` after `++spoll`), the address, and the `++ver` string.

## Prerequisites (a Prologix adapter)

Point `ADAPTER_IP` at a Prologix GPIB-Ethernet controller (or an
[AR488](https://github.com/Twilight-Logic/AR488)-Ethernet build) with a GPIB instrument at
`INSTRUMENT_ADDR`. No adapter handy? A Prologix-emulating socket server on a host works for a
dry run.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_GPIB=1" \
  --lib="." examples/L7-Application/Gpib/Gpib.ino
```

Flash and watch Serial @ 115200:

```
IP: 192.168.1.42
[gpib] *IDN? -> HEWLETT-PACKARD,34401A,0,11-5-2
[gpib] adapter version 1.6.6.0
[gpib] done
```

## Annotated source

The complete sketch is [Gpib.ino](Gpib.ino). The codec itself is in
[src/services/instrumentation/gpib/gpib.h](../../../src/services/instrumentation/gpib/gpib.h); the command set, the `++eos`
mapping, the data-escaping rule, and the TCP port are verified against the Prologix GPIB-Ethernet
and GPIB-USB controller manuals (cross-checked with AR488).
