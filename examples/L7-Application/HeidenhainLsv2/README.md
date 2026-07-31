# HeidenhainLsv2 - read run state from a Heidenhain TNC control

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_LSV2`

## What this example teaches

Heidenhain **TNC** controls (iTNC 530, TNC 320 / 620 / 640, ...) speak **LSV/2** for DNC and data
access - file transfer, program upload/download, and run/status inspection - over a serial link or,
as here, **LSV/2-over-TCP** (default port **19000**). It is the CNC-native way to pull live machine
state from the common European control with no vendor middleware. `services/lsv2` is a pure codec for
the telegram framing; the sketch owns the socket.

Every LSV/2 telegram is a **4-byte big-endian payload-length prefix**, a **4-character mnemonic**,
then the payload. The length counts the payload only, so a bare acknowledgement is exactly 8 bytes:

```
A_LG "INSPECT\0"     ->  00 00 00 00 T_OK            (access granted)
R_RI <EXEC_STATE=23> ->  00 00 00 NN S_RI <payload>  (run-info data)   or  ... T_ER <class> <code>
A_LO                 ->  00 00 00 00 T_OK            (rights dropped)
```

The codec frames each request and slices each reply off the byte stream:

```cpp
pc_lsv2_build_login(tx, sizeof(tx), PC_LSV2_LOGIN_INSPECT, nullptr); // A_LG "INSPECT\0"
pc_lsv2_build_run_info(tx, sizeof(tx), LSV2_RI_EXEC_STATE);           // R_RI <2-byte selector>
pc_lsv2_build_filename(tx, sizeof(tx), PC_LSV2_CMD_FILE_LOAD, "PGM.H"); // R_FL "PGM.H\0"
Lsv2Telegram r;  size_t used;  pc_lsv2_parse(rx, n, &r, &used);       // one telegram off the stream
pc_lsv2_is_ok(&r);                                                    // T_OK?
pc_lsv2_error(&r, &err_class, &err_code);                             // T_ER / T_BD 2-byte error
```

Access is privilege-gated: log in to a group (`INSPECT` to read, `FILE` for the file system, `DNC`
for DNC functions, ...) with `A_LG`, then issue commands; the control answers `T_OK` / `T_FD`, a
`S_*` data reply, or `T_ER` / `T_BD` carrying a two-byte error-class + error-code.

## Prerequisites (a Heidenhain control, or a simulator)

Point `TNC_IP` at a TNC with the **LSV/2 / DNC** Ethernet interface enabled (the control's network /
DNC settings). No machine handy? The `pyLSV2` project ships a small LSV/2 server stub, or a socket
server that answers each request with the `00 00 00 00 T_OK` / `S_RI` frames above works for a dry run.

## Build and run

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_LSV2=1" \
  --lib="." examples/L7-Application/HeidenhainLsv2/HeidenhainLsv2.ino
```

Flash and watch Serial @ 115200:

```
IP: 192.168.1.42
[lsv2] login INSPECT: T_OK
[lsv2] exec-state: S_RI, 2 payload bytes: 00 03
[lsv2] selected-pgm: S_RI, 24 payload bytes: 54 4E 43 3A 5C ...
[lsv2] override: S_RI, 6 payload bytes: 00 64 00 64 00 64
[lsv2] done
```

## Annotated source

The complete sketch is [HeidenhainLsv2.ino](HeidenhainLsv2.ino). The codec itself is in
[src/services/machine_tool/lsv2/lsv2.h](../../../src/services/machine_tool/lsv2/lsv2.h); the telegram framing, the command /
response mnemonic set, and the login / filename / run-info payload layouts are cross-checked
byte-for-byte against the [pyLSV2](https://github.com/drunsinn/pyLSV2) reference. Pairs with the
[HaasMdc](../HaasMdc/README.md) and Fanuc FOCAS machine-tool examples.
