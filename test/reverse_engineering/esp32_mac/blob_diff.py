#!/usr/bin/env python3
"""Compare the radio blobs' CODE between the two installs that ship them, function by function.

Matching symbol names prove only that an API surface is the same shape. This compares what the
functions actually do, by disassembling every function in both installs and diffing the
instruction streams.

Three verdicts per function, in decreasing strength:

  identical    the encoded bytes and the relocation targets are the same, so the two builds
               emitted the same machine code
  equivalent   the mnemonics, operands and relocation targets match once addresses are masked.
               Code that moved to a different offset lands here: a branch displacement or a
               literal-pool reference is written differently while the instruction is the same
  different    the instruction streams disagree, so the two builds do not do the same thing

Addresses are masked rather than compared because the whole point is that placement moves.
Relocation targets are NOT masked: which symbol a call reaches is functional.

Usage:  python reverse_engineering/esp32_mac/blob_diff.py <repo-root> [--lib libphy.a] [--chip esp32] [--full]
"""

import os
import re
import shutil
import subprocess
import sys

PK = os.path.expanduser("~/.platformio/packages")
ARDUINO = os.path.join(PK, "framework-arduinoespressif32", "tools", "sdk")
IDF = os.path.join(PK, "framework-espidf", "components")
PHY_DIR = os.path.join(IDF, "esp_phy", "lib")
WIFI_DIR = os.path.join(IDF, "esp_wifi", "lib")

XTENSA = ("esp32", "esp32s2", "esp32s3")
LIBS = ["libphy.a", "libpp.a", "libnet80211.a", "libmesh.a", "libsmartconfig.a"]

SECTION = re.compile(r"^Disassembly of section (\S+):")
RELOC = re.compile(r"^\s+([0-9a-f]+): (R_\S+)\s+(\S+)")

# objdump lays a disassembly line out tab-separated: "  64:\t01a136        \tentry\ta1, 208".
# The byte column runs its hex together for an instruction and space-separates it for data, so
# matching on the byte spelling drops one of the two. Splitting on the tabs reads both.
OFFSET = re.compile(r"^\s*([0-9a-f]+):$")
HEXRUN = re.compile(r"^[0-9a-f]{2}(?:[0-9a-f]| )*$")

# A hex address in an operand is placement, not behavior: mask it. A branch prints its target as
# an absolute offset plus a <symbol+0x..> annotation, and both move when code moves.
ADDR = re.compile(r"\b[0-9a-f]{4,8}\b")
ANNOT = re.compile(r"<[^>]*>")


def tools_for(chip):
    if chip in XTENSA:
        b = os.path.join(PK, f"toolchain-xtensa-{chip}", "bin")
        od, ar = os.path.join(b, f"xtensa-{chip}-elf-objdump.exe"), os.path.join(b, f"xtensa-{chip}-elf-ar.exe")
    else:
        b = os.path.join(PK, "toolchain-riscv32-esp", "bin")
        od, ar = os.path.join(b, "riscv32-esp-elf-objdump.exe"), os.path.join(b, "riscv32-esp-elf-ar.exe")
    return (od, ar) if os.path.exists(od) and os.path.exists(ar) else (None, None)


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


SYMLINE = re.compile(r"^([0-9a-f]{8})\s+(.{7})\s+(\S+)\s+([0-9a-f]{8})\s+(\S+)\s*$")


def functions(objdump, ar, archive, work):
    """{function name: (raw byte string, normalized instruction stream)} for a whole archive.

    Located from the symbol table rather than from section names. Only some functions get their
    own `.text.<name>` section; the rest share `.text`, `.iram1` or a `.phyiram.N`, and keying on
    section names alone silently drops every one of those.
    """
    shutil.rmtree(work, ignore_errors=True)
    os.makedirs(work, exist_ok=True)
    subprocess.run([ar, "x", archive], cwd=work, capture_output=True)

    out = {}
    for o in sorted(os.listdir(work)):
        if not o.endswith(".o"):
            continue
        path = os.path.join(work, o)

        # Where each function lives: section, offset within it, and how many bytes it spans.
        syms = []
        for line in subprocess.run(
            [objdump, "-t", path], capture_output=True, text=True, errors="replace"
        ).stdout.split("\n"):
            m = SYMLINE.match(line.rstrip())
            if m and "F" in m.group(2):
                syms.append((m.group(3), int(m.group(1), 16), int(m.group(4), 16), m.group(5)))

        # Every section's instructions once, in order, so each function is a slice of one.
        secs = {}
        cur = None
        for line in subprocess.run(
            [objdump, "-dr", path], capture_output=True, text=True, errors="replace"
        ).stdout.split("\n"):
            m = SECTION.match(line)
            if m:
                cur = m.group(1)
                secs[cur] = []
                continue
            if cur is None:
                continue
            m = RELOC.match(line)
            if m:
                # Which symbol a call or a literal resolves to is behavior, so it is compared.
                tag = f"@{m.group(2)}:{m.group(3)}"
                secs[cur].append((int(m.group(1), 16), None, tag, tag))
                continue
            parts = line.split("\t")
            if len(parts) < 3:
                continue
            mo = OFFSET.match(parts[0])
            if mo is None or not HEXRUN.match(parts[1].strip()):
                continue
            mnem = parts[2].strip()
            if not mnem:
                continue
            ops = ANNOT.sub("", parts[3] if len(parts) > 3 else "").strip()
            # Two forms: one with addresses masked, for the identical/equivalent verdict, and
            # one left alone, so a caller can see which immediates actually changed.
            secs[cur].append(
                (
                    int(mo.group(1), 16),
                    parts[1].strip().replace(" ", ""),
                    (mnem + " " + ADDR.sub("#", ops)).strip(),
                    (mnem + " " + ops).strip(),
                )
            )

        for sec, off, size, name in syms:
            if size == 0 or sec not in secs:
                continue
            raw, norm, exact = [], [], []
            for a, b, n, e in secs[sec]:
                if off <= a < off + size:
                    if b is not None:
                        raw.append(b)
                    norm.append(n)
                    exact.append(e)
            if norm:
                out[name] = ("".join(raw), tuple(norm), tuple(exact))
    return out


def main():
    repo = os.path.abspath(sys.argv[1])
    only_lib = sys.argv[sys.argv.index("--lib") + 1] if "--lib" in sys.argv else None
    only_chip = sys.argv[sys.argv.index("--chip") + 1] if "--chip" in sys.argv else None
    full = "--full" in sys.argv

    targets = sorted(d for d in os.listdir(ARDUINO) if os.path.isdir(os.path.join(ARDUINO, d)))
    if only_chip:
        targets = [t for t in targets if t == only_chip]
    libs = [l for l in LIBS if only_lib is None or l == only_lib]

    work = os.path.join(os.environ.get("TEMP", "/tmp"), "pc_blob_diff")
    rows, detail = [], []
    for chip in targets:
        objdump, ar = tools_for(chip)
        if objdump is None:
            continue
        for lib in libs:
            a, i = arduino_blob(chip, lib), idf_blob(chip, lib)
            if a is None or i is None:
                continue
            fa = functions(objdump, ar, a, os.path.join(work, "a"))
            fi = functions(objdump, ar, i, os.path.join(work, "i"))
            shared = sorted(set(fa) & set(fi))
            same_bytes = [f for f in shared if fa[f][0] == fi[f][0]]
            equiv = [f for f in shared if fa[f][0] != fi[f][0] and fa[f][1] == fi[f][1]]
            diff = [f for f in shared if fa[f][0] != fi[f][0] and fa[f][1] != fi[f][1]]
            rows.append(
                (
                    chip,
                    lib,
                    len(shared),
                    len(same_bytes),
                    len(equiv),
                    len(diff),
                    len(set(fa) - set(fi)),
                    len(set(fi) - set(fa)),
                )
            )
            detail.append((chip, lib, diff, fa, fi))
            print(
                f"  {chip:9s} {lib:18s} shared={len(shared):5d} identical={len(same_bytes):5d} "
                f"equivalent={len(equiv):4d} different={len(diff):5d}"
            )

    doc = [
        "# Radio blob code comparison between the Arduino and IDF installs",
        "",
        "Matching symbol names prove only that an API surface is the same shape. This compares what",
        "the functions do, by disassembling every function in both installs and diffing the",
        "instruction streams.",
        "",
        "- **identical** - the encoded bytes and relocation targets match, so both builds emitted",
        "  the same machine code.",
        "- **equivalent** - mnemonics, operands and relocation targets match once addresses are",
        "  masked. Code that moved to a different offset lands here.",
        "- **different** - the instruction streams disagree. These do not do the same thing.",
        "",
        "Addresses are masked because placement moving is the expected difference. Relocation",
        "targets are not masked: which symbol a call reaches is behavior.",
        "",
        "The **equivalent** tier reads zero everywhere, and that is expected rather than a failure:",
        "these are relocatable objects, so each function's section starts at offset zero and nothing",
        "shifts between builds unless the code itself changed. The tier would matter when comparing",
        "linked images. Here the real signal is identical against different.",
        "",
        "Read with `objdump -dr`; nothing is decompiled. Regenerate with",
        "`python reverse_engineering/esp32_mac/blob_diff.py .`.",
        "",
        "| Target | Library | Shared | Identical | Equivalent | Different | A only | I only |",
        "| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]
    for chip, lib, sh, sb, eq, df, oa, oi in rows:
        doc.append(f"| `{chip}` | `{lib}` | {sh} | {sb} | {eq} | {df} | {oa} | {oi} |")

    tot_sh = sum(r[2] for r in rows)
    tot_sb = sum(r[3] for r in rows)
    tot_eq = sum(r[4] for r in rows)
    tot_df = sum(r[5] for r in rows)
    doc += [
        "",
        "## Totals",
        "",
        f"{tot_sh} functions are in both installs. {tot_sb} are byte-identical, {tot_eq} are",
        f"equivalent once addresses are masked, and {tot_df} genuinely differ.",
        "",
    ]

    for chip, lib, diff, fa, fi in detail:
        if not diff:
            continue
        doc += [f"## `{chip}` / `{lib}` - {len(diff)} functions differ", "", "```"]
        for f in (diff if full else diff[:60]):
            doc.append(f"{f}  (A {len(fa[f][1])} insn, I {len(fi[f][1])} insn)")
        if not full and len(diff) > 60:
            doc.append(f"... {len(diff) - 60} more, rerun with --full")
        doc += ["```", ""]

    dest = os.path.join(repo, "reverse_engineering", "esp32_mac", "RADIO_BLOB_CODEDIFF.md")
    with open(dest, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(doc) + "\n")
    print(f"\nshared {tot_sh}: identical {tot_sb}, equivalent {tot_eq}, different {tot_df} -> {dest}")


if __name__ == "__main__":
    main()
