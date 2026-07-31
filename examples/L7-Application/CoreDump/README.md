# CoreDump - recover a crash after the reboot that erased the evidence

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_EXC_DECODER`, `PC_ENABLE_FTP`, `PC_ENABLE_FTP_SESSION`

## What this example teaches

A panic prints a Guru Meditation dump to a console nobody is watching, then reboots and takes the
evidence with it. On a headless device in the field that is the whole problem: it rebooted, and you
have no idea why.

ESP-IDF also writes a **core dump to a flash partition**, and that survives the reboot. So the next
boot can recover it:

| Step               | Call                                          | What it gives you                               |
| ------------------ | --------------------------------------------- | ----------------------------------------------- |
| 1. is one waiting? | `pc_exc_coredump_present(&img)`               | size + flash address, checksum verified         |
| 2. what crashed?   | `pc_exc_coredump_summary(&info)`              | fills the same `ExcInfo` the live panel renders |
| 3a. keep it        | `pc_exc_coredump_save(SD_MMC, "/crash.bin")`  | streamed to a file, no heap                     |
| 3b. get it off-box | `pc_ftp_store(&target, path, size, src, ctx)` | streamed to an FTP server, no heap              |
| 4. make room       | `pc_exc_coredump_erase()`                     | so the next crash can be stored                 |

Step 2 reuses `pc_exc_json()`, so a crash recovered from flash renders through the exact same
`/exception` panel as a live console capture.

## Why both offloads

A dump that only ever reaches the device's own SD card is still lost with the device. Getting it to a
machine that has the firmware ELF is the point, so this example does both, and erases only once at
least one succeeded - otherwise the dump stays in flash and the next boot tries again.

The two share one seam. `pc_exc_coredump_read(offset, buf, len)` pulls the image in chunks, and the
FTP uploader is handed a source callback that calls it:

```cpp
static size_t coredump_source(void *ctx, size_t offset, uint8_t *buf, size_t cap)
{
    return pc_exc_coredump_read(offset, buf, cap) ? cap : 0;
}
```

So the dump streams straight from flash to the socket: no temp file, no buffer holding it, and neither
owner knows about the other.

## Architecture matters here

- **Xtensa** (ESP32 / S2 / S3): the windowed register ABI lets the device walk its own stack, so the
  summary carries a **real backtrace** and `frames` is populated.
- **RISC-V** (C3 / C6 / H2 / **P4**): the summary carries a **stack dump**, not a backtrace -
  unwinding it needs DWARF, which lives off-device. `frame_count` is 0 and only the faulting PC and
  the machine trap cause/value are reported.

The API does not paper over that difference: on RISC-V it reports no frames rather than inventing
them, which is precisely why offloading the raw image (step 3) matters on those parts.

## The saved file is a raw image, not a bare ELF

`pc_exc_coredump_save()` writes the partition **verbatim**, in ESP-IDF's flash format:

```
offset 0    uint32 total image size   (e.g. 0x00002f44 = 12100)
offset 4    ESP-IDF core-dump header
offset 24   ET_CORE ELF starts here   (7f 45 4c 46 ...)
...         trailing checksum
```

So hand it to the host tool as **raw**, not elf:

```sh
esp-coredump info_corefile -c crash.bin -t raw build/firmware.elf
dd if=crash.bin bs=1 skip=24 of=crash.elf     # if you want a plain ELF
```

## Verified on hardware

### ESP32-P4 (RISC-V, wired Ethernet, SD offload)

**HW-verified (2026-07-19).** The full cycle was run twice to prove the erase works:

```
boot (clean)          GET /exception -> {}
GET /crash            null dereference -> panic -> reboot
boot (recovered)      GET /exception -> {"cause":"pc_worker","pc":"0x4000004e",
                                         "excvaddr":"0x00000000","backtrace":[]}
                      GET /files/crash.bin -> 200, 12100 bytes
boot (after erase)    GET /exception -> {}          <- partition really was cleared
GET /crash            ... and the whole cycle repeats identically
```

`excvaddr` is `0x00000000` because the test crash dereferences a null pointer - the recovered fault
address is exactly right. `cause` carries the crashing task name (`pc_worker`, the server worker),
and `backtrace` is empty because this is RISC-V, as documented above. The saved image's first word
(`0x00002f44`) equals its own size, and `7f 45 4c 46` appears at offset 24 with `e_machine = 0xf3`
(EM_RISCV) - a genuine ET_CORE ELF wrapped in the IDF header.

### ESP32-S3 (Xtensa, WiFi, FTP offload)

**HW-verified (2026-07-19)** against a live `pyftpdlib` server. This run covers the other
architecture (a real backtrace) and the FTP transport. The whole RFC 959 conversation, traced by the
library's own `PC_LOGD` lines:

```
core dump present: 21540 bytes @ 0x003F0000
crash: {"cause":"pc_worker","pc":"0x42002dde","excvaddr":"0x00000000",
        "backtrace":["0x42002dde","0x42003b35","0x42003da5","0x42003eb9", ... ]}
[D] ftp< 220     [D] ftp> USER   [D] ftp< 331   [D] ftp> PASS   [D] ftp< 230
[D] ftp> TYPE    [D] ftp< 200    [D] ftp> EPSV  [D] ftp< 229
[D] ftp> STOR    [D] ftp< 150    [D] ftp< 226   [D] ftp> QUIT
FTP offload: ok
core-dump partition erased
```

Server side: `STOR .../crash.bin completed=1 bytes=21540`.

The recovered PCs resolve to the true crash path - not a plausible-looking list of numbers:

```
0x42003b35: PC::dispatch_matched_route(...)  protocore.cpp:1462
0x42003da5: PC::match_and_execute(...)       protocore.cpp:1524
0x42003eb9: PC::http_poll_slot(...)          protocore.cpp:1094
```

And the decisive check - Espressif's own tool reading the file that came off the wire:

```
$ esp-coredump info_corefile -c crash.bin -t raw CoreDump.ino.elf
Crashed task handle: 0x3fcc40a8, name: 'pc_worker'
exccause  0x1d (StoreProhibitedCause)
excvaddr  0x0
pc        0x42002ec1   <crash_handler(uint8_t, HttpReq*)+33>
```

`esp-coredump` independently names the crashing function and the same task this decoder reported. If
the uploaded bytes were wrong anywhere, that parse would fail - it even refuses a dump whose embedded
app SHA-256 does not match the ELF you hand it, which is worth knowing: symbolize against the exact
firmware that crashed, not a rebuild.

## Requirements

A `coredump` partition must exist in the partition table (the default Arduino tables have one), and
the IDF build must have `CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH` + `DATA_FORMAT_ELF` - both are on in
arduino-esp32 3.x. Without them `pc_exc_coredump_summary()` returns false rather than guessing.

## Tunables

| Flag                    | Default | Meaning                                              |
| ----------------------- | ------- | ---------------------------------------------------- |
| `PC_EXC_COREDUMP_CHUNK` | 512     | bytes streamed per read when copying out of flash    |
| `PC_EXC_MAX_FRAMES`     | 32      | backtrace frames retained (Xtensa)                   |
| `PC_FTP_CHUNK`          | 512     | bytes staged per data-channel write                  |
| `PC_FTP_REPLY_BUF`      | 512     | control-reply accumulator                            |
| `PC_FTP_TIMEOUT_MS`     | 8000    | per-step timeout: connect, and each control reply    |
| `PC_LOG_LEVEL`          | `NONE`  | set to `PC_LOG_LEVEL_DEBUG` to trace the FTP session |

`PC_ENABLE_FTP_SESSION` needs `PC_CLIENT_CONNS >= 2` (a control connection and a data connection are
open at once); a compile-time check enforces that rather than letting the transfer fail after login.

## Build footprint

| Board    | Flash           | Notes                             |
| -------- | --------------- | --------------------------------- |
| ESP32-S3 | 998,039 B (76%) | with the FTP session + debug logs |
| ESP32-S3 | 965,280 B (73%) | decoder + SD offload only         |
| ESP32-P4 | 737,586 B (56%) | decoder + SD offload only         |

Enabling `PC_ENABLE_FTP_SESSION` also links the outbound TCP client and the DNS resolver, which is
why it is a separate gate from the pure `PC_ENABLE_FTP` codec (BSS goes from ~86 KB to ~104 KB for
the connection pool).

## Build-flag note

The flags must reach the library build, so pass them as build flags:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_EXC_DECODER=1 -DPC_ENABLE_FTP=1 -DPC_ENABLE_FTP_SESSION=1" \
  --lib="." examples/L7-Application/CoreDump/CoreDump.ino
```
