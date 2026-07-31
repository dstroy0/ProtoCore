# HotSwapStorage - survive an SD card being pulled mid-write

**Layer:** L7 Application · **Build flags:** `PC_ENABLE_HOTSWAP`

## What this example teaches

An SD card is a connector, so it can leave while you are writing to it. What makes that dangerous is
how quiet it is: the driver still reports a mounted volume, every write fails into nothing, and code
that does not check the return carries on believing it has storage. Logs vanish, uploads truncate,
and nothing anywhere says why.

`services/hotswap` turns that into a state machine with one job - never let a caller write into
storage that is not there:

```
StorageState::ABSENT  --probe finds a card, mount ok-->  READY
READY   --N consecutive I/O errors------>  FAULTED   (unmounts immediately)
FAULTED --probe interval, remount ok---->  READY
```

The contract for callers is two lines:

```cpp
if (!pc_hotswap_ready())        // the gate: refuse rather than write into a stale mount
    return;
bool ok = do_the_filesystem_call();
pc_hotswap_io(ok);              // report it either way
```

## Why a run of errors, not one

A single failed write is not proof a card left - it can be a transient bus error or a full volume -
and tearing down a working mount over one error would be its own bug. So `PC_HOTSWAP_FAIL_THRESHOLD`
(default 3) consecutive failures declare removal, and **any success resets the run**, which is what
stops intermittent noise from slowly accumulating into a false removal.

Two more choices worth naming:

- The volume starts **ABSENT, not READY**. Nothing may touch it until a probe has actually mounted
  it - assuming storage exists because the code compiled is how you get silent data loss at boot.
- A card that is present but will not mount stays ABSENT. A card that will not mount is not storage,
  whatever the detect pin says.

## What the app owns

Mounting is the application's business, not this owner's, so it supplies three callbacks:

| Callback  | Job                                             | Optional?                         |
| --------- | ----------------------------------------------- | --------------------------------- |
| `mount`   | `SD_MMC.begin()` - bring the volume up          | no                                |
| `unmount` | `SD_MMC.end()` - drop the mount and its handles | no                                |
| `present` | read a card-detect GPIO                         | yes - `nullptr` lets mount decide |

If your board wires a card-detect pin, supply `present`: it is far cheaper than a failed mount
attempt every probe interval.

## Routes

| Route      | What it does                                                                 |
| ---------- | ---------------------------------------------------------------------------- |
| `/storage` | `{"storage":"ready","mounts":1,"faults":0}` for a health panel               |
| `/write`   | appends a line, gated on `ready()` and reporting the outcome                 |
| `/yank`    | unmounts underneath the app, so the fault path can be watched without a card |

`/yank` is not a mock. It calls `SD_MMC.end()`, so the writes that follow really do fail at the
driver - the same failures a physical removal produces.

## Verified on hardware

**HW-verified (2026-07-19)** on an **ESP32-P4** (wired Ethernet, real SD card). The full cycle:

```
boot                storage: absent -> ready
                    /storage -> {"storage":"ready","mounts":1,"faults":0}

three good writes   /write   -> ok  ok  ok
                    /storage -> {"storage":"ready","mounts":1,"faults":0}   <- successes do not drift

/yank               unmounted

three failed writes /write   -> write failed  write failed  write failed
                    /storage -> {"storage":"faulted","mounts":1,"faults":1} <- faulted at the 3rd, not before

the gate            /write   -> storage not ready                          <- refused, not attempted

after the probe     /storage -> {"storage":"ready","mounts":2,"faults":1}   <- remounted by itself
                    /write   -> ok
```

Note the two numbers that prove the design rather than just the happy path: it faulted on the
**third** consecutive failure (not the first), and the fourth write was **refused** with
`storage not ready` instead of being attempted against a dead mount.

### What this run does not cover

`/yank` reproduces removal as the filesystem sees it, which is the case the safeties exist for. It
does not reproduce the _electrical_ transient of pulling a card mid-DMA-burst - that needs a hand on
the card. If you have the board in front of you, physically pull it during a `/write` loop: the
expected result is identical (a run of failures, then FAULTED), and reinserting should return it to
READY within `PC_HOTSWAP_PROBE_MS`.

## Tunables

| Flag                        | Default | Meaning                                              |
| --------------------------- | ------- | ---------------------------------------------------- |
| `PC_HOTSWAP_FAIL_THRESHOLD` | 3       | consecutive I/O errors that declare the medium gone  |
| `PC_HOTSWAP_PROBE_MS`       | 2000    | minimum gap between remount attempts while not READY |

## Build footprint

| Board    | Flash           |
| -------- | --------------- |
| ESP32-P4 | 735,242 B (56%) |

## Build-flag note

The flags must reach the library build, so pass them as build flags:

```sh
pio ci --board=esp32dev --project-option="framework=arduino" \
  --project-option="build_flags=-DPC_ENABLE_HOTSWAP=1" \
  --lib="." examples/L7-Application/HotSwapStorage/HotSwapStorage.ino
```
