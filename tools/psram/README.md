<!-- Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com> -->
<!-- SPDX-License-Identifier: AGPL-3.0-or-later -->

# Putting the TLS arena in PSRAM (zero heap): a complete, hand-held guide

This guide walks you, step by step, through moving the ProtoCore
**TLS arena** (a big static buffer that mbedTLS works out of) from the ESP32's small
internal RAM into the much larger external **PSRAM**, **without using the heap at all**.

It assumes **zero prior knowledge**. If you already know what `.bss`, PSRAM, and `sdkconfig`
are, skip to [The two paths](#the-two-paths).

> **You usually do not need this.** One TLS connection fits in internal RAM. You only need
> PSRAM offload if you want **several TLS connections at once** (`MAX_TLS_CONNS > 1`) on a
> board that has PSRAM. If that is not you, stop here and enjoy your day.

## The 60-second background (plain English)

**Internal RAM vs PSRAM.** An ESP32-S3 has ~320 KB of fast internal RAM, but the part that
static buffers can live in (`dram0_0_seg`) is only about **122 KB** after the ROM takes its
cut. A single TLS connection needs ~41 KB of working memory; two or three at once blow past
122 KB. Many S3 boards (like the **N16R8**: 16 MB flash, **8 MB PSRAM**) add a big external
RAM chip called **PSRAM**. If we can put the TLS buffer there, internal RAM stays free.

**`.bss` and "static" memory.** A global array like `static uint8_t arena[48000];` is not
allocated at runtime. The linker reserves space for it up front, in a section called `.bss`.
That is the opposite of `malloc`/`ps_malloc` (the "heap"). This library is **deterministic and
zero-heap**, so the arena must be a static `.bss` array, never a heap allocation.

**How you ask for a static array in PSRAM.** Espressif provides a macro, `EXT_RAM_BSS_ATTR`.
You write:

```c
EXT_RAM_BSS_ATTR static uint8_t arena[48000];   // "put this in PSRAM, please"
```

and, _if the framework was built to allow it_, the linker puts `arena` in a special section
`.ext_ram.bss` that lives in PSRAM. Zero heap. The library already does exactly this when you
set `-DPC_TLS_ARENA_IN_PSRAM=1`.

**The catch (this is the whole reason this guide exists).** `EXT_RAM_BSS_ATTR` only works if
the framework was compiled with one setting turned on:

```ini
CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y
```

**The stock Arduino-ESP32 core ships this setting OFF.** (Verified on both the PlatformIO
`espressif32` core, arduino-esp32 2.0.x, and the Arduino IDE / `arduino-cli` core 3.3.x /
IDF 5.5.) With it off, `EXT_RAM_BSS_ATTR` quietly expands to **nothing**, and your "PSRAM"
array silently ends up in internal RAM anyway. There is no warning. You have to **rebuild the
core** with that setting on.

### Proof, so you believe it

A 64 KB `EXT_RAM_BSS_ATTR` test array, same sketch, two cores:

| Core                               | Array address | `.ext_ram.bss` section? | Where it really is    |
| ---------------------------------- | ------------- | ----------------------- | --------------------- |
| **Stock** arduino-esp32 (flag off) | `0x3fc994d8`  | **absent**              | internal DRAM (no-op) |
| **Rebuilt** with the flag on       | `0x3c0e0000`  | present, in PSRAM       | **PSRAM**, zero heap  |

On the S3, addresses starting `0x3F...` are internal RAM; `0x3C...` is PSRAM. Same source
code, same `EXT_RAM_BSS_ATTR`. The _only_ difference is the framework build. That is why we
rebuild.

> WARNING: do **not** try to force it by only adding
> `-DCONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` to your build flags against the stock core.
> The macro will expand and the linker will place the array at PSRAM addresses, **but the stock
> bootloader/linker never set up that region**, so the chip **crashes on boot** (a watchdog
> reset loop). The setting has to be baked into the whole framework build, which is what the
> two paths below do.

## The two paths

You get the framework rebuilt in one of two ways, depending on your IDE:

- **Path A (PlatformIO)**: one line in `platformio.ini`. Recommended if you use PlatformIO.
- **Path B (Arduino IDE / arduino-cli)**: rebuild the core with Espressif's
  `esp32-arduino-lib-builder`. More work, about 30 to 60 minutes.

Both paths need to know your **PSRAM type**. The N16R8 has **octal (OPI)** PSRAM. This
matters (see [Gotcha 1](#gotcha-1-boot-loop-octal-vs-quad-psram)). If you have a different
board, check its docs (WROVER modules are usually **quad**).

## Path A: PlatformIO (pioarduino)

The official PlatformIO `espressif32` platform stopped at arduino-esp32 2.0.x, so use the
community **pioarduino** platform (arduino-esp32 3.x). Its `custom_sdkconfig` option rebuilds
the framework with your extra settings baked in, exactly what we need.

> **IMPORTANT: Path A does not work for _octal_-PSRAM boards (the N16R8 / anything OPI).**
> When you use `custom_sdkconfig`, pioarduino builds Arduino as an ESP-IDF component using the
> **default `esp32` variant** ("doesn't handle the `variant` field" warning). Plain ESP32 has
> no octal PSRAM, so `confgen` **silently overrides** `CONFIG_SPIRAM_MODE_OCT=y` back to `QUAD`
> and drops `CONFIG_SPIRAM_BOOT_INIT`, no matter what you put in `custom_sdkconfig`. The result
> boots-loops on an OPI board (PSRAM never initializes; only `ALLOW_BSS` survives). Path A is
> fine for **quad**-PSRAM modules (most WROVERs); for **octal** boards use **Path B**, whose
> `qio_opi` variant sets octal + boot-init correctly.

Full `platformio.ini` for a **quad**-PSRAM board:

```ini
[env:s3psram]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/53.03.13/platform-espressif32.zip
board = esp32-s3-devkitc-1
framework = arduino
board_build.arduino.memory_type = qio_opi   ; QIO flash + OPI (octal) PSRAM
board_build.psram_type = opi
board_upload.flash_size = 16MB
board_build.flash_size = 16MB
board_build.partitions = default_16MB.csv
custom_sdkconfig =
    CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y   ; the whole point
    CONFIG_SPIRAM_MODE_OCT=y                        ; N16R8 = OCTAL PSRAM (omit/QUAD for WROVER)
    CONFIG_SPIRAM_SPEED_80M=y
build_flags =
    -DBOARD_HAS_PSRAM
    -DPC_ENABLE_TLS=1
    -DPC_TLS_ARENA_IN_PSRAM=1
    -DMAX_TLS_CONNS=3
lib_deps = <your path or registry name for ProtoCore>
```

Then, **from a clean state** (important, stale build caches cause confusing failures):

```bash
pio run -t clean
pio run            # first build is slow: it recompiles the framework with your settings
pio run -t upload
```

Notes and gotchas specific to Path A:

- The first `pio run` recompiles the ESP-IDF libraries (several minutes). Later builds are
  fast.
- **Windows only:** the deep default project path can exceed Windows' 260-char limit and the
  build fails with `filename or extension is too long`. Put the project at a short path such
  as `C:\t\myproj\`.
- If the build pulls in Espressif RainMaker/Insights components and dies on a
  `https_server.crt.S not found` error, add:

    ```ini
    custom_component_remove =
        espressif/esp_insights
        espressif/esp_diagnostics
        espressif/esp_diag_data_store
        espressif/esp_rainmaker
        espressif/rmaker_common
    ```

- **Watch the PSRAM mode.** pioarduino's IDF-component build does not always inherit the
  board's octal-PSRAM setting, and will happily build **quad** mode for an **octal** board,
  which **boot-loops**. Always pass `CONFIG_SPIRAM_MODE_OCT=y` for N16R8-class boards, and
  confirm it stuck (see [Verify](#verify-it-actually-worked)).

Skip to [Verify](#verify-it-actually-worked).

## Path B: Arduino IDE / arduino-cli (rebuild the core)

> ### Newbies: install a prebuilt core, do NOT build anything
>
> Rebuilding the core is a **one-time maintainer job**. If someone has already built a
> flag-enabled core for your board (or you did it once on another machine), everyone else just
> **installs the prebuilt files** - no WSL, no Docker, no lib-builder, no command line:
>
> 1. **Close** the Arduino IDE completely.
> 2. Find your ESP32-S3 libraries folder:
>     - Windows: `C:\Users\<you>\AppData\Local\Arduino15\packages\esp32\tools\esp32s3-libs\<version>\`
>     - macOS: `~/Library/Arduino15/packages/esp32/tools/esp32s3-libs/<version>/`
>     - Linux: `~/.arduino15/packages/esp32/tools/esp32s3-libs/<version>/`
> 3. **Back it up:** rename that `<version>` folder to `<version>.bak` (so you can undo).
> 4. **Unzip** the provided prebuilt archive so its contents replace the `<version>` folder.
> 5. **Reopen** the Arduino IDE, pick **Tools -> Board -> ESP32S3 Dev Module**, set **Tools ->
>    PSRAM -> OPI PSRAM**, then **Sketch -> Verify** once (a full clean).
> 6. Confirm it worked (a `0x3C...` address) as in [Verify](#verify-it-actually-worked). To
>    undo, delete the folder and rename `<version>.bak` back.
>
> That is the whole newbie experience: back up, unzip, done. Everything below (installing WSL,
> running lib-builder) is only for the **maintainer** who produces that archive once and shares
> it. An even smoother option a maintainer can offer is a Boards Manager "Additional Boards
> Manager URL" pointing at a prebuilt package, so installing is a checkbox in the IDE.

The Arduino core ships precompiled libraries, so you cannot flip an `sdkconfig` option from a
sketch. You rebuild the libraries with Espressif's official tool,
[esp32-arduino-lib-builder](https://github.com/espressif/esp32-arduino-lib-builder), then drop
the result into your installed core. The helper script
[`rebuild_arduino_core_psram.sh`](rebuild_arduino_core_psram.sh) automates the whole thing;
this section explains every step it does, so you can do it by hand or debug it.

### B.0 Match the versions (do this first)

Your rebuilt libraries **must** match your installed core, or you get link errors. Find your
core's IDF version:

```bash
# Arduino IDE / arduino-cli, adjust the version folder to yours:
grep CONFIG_IDF_INIT_VERSION \
  ~/.arduino15/packages/esp32/tools/esp32s3-libs/*/sdkconfig
# e.g. CONFIG_IDF_INIT_VERSION="5.5.4"  -> lib-builder ref idf-release_v5.5 (a tag)
```

| Your core's IDF | arduino-esp32 | lib-builder branch       |
| --------------- | ------------- | ------------------------ |
| 5.5.x           | 3.3.x         | `idf-release_v5.5` (tag) |
| 5.3.x           | 3.1.x         | `release/v5.3`           |
| 5.1.x           | 3.0.x         | `release/v5.1`           |

### B.1 Prerequisites (pick ONE environment)

`esp32-arduino-lib-builder` runs on **Linux, macOS, or WSL** (not native Windows). Two ways:

**Option 1, Docker (recommended, most reproducible - this is the recipe that actually worked).**
Espressif's image already has every prerequisite and, crucially, its `/opt/esp/lib-builder`
has the components/submodules pre-populated - so it skips the un-populated-TinyUSB and stalled
WiFi-blob-clone failures that fresh native clones hit. Use the image's built-in lib-builder
(do not bind-mount your own clone) and **cap parallelism low** (see the GCC-ICE note below):

```bash
mkdir -p out
docker run --rm -e CMAKE_BUILD_PARALLEL_LEVEL=2 -v "$PWD/out":/out \
  espressif/esp32-arduino-lib-builder:release-v5.5 bash -c '
    cd /opt/esp/lib-builder &&
    echo CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y >> configs/defconfig.esp32s3 &&
    ./build.sh -t esp32s3 &&
    cp -a out/tools/esp32-arduino-libs/esp32s3 /out/'
# result: ./out/esp32s3/  (copy its contents over your core's esp32s3-libs/<ver>/)
```

> **GCC internal compiler error = lower `CMAKE_BUILD_PARALLEL_LEVEL`.** The heavy C++ template
> libraries (esp-tflite-micro/TensorFlow, Matter) crash GCC with an `internal compiler error`
> under parallel load - the crash point moves between files run-to-run, so it looks random, but
> it is memory/parallelism pressure (seen at 6 jobs even with 31 GB free; **2 jobs builds clean
> to 2965/2965**). It is not a code bug and `EXCLUDE_COMPONENTS` does not skip these managed
> components. If you see it, drop to `=2`.

**Option 2, native Linux / WSL.** More setup and more failure modes (see above), but no Docker
needed. On Windows, install **WSL** first (`wsl --install -d Ubuntu` in an admin PowerShell,
then reboot). Inside Ubuntu/Linux install the build prerequisites (note `jq`, easy to miss),
and clone with a **branch you can build from** (a bare tag checkout leaves a detached HEAD that
`build.sh` chokes on - the helper script pins a local branch for you):

```bash
sudo apt-get update
sudo apt-get install -y git wget curl python3 python3-pip python3-venv \
                        cmake ninja-build ccache jq libffi-dev libssl-dev dfu-util
```

### B.2 Clone the builder and turn the flag on

```bash
git clone --depth 1 -b idf-release_v5.5 \
  https://github.com/espressif/esp32-arduino-lib-builder.git
cd esp32-arduino-lib-builder

# Enable the one capability we need, for every ESP32-S3 variant:
echo "CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y" >> configs/defconfig.esp32s3
```

You do **not** need to add the octal-PSRAM mode here. lib-builder already builds a proper
per-board `qio_opi` (octal) variant. (This is the difference from Path A: the official builder
gets the board config right, so no boot loop.)

### B.3 Build (the slow part)

```bash
# Docker:
docker run --rm -v "$PWD":/arduino-esp32 -e "TARGET=esp32s3" \
  espressif/esp32-arduino-lib-builder ./build.sh -t esp32s3

# Native / WSL:
./build.sh -t esp32s3
```

This clones ESP-IDF (~2 GB), installs the toolchains, and compiles the libraries. Expect **30
to 60 minutes** the first time, faster after. It finishes with the libraries under
`out/tools/esp32-arduino-libs/esp32s3/`.

> **Do not let it use every core.** The IDF build is memory-hungry; saturating all cores can
> wedge or crash a workstation. Cap it. The helper script does this automatically (half your
> cores by default, `--jobs N` to override). By hand, prefix the build:
> `CMAKE_BUILD_PARALLEL_LEVEL=6 MAKEFLAGS=-j6 ./build.sh -t esp32s3`.

### B.4 Install into your core (with a backup)

```bash
CORE=~/.arduino15/packages/esp32/tools/esp32s3-libs/3.3.10   # your version
cp -a "$CORE" "$CORE.bak"                                    # ALWAYS back up first
cp -a out/tools/esp32-arduino-libs/esp32s3/. "$CORE/"
```

To undo later: delete `$CORE` and rename `$CORE.bak` back.

> On **Windows**, your core lives at
> `C:\Users\<you>\AppData\Local\Arduino15\packages\esp32\tools\esp32s3-libs\<ver>`. If you
> built in WSL, install straight into it with
> `--core-dir /mnt/c/Users/<you>/AppData/Local/Arduino15/packages/esp32/tools/esp32s3-libs/<ver>`.

### B.5 Rebuild your sketch, fully clean

In Arduino IDE, select an **S3 board with PSRAM = OPI PSRAM**, set `Erase All Flash Before
Sketch Upload: Enabled` for the first flash, and do **Sketch, Verify** after closing and
reopening so no stale objects remain. `EXT_RAM_BSS_ATTR` arrays now land in PSRAM.

## The helper script

[`rebuild_arduino_core_psram.sh`](rebuild_arduino_core_psram.sh) does Path B steps B.2 to B.4
for you (clone, add the flag, build with capped cores, back up, install):

```bash
tools/psram/rebuild_arduino_core_psram.sh                 # auto-detect core, ref idf-release_v5.5
tools/psram/rebuild_arduino_core_psram.sh --branch release/v5.3
tools/psram/rebuild_arduino_core_psram.sh --jobs 4        # cap to 4 cores
tools/psram/rebuild_arduino_core_psram.sh --no-install    # just build; install yourself
tools/psram/rebuild_arduino_core_psram.sh \
  --core-dir /mnt/c/Users/you/AppData/Local/Arduino15/packages/esp32/tools/esp32s3-libs/3.3.10
```

Run it inside Linux/WSL/macOS. It backs up your core before touching it and caps build
parallelism (half your cores by default) so it does not hog the machine.

After installing the libs it applies two small compatibility fixes automatically, because the
`idf-release_v5.5` builder tracks a newer commit than the stock arduino-cli 3.3.x **hardware**
core it sits next to, and the two diverge in a way that breaks a sketch build:

1. It removes the rebuilt `include/newlib/platform_include/errno.h` shim. That file is an
   `#include_next` wrapper, but on arduino-cli's include order the `#include_next` lands on the
   neighboring lwip `errno.h` instead of newlib's, so `<errno.h>` stops defining `errno` and
   `FS/vfs_api.cpp` fails with `'errno' was not declared`. Stock libs do not ship it.
2. It localizes `__wrap_esp_log_write` / `__wrap_esp_log_writev` inside
   `libespressif__esp_diagnostics.a`. The newer `esp_diagnostics` wraps those log symbols, which
   collide with the arduino core's own `esp32-hal-log-wrapper.c` (a multiple-definition link
   error); the same object also exports `__wrap_log_printf`, which the core needs, so localizing
   just the two duplicates keeps the good symbol while letting the core's win.

Both are verified end to end on an ESP32-S3: a 48 KB `EXT_RAM_BSS_ATTR` arena links into
`.ext_ram.bss` at `0x3c050000` (zero internal DRAM) and is read/write at runtime (the board
boots clean and sums the whole arena correctly). If you rebuild the libs by hand (Path B),
apply these two fixes yourself.

## Verify it actually worked

Do not trust it, check. Two ways.

**Static check (the ELF, no flashing needed).** Find your build's `.elf`, then:

```bash
# find the toolchain nm; then:
xtensa-esp-elf-nm firmware.elf | grep -i s_arena
#   3c0e0000 b _ZL7s_arena     <- 0x3C... = PSRAM   (good)
#   3fc994d8 b _ZL7s_arena     <- 0x3F... = internal DRAM  (flag not active / no clean rebuild)

xtensa-esp-elf-readelf -S firmware.elf | grep ext_ram
#   [18] .ext_ram.bss  NOBITS  3c0e0000 ...   <- a real section at a PSRAM address (good)
```

**Runtime check.** In `setup()`:

```c
#include <esp_heap_caps.h>
Serial.printf("arena external_ram=%d, internal free=%u\n",
              (int)esp_ptr_external_ram((void*)your_arena),
              (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
```

`external_ram=1` means it is in PSRAM. You should also see internal free RAM jump up by
roughly the arena size versus the DRAM build.

## Gotchas and troubleshooting

### Gotcha 1: boot loop, octal vs quad PSRAM

Symptom: the board resets over and over, serial shows `rst:0x7 (TG0WDT_SYS_RST)` and nothing
else. Cause: PSRAM was configured **quad** but your chip is **octal** (or vice versa), so PSRAM
never initializes and zeroing the `.ext_ram.bss` region faults. Fix: match the mode to the
chip. `CONFIG_SPIRAM_MODE_OCT=y` for N16R8-class (OPI) boards; quad for most WROVER modules. On
Path A confirm the mode actually stuck (the IDF-component build can silently revert it).

### Gotcha 2: array still ends up in internal RAM

The flag did not take, or you did not do a **full clean** rebuild. Re-check the ELF (section
above). On Path A, confirm `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y` shows up in the
generated `.../config/sdkconfig.h`. On Path B, confirm you rebuilt the libraries and copied
them into the right core version.

### Gotcha 3: crash only when writing flash / NVS

Espressif's own caveat: _"When flash cache is disabled (for example, if the flash is being
written to), the external RAM also becomes inaccessible."_ If your code touches the PSRAM arena
on the same core **while** a flash write (NVS commit, OTA) is in progress, you get an
illegal-cache-access crash. In practice TLS traffic and flash writes are not concurrent, but if
you do heavy NVS/OTA work, keep it off the core that runs TLS.

### Gotcha 4: the `-D` shortcut crashes

Adding only `-DCONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY` to build flags against the
**stock** core makes the app _think_ the region is mapped when it is not, which boot-crashes.
There is no shortcut around rebuilding the framework.

## Alternative: the `extram_bss` linker fragment

Instead of tagging one variable with `EXT_RAM_BSS_ATTR`, ESP-IDF can move an entire
component's or library's `.bss` to PSRAM with the linker fragment scheme `extram_bss` (a
`linker.lf` with `[mapping]` mapping `* (extram_bss)`). It needs the **same**
`CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`. We use the per-variable attribute instead so
that _only_ the 48 KB TLS arena moves to PSRAM and the rest of the TLS state stays in fast
internal RAM. Mentioned here for completeness.

## Summary

- Stock Arduino core: `EXT_RAM_BSS_ATTR` is a **no-op**; the arena stays in DRAM.
- To get the zero-heap arena into PSRAM you must **rebuild the framework** with
  `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`, via Path A (PlatformIO one-liner) or Path B
  (`esp32-arduino-lib-builder`).
- Match the **PSRAM mode** to your chip (octal for N16R8) or it boot-loops.
- Always **verify** with `nm`/`readelf` (address `0x3C...`) before trusting it.
