# NfcGateway - a PN532 NFC reader bridged to the gateway

**Layer:** Foundation · **Build flags:** `PC_ENABLE_PN532`, `PC_ENABLE_GATEWAY`

## What this example teaches

An **NFC/RFID** reader plugged into the [gateway](../RadioGateway/README.md) - the
ubiquitous NXP **PN532** over I2C. Scanning a tag becomes a northbound event: the tag UID
is enveloped and published.

```
tag scan --PN532/I2C--> pc_pn532_parse_frame() -> UID -> pc_gateway_uplink()
                                                       |
                                    envelope + topic  nfc/0/<target>
                                                       |
                                           northbound publish (MQTT/HTTP/WS)
```

The only hardware-specific code is the I2C carry of the frame bytes. The PN532 protocol -
build a command, verify the 6-byte ACK, parse the response - is `services/pn532`:

```cpp
uint8_t frame[32];
uint16_t n = pc_pn532_build_frame(PN532_TFI_HOST, cmd, cmd_len, frame, sizeof(frame));
// ... write frame over I2C, read back ...
if (pc_pn532_is_ack(ack, 6)) { /* command accepted */ }
uint8_t tfi; const uint8_t *pd; uint8_t pdlen;
if (pc_pn532_parse_frame(resp, r, &tfi, &pd, &pdlen) > 0) { /* pd = response PData */ }
```

Each frame is `00 00 FF | LEN | LCS | TFI | PData | DCS | 00` with a length checksum and a
data checksum, both verified by the codec. This sketch runs GetFirmwareVersion at boot to
confirm the chip, then polls `InListPassiveTarget` and bridges each detected tag's UID. The
codec is host-tested against the documented command / response frames in `test/test_pn532`.

## Wiring (ESP32 <-> PN532, I2C mode)

| PN532 | ESP32   |
| ----- | ------- |
| SDA   | GPIO 21 |
| SCL   | GPIO 22 |
| VCC   | 3V3     |
| GND   | GND     |

Set the PN532's DIP switches to I2C. Its I2C address is `0x24`.

## Build-flag note

The flags must reach the library build, so pass them as build flags:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_PN532=1 -DPC_ENABLE_GATEWAY=1" \
  --lib="." examples/Drivers/NfcGateway/NfcGateway.ino
```
