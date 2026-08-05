#!/usr/bin/env python3
"""Inventory which radio blob functions are IRAM-resident, and are therefore ISR-reachable.

A function reached from an interrupt cannot live in flash: the cache is disabled during a flash
write and while the SPI flash is otherwise busy, so a flash-resident handler faults there. The
radio blobs place every such routine in an IRAM section, which is why they carry a `ram_` prefix
and why the PHY dispatch table points into IRAM rather than flash.

That placement is a constraint on any replacement rather than an implementation detail: a function
listed here has to be IRAM-resident in our own driver too, or it fails only during flash activity,
which is the hardest failure to reproduce.

The same placement is why the radio pins to one core: the ISRs are registered on the core that
brings WiFi up, and the tables they dispatch through are shared per-core state.

Reads section placement from the symbol table; no code is disassembled.

Usage:  python tools/dev_env/blob_iram.py <repo-root>
"""
import os
import re
import shutil
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from blob_diff import tools_for  # noqa: E402

PK = os.path.expanduser("~/.platformio/packages")
IDF = os.path.join(PK, "framework-espidf", "components")
PHY_DIR = os.path.join(IDF, "esp_phy", "lib")
WIFI_DIR = os.path.join(IDF, "esp_wifi", "lib")

LIBS = ["libphy.a", "libpp.a", "libnet80211.a", "libcoexist.a"]

SYM = re.compile(r"^([0-9a-f]{8})\s+(.{7})\s+(\S+)\s+([0-9a-f]{8})\s+(\S+)\s*$")

# Any section the linker maps into internal RAM. The blobs use several names for it, and a
# function in any of them is one the cache being off must not take out.
IRAM = re.compile(r"^\.(iram|phyiram|wifi\w*iram|wifirxiram|wifi0iram|coexiram|rtc\w*iram)", re.I)


def find_blob(chip, lib):
    for d in (PHY_DIR, WIFI_DIR):
        p = os.path.join(d, chip, lib)
        if os.path.exists(p):
            return p
    return None


def placement(objdump, ar, archive, work):
    """{function: section} for every defined function in the archive."""
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work, exist_ok=True)
    subprocess.run([ar, "x", archive], cwd=work, capture_output=True)
    out = {}
    for o in sorted(os.listdir(work)):
        if not o.endswith(".o"):
            continue
        for line in subprocess.run([objdump, "-t", os.path.join(work, o)],
                                   capture_output=True, text=True, errors="replace").stdout.split("\n"):
            m = SYM.match(line.rstrip())
            if m and "F" in m.group(2) and int(m.group(4), 16) > 0:
                out[m.group(5)] = m.group(3)
    return out


def main():
    repo = os.path.abspath(sys.argv[1])
    targets = sorted(d for d in os.listdir(PHY_DIR) if os.path.isdir(os.path.join(PHY_DIR, d)))
    work = os.path.join(os.environ.get("TEMP", "/tmp"), "pc_blob_iram")

    doc = [
        "# Radio blob functions that must live in IRAM",
        "",
        "A function reached from an interrupt cannot live in flash: the cache is disabled during a",
        "flash write and while the SPI flash is otherwise busy, so a flash-resident handler faults",
        "there. The blobs place every such routine in an IRAM section, which is what the `ram_`",
        "prefix marks and why the PHY dispatch table points into IRAM rather than flash.",
        "",
        "This is a constraint on a replacement, not a detail of theirs. A function listed here has",
        "to be IRAM-resident in our driver too, or it fails only while flash is busy, which is the",
        "hardest class of failure to reproduce. It is also why the radio pins to one core: the ISRs",
        "are registered on the core that brings WiFi up.",
        "",
        "Regenerate with `python tools/dev_env/blob_iram.py .`.",
        "",
        "| Target | Library | Functions | In IRAM | Share |",
        "| --- | --- | ---: | ---: | ---: |",
    ]
    per_target = {}
    for chip in targets:
        objdump, ar = tools_for(chip)
        if objdump is None:
            continue
        for lib in LIBS:
            p = find_blob(chip, lib)
            if p is None:
                continue
            place = placement(objdump, ar, p, os.path.join(work, "x"))
            iram = sorted(f for f, s in place.items() if IRAM.match(s))
            if not place:
                continue
            per_target.setdefault(chip, {})[lib] = (len(place), iram)
            doc.append(f"| `{chip}` | `{lib}` | {len(place)} | {len(iram)} | "
                       f"{100 * len(iram) // max(1, len(place))}% |")
            print(f"  {chip:9s} {lib:16s} {len(place):5d} functions, {len(iram):4d} in IRAM")

    # The set every WiFi die keeps in IRAM is the floor a portable driver has to meet.
    wifi = [c for c in per_target if "libphy.a" in per_target[c] and "libpp.a" in per_target[c]]
    sets = []
    for c in wifi:
        s = set()
        for lib in per_target[c]:
            s |= set(per_target[c][lib][1])
        sets.append(s)
    common = set.intersection(*sets) if sets else set()

    doc += ["", "## IRAM-resident on every WiFi die", "",
            f"{len(common)} functions. These are the ISR-reachable core: any replacement keeps them",
            "in internal RAM on every target.", "", "```"]
    doc += sorted(common)
    doc += ["```", ""]

    for chip in sorted(per_target):
        for lib in sorted(per_target[chip]):
            total, iram = per_target[chip][lib]
            if not iram:
                continue
            doc += [f"## `{chip}` / `{lib}` - {len(iram)} of {total} in IRAM", "", "```"]
            doc += iram
            doc += ["```", ""]

    dest = os.path.join(repo, "src", "board_drivers", "hal", "esp", "RADIO_BLOB_IRAM.md")
    with open(dest, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(doc) + "\n")
    print(f"\n{len(common)} IRAM-resident on every WiFi die -> {dest}")


if __name__ == "__main__":
    main()
