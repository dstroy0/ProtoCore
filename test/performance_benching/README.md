# Performance benchmarks

Benchmarks are organized to **mirror `src/`** - a feature's bench lives at the same layer path as its
implementation, so there are no bare files and every directory has the same shape.

```
performance_benching/
  library_comparison/     A/B vs ESP32Async/ESPAsyncWebServer (protocore + eaws + measure.sh)
  common/                 device_bench.h + the shared common.ini (on the include path)
  common.ini              shared board/core/flags, extended by every device sketch
  services/<name>/        one dir per src/services/<name>
  network_drivers/<layer>/<name>/   mirrors src/network_drivers/<layer> (presentation, transport, ...)
  server/<name>/          mirrors src/server
  core/<name>/            shared_primitives + foundational bits (crc, numparse, ...)
```

## The two bench kinds (uniform in every feature dir)

- **On-device CCOUNT bench** - `<feature>/platformio.ini` + `<feature>/src/main.cpp`. A PlatformIO
  sketch that times the feature's pure hot path on a real ESP32-S3 with the Xtensa cycle counter
  (`CCOUNT`, via `pc_cycles()`), printing `DB ...` lines over USB-CDC. This is the real device cost
  in cycles / ns / MB/s at 240 MHz.
- **Host bench** - `<feature>/host.cpp`. A standalone `g++` program giving a fast relative ns/op +
  MB/s baseline on a desktop/RPi core (not the device cost). Its build command is in its header
  comment. Present only where a host bench exists; the device sketch is the common denominator.

Every `src/services/*` feature has a device sketch; the network_drivers / server / core features have
them too. Where a feature previously had a bare `performance_benching/bench_<name>.cpp`, it is now that feature's
`host.cpp`.

## device_bench.h and the include path

`performance_benching/common/device_bench.h` provides `DBENCH_OP(label, N, expr)` and
`DBENCH_BULK(label, N, bytes, expr)` (built on `pc_cycles()` / `pc_cycles_to_ns()`). Each sketch's
`platformio.ini` puts `performance_benching/common` on the include path (`-I<rel>/common`), so every `src/main.cpp`
uses a uniform `#include "device_bench.h"` regardless of how deep it sits. The relative depth of
`lib_deps = symlink://...`, `extra_configs`, and `-I` is the only thing that differs between a
depth-3 dir (services/server/core) and a depth-4 dir (network_drivers/<layer>) - the sketch source is
identical in shape.

## Build, flash, capture

The `esp32-s3-devkitc-1` target + `espressif32@6.13.0` toolchain match the rest of the repo.

```sh
pio run -d performance_benching/services/modbus                     # compile-check (no hardware)
pio run -d performance_benching/services/modbus -t upload --upload-port COM7
pio device monitor -p COM7 -b 115200                # watch the DB lines
```

```
DB ==== modbus device microbench start (CCOUNT @ 240 MHz) ====
DB pc_modbus_process_adu read x8 (FC3)  cyc=1830        us=7.63      ns=7625
DB ==== DONE ====
```

## Worked example templates

- `services/fieldbus/modbus/` - a pure protocol codec (every benched call is the real production path).
- `services/peripherals/ads1115/` - a peripheral driver (only the CPU-side codec is timed; the I2C transfer is
  out of scope, as with every driver - this rig has no peripherals wired up).

Copy whichever fits, keep `#include "device_bench.h"`, and add a `host.cpp` if a host baseline helps.

## library_comparison

A reproducible head-to-head against `ESP32Async/ESPAsyncWebServer`. `protocore/` builds **this repo's live
library** via `lib_deps = symlink://../../../` (never a stale copy), `eaws/` pins the ESPAsyncWebServer
release; the two sketches are identical except for the library. See `library_comparison/README.md`.
