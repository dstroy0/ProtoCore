#!/usr/bin/env python3
"""Name the PHY dispatch table slots by matching the probe's dump against the build's ELF.

The probe dumps the live table and nothing else, so the link is left exactly as the radio expects.
The names come from here: the ELF the same build produced carries every linked symbol and its
address, so each slot resolves by lookup rather than by inference.

Usage:  python parse_probe.py <serial-capture> <build.elf> [--nm <path>]
"""

import os
import re
import subprocess
import sys

SLOT = re.compile(r"^SLOT\s+(\d+)\s+\+(\d+)\s+0x([0-9A-Fa-f]+)")
NM = re.compile(r"^([0-9a-fA-F]+)\s+(\S)\s+(\S+)$")

# The ROM linker script maps a name to a fixed address for everything burned into the die. Most
# table slots point there, because libphy overrides only the few it has its own version of.
ROMSYM = re.compile(r"^\s*(?:PROVIDE\s*\(\s*)?([A-Za-z_]\w*)\s*=\s*(0x[0-9a-fA-F]+)\s*\)?\s*;")


def rom_symbols(chip):
    """{address: name} from every ROM linker script shipped for this die."""
    out = {}
    for base in ("~/.platformio/packages/framework-arduinoespressif32/tools/sdk/{}/ld",):
        d = os.path.expanduser(base.format(chip))
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            if not f.endswith(".ld"):
                continue
            for line in open(os.path.join(d, f), encoding="utf-8", errors="replace"):
                m = ROMSYM.match(line)
                if m:
                    out.setdefault(int(m.group(2), 16), m.group(1))
    return out


# ESP32-S3 windows, so a slot the ELF does not name still says where it points.
RANGES = [
    ("iram", 0x40370000, 0x403E0000),
    ("rom", 0x40000000, 0x40060000),
    ("flash text", 0x42000000, 0x42800000),
    ("dram", 0x3FC80000, 0x3FD00000),
]


def region(v):
    for name, lo, hi in RANGES:
        if lo <= v < hi:
            return name
    return "?"


def main():
    capture, elf = sys.argv[1], sys.argv[2]
    nm = sys.argv[sys.argv.index("--nm") + 1] if "--nm" in sys.argv else None
    if nm is None:
        nm = os.path.expanduser("~/.platformio/packages/toolchain-xtensa-esp32s3/bin/xtensa-esp32s3-elf-nm.exe")

    syms = {}
    for line in subprocess.run([nm, elf], capture_output=True, text=True, errors="replace").stdout.split("\n"):
        m = NM.match(line.strip())
        if m and m.group(2).lower() in "tw":
            syms.setdefault(int(m.group(1), 16), m.group(3))

    slots = []
    for line in open(capture, encoding="utf-8", errors="replace"):
        m = SLOT.match(line.strip())
        if m:
            slots.append((int(m.group(1)), int(m.group(2)), int(m.group(3), 16)))

    if not slots:
        print("no SLOT lines in the capture")
        raise SystemExit(1)

    chip = sys.argv[sys.argv.index("--chip") + 1] if "--chip" in sys.argv else "esp32s3"
    rom = rom_symbols(chip)

    def name_of(a):
        if a in syms:
            return syms[a], "elf"
        if a in rom:
            return rom[a], "rom"
        return "(" + region(a) + ")", ""

    live = [s for s in slots if s[2] not in (0, 0xFFFFFFFF)]
    from_elf = sum(1 for _, _, a in live if a in syms)
    from_rom = sum(1 for _, _, a in live if a not in syms and a in rom)
    print(f"{len(syms)} ELF symbols, {len(rom)} ROM symbols, {len(slots)} slots, " f"{len(live)} populated")
    print(
        f"named: {from_elf} overridden in the image, {from_rom} straight from ROM, "
        f"{len(live) - from_elf - from_rom} unresolved\n"
    )
    print(f"{'slot':>4} {'off':>5}  {'target':<12} {'src':<4} name")
    for i, off, a in slots:
        if a in (0, 0xFFFFFFFF):
            continue
        n, src = name_of(a)
        print(f"{i:4d} {off:5d}  0x{a:08X}  {src:<4} {n}")

    print("\nthe offsets the esp32 analog capture uses, on this die:")
    for off in (152, 160, 164, 168, 184, 188, 192, 208, 220):
        hit = [(i, a) for i, o, a in slots if o == off]
        if hit:
            i, a = hit[0]
            n, src = name_of(a)
            print(f"  +{off:<4} 0x{a:08X}  {src:<4} {n}")


if __name__ == "__main__":
    main()
