#!/usr/bin/env bash
# Wrap a cross-toolchain's gcc/g++ with ccache so ABSOLUTE-path compiler invocations route through it.
#
# PlatformIO (pio ci) and arduino-cli invoke the ESP32 xtensa/riscv compiler by absolute path, so a
# PATH masquerade (as used for the native host gcc) is bypassed. The reliable fix is to replace the
# toolchain binary with a tiny shim that execs `ccache <real> "$@"`. Every example then reuses the
# cached Arduino-core object compiles (the bulk of each build) across examples and, with ~/.ccache
# restored, across CI runs. Idempotent (re-running skips already-wrapped binaries) and a no-op when
# ccache is absent or the toolchain is not installed yet (cold cache; the next run wraps it).
#
#   ci_tooling/build/ccache_wrap.sh <search-root> <compiler-basename>...
#   ci_tooling/build/ccache_wrap.sh ~/.platformio/packages xtensa-esp32-elf-gcc xtensa-esp32-elf-g++
set -uo pipefail

command -v ccache >/dev/null 2>&1 || {
    echo "ccache_wrap: ccache not installed - skipping"
    exit 0
}
ROOT="${1:?usage: ccache_wrap.sh <search-root> <compiler-basename>...}"
shift
[ -d "$ROOT" ] || {
    echo "ccache_wrap: $ROOT not present yet (cold cache) - skipping"
    exit 0
}

wrapped=0
for name in "$@"; do
    # -type f matches regular files only, so the toolchain's compat symlinks (e.g. the
    # legacy xtensa-esp32-elf-* aliases pointing at xtensa-esp-elf-*) are left untouched -
    # wrapping one would corrupt the name the platform actually invokes.
    while IFS= read -r real; do
        [ -n "$real" ] || continue
        case "$real" in *.real) continue ;; esac # already the saved original
        [ -e "$real.real" ] && continue           # already wrapped
        # Copy-then-replace (never mv): the saved original exists before we touch the live
        # binary, and the shim is staged and atomically moved into place. A failure at any
        # step restores the original, so this can never leave a shim whose .real is missing
        # (which would break every compile that follows).
        cp -p "$real" "$real.real" || {
            echo "ccache_wrap: cp failed for $real - skipping"
            continue
        }
        tmp="$real.shim.$$"
        if printf '#!/bin/sh\nexec ccache "%s" "$@"\n' "$real.real" >"$tmp" && chmod +x "$tmp" && mv -f "$tmp" "$real"; then
            echo "ccache_wrap: wrapped $real"
            wrapped=$((wrapped + 1))
        else
            echo "ccache_wrap: shim install failed for $real - restoring original"
            rm -f "$tmp" "$real.real"
        fi
    done < <(find "$ROOT" -type f -name "$name" 2>/dev/null)
done
echo "ccache_wrap: $wrapped binaries newly wrapped"
