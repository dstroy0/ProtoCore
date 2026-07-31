# SigfoxUplink - send a tiny reading over the Sigfox 0G network

**Layer:** Foundation · **Build flags:** `PC_ENABLE_SIGFOX`

## What this example teaches

The one radio that runs the _opposite_ direction from the gateway examples. Instead of
bridging a southbound radio northbound, the device itself **uplinks** a tiny message
(<= 12 bytes, ~140 per day) over the **Sigfox** 0G network straight to the cloud -
ultra-low-power telemetry from places with no Wi-Fi. A Wisol / Murata Sigfox modem is
driven by **AT commands** over UART; the only hardware-specific code is the UART carry.

```
reading --pc_sigfox_build_uplink()--> "AT$SF=<hex>" --UART--> modem --> Sigfox cloud
                                                modem reply --> pc_sigfox_parse_response()
```

`services/sigfox` is a pure AT-command codec:

```cpp
uint8_t payload[4] = { ... };            // your reading, <= 12 bytes
char cmd[32];
uint16_t n = pc_sigfox_build_uplink(payload, sizeof(payload), cmd, sizeof(cmd));
Serial2.write(cmd, n);                    // "AT$SF=xxxxxxxx\r\n"
// then, over the modem reply:
pc_sigfox_result r = pc_sigfox_parse_response(buf, len);  // SIGFOX_OK / _ERROR / _PENDING
```

The payload is hex-encoded (uppercase, two nibbles per byte), and the response is
classified as OK, ERROR, or still pending. The codec is host-tested in `test/test_sigfox`.

## Wiring (ESP32 <-> Sigfox modem)

| Modem | ESP32        |
| ----- | ------------ |
| TX    | GPIO 16 (RX) |
| RX    | GPIO 17 (TX) |
| VCC   | 3V3          |
| GND   | GND          |

9600 8N1. Needs a Sigfox modem and an active Sigfox subscription for the region to
actually transmit.

## Build-flag note

The flag must reach the library build, so pass it as a build flag:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_SIGFOX=1" \
  --lib="." examples/Drivers/SigfoxUplink/SigfoxUplink.ino
```
