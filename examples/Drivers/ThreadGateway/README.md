# ThreadGateway - an OpenThread RCP bridged to the gateway

**Layer:** Foundation · **Build flags:** `PC_ENABLE_THREAD`, `PC_ENABLE_GATEWAY`

## What this example teaches

A **Thread** mesh plugged into the [gateway](../RadioGateway/README.md) through an
OpenThread **radio co-processor** (RCP - an nRF52840 / EFR32), which speaks **spinel** over
**HDLC-lite** framing on a UART. This decodes the HDLC frames and bridges each spinel
payload northbound - the basis of a Thread / Matter border router.

```
Thread RCP --UART--> pc_spinel_frame_decode() --> spinel payload -> pc_gateway_uplink()
                                                                      |
                                               envelope + topic  thread/0/<tid>
                                                                      |
                                                       northbound publish (MQTT/HTTP/WS)
```

HDLC-lite wraps each spinel frame with an **FCS** (CRC-16/X-25), byte-stuffs the reserved
bytes, and terminates with a Flag `0x7E`. `services/thread` does the framing:

```cpp
uint8_t payload[256]; uint16_t plen;
int n = pc_spinel_frame_decode(buf, len, payload, sizeof(payload), &plen);  // >0 / need-more / -1
if (n > 0)
    pc_gateway_uplink(0, tid, payload, plen, 0);
```

`pc_spinel_frame_encode()` builds a frame the same way (this sketch sends a spinel RESET at
boot). Note the FCS is CRC-16/**X-25** (reflected, final XOR), transmitted low byte first -
distinct from Zigbee's ASH CRC. The codec is host-tested against the X-25 catalog check
value (`0x906E`) and the byte-stuffing in `test/test_thread`.

## Reading the spinel command inside a frame

`services/thread` also does the spinel **command** layer and the **property registry / value
semantics**, so the sketch reads each frame's meaning, not just its bytes. A frame's payload is
`header | command | property | value`; `pc_spinel_command_parse()` splits it, the registry names
the property, and a typed cursor decodes the value by the property's datatype:

```cpp
uint8_t hdr; uint32_t cmd, prop; const uint8_t *val; uint16_t vlen;
pc_spinel_command_parse(payload, plen, &hdr, &cmd, &prop, &val, &vlen);
if (cmd == SpinelCmd::SPINEL_CMD_PROP_VALUE_IS && prop == SpinelProp::SPINEL_PROP_NCP_VERSION) {
    SpinelReader r; pc_spinel_reader_init(&r, val, vlen);
    const char *s; uint16_t slen;
    if (pc_spinel_get_utf8(&r, &s, &slen))       // "OPENTHREAD/... ; ESP32C6; ..."
        Serial.printf("%s = %.*s\n", pc_spinel_prop_name(prop), (int)slen, s);
}
```

The cursor has an accessor per spinel datatype (`get_bool` / `get_u8` / `get_i8` / `get_u16` /
`get_u32` / `get_uint` (packed) / `get_eui64` / `get_ipv6` / `get_utf8` / `get_data` /
`get_data_wlen`) and matching `put_*` builders; an out-of-bounds read latches an error you check
once with `pc_spinel_reader_ok()`. This sketch GETs `PROTOCOL_VERSION`, `NCP_VERSION`, and
`HWADDR` at boot and prints each inbound `PROP_VALUE_IS` by name. `pc_spinel_status_name()` maps
a `LAST_STATUS` code to text (e.g. a reset cause `0x70` -> `RESET`).

## Wiring (ESP32 host <-> Thread RCP)

| RCP | ESP32        |
| --- | ------------ |
| TX  | GPIO 16 (RX) |
| RX  | GPIO 17 (TX) |
| VCC | 3V3          |
| GND | GND          |

460800 8N1 (the OpenThread RCP default). The RCP is any spinel radio co-processor: an nRF52840
dongle flashed with the OpenThread RCP firmware, an EFR32, or an **ESP32-C6 flashed with ESP-IDF's
`ot_rcp`** (the C6 is a native 802.15.4 radio, so that route needs no external module - the host
UART wires to the C6's RCP UART pins).

## Verified on real hardware (ESP32-C6 RCP)

**HW-verified (2026-07-19)** against a real **ESP32-C6 running ESP-IDF `ot_rcp`** - genuine
OpenThread RCP firmware on a native 802.15.4 radio. The shipped codec built every byte sent and
decoded every byte received (`pc_spinel_frame_encode`/`_decode`, `pc_spinel_command_build`/`_parse`,
the property registry and typed accessors); nothing hand-rolled HDLC or spinel:

```
-- reset --
  LAST_STATUS        = PackMlCommand::RESET (114)   [tid 0]
-- properties --
  PROTOCOL_VERSION   = 4.3
  INTERFACE_TYPE     = 3 (3 = Thread)
  NCP_VERSION        = openthread-esp32/8c750b08-ec2b0d487; esp32c6;  2026-07-19 22:46:33 UTC
  CAPS               = 5 12 24 34 513 64 65   (7 caps)
  HWADDR             = ACEBE6FFFEC1DE00
  PHY_CHAN           = 11
  MAC_15_4_PANID     = 0xFFFF
  MAC_15_4_LADDR     = 0000000000000000
== 8/8 properties decoded by the PC codec ==
```

`HWADDR` is that chip's real factory EUI64 and `CAPS` includes `513`, which only decodes correctly
if the packed-uint reader handles a multi-byte value. `MAC_15_4_LADDR` is all-zero because the radio
had not been brought up - the RCP is answering honestly, and the codec reports it faithfully.

**Transport note (important):** stock `ot_rcp` puts spinel on **UART0's GPIO pins**, which a devkit
whose only host link is the native USB-Serial-JTAG cannot expose without external wiring - a host
write to that port just times out. For this test the RCP was rebuilt with its spinel fd pointed at
`/dev/usbserjtag` instead, so the stream rides USB. That changes only the **transport** the RCP
firmware uses; the spinel/HDLC bytes on the wire are unchanged, which is what this codec is being
verified against.

## Also verified against the OpenThread reference RCP

For protocol conformance against the reference implementation, the same codec was driven over a
raw-mode pty against OpenThread's own `ot-rcp` (POSIX/simulation build), decoding 7/7 properties
including `NCP_VERSION` `OPENTHREAD/5808cb4; SIMULATION; ...` and `HWADDR` `18B4300000000001`.
Because that peer is OpenThread's own implementation rather than a re-encode of our own bytes, it
is a real conformance check and not a self-round-trip. See the `## Thread` section of
[FEATURES.md](../../../docs/FEATURES.md).

## Build-flag note

The flags must reach the library build, so pass them as build flags:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_THREAD=1 -DPC_ENABLE_GATEWAY=1" \
  --lib="." examples/Drivers/ThreadGateway/ThreadGateway.ino
```
