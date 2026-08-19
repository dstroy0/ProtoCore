# Performance benchmarks

Benchmarks are organized to **mirror `src/`** - a feature's bench lives at the same layer path as its
implementation, so there are no bare files and every directory has the same shape. Every bench is
C11: the on-device ones build with the ESP-IDF toolchain, the host ones with `gcc`.

```
test/performance_benching/
  common/                 device_bench.h + host_bench.h (on the include path)
  services/<name>/        one dir per src/services/<name>
  network_drivers/<layer>/<name>/   mirrors src/network_drivers/<layer> (presentation, transport, ...)
  server/<name>/          mirrors src/server
  core/<name>/            shared + foundational bits (crc, numparse, ...)
```

## The two bench kinds (uniform in every feature dir)

- **On-device CCOUNT bench** - `<feature>/main/main.c`. An ESP-IDF application that times the
  feature's pure hot path on a real ESP32-S3 with the Xtensa cycle counter (`CCOUNT`, via
  `protocore_cycles()`), printing `DB ...` lines over USB-Serial/JTAG. This is the real device cost in
  cycles / ns / MB/s at 240 MHz.
- **Host bench** - `<feature>/host.c`. A standalone `gcc` program giving a fast relative ns/op +
  MB/s baseline on a desktop/RPi core (not the device cost). Its build command is in its header
  comment. Present only where a host bench exists; the device bench is the common denominator.

Every `src/services/*` feature has a device bench; the network_drivers / server / core features have
them too.

## device_bench.h

`common/device_bench.h` provides the measurement macros `DBENCH_OP(label, N, expr)` and
`DBENCH_BULK(label, N, bytes, expr)` (built on `protocore_cycles()` / `protocore_cycles_to_ns()`), the
`DBENCH_BANNER` / `DBENCH_DONE` frame lines, and `DBENCH_MAIN(label)`, which supplies `app_main()`
and the pinned task. A bench therefore holds only what is specific to it:

```c
#include "device_bench.h"
#include "services/fieldbus/modbus/modbus/modbus.h"

void dbench_run(void)
{
    // fixtures stay local to the run
    for (;;)
    {
        DBENCH_BANNER("modbus");
        DBENCH_OP("protocore_modbus_process_adu read x8 (FC3)", 20000, ...);
        DBENCH_DONE();
    }
}

DBENCH_MAIN("modbus")
```

The clock the cycle counts are divided by is `DBENCH_CPU_MHZ` (240 by default); override it with
`-DDBENCH_CPU_MHZ=<n>` to bench at another. `common/host_bench.h` is the host counterpart:
`HBENCH_NS(iters, expr, out_ns)` against `CLOCK_MONOTONIC`, plus `hbench_header()` / `hbench_row()`.

## Build, flash, capture

```sh
idf.py -C test/performance_benching/services/modbus set-target esp32s3
idf.py -C test/performance_benching/services/modbus build          # compile-check (no hardware)
idf.py -C test/performance_benching/services/modbus -p COM7 flash monitor
```

```
DB ==== modbus device microbench start (CCOUNT @ 240 MHz) ====
DB protocore_modbus_process_adu read x8 (FC3)  cyc=1830        us=7.63      ns=7625
DB ==== DONE ====
```

A feature's `PROTOCORE_ENABLE_*` flags go in its own `CMakeLists.txt` as `add_compile_definitions()` before
`project()`, so they reach the separately-compiled library and not just the bench translation unit.

## Worked example templates

- `services/modbus/` - a pure protocol codec (every benched call is the real production path).
- `services/ads1115/` - a peripheral driver (only the CPU-side codec is timed; the I2C transfer is
  out of scope, as with every driver - this rig has no peripherals wired up).

Copy whichever fits, keep `#include "device_bench.h"`, and add a `host.c` if a host baseline helps.
