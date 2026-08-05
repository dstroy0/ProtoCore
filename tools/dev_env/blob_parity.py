#!/usr/bin/env python3
"""Cross-reference the Espressif radio blobs across every ESP variant the SDK ships.

A driver that replaces the vendor radio has to work on more than one die, so the first question is
which entry points every variant has in common and which are one part's alone. This reads each
variant's blobs with that variant's own objdump - the C-series parts are RISC-V, not xtensa, so one
toolchain does not cover them - and reports the parity.

Symbol tables only: this reads names, not code.

Usage:  python tools/dev_env/blob_parity.py <repo-root>
"""
import os
import re
import subprocess
import sys

PK = os.path.expanduser("~/.platformio/packages")
SDK = os.path.join(PK, "framework-arduinoespressif32", "tools", "sdk")

# Each die needs its own objdump: the S-series are xtensa with their own configuration, and the
# C-series are RISC-V, so a single cross-objdump does not read all of them.
CHIPS = [
    ("esp32", os.path.join(PK, "toolchain-xtensa-esp32", "bin", "xtensa-esp32-elf-objdump.exe"), "xtensa"),
    ("esp32s2", os.path.join(PK, "toolchain-xtensa-esp32s2", "bin", "xtensa-esp32s2-elf-objdump.exe"), "xtensa"),
    ("esp32s3", os.path.join(PK, "toolchain-xtensa-esp32s3", "bin", "xtensa-esp32s3-elf-objdump.exe"), "xtensa"),
    ("esp32c3", os.path.join(PK, "toolchain-riscv32-esp", "bin", "riscv32-esp-elf-objdump.exe"), "riscv"),
]

BLOBS = ["libphy.a", "librtc.a", "libpp.a", "libnet80211.a", "libcoexist.a"]

# The entry points a replacement radio driver has to stand in for. Parity on these decides whether
# one bring-up path covers every die or each needs its own.
WATCH = [
    "ram_chip_i2c_writeReg", "ram_chip_i2c_readReg", "g_phyFuns", "phy_get_romfuncs",
    "phy_i2c_init", "i2c_rfpll_init", "i2c_bbtop_init", "i2c_bias_init", "i2c_xtal_init",
    "i2cmst_reg_init", "ram_rfpll_set_freq", "ram_set_pbus_mem", "bb_init", "agc_reg_init",
    "bb_reg_init", "RFChannelSel", "phy_enter_critical", "phy_dis_hw_set_freq",
    "register_chipv7_phy", "phy_close_rf", "chip_v7_set_chan",
]

SYM = re.compile(r"^([0-9a-f]{8})\s+(.{7})\s+(\S+)\s+([0-9a-f]{8})\s+(\S+)\s*$")


def find_blob(chip, lib):
    for sub in ("lib", "ld"):
        p = os.path.join(SDK, chip, sub, lib)
        if os.path.exists(p):
            return p
    return None


def scan(objdump, path):
    """(defined functions, defined objects, undefined names) in one archive."""
    r = subprocess.run([objdump, "-t", path], capture_output=True, text=True, errors="replace")
    funcs, objs, undef = set(), set(), set()
    for line in r.stdout.split("\n"):
        if "*UND*" in line:
            n = line.split()[-1] if line.split() else ""
            if n and not n.startswith("*"):
                undef.add(n)
            continue
        m = SYM.match(line.rstrip())
        if not m:
            continue
        flags, name = m.group(2), m.group(5)
        if "g" not in flags:
            continue
        if "F" in flags:
            funcs.add(name)
        elif "O" in flags:
            objs.add(name)
    return funcs, objs, undef


def main():
    repo = os.path.abspath(sys.argv[1])
    per_chip = {}      # chip -> set of every defined symbol
    per_undef = {}     # chip -> set of names the blobs import
    per_chip_lib = {}  # (chip, lib) -> (funcs, objs, undef, size) or None
    present = []

    for chip, objdump, arch in CHIPS:
        if not os.path.exists(objdump):
            print(f"  {chip}: no objdump, skipped")
            continue
        allsyms, allundef = set(), set()
        got = False
        for lib in BLOBS:
            p = find_blob(chip, lib)
            if p is None:
                per_chip_lib[(chip, lib)] = None
                continue
            f, o, u = scan(objdump, p)
            per_chip_lib[(chip, lib)] = (f, o, u, os.path.getsize(p))
            allsyms |= f | o
            allundef |= u
            got = True
        if got:
            per_chip[chip] = allsyms
            # A name defined by one object in the archive and referenced by another shows up in
            # both lists; an import is what nothing in the set defines.
            per_undef[chip] = allundef - allsyms
            present.append((chip, arch))
            print(f"  {chip:9s} {arch:7s} {len(allsyms):5d} defined, {len(per_undef[chip]):4d} imported")

    chips = [c for c, _ in present]
    common = set.intersection(*(per_chip[c] for c in chips)) if chips else set()
    union = set.union(*(per_chip[c] for c in chips)) if chips else set()

    doc = [
        "# Radio blob parity across the ESP variants",
        "",
        "Which radio entry points every ESP die has in common, and which belong to one part. Read",
        "from the archives' symbol tables with each variant's own `objdump`: the C-series parts are",
        "RISC-V rather than xtensa, so one cross-toolchain does not read all of them. Names only,",
        "no code.",
        "",
        "Regenerate with `python tools/dev_env/blob_parity.py .`.",
        "",
        "## Variants",
        "",
        "| Chip | ISA | Symbols |",
        "| --- | --- | ---: |",
    ]
    for chip, arch in present:
        doc.append(f"| `{chip}` | {arch} | {len(per_chip[chip])} |")

    doc += ["", "## Which blobs each variant ships", "", "| Library | " + " | ".join(f"`{c}`" for c in chips) + " |",
            "| --- | " + " | ".join("---" for _ in chips) + " |"]
    for lib in BLOBS:
        cells = []
        for c in chips:
            v = per_chip_lib.get((c, lib))
            cells.append("-" if v is None else f"{v[3] // 1024} KB")
        doc.append(f"| `{lib}` | " + " | ".join(cells) + " |")

    doc += [
        "",
        f"## Parity: {len(common)} of {len(union)} symbols are on every variant",
        "",
        "| Chip | Total | Shared with all | Only on this one |",
        "| --- | ---: | ---: | ---: |",
    ]
    for chip in chips:
        others = set.union(*(per_chip[c] for c in chips if c != chip)) if len(chips) > 1 else set()
        doc.append(f"| `{chip}` | {len(per_chip[chip])} | {len(common)} | {len(per_chip[chip] - others)} |")

    doc += ["", "## The entry points a replacement driver stands in for", "",
            "`def` is defined by that variant's blobs, `imp` is imported by them and has to come",
            "from somewhere else, and `-` is absent entirely.", "",
            "| Symbol | " + " | ".join(f"`{c}`" for c in chips) + " |",
            "| --- | " + " | ".join("---" for _ in chips) + " |"]
    for w in WATCH:
        cells = []
        for c in chips:
            cells.append("def" if w in per_chip[c] else ("imp" if w in per_undef[c] else "-"))
        doc.append(f"| `{w}` | " + " | ".join(cells) + " |")

    doc += ["", "## What the blobs import on every variant", "",
            "Symbols no variant's blobs define, so a replacement has to supply them. This is the",
            "porting surface that is the same everywhere.", ""]
    common_imp = set.intersection(*(per_undef[c] for c in chips)) if chips else set()
    doc += [f"{len(common_imp)} symbols.", "", "```"]
    doc += sorted(common_imp)
    doc += ["```", ""]

    doc += ["## On every variant", "", f"{len(common)} symbols.", "", "```"]
    doc += sorted(common)
    doc += ["```", ""]

    for chip in chips:
        others = set.union(*(per_chip[c] for c in chips if c != chip)) if len(chips) > 1 else set()
        only = sorted(per_chip[chip] - others)
        missing = sorted(common_missing(per_chip, chips, chip))
        doc += [f"## `{chip}` alone", "", f"{len(only)} symbols no other variant defines.", "", "```"]
        doc += only
        doc += ["```", ""]
        if missing:
            doc += [f"### Absent from `{chip}` but on every other variant", "",
                    f"{len(missing)} symbols.", "", "```"]
            doc += missing
            doc += ["```", ""]

    dest = os.path.join(repo, "src", "board_drivers", "hal", "esp", "RADIO_BLOB_PARITY.md")
    with open(dest, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(doc) + "\n")
    print(f"\ncommon {len(common)} / union {len(union)} -> {dest}")


def common_missing(per_chip, chips, chip):
    """Symbols every other variant has and this one does not."""
    others = [per_chip[c] for c in chips if c != chip]
    if not others:
        return set()
    return set.intersection(*others) - per_chip[chip]


main()
