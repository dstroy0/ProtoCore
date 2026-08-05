#!/usr/bin/env python3
"""Survey a DRAM dump taken over JTAG: what is populated, and where the dispatch tables are.

A function-pointer table shows up as a run of consecutive words that all land in an executable
window. The radio's PHY dispatch table is one of those, and its entries point into IRAM rather
than flash because the routines it reaches live in `.iram1`. That is how it is found on a board
whose application ELF is not available.

Take the dump with the board's built-in JTAG, which needs no flashing and no panic:

    openocd -s <scripts> -f board/esp32s3-builtin.cfg \\
        -c "init; halt; dump_image dram.bin 0x3FC88000 0x80000; resume; exit"

Attaching halts the cores, so resume (or `reset run`) afterwards or the board stays stopped.

Usage:  python tools/dev_env/dram_scan.py <dump> [base-address-hex]
"""
import struct
import sys
from collections import Counter

data = open(sys.argv[1], "rb").read()
BASE = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x3FC88000
words = struct.unpack(f"<{len(data) // 4}I", data[:len(data) // 4 * 4])

# ESP32-S3 address windows.
RANGES = {
    "iram (code)": (0x40370000, 0x403E0000),
    "flash text": (0x42000000, 0x42800000),
    "flash rodata": (0x3C000000, 0x3D000000),
    "dram": (0x3FC88000, 0x3FD00000),
    "rtc": (0x50000000, 0x50002000),
    "periph": (0x60000000, 0x60100000),
}


def where(v):
    for name, (lo, hi) in RANGES.items():
        if lo <= v < hi:
            return name
    return None


nonzero = sum(1 for w in words if w != 0)
print(f"{len(words)} words from 0x{BASE:08X}, {nonzero} non-zero ({100 * nonzero // len(words)}%)\n")

kinds = Counter()
for w in words:
    k = where(w)
    if k:
        kinds[k] += 1
print("words that look like a pointer:")
for k, n in kinds.most_common():
    print(f"  {k:16s} {n:7d}")

CODE = ("iram (code)", "flash text")
runs, start, cur = [], None, 0
for i, w in enumerate(words):
    if where(w) in CODE:
        if start is None:
            start = i
        cur += 1
    else:
        if start is not None and cur >= 6:
            runs.append((BASE + start * 4, cur))
        start, cur = None, 0
if start is not None and cur >= 6:
    runs.append((BASE + start * 4, cur))
runs.sort(key=lambda r: -r[1])

print(f"\n{len(runs)} runs of 6+ consecutive code pointers; longest 20:")
print(f"  {'address':12s} {'entries':>7s} {'iram':>6s} {'flash':>6s} {'distinct':>8s}  verdict")
for addr, n in runs[:20]:
    i0 = (addr - BASE) // 4
    seg = words[i0:i0 + n]
    ir = sum(1 for w in seg if where(w) == "iram (code)")
    fl = sum(1 for w in seg if where(w) == "flash text")
    uniq = len(set(seg))
    # One address repeated is a default-handler fill, not a dispatch table.
    if uniq <= 2:
        verdict = "filled with one handler"
    elif ir > fl:
        verdict = "IRAM table"
    elif fl > ir:
        verdict = "flash table"
    else:
        verdict = "mixed"
    print(f"  0x{addr:08X} {n:7d} {ir:6d} {fl:6d} {uniq:8d}  {verdict}")

print("\nIRAM-dominant runs with distinct entries, the shape a PHY dispatch table has:")
for addr, n in runs:
    i0 = (addr - BASE) // 4
    seg = words[i0:i0 + n]
    ir = sum(1 for w in seg if where(w) == "iram (code)")
    if ir > n // 2 and n >= 8 and len(set(seg)) > n // 2:
        print(f"  0x{addr:08X}  {n} entries, {ir} into IRAM, {len(set(seg))} distinct")
        for k, w in enumerate(seg[:10]):
            print(f"      +{k * 4:<4} 0x{w:08X}")
