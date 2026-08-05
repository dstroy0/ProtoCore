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

# ESP-IDF carries a blob set per target, and covers far more dies than the Arduino SDK does: the
# Arduino tree stops at esp32 / s2 / s3 / c3, which leaves out every part shipped since.
IDF = os.path.join(PK, "framework-espidf", "components")
PHY_DIR = os.path.join(IDF, "esp_phy", "lib")
WIFI_DIR = os.path.join(IDF, "esp_wifi", "lib")

XTENSA = ("esp32", "esp32s2", "esp32s3")


def objdump_for(chip):
    """The toolchain that reads this die. The C and H series are RISC-V, not xtensa, and each
    xtensa die has its own configured toolchain."""
    if chip in XTENSA:
        p = os.path.join(PK, f"toolchain-xtensa-{chip}", "bin", f"xtensa-{chip}-elf-objdump.exe")
        if os.path.exists(p):
            return p, "xtensa"
        p = os.path.join(PK, "toolchain-xtensa", "bin", f"xtensa-{chip}-elf-objdump.exe")
        return (p if os.path.exists(p) else None), "xtensa"
    p = os.path.join(PK, "toolchain-riscv32-esp", "bin", "riscv32-esp-elf-objdump.exe")
    return (p if os.path.exists(p) else None), "riscv"


BLOBS = ["libphy.a", "libpp.a", "libnet80211.a", "libcore.a", "libespnow.a", "libmesh.a",
         "libsmartconfig.a", "libwapi.a", "libbtbb.a"]

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


def idf_targets():
    """Every target IDF ships a radio blob for."""
    if not os.path.isdir(PHY_DIR):
        return []
    return sorted(d for d in os.listdir(PHY_DIR) if os.path.isdir(os.path.join(PHY_DIR, d)))


def find_blob(chip, lib):
    for d in (PHY_DIR, WIFI_DIR):
        p = os.path.join(d, chip, lib)
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

    for chip in idf_targets():
        objdump, arch = objdump_for(chip)
        if objdump is None:
            print(f"  {chip}: no objdump for this die, skipped")
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
    # A die that ships no 802.11 MAC has nothing to be at parity with on the WiFi path: H2 is
    # 802.15.4 and BLE only. Intersecting across those collapses the answer to noise, so the
    # parity figures below are over the dies that carry both libpp and libnet80211.
    wifi = [c for c in chips
            if per_chip_lib.get((c, "libpp.a")) is not None
            and per_chip_lib.get((c, "libnet80211.a")) is not None]
    non_wifi = [c for c in chips if c not in wifi]
    common = set.intersection(*(per_chip[c] for c in wifi)) if wifi else set()
    union = set.union(*(per_chip[c] for c in wifi)) if wifi else set()

    doc = [
        "# Radio blob parity across the ESP variants",
        "",
        "Which radio entry points every ESP die has in common, and which belong to one part. Read",
        "from the archives' symbol tables with each variant's own `objdump`: the C and H series are",
        "RISC-V rather than xtensa, so one cross-toolchain does not read all of them. Names only,",
        "no code.",
        "",
        "Sourced from ESP-IDF's `esp_phy` and `esp_wifi` component libraries, which carry a blob set",
        "per target. The Arduino SDK stops at esp32 / s2 / s3 / c3 and leaves out every die shipped",
        "since, so it cannot answer this question.",
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
        f"## Parity: {len(common)} of {len(union)} symbols are on every WiFi die",
        "",
        "Over the dies that ship both `libpp.a` and `libnet80211.a`: "
        + ", ".join(f"`{c}`" for c in wifi) + ".",
        "",
    ]
    if non_wifi:
        doc += ["Excluded, having no 802.11 MAC to be at parity with: "
                + ", ".join(f"`{c}`" for c in non_wifi) + ".", ""]
    doc += ["| Chip | Total | Shared with all WiFi dies | Only on this one |", "| --- | ---: | ---: | ---: |"]
    for chip in wifi:
        others = set.union(*(per_chip[c] for c in wifi if c != chip)) if len(wifi) > 1 else set()
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
    common_imp = set.intersection(*(per_undef[c] for c in wifi)) if wifi else set()
    doc += [f"{len(common_imp)} symbols.", "", "```"]
    doc += sorted(common_imp)
    doc += ["```", ""]

    doc += ["## On every variant", "", f"{len(common)} symbols.", "", "```"]
    doc += sorted(common)
    doc += ["```", ""]

    for chip in wifi:
        others = set.union(*(per_chip[c] for c in wifi if c != chip)) if len(wifi) > 1 else set()
        only = sorted(per_chip[chip] - others)
        missing = sorted(common_missing(per_chip, wifi, chip))
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
