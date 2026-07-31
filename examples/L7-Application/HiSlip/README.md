# HiSlip - drive an instrument over HiSLIP (the modern LXI transport)

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_HISLIP`

## What this example teaches

HiSLIP (High-Speed LAN Instrument Protocol, IVI-6.1) is VXI-11's higher-throughput
successor and the current LXI transport for LAN test instruments. It carries SCPI over
**two TCP connections to port 4880** - a synchronous channel (the ordered SCPI
command/response stream) and an asynchronous channel (out-of-band control: lock,
status/SRQ, device-clear) - correlated by a 16-bit **SessionID** from the handshake.
`services/hislip` is a pure codec: it builds and parses the fixed 16-byte message header
and the handshake / data messages; the sketch owns the two sockets.

Every message is a 16-byte header (`"HS"` + message type + control code + 32-bit
MessageParameter + 64-bit PayloadLength, all big-endian) optionally followed by a payload:

```cpp
// Initialize the session (sync channel): offer v1.1, a vendor id, sub-address "hislip0"
size_t n = pc_hislip_build_initialize(buf, sizeof(buf), PC_HISLIP_VERSION_1_1, 0x4457, "hislip0");
// ...write, read the reply, parse it...
HislipInitializeResponse ir;
pc_hislip_parse_initialize_response(resp, PC_HISLIP_HEADER_LEN, &ir);   // ir.session_id
```

The second channel binds to the same session, then SCPI flows as Data / DataEND messages
keyed by a MessageID:

```cpp
pc_hislip_build_async_initialize(buf, sizeof(buf), ir.session_id);        // bind async channel
uint32_t id = PC_HISLIP_MESSAGE_ID_INIT;                                  // 0xFFFFFF00
pc_hislip_build_data(buf, sizeof(buf), /*is_end=*/true, 0, id, (const uint8_t *)"*IDN?\n", 6);
id = pc_hislip_next_message_id(id);                                       // += 2
```

The full `HislipMsg` message-type enum (0-38, including the HiSLIP 2.0 TLS / SASL types),
the header byte layout, and the MessageID rule are verified against IVI-6.1. Pair with
`PC_ENABLE_SCPI` to build / parse the SCPI payloads themselves (see the `Scpi` example).

## Prerequisites (an instrument or a simulator)

Point `INSTRUMENT_IP` at anything that speaks HiSLIP on TCP 4880: a real LXI instrument,
a Python `pyvisa` setup with a HiSLIP server, or the [`PyHiSLIP`](https://github.com/llemish/PyHiSLIP)
reference server for a dry run.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_HISLIP=1" \
  --lib="." examples/L7-Application/HiSlip/HiSlip.ino
```

Flash and watch Serial @ 115200:

```
IP: 192.168.1.42
[hislip] session=1 server-version=0x0101 overlap=0
[hislip] *IDN? -> KEYSIGHT TECHNOLOGIES,34470A,MY12345678,A.02.16
[hislip] done
```

## Annotated source

The complete sketch is [HiSlip.ino](HiSlip.ino). The codec itself is in
[src/services/instrumentation/hislip/hislip.h](../../../src/services/instrumentation/hislip/hislip.h); the header layout,
message-type codes, and handshake vectors are verified against the IVI-6.1 HiSLIP 2.0
specification (cross-checked with the Wireshark dissector, MSL-equipment, and PyHiSLIP).
