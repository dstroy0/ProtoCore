#!/usr/bin/env python3
"""Classify what actually changed in each radio blob function that differs between the installs.

Knowing that 768 functions differ says nothing about whether any of them behave differently. Two
builds of the same routine can disagree because the scheduler moved instructions around, which is
how a compiler expresses RF timing, or because the code now writes a different register with a
different value, which is a behavior change.

Each differing function is put in one bucket, checked in this order:

  reordered    the same instructions in a different order, and every immediate, call target and
               register reference is the same multiset. Timing, not behavior
  values       the same instruction shapes and the same registers, with different immediates.
               A tuning constant moved: transmit power, a gain step, a threshold
  calls        the set of symbols the function reaches changed
  registers    the set of peripheral registers it touches changed
  structural   instructions were added or removed beyond the above

Reads the instruction streams `blob_diff.py` extracts, so the two agree by construction.

Usage:  python reverse_engineering/esp32_mac/blob_behavior.py <repo-root> [--lib libphy.a] [--chip esp32]
"""
import os
import re
import sys
from collections import Counter

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from blob_diff import LIBS, arduino_blob, functions, idf_blob, tools_for  # noqa: E402

ARDUINO = os.path.expanduser("~/.platformio/packages/framework-arduinoespressif32/tools/sdk")

# A peripheral address, as it appears in a literal pool or an operand.
PERIPH = re.compile(r"\b(3f[f4-7][0-9a-f]{5}|600[0-3][0-9a-f]{4})\b", re.I)
IMM = re.compile(r"(?<![\w.])(-?(?:0x[0-9a-fA-F]+|\d+))(?![\w.])")


def shapes(stream):
    """Mnemonics alone: what the function does, with the operands stripped off."""
    return Counter(e.split(" ")[0] for e in stream if not e.startswith("@"))


def immediates(stream):
    out = Counter()
    for e in stream:
        if e.startswith("@"):
            continue
        parts = e.split(" ", 1)
        if len(parts) == 2:
            for v in IMM.findall(parts[1]):
                out[v] += 1
    return out


def calls(stream):
    """Relocations that name another symbol.

    A relocation against a section (`.text.foo+0x20`, `.literal`, `.rodata`) is this function
    pointing at its own literal pool or constants: the offset moves whenever anything above it in
    the object moves, so counting those reports placement as a behavior change.
    """
    out = Counter()
    for e in stream:
        if not e.startswith("@"):
            continue
        sym = e.split(":", 1)[1] if ":" in e else ""
        if sym.startswith("."):
            continue
        out[sym.split("+")[0]] += 1
    return out


def registers(stream):
    out = Counter()
    for e in stream:
        for m in PERIPH.findall(e):
            out[m.lower()] += 1
    return out


def classify(a, i):
    """a and i are the unmasked instruction streams."""
    if Counter(a) == Counter(i):
        return "reordered"
    sa, si = shapes(a), shapes(i)
    ca, ci = calls(a), calls(i)
    ra, ri = registers(a), registers(i)
    if ca != ci:
        return "calls"
    if ra != ri:
        return "registers"
    if sa == si and immediates(a) != immediates(i):
        return "values"
    return "structural"


def main():
    repo = os.path.abspath(sys.argv[1])
    only_lib = sys.argv[sys.argv.index("--lib") + 1] if "--lib" in sys.argv else None
    only_chip = sys.argv[sys.argv.index("--chip") + 1] if "--chip" in sys.argv else None

    targets = sorted(d for d in os.listdir(ARDUINO) if os.path.isdir(os.path.join(ARDUINO, d)))
    if only_chip:
        targets = [t for t in targets if t == only_chip]
    libs = [l for l in LIBS if only_lib is None or l == only_lib]

    work = os.path.join(os.environ.get("TEMP", "/tmp"), "pc_blob_behavior")
    ORDER = ["reordered", "values", "calls", "registers", "structural"]
    rows, detail, totals = [], [], Counter()

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
            got = Counter()
            items = []
            for f in sorted(set(fa) & set(fi)):
                if fa[f][0] == fi[f][0]:
                    continue
                kind = classify(fa[f][2], fi[f][2])
                got[kind] += 1
                totals[kind] += 1
                items.append((f, kind))
            if items:
                rows.append((chip, lib, got, len(items)))
                detail.append((chip, lib, items))
                print(f"  {chip:9s} {lib:18s} " + "  ".join(f"{k}={got[k]}" for k in ORDER if got[k]))

    doc = [
        "# What changed in the radio blob functions that differ",
        "",
        "768 functions differ between the Arduino and IDF builds. That figure alone says nothing",
        "about behavior: two builds of one routine disagree either because the scheduler moved",
        "instructions, which is how a compiler expresses RF timing, or because the code now writes",
        "a different register with a different value, which is a behavior change.",
        "",
        "Each differing function lands in one bucket, tested in this order:",
        "",
        "| Bucket | Meaning |",
        "| --- | --- |",
        "| `reordered` | the same instructions in a different order, every immediate, call and register unchanged. Timing |",
        "| `values` | the same instruction shapes and registers, different immediates. A tuning constant moved |",
        "| `calls` | the set of symbols the function reaches changed |",
        "| `registers` | the set of peripheral registers it touches changed |",
        "| `structural` | instructions added or removed beyond the above |",
        "",
        "Only `registers`, `calls` and `structural` can change what the radio does. `reordered` and",
        "`values` are the tuning that moves between SDK releases.",
        "",
        "Regenerate with `python reverse_engineering/esp32_mac/blob_behavior.py .`.",
        "",
        "| Target | Library | Differ | " + " | ".join(f"`{k}`" for k in ORDER) + " |",
        "| --- | --- | ---: | " + " | ".join("---:" for _ in ORDER) + " |",
    ]
    for chip, lib, got, n in rows:
        doc.append(f"| `{chip}` | `{lib}` | {n} | " + " | ".join(str(got[k]) for k in ORDER) + " |")

    tot = sum(totals.values())
    doc += ["", "## Totals", "",
            "| Bucket | Functions | Share |", "| --- | ---: | ---: |"]
    for k in ORDER:
        doc.append(f"| `{k}` | {totals[k]} | {100 * totals[k] // max(1, tot)}% |")
    behavioral = totals["calls"] + totals["registers"] + totals["structural"]
    doc += ["", f"{totals['reordered'] + totals['values']} of {tot} are reordering or tuning "
                f"constants. {behavioral} touch calls, registers or structure.", ""]

    for chip, lib, items in detail:
        doc += [f"## `{chip}` / `{lib}`", "", "```"]
        for f, kind in sorted(items, key=lambda x: (x[1], x[0])):
            doc.append(f"{kind:11s} {f}")
        doc += ["```", ""]

    dest = os.path.join(repo, "reverse_engineering", "esp32_mac", "RADIO_BLOB_BEHAVIOR.md")
    with open(dest, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(doc) + "\n")
    print(f"\n{tot} differing: " + ", ".join(f"{k} {totals[k]}" for k in ORDER) + f" -> {dest}")


if __name__ == "__main__":
    main()
