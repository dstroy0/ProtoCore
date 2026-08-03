# ESP32-S3 test-rig firmware

The **target firmware** for the network attack tool (`../pc_pentest.py`) and the JTAG
perf-profiling harness. It brings up `PC` on a broad feature set and exposes the
oracle endpoints the tools rely on, so attacks and benchmarks run against a real device.

This is a standalone PlatformIO project that consumes the library from the repo root
(`lib_deps = symlink://../..`), kept **reproducible** so any run can be rebuilt byte-for-byte
and any bug it surfaces is traceable to an exact build.

## What it exposes

| Route                                      | Purpose                                                                 |
| ------------------------------------------ | ----------------------------------------------------------------------- |
| `GET /`                                    | liveness baseline                                                       |
| `GET /health`                              | `{"heap":<free>}` - the determinism-oracle heap source                  |
| `GET /diag` `GET /stats` `GET /metrics`    | feature snapshot + heap (auto-detection + oracle)                       |
| `GET /bench`                               | cycle-accurate CCOUNT microbench of hot pure ops (JSON)                 |
| `POST /api/echo`                           | body echo (upload / smuggling surface)                                  |
| `GET /secure`                              | Basic-auth protected (`rig` / `admin` / `admin`)                        |
| `GET /ws` (WebSocket), `GET /events` (SSE) | upgrade surface                                                         |
| `GET /mqtt/probe?host=&port=`              | device-as-client MQTT trigger (connect out + sub/pub) for the attack    |
| `GET /probe` `/current` `/sample`          | MTConnect agent (ANSI/MTC1.4): devices + streams XML; sample from/count |
| `POST /redis/parse`                        | RESP reply-parser fuzz surface (runs resp_parse on the POST body)       |
| `GET /redis/probe?host=&port=`             | device-as-Redis-client (connect out + PING/SET/GET) for interop         |
| `/dav` (WebDAV, RFC 4918)                  | LittleFS-backed share (OPTIONS/PROPFIND/PUT/COPY/...)                   |
| CoAP UDP/5683 (RFC 7252)                   | `/info` `/hello` `/echo` + `.well-known/core`; Observe + Block-wise     |
| SNMP UDP/161 (v1/v2c, RFC 1157/3416)       | MIB-II system group + a private free-heap Gauge32; Get/GetNext/GetBulk  |
| OPC UA TCP/4840 (IEC 62541)                | handshake + SecureChannel + Session + Read/Write/Browse (ns=1 objects)  |
| Modbus TCP/502 (Modbus App Protocol)       | slave with 16 holding regs + coils/inputs; FC 1-6/15/16 + exceptions    |

Enabled features (see `platformio.ini`): DIAG, STATS, METRICS, AUTH (+lockout), FILE_SERVING,
RANGE, ETAG, ACCEPT_THROTTLE, CSRF, WEBDAV (LittleFS `/dav` share), COAP (+OBSERVE +BLOCK on UDP/5683),
SNMP (v1/v2c agent on UDP/161), OPCUA (Binary server on TCP/4840), MODBUS (TCP slave on :502), MQTT
(3.1.1 client codec - `/bench` only, no broker connection), MTCONNECT (ANSI/MTC1.4 agent:
`/probe` `/current` `/sample`), the default-on WEBSOCKET + SSE, and WS_DEFLATE (permessage-deflate on `/ws`).

## Hardware: W5500 SPI Ethernet (both rigs)

**Board: ESP32-S3-DevKitC.** Both rigs have a WIZnet **W5500** SPI Ethernet module wired for HW-verifying the
wired link (the SPI-Ethernet path, distinct from the WiFi station bring-up used above). Bus: **HSPI**.

| W5500 signal | ESP32-S3 GPIO | Note                                      |
| ------------ | ------------- | ----------------------------------------- |
| SCS (CS)     | **GPIO 7**    | chip select                               |
| RST          | **GPIO 5**    | reset                                     |
| INT          | **GPIO 6**    | interrupt                                 |
| SCLK         | GPIO 12       | HSPI clock (ESP32-S3-DevKitC default) [1] |
| MOSI         | GPIO 11       | HSPI MOSI (ESP32-S3-DevKitC default) [1]  |
| MISO         | GPIO 13       | HSPI MISO (ESP32-S3-DevKitC default) [1]  |
| VCC          | 3V3           | the W5500 runs at 3.3 V                   |
| GND          | GND           |                                           |

CS / RST / INT are the wired pins (GPIO 7 / 5 / 6). [1] The SCLK / MOSI / MISO bus pins are recorded as the
ESP32-S3-DevKitC default SPI pins - confirm against the actual wiring before the first link-up.

## Reproducible build + flash

Prereqs: PlatformIO, and one of the two ESP32-S3 rigs on the host (native USB, VID:PID `303A:1001`).
The board target is `esp32-s3-devkitc-1`; platform is **pinned** to `espressif32@6.13.0`
(Arduino-ESP32 2.x core). WiFi credentials come from the environment and are **never committed**:

```sh
cd penetration_testing/rig_firmware
RIG_WIFI_SSID='your-ssid' RIG_WIFI_PASS='your-pass' \
  pio run -e rig_s3 -t upload --upload-port /dev/ttyACM0
```

Record the exact toolchain for a given build (paste into the bug log when filing a finding):

```sh
pio system info | grep -i version
pio pkg list -e rig_s3 | grep -iE 'espressif32|framework-arduino|toolchain|esptool'
git rev-parse HEAD            # library commit under test
```

### Firmware migration status (envs)

The **slim** rigs are current with the `pc_` API rename (7.0.0) and the crypto consolidation, and build
green on the stock core: **`rig_s3_ssh`** (SSH server; also carries the `PC_SSH_KEX_BENCH` wall-clock probe),
**`rig_s3_tls`** (HTTPS), **`rig_s3_smb`** (SMB2 client). Two mains still use pre-`pc_`/pre-consolidation
symbols and are **not migrated**: the full **`rig_s3`** (`main.cpp`, ~45 `pc_client_*`/`pc_hex_*`/`PC_`
call sites; also needs the PSRAM arena + a rebuilt arduino-cli core, so it does not build on the stock
toolchain here) and **`rig_s3_cryptobench`** (`main_cryptobench.cpp`, ~8 renamed crypto symbols such as
`quic_hkdf_*`/`mlkem768_encaps`/`dtls_*` and the moved `ssh_sha512.h`). Both are a separate, HW-verified
migration pass; the crypto-sweep numbers already in `docs/FEATURE_PERFORMANCE.md` came from the last good
`rig_s3_cryptobench` run.

## Read the rig's IP

Do **not** `pio monitor` - RTS auto-reset wedges the S3 USB-JTAG. Use the non-resetting logger
(one per rig) and grep its rolling log:

```sh
pc_serial_logger.py /dev/ttyACM0 ~/serial-ttyACM0.log &
grep -a 'RIG_IP=' ~/serial-ttyACM0.log | tail -1
```

## Attack it

```sh
python3 ../pc_pentest.py --host <rig-ip> --diag --intensity high --json report.json --authorized
```

## JTAG perf profiling (cycle-accurate)

The built-in USB-JTAG is a separate USB interface from the CDC serial, so openocd coexists with
the logger. `-Og -ggdb3` keeps the `.elf` symbol-rich for zero-instrumentation profiling.

```sh
# 1) gdb server over the built-in USB-JTAG
openocd -f board/esp32s3-builtin.cfg          # -> gdb server on :3333

# 2) attach with symbols; PC-sample (statistical profiler) or read CCOUNT for cycles/op
xtensa-esp32s3-elf-gdb .pio/build/rig_s3/firmware.elf \
  -ex 'target remote :3333' -ex 'monitor halt' -ex 'p/x $pc' -ex 'monitor resume'
```

## Bugs

Every bug this rig surfaces (attack findings, perf anomalies, crashes) is logged in
[`docs/BUGS.md`](../../docs/BUGS.md) with: the library commit, the pinned build, the exact
attack/bench command, and the observed vs expected behavior - so it is reproducible.
