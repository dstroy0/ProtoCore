#!/usr/bin/env python3
"""Capture the ESP32 analog RF register writes the radio blobs make, in order, with their values.

The RF synthesizer, PLL, crystal and bias blocks are not memory mapped. They sit behind an
internal serial bus reached through two ROM routines, which libphy calls indirectly through the
`g_phyFuns` table:

    movi   a13, 243     ; data
    movi.n a12, 1       ; reg_add
    movi.n a11, 0       ; host_id
    movi   a10, 99      ; block
    l32i.n a8, a2, 0    ; a2 = &g_phyFuns
    l32i   a8, a8, 160  ; the table slot
    callx8 a8

Every argument is an immediate, so the whole sequence is recoverable from the instruction stream:
this tracks the windowed-ABI argument registers a10..a15 through `movi` and `mov`, follows the
table dereference to the slot number, and emits one line per call.

A slot is reported by its byte offset. The table is filled at runtime from ROM addresses, so no
header names the slots; the offset plus the argument count is what identifies each one.

Usage:  python reverse_engineering/esp32_mac/blob_analog.py <repo-root>
"""
import os
import re
import shutil
import subprocess
import sys

TC = os.path.expanduser("~/.platformio/packages/toolchain-xtensa-esp32/bin")
OBJDUMP = os.path.join(TC, "xtensa-esp32-elf-objdump.exe")
AR = os.path.join(TC, "xtensa-esp32-elf-ar.exe")
SDK = os.path.expanduser("~/.platformio/packages/framework-arduinoespressif32/tools/sdk/esp32")

BLOBS = [("libphy.a", "ld"), ("librtc.a", "ld")]

SECTION = re.compile(r"^Disassembly of section (\S+):")
LABEL = re.compile(r"^([0-9a-f]+) <([^>]+)>:")
DATA = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} ){4})\s*$")
INSN = re.compile(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2,8} )+\s*(\S+)\s*(.*?)\s*$")
RELOC = re.compile(r"^\s+([0-9a-f]+): R_XTENSA\S*\s+(\S+)")

# Windowed-ABI outgoing arguments for a call8: the caller's a10 becomes the callee's a2.
ARGS = (10, 11, 12, 13, 14, 15)

# Slots whose argument shape was read off the disassembly by hand and matches the ROM's serial-bus
# routines: (block, host_id, reg_add, data) for the plain write, and the same with (msb, lsb)
# ahead of the data for the masked one. Argument 0 is an analog block only for these.
I2C_SLOTS = (160, 168)


def split_sections(text):
    cur, groups = [], []
    for line in text.split("\n"):
        if SECTION.match(line):
            if cur:
                groups.append(cur)
            cur = []
        cur.append(line)
    if cur:
        groups.append(cur)
    return groups


def walk_section(lines):
    """(function, [(slot, args)]) for the indirect calls made through a symbol table."""
    literals, relocs = {}, {}
    for line in lines:
        m = RELOC.match(line)
        if m:
            relocs[int(m.group(1), 16)] = m.group(2)
            continue
        m = DATA.match(line)
        if m:
            b = bytes(int(x, 16) for x in m.group(2).split())
            literals[int(m.group(1), 16)] = int.from_bytes(b, "little")

    out, calls = [], []
    fname = None
    imm = {}    # register -> immediate it holds
    syms = {}   # register -> symbol whose address it holds
    base = {}   # register -> symbol it was dereferenced from
    slot = {}   # register -> byte offset within that symbol

    def flush():
        if fname is not None and calls:
            out.append((fname, list(calls)))

    for line in lines:
        if RELOC.match(line) or DATA.match(line):
            continue
        m = LABEL.match(line)
        if m:
            name = m.group(2)
            if "-0x" not in name and "+0x" not in name:
                flush()
                fname, calls = name, []
                imm, syms, base, slot = {}, {}, {}, {}
            continue
        m = INSN.match(line)
        if not m:
            continue
        off, op, args = int(m.group(1), 16), m.group(2), m.group(3)

        if op == "l32r":
            a = re.match(r"a(\d+),\s*([0-9a-f]+)", args)
            if a:
                d, src = int(a.group(1)), int(a.group(2), 16)
                imm.pop(d, None)
                base.pop(d, None)
                slot.pop(d, None)
                syms.pop(d, None)
                if src in relocs:
                    syms[d] = relocs[src]
                elif src in literals:
                    imm[d] = literals[src]
            continue

        if op in ("movi", "movi.n"):
            a = re.match(r"a(\d+),\s*(-?(?:0x)?[0-9a-fA-F]+)", args)
            if a:
                d = int(a.group(1))
                v = a.group(2)
                imm[d] = int(v, 16) if v.lower().startswith(("0x", "-0x")) else int(v)
                syms.pop(d, None)
                base.pop(d, None)
                slot.pop(d, None)
            continue

        if op in ("mov", "mov.n"):
            a = re.match(r"a(\d+),\s*a(\d+)", args)
            if a:
                d, s = int(a.group(1)), int(a.group(2))
                for t in (imm, syms, base, slot):
                    t.pop(d, None)
                    if s in t:
                        t[d] = t[s]
            continue

        if op in ("l32i", "l32i.n"):
            a = re.match(r"a(\d+),\s*a(\d+),\s*(-?\d+)", args)
            if a:
                d, s, o = int(a.group(1)), int(a.group(2)), int(a.group(3))
                sym, prev = syms.get(s), base.get(s)
                for t in (imm, syms, base, slot):
                    t.pop(d, None)
                if sym is not None and o == 0:
                    base[d] = sym          # a8 = *g_phyFuns, the table itself
                elif prev is not None:
                    base[d] = prev         # a8 = table[o], the slot
                    slot[d] = o
            continue

        if op == "callx8":
            a = re.match(r"a(\d+)", args)
            if a:
                r = int(a.group(1))
                if r in slot:
                    vals = [imm.get(x) for x in ARGS]
                    calls.append((base[r], slot[r], vals))
            # A call clobbers the outgoing argument registers.
            for x in ARGS:
                for t in (imm, syms, base, slot):
                    t.pop(x, None)
            continue

        if op in ("call8", "call4", "call12"):
            for x in ARGS:
                for t in (imm, syms, base, slot):
                    t.pop(x, None)
            continue

    flush()
    return out


def main():
    repo = os.path.abspath(sys.argv[1])
    work = os.path.join(os.environ.get("TEMP", "/tmp"), "pc_blob_analog")
    doc = [
        "# Radio blob analog RF sequences",
        "",
        "The ESP32's RF synthesizer, PLL, crystal and bias blocks are not memory mapped. They sit",
        "behind an internal serial bus reached through two ROM routines, which `libphy` calls",
        "indirectly through the `g_phyFuns` function table. Every argument at those call sites is an",
        "immediate, so the full programming sequence is recoverable from the instruction stream.",
        "",
        "Read out with `xtensa-esp32-elf-objdump -dr`; nothing here is decompiled. Regenerate with",
        "`python reverse_engineering/esp32_mac/blob_analog.py .`.",
        "",
        "A slot is given by its byte offset into the table. The table is filled at runtime from ROM",
        "addresses, so no header names the slots: the offset and the argument count identify one.",
        "The four-argument shape matches the ROM's `(block, host_id, reg_add, data)`.",
        "",
        "Arguments are shown through the last one the call site sets. Slot 160 sets four,",
        "matching `(block, host_id, reg_add, data)`; slot 168 sets six, matching the same with",
        "`(msb, lsb)` ahead of the data, which is a read-modify-write of a bit field.",
        "",
        "`?` is an argument this could not follow to a constant, which means it is computed.",
        "",
        "## The primitive",
        "",
        "`ram_chip_i2c_writeReg` and `ram_chip_i2c_readReg` are not in ROM: `libphy.a` defines them",
        "in its own `.iram1`, so the hardware sequence is readable. They appear as undefined only",
        "because sibling objects in the archive reference them.",
        "",
        "Reading `ram_chip_i2c_writeReg(block, host_id, reg_add, data)` out of the disassembly:",
        "",
        "- each of the four arguments is masked to 8 bits (`extui aN, aN, 0, 8`),",
        "- the body runs inside `phy_enter_critical` / `phy_exit_critical`,",
        "- a `host_id` below 2 takes a path that calls `phy_dis_hw_set_freq` first, then clears",
        "  bit 8 of `0x3FF4E0C4` through `esp_dport_access_reg_read` and writes it back,",
        "- the per-transfer register address is `(0x0FFD3800 + host_id) << 2`, which is",
        "  `0x3FF4E000 + host_id * 4`. The base is pre-divided by four so the shift lands it.",
        "",
        "So the analog bus controller is at `0x3FF4E000` indexed by `host_id * 4`, with a control",
        "register at `+0xC4`. That base is not one of the documented ESP32 peripherals. `memw`",
        "separates the read from the write-back, and the DPORT read path is mandatory there.",
        "",
        "The `g_phyFuns` slots below are still numbered rather than named: `esp32.rom.ld` places the",
        "table at `g_phyFuns_instance = 0x3ffae0c4` in DRAM and it is filled at runtime by",
        "`phy_get_romfuncs` (`0x40004100`), so no static artifact carries the mapping. Reading it",
        "out of a live coredump and matching each pointer against the 1 601 `PROVIDE` addresses in",
        "`esp32.rom.ld` is what names them.",
        "",
    ]
    slots = {}
    blocks = {}
    total = 0
    for lib, sub in BLOBS:
        path = os.path.join(SDK, sub, lib)
        if not os.path.exists(path):
            continue
        d = os.path.join(work, lib)
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d, exist_ok=True)
        subprocess.run([AR, "x", path], cwd=d, capture_output=True)

        funcs = []
        for o in sorted(os.listdir(d)):
            if not o.endswith(".o"):
                continue
            text = subprocess.run([OBJDUMP, "-dr", os.path.join(d, o)],
                                  capture_output=True, text=True, errors="replace").stdout
            for lines in split_sections(text):
                for fname, calls in walk_section(lines):
                    funcs.append((o, fname, calls))
                    for tbl, off, vals in calls:
                        slots[(tbl, off)] = slots.get((tbl, off), 0) + 1
                        # Only the slots whose shape is confirmed carry a block in argument 0;
                        # counting every slot's first argument as one would invent blocks.
                        if off in I2C_SLOTS and vals[0] is not None:
                            blocks[vals[0]] = blocks.get(vals[0], 0) + 1
                        total += 1

        print(f"  {lib:12s} {len(funcs):4d} functions, {total} indirect table calls so far")
        doc += [f"## `{lib}`", "", f"{len(funcs)} functions call through a table.", ""]
        for o, fname, calls in funcs:
            doc += [f"### `{fname}`  <sub>{o}</sub>", "", "```"]
            for tbl, off, vals in calls:
                # Show through the last argument that was set: the masked write uses all six,
                # the plain one four, and trailing unknowns are registers the call never reads.
                last = max((i for i, v in enumerate(vals) if v is not None), default=-1)
                shown = " ".join("?" if v is None else f"0x{v & 0xFFFFFFFF:02X}" for v in vals[:last + 1])
                doc.append(f"{tbl}+{off:<4} ({shown})")
            doc += ["```", ""]

    doc += ["## Table slots by call count", "", "| Slot | Calls |", "| --- | ---: |"]
    for (tbl, off), n in sorted(slots.items(), key=lambda kv: -kv[1]):
        doc.append(f"| `{tbl}+{off}` | {n} |")
    doc += ["", "## Analog blocks addressed", "",
            "Argument 0 of the two serial-bus slots (160 and 168) only.", "",
            "| Block | Accesses |", "| --- | ---: |"]
    for b, n in sorted(blocks.items(), key=lambda kv: -kv[1]):
        doc.append(f"| `0x{b & 0xFF:02X}` | {n} |")
    doc.append("")

    dest = os.path.join(repo, "reverse_engineering", "esp32_mac", "RADIO_BLOB_ANALOG.md")
    with open(dest, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(doc) + "\n")
    print(f"\n{total} calls, {len(slots)} distinct slots, {len(blocks)} analog blocks -> {dest}")


main()
