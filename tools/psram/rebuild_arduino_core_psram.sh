#!/usr/bin/env bash
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
#
# rebuild_arduino_core_psram.sh - rebuild the arduino-esp32 core for ESP32-S3 with
# CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y so a static EXT_RAM_BSS_ATTR array (the
# ProtoCore TLS arena under PC_TLS_ARENA_IN_PSRAM=1) is placed in
# PSRAM with ZERO heap.
#
# WHY THIS EXISTS
#   The stock precompiled arduino-esp32 core (verified 2.0.x and 3.3.x) ships
#   CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY OFF. With it off, EXT_RAM_BSS_ATTR expands
#   to nothing and the "PSRAM" array silently stays in internal DRAM (empirically: the
#   symbol lands at 0x3fxxxxxx, and there is no .ext_ram.bss output section). The linker
#   script even routes .ext_ram.bss *into* .dram0.bss. Only a core rebuilt with the flag
#   regenerates sections.ld to place .ext_ram.bss in the external-RAM segment (0x3Cxxxxxx)
#   and compiles the startup code that maps/zeroes it. Just -D-defining the macro against
#   the stock core CRASHES (the app expects a mapping the bootloader/linker never set up).
#
#   This uses Espressif's official esp32-arduino-lib-builder to produce a self-consistent
#   core (proper octal-PSRAM config for N16R8 boards + the BSS-external flag) and installs
#   it over the arduino-cli / Arduino IDE core. Runs on Linux / WSL / macOS (needs the
#   ESP-IDF build prerequisites: git, python3, cmake, ninja, wget, jq). ~30-60 min the first
#   time; it clones ESP-IDF and builds the libraries from source.
#
# USAGE
#   ./rebuild_arduino_core_psram.sh [--branch idf-release_v5.5] [--core-dir <path>]
#     --branch     esp32-arduino-lib-builder branch (default idf-release_v5.5 = IDF 5.5,
#                  matching arduino-esp32 3.3.x). Use release/v5.3 for arduino 3.1.x.
#     --core-dir   arduino-cli/IDE core dir to install into (auto-detected if omitted),
#                  e.g. ~/.arduino15/packages/esp32/tools/esp32s3-libs/<ver>
#     --no-install build only; print where the libs are so you can install manually.
#     --jobs N     cap build parallelism to N cores (default: half of nproc, min 2). Keep
#                  this well under your core count - the IDF build is memory-hungry and
#                  saturating every core can wedge or crash a workstation.
#
# After running, rebuild your sketch (a FULL clean) for an S3 board with PSRAM=OPI, and
# EXT_RAM_BSS_ATTR arrays land in PSRAM. Verify with: nm firmware.elf | grep <symbol>
# (address 0x3Cxxxxxx = PSRAM) and check the ".ext_ram.bss" section exists.
set -euo pipefail

BRANCH="idf-release_v5.5" # tag for IDF 5.5 (arduino-esp32 3.3.x); no release/v5.5 branch exists
CORE_DIR=""
INSTALL=1
JOBS=""
# Work dir needs ~6 GB free and must NOT be a small RAM-backed tmpfs (e.g. /tmp on a
# Raspberry Pi is tmpfs and overflows mid-clone with "No space left on device"). Default to a
# home-dir path on real storage; override with PC_PSRAM_WORK.
WORK="${PC_PSRAM_WORK:-$HOME/.cache/pc-arduino-psram}"

while [ $# -gt 0 ]; do
  case "$1" in
    --branch) BRANCH="$2"; shift 2 ;;
    --core-dir) CORE_DIR="$2"; shift 2 ;;
    --no-install) INSTALL=0; shift ;;
    --jobs) JOBS="$2"; shift 2 ;;
    -h|--help) sed -n '2,48p' "$0"; exit 0 ;;
    *) echo "unknown arg: $1" >&2; exit 2 ;;
  esac
done

# LOW default parallelism ON PURPOSE. The heavy C++ template libraries (esp-tflite-micro /
# TensorFlow, Matter) crash GCC with an INTERNAL COMPILER ERROR ("internal_error ...
# finalize_compilation_unit", the crash point moving between files run-to-run) when several
# big TUs compile at once - it looks like a random compile failure but it is memory/parallelism
# pressure (seen at 6 jobs even with 31 GB free; 2 jobs builds clean to 2965/2965). So cap at 2
# by default. Override with --jobs N if your box tolerates more; if you hit "internal compiler
# error", lower it. Do NOT export a global MAKEFLAGS=-j (it breaks recursive-make sub-builds).
if [ -z "$JOBS" ]; then JOBS=2; fi
export CMAKE_BUILD_PARALLEL_LEVEL="$JOBS"
echo "==> build parallelism capped at $JOBS core(s) (low on purpose: avoids GCC ICE on tflite/Matter)"

echo "==> esp32-arduino-lib-builder ref: $BRANCH"
mkdir -p "$WORK"; cd "$WORK"
if [ ! -d esp32-arduino-lib-builder ]; then
  git clone --depth 1 -b "$BRANCH" https://github.com/espressif/esp32-arduino-lib-builder.git
fi
cd esp32-arduino-lib-builder
# build.sh runs `git symbolic-ref HEAD`; a tag checkout leaves a detached HEAD ("fatal: ref
# HEAD is not a symbolic ref") which breaks it. Pin to a local branch so HEAD is symbolic.
git switch -c pc-psram-build 2>/dev/null || git checkout -B pc-psram-build 2>/dev/null || true

# (We do not try to EXCLUDE_COMPONENTS the ML libs - idf.py's EXCLUDE_COMPONENTS is ignored for
# managed components, so they build regardless. Low parallelism above is what makes them
# compile without the GCC ICE. The libs build fine at 2 jobs.)

# Enable the BSS-in-external-RAM Kconfig for every ESP32-S3 build variant. The octal-PSRAM
# mode (needed by N16R8 boards) already comes from the per-variant (qio_opi) config that
# lib-builder builds, so we only add the one capability flag here.
CFG="configs/defconfig.esp32s3"
touch "$CFG"
if ! grep -q "^CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y" "$CFG"; then
  echo "CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y" >> "$CFG"
  echo "==> added CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y to $CFG"
fi

echo "==> building arduino-esp32 libraries for esp32s3 (this is the long part)"
./build.sh -t esp32s3

LIBS_OUT="$(pwd)/out/tools/esp32-arduino-libs/esp32s3"
[ -d "$LIBS_OUT" ] || LIBS_OUT="$(pwd)/out/esp32s3"
echo "==> built libraries at: $LIBS_OUT"

if [ "$INSTALL" = "0" ]; then
  echo "==> --no-install: copy $LIBS_OUT/* over your core's esp32s3-libs dir yourself."
  exit 0
fi

# Auto-detect the arduino-cli / IDE S3 libs dir if not given.
if [ -z "$CORE_DIR" ]; then
  for base in "$HOME/.arduino15" "$HOME/Library/Arduino15" "$HOME/AppData/Local/Arduino15"; do
    cand="$(ls -d "$base"/packages/esp32/tools/esp32s3-libs/* 2>/dev/null | sort -V | tail -1 || true)"
    [ -n "$cand" ] && CORE_DIR="$cand" && break
  done
fi
[ -n "$CORE_DIR" ] || { echo "ERROR: could not find the arduino esp32s3-libs dir; pass --core-dir" >&2; exit 1; }

echo "==> installing into: $CORE_DIR"
BK="${CORE_DIR}.bak.$(date +%s)"
cp -a "$CORE_DIR" "$BK"; echo "==> backed up original to $BK"
cp -a "$LIBS_OUT/." "$CORE_DIR/"

# --- Compatibility post-processing ---------------------------------------------------------
# The idf-release_v5.5 lib-builder tracks a newer commit than the stock arduino-cli 3.3.x
# *hardware* core sources it is installed alongside, which introduces two mismatches that break
# an otherwise-clean sketch build. Both fixes are targeted and reshape the libs to match stock.
# (Verified on hardware: an EXT_RAM_BSS_ATTR arena links at 0x3c0xxxxx and is read/write at
# runtime, board boots clean.)
TOOLS_DIR="$(cd "$CORE_DIR/../.." && pwd)"                                   # .../packages/esp32/tools
TC_BIN="$(ls -d "$TOOLS_DIR"/esp-x32/*/bin 2>/dev/null | sort -V | tail -1 || true)"

# (1) The rebuilt libs ship include/newlib/platform_include/errno.h - an #include_next wrapper
#     that adds ESP-specific error codes. On arduino-cli's include ordering its #include_next
#     resolves to the neighbouring lwip errno.h instead of newlib's, so <errno.h> never defines
#     errno and FS/vfs_api.cpp fails to compile ("'errno' was not declared"). Stock libs do not
#     ship this file; remove it so <errno.h> resolves to the toolchain's newlib header.
ERRNO_SHIM="$CORE_DIR/include/newlib/platform_include/errno.h"
if [ -f "$ERRNO_SHIM" ]; then
  mv "$ERRNO_SHIM" "$ERRNO_SHIM.disabled"
  echo "==> disabled shadowing platform_include/errno.h (fixes 'errno was not declared' in FS/vfs_api.cpp)"
fi

# (2) The newer esp_diagnostics wraps esp_log_write/writev; the arduino core's own
#     esp32-hal-log-wrapper.c ALSO defines __wrap_esp_log_write/writev -> a multiple-definition
#     link error. The same diag object provides __wrap_log_printf, which the core
#     (chip-debug-report.cpp) needs, so it cannot simply be dropped. Localize only the two
#     conflicting symbols so the core's definitions win while __wrap_log_printf stays exported.
DIAG="$CORE_DIR/lib/libespressif__esp_diagnostics.a"
if [ -n "$TC_BIN" ] && [ -f "$DIAG" ]; then
  TMP="$(mktemp -d)"
  ( cd "$TMP" \
    && "$TC_BIN/xtensa-esp-elf-ar" x "$DIAG" esp_diagnostics_log_hook.c.obj \
    && "$TC_BIN/xtensa-esp-elf-objcopy" --localize-symbol=__wrap_esp_log_write \
         --localize-symbol=__wrap_esp_log_writev esp_diagnostics_log_hook.c.obj \
    && "$TC_BIN/xtensa-esp-elf-ar" r "$DIAG" esp_diagnostics_log_hook.c.obj )
  rm -rf "$TMP"
  echo "==> localized duplicate __wrap_esp_log_write/writev in esp_diagnostics (keeps __wrap_log_printf)"
fi
# -------------------------------------------------------------------------------------------

echo "==> done. Do a FULL clean rebuild of your sketch (S3 + PSRAM=OPI)."
echo "    EXT_RAM_BSS_ATTR arrays now land in PSRAM (0x3Cxxxxxx), zero heap."
