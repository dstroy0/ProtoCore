#!/usr/bin/env python3
"""Compare the radio blobs' symbols between the two installs that ship them.

The Arduino framework and ESP-IDF each carry their own copy of the radio libraries, built at
different times from different sources. If the exported and imported names are the same set in
both, then the entry points are fixed by the ecosystem and any analysis of one install transfers
to the other, with only addresses, sizes and code layout differing. If they are not, then anything
read out of one install is version-specific and has to be regenerated per SDK.

This compares per library, on the targets both installs carry, and reports name differences in
both directions alongside the size difference that shows they are genuinely separate builds.

Symbol tables only: this reads names, not code.

Usage:  python tools/dev_env/blob_crossinstall.py <repo-root>
"""
import os
import re
import subprocess
import sys

PK = os.path.expanduser("~/.platformio/packages")
ARDUINO = os.path.join(PK, "framework-arduinoespressif32", "tools", "sdk")
IDF = os.path.join(PK, "framework-espidf", "components")
PHY_DIR = os.path.join(IDF, "esp_phy", "lib")
WIFI_DIR = os.path.join(IDF, "esp_wifi", "lib")

XTENSA = ("esp32", "esp32s2", "esp32s3")

# Libraries both installs ship under the same name; anything else is not a like-for-like compare.
SHARED_LIBS = ["libphy.a", "libpp.a", "libnet80211.a", "libmesh.a", "libsmartconfig.a"]

SYM = re.compile(r"^([0-9a-f]{8})\s+(.{7})\s+(\S+)\s+([0-9a-f]{8})\s+(\S+)\s*$")

# A function that moves between ROM and RAM between SDK versions keeps its body and changes its
# prefix: rom1_set_pbus_reg becomes ram1_set_pbus_reg. Folding the two prefixes together separates
# "this function was relocated" from "this function is new".
PLACEMENT = re.compile(r"^(?:rom|ram)(\d*)_")


def normalize(name):
    return PLACEMENT.sub(r"@\1_", name)


def objdump_for(chip):
    if chip in XTENSA:
        p = os.path.join(PK, f"toolchain-xtensa-{chip}", "bin", f"xtensa-{chip}-elf-objdump.exe")
        return p if os.path.exists(p) else None
    p = os.path.join(PK, "toolchain-riscv32-esp", "bin", "riscv32-esp-elf-objdump.exe")
    return p if os.path.exists(p) else None


def arduino_blob(chip, lib):
    for sub in ("lib", "ld"):
        p = os.path.join(ARDUINO, chip, sub, lib)
        if os.path.exists(p):
            return p
    return None


def idf_blob(chip, lib):
    for d in (PHY_DIR, WIFI_DIR):
        p = os.path.join(d, chip, lib)
        if os.path.exists(p):
            return p
    return None


def scan(objdump, path):
    """(defined, imported) name sets for one archive."""
    r = subprocess.run([objdump, "-t", path], capture_output=True, text=True, errors="replace")
    defined, undef = set(), set()
    for line in r.stdout.split("\n"):
        if "*UND*" in line:
            parts = line.split()
            if parts and not parts[-1].startswith("*"):
                undef.add(parts[-1])
            continue
        m = SYM.match(line.rstrip())
        if not m:
            continue
        flags, name = m.group(2), m.group(5)
        if "g" in flags and ("F" in flags or "O" in flags):
            defined.add(name)
    return defined, undef - defined


def main():
    repo = os.path.abspath(sys.argv[1])
    targets = sorted(d for d in os.listdir(ARDUINO) if os.path.isdir(os.path.join(ARDUINO, d)))

    ver = "unknown"
    vf = os.path.join(PK, "framework-espidf", "version.txt")
    if os.path.exists(vf):
        ver = open(vf).read().strip()

    doc = [
        "# Radio blob parity between the Arduino and IDF installs",
        "",
        "The Arduino framework and ESP-IDF each ship their own copy of the radio libraries, built",
        "separately. This compares the exported and imported names between them, per library, on",
        "the targets both installs carry.",
        "",
        "The question it answers: is anything read out of one install version-specific, or are the",
        "entry points fixed by the ecosystem so that only addresses and layout move?",
        "",
        f"Arduino SDK targets compared: {', '.join('`' + t + '`' for t in targets)}. ESP-IDF {ver}.",
        "",
        "Regenerate with `python tools/dev_env/blob_crossinstall.py .`.",
        "",
        "## Per library",
        "",
        "`A only` and `I only` count names present in one install and not the other. `moved` is",
        "how many of those are the same function relocated between ROM and RAM, which changes a",
        "`rom*_` prefix to `ram*_` and nothing else. The last two columns are what remains once",
        "that is folded out: genuinely added or removed entry points.",
        "",
        "| Target | Library | Arduino | IDF | Defined | Imported | A only | I only | moved | real A | real I |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]

    diffs = []
    rows = 0
    identical = 0
    same_after = 0
    for chip in targets:
        objdump = objdump_for(chip)
        if objdump is None:
            continue
        for lib in SHARED_LIBS:
            a, i = arduino_blob(chip, lib), idf_blob(chip, lib)
            if a is None or i is None:
                continue
            ad, ai = scan(objdump, a)
            idd, ii = scan(objdump, i)
            a_all, i_all = ad | ai, idd | ii
            only_a, only_i = sorted(a_all - i_all), sorted(i_all - a_all)
            # Same comparison with ROM/RAM placement folded out: what is left is genuinely added
            # or removed rather than relocated.
            na, ni = {normalize(x) for x in a_all}, {normalize(x) for x in i_all}
            moved = len((na & ni) - {normalize(x) for x in (a_all & i_all)})
            n_only_a, n_only_i = sorted(na - ni), sorted(ni - na)
            rows += 1
            if not only_a and not only_i:
                identical += 1
            if not n_only_a and not n_only_i:
                same_after += 1
            doc.append(
                f"| `{chip}` | `{lib}` | {os.path.getsize(a) // 1024} KB | {os.path.getsize(i) // 1024} KB "
                f"| {len(ad)} | {len(ai)} | {len(only_a)} | {len(only_i)} | {moved} | "
                f"{len(n_only_a)} | {len(n_only_i)} |")
            if n_only_a or n_only_i:
                diffs.append((chip, lib, n_only_a, n_only_i))
            print(f"  {chip:9s} {lib:18s} A={len(a_all):5d} I={len(i_all):5d} "
                  f"only-A={len(only_a):4d} only-I={len(only_i):4d} moved={moved:3d} "
                  f"real-A={len(n_only_a):4d} real-I={len(n_only_i):4d}")

    doc += ["", "## Verdict", "",
            f"{identical} of {rows} library pairs match name for name.",
            f"{same_after} of {rows} match once ROM/RAM placement is folded out.", ""]
    if not diffs:
        doc += ["Every pair matches, so the entry points are fixed across the two installs and an",
                "analysis of one transfers to the other. Only addresses, sizes and code layout move.",
                ""]
    else:
        doc += ["The pairs below still differ after folding, so those are real API changes and",
                "anything read out of one install has to be checked against the other.", "",
                "Names below are shown with the placement prefix folded to `@`.", ""]
        for chip, lib, only_a, only_i in diffs:
            doc += [f"### `{chip}` / `{lib}`", ""]
            if only_a:
                doc += [f"{len(only_a)} only in the Arduino build.", "", "```"] + only_a + ["```", ""]
            if only_i:
                doc += [f"{len(only_i)} only in the IDF build.", "", "```"] + only_i + ["```", ""]

    dest = os.path.join(repo, "src", "board_drivers", "hal", "esp", "RADIO_BLOB_CROSSINSTALL.md")
    with open(dest, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(doc) + "\n")
    print(f"\n{identical}/{rows} pairs identical -> {dest}")


main()
