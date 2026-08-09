# test/ inventory

What is in this tree and what drives it, indexed by need rather than by name. `README.md` is the
hand-written philosophy plus generated tables; this is the map.

**Derived from code, not from docstrings.** Columns come from each script's flag surface, the paths
it names, the commands it invokes, and whether it holds a write primitive. `W` = writes.

## Shape

| Directory               | Files | Holds                                                        |
| ----------------------- | ----- | ------------------------------------------------------------ |
| `unit/`                 | 251   | 245 suites across 31 groups, one per primitive or codec      |
| `integration/`          | 57    | 56 suites that drive a connection through the session layer  |
| `performance_benching/` | 455   | host and on-device benches, per subsystem                    |
| `penetration_testing/`  | 197   | `pc_pentest.py` plus the ESP32 rig firmware and its builders |
| `servers/`              | 65    | real third-party peers the library is driven against         |
| `reverse_engineering/`  | 34    | side-channel engines and the ESP32 MAC blob analysis         |
| `protocols/`            | 35    | protocol fixtures and captures                               |
| `mocks/`                | 17    | host stand-ins (physical, tcp capture, rx feed)              |
| `vectors/`              | 9     | vendored crypto KAT JSON, compiled into C by `tools/crypto/` |
| `fixtures/`             | 4     | generated SSH keys and similar                               |
| `support/`              | 3     | shared test helpers                                          |
| `interop/`              | 4     | interop run artifacts                                        |

`test_matrix.json` declares **328 envs**. `platformio.ini` is generated from it - never hand-edited.

## I need to...

| I need to                                   | Run                                                                  |
| ------------------------------------------- | -------------------------------------------------------------------- |
| know which envs a change affects            | `select_envs.py` (stdin, or `--changed-file`)                        |
| add or edit an env                          | `test_matrix.json`, then `gen_test_envs.py`                          |
| regenerate `platformio.ini`                 | `gen_test_envs.py` (`--check` to assert no drift)                    |
| regenerate the README's generated sections  | `gen_test_readme.py --check`                                         |
| run everything and write the report         | `run_tests.sh` (POSIX) / `run_tests.ps1` (PowerShell)                |
| diff two runs without timing noise          | `report_stable.py`                                                   |
| merge per-env reports into one              | `merge_report.py`                                                    |
| provision SSH host keys before a build      | `ensure_test_keys.py --if-absent` (pre-build hook)                   |
| drive the device with a real protocol peer  | `servers/interop.py --list`, then pick a peer                        |
| run a real-peer TLS/DTLS/SSH interop        | `servers/{cyclone_dtls,cyclone_ssh,dtls_wolfssl}/run_interop.sh`     |
| attack the running server                   | `penetration_testing/pc_pentest.py`                                  |
| build an on-device bench                    | `penetration_testing/rig_firmware/{s3,p4}/build_*.sh`                |
| verify HAL register maps against the dies   | `penetration_testing/rig_firmware/hal_verify/verify_regmaps.sh`      |
| time a TLS or SSH handshake against the rig | `performance_benching/{tls,ssh}/*.py`                                |
| do side-channel analysis                    | `reverse_engineering/{dpa_cpa,spa,template_attack,timing}_engine.py` |
| analyse the ESP32 MAC blob                  | `reverse_engineering/esp32_mac/blob_*.py`                            |

## Drivers

| Script                | W   | Flags                                       | Shells out to |
| --------------------- | --- | ------------------------------------------- | ------------- |
| `run_tests.sh`        | W   |                                             | pio           |
| `run_tests.ps1`       | W   |                                             | pio           |
| `select_envs.py`      |     | `--base --head --changed-file --full --ini` | git           |
| `gen_test_envs.py`    | W   | `--check`                                   |               |
| `gen_test_readme.py`  | W   | `--check`                                   |               |
| `merge_report.py`     | W   |                                             |               |
| `report_stable.py`    |     |                                             |               |
| `ensure_test_keys.py` |     | `--if-absent`                               |               |

`select_envs.py` prints `FULL`, `NONE`, or a space-separated env list. It consults
`dep_graph.json`; a file missing from the graph falls back to `FULL`, so regenerate the graph after
a rename or the answer is uselessly broad.

## Real-peer interop - `servers/`

`interop.py` is the front door (`--list` enumerates every peer). 40 peer modules under
`servers/peers/`, one per protocol: amqp, coap, dns, euromap77, fanuc_j519, focas, ftp, graphql,
grpcweb, h2, h3, http, jwt, modbus, mqtt, mtconnect, nats, ntp, opcua, openadr, redis, robotics,
sep2, simatic, smb, smtp, snmp, sparkplug, sse, ssh, statsd, stomp, sunspec, syslog, tls, wamp,
webdav, ws, xmpp. Each states whether the device is the client or the server for that protocol.

Three build their peer from source on first run, into `.work/` (not tracked):

| Runner                                    | W   | Peer                                      |
| ----------------------------------------- | --- | ----------------------------------------- |
| `cyclone_ssh/build.sh` + `run_interop.sh` | W   | Oryx CycloneSSH client                    |
| `cyclone_dtls/run_interop.sh`             | W   | Oryx CycloneSSL DTLS 1.3 client (`--rpk`) |
| `dtls_wolfssl/run_interop.sh`             | W   | wolfSSL DTLS 1.3 client                   |

## Rig firmware - `penetration_testing/rig_firmware/`

Builders stage a shared bench body next to its sketch anchor and compile with `arduino-cli`, then
print the `esptool` flash command.

| Target | Builders                                                                                 |
| ------ | ---------------------------------------------------------------------------------------- |
| S3     | `cryptobench`, `fmtbench`, `poolbench`, `poolbench_lto`, `swarbench`, `nsbench`, `nsabi` |
| P4     | `cryptobench`, `linktest`, `tls`                                                         |

`hal_verify/verify_regmaps.sh` compiles a `static_assert` harness per die against
`core_setup/hal/esp/esp_crypto_hal.h`.

## Side channel - `reverse_engineering/`

| Engine                       | Approach                                    |
| ---------------------------- | ------------------------------------------- |
| `timing_engine.py`           | timing only, no probe or scope              |
| `spa_engine.py`              | single-trace structural                     |
| `dpa_cpa_engine.py`          | many-trace statistical (TVLA/SNR, CPA)      |
| `template_attack_engine.py`  | profiled templates                          |
| `dpa_cpa_network_engine.py`  | receives traces over the DAQ wire protocol  |
| `hardening_demo.py`          | same attack against vulnerable vs hardened  |
| `tests/simulate_pipeline.py` | synthesizes packets and verifies end to end |

`esp32_mac/` analyses the vendor blob: `blob_diff`, `blob_parity`, `blob_behavior`,
`blob_crossinstall`, `blob_iram`, `dram_scan`, and `xtensa/blob_{analog,registers}.py`
(`--chip --lib --only --full`). They shell out to `objdump` / `nm`.

## See also

- [README.md](README.md) - philosophy, mocking strategy, and the generated env tables
- [../tools/TOOLS.md](../tools/TOOLS.md) - the same treatment for `tools/`
