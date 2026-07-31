# Vxi11 - drive an instrument over VXI-11 (ONC RPC / XDR)

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_VXI11`

## What this example teaches

VXI-11 is the LAN eXtensions for Instrumentation transport that predates HiSLIP - still the
fallback for a large installed base of LAN instruments. It rides on **ONC RPC (Sun RPC)** with
**XDR** encoding over TCP. `services/vxi11` is a pure codec: it builds the RPC calls and parses
the replies; the sketch owns the sockets and runs the standard session.

The instrument channel is on a **dynamic** port, so you first ask the portmapper (TCP 111):

```cpp
size_t n = pc_vxi11_build_getport(req, sizeof(req), 1,
                                   PC_VXI11_CORE_PROG, PC_VXI11_CORE_VERS, PC_RPC_PROTO_TCP);
// ...send, read the reply...
uint32_t core_port;
pc_vxi11_parse_getport_resp(resp, resp_len, &core_port);
```

Then open a link, write a SCPI command, and read the reply - each call is XDR over an ONC-RPC
record mark:

```cpp
pc_vxi11_build_create_link(req, sizeof(req), 2, clientId, /*lock=*/false, 0, "inst0");   // -> lid
pc_vxi11_build_device_write(req, sizeof(req), 3, lid, 10000, 0, PC_VXI11_FLAG_END,
                             (const uint8_t *)"*IDN?\n", 6);
pc_vxi11_build_device_read (req, sizeof(req), 4, lid, 1024, 10000, 0, 0, 0);             // -> data
pc_vxi11_build_destroy_link(req, sizeof(req), 5, lid);
```

The reusable ONC-RPC framing (`pc_rpc_record_mark`, `pc_rpc_parse_reply`) and the XDR layout
(big-endian, 4-byte aligned, length-prefixed opaque/string) are verified against the VXI-11 spec +
RFC 5531 / 4506 / 1833, with a byte-exact create_link vector. Pair with `PC_ENABLE_SCPI` to
build / parse the SCPI payloads themselves (see the `Scpi` example).

## Prerequisites (an instrument or a simulator)

Point `INSTRUMENT_IP` at a real VXI-11 / LXI instrument, or run a
[`python-vxi11`](https://github.com/python-ivi/python-vxi11) server for a dry run (it registers with
the host's `rpcbind`/portmapper on 111).

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_VXI11=1" \
  --lib="." examples/L7-Application/Vxi11/Vxi11.ino
```

Flash and watch Serial @ 115200:

```
IP: 192.168.1.42
[vxi11] DEVICE_CORE port = 1024
[vxi11] link=0 maxRecv=1048576
[vxi11] *IDN? -> KEYSIGHT TECHNOLOGIES,34470A,MY12345678,A.02.16
[vxi11] done
```

## Annotated source

The complete sketch is [Vxi11.ino](Vxi11.ino). The codec itself is in
[src/services/instrumentation/vxi11/vxi11.h](../../../src/services/instrumentation/vxi11/vxi11.h); the XDR struct layouts, procedure
numbers, and the ONC-RPC call/reply headers are verified against the VXI-11 specification and RFC
5531 / 4506 / 1833 (cross-checked with python-vxi11 and the Wireshark dissector).
