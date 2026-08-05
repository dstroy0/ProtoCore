#!/usr/bin/env python3
"""Group the Espressif radio blobs' register accesses by the function that makes them.

The blobs ship with -ffunction-sections and an intact symbol table, so each function's code and
its literal pool sit in their own section and carry the vendor's own name. Xtensa reaches an
absolute address through `l32r`, which loads a 32-bit word out of that pool, so a peripheral
register appears as a literal and every access to it is that literal plus a displacement.

This walks the disassembly keeping a value per address register: `l32r` puts a literal into one,
and a later load or store through it names a register and a width. What comes out is, per
function, the ordered list of registers it touches and the calls it makes between them.

Nothing is decompiled: this reads the instruction stream objdump prints.

**XTENSA ONLY.** The extraction is built on `l32r` reading a literal pool, which is how xtensa
reaches a 32-bit constant. The RISC-V dies (C2, C3, C5, C6, C61, H2) have no literal pool: they
build an address from a `lui` / `addi` pair whose value comes from a relocation, and they call
through `auipc` / `jalr` rather than a table. Pointed at one of those this would find nothing and
report it as "touches no registers", which is a false negative rather than a result, so it
refuses instead. A RISC-V port is a separate extraction, not a flag.

Usage:  python reverse_engineering/esp32_mac/xtensa/blob_registers.py <repo-root> [--only <substring>]
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

BLOBS = [
    ("libphy.a", "ld", "RF, PLL, baseband and calibration"),
    ("librtc.a", "ld", "RTC / low-power domain and clock"),
    ("libpp.a", "lib", "WiFi MAC, the 802.11 packet processor"),
    ("libnet80211.a", "lib", "802.11 MLME: scan, auth, assoc"),
    ("libcoexist.a", "lib", "WiFi / Bluetooth radio arbitration"),
]

# ESP32 peripheral windows. DPORT and the APB peripherals share 0x3FF4_xxxx - 0x3FF7_xxxx; the
# second range is the data bus alias the ROM uses.
RANGES = ((0x3FF00000, 0x3FF80000), (0x60000000, 0x60040000))

SECTION = re.compile(r"^Disassembly of section (\S+):")
LABEL = re.compile(r"^([0-9a-f]+) <([^>]+)>:")
DATA = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} ){4})\s*$")
INSN = re.compile(r"^\s*([0-9a-f]+):\s+(?:[0-9a-f]{2,8} )+\s*(\S+)\s*(.*?)\s*$")
RELOC = re.compile(r"^\s+([0-9a-f]+): R_XTENSA\S*\s+(\S+)")

LOADS = {"l32i": 32, "l32i.n": 32, "l16ui": 16, "l16si": 16, "l8ui": 8}
STORES = {"s32i": 32, "s32i.n": 32, "s16i": 16, "s8i": 8}


# Dies this extraction is valid for. Everything else is RISC-V, where none of the instruction
# forms below exist.
XTENSA = ("esp32", "esp32s2", "esp32s3")


def require_xtensa(chip):
    if chip not in XTENSA:
        raise SystemExit(
            f"{chip} is RISC-V: this reads xtensa l32r literal pools and callx8 tables, which it "
            f"has none of. It would report every function as touching no registers. Use the "
            f"arch-agnostic tools (blob_parity, blob_diff, blob_behavior, blob_iram) for that die."
        )


def in_range(v):
    return any(lo <= v < hi for lo, hi in RANGES)


def disasm(obj):
    return subprocess.run([OBJDUMP, "-dr", obj], capture_output=True, text=True, errors="replace").stdout


def split_sections(text):
    """The disassembly's lines, grouped by the section they belong to."""
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


def walk(text):
    """Yield (function, [events]) for every section that holds one."""
    out = []
    for lines in split_sections(text):
        out.extend(walk_section(lines))
    return out


def walk_section(lines):
    """One section. Relocations are collected first: objdump prints each one on the line AFTER
    the instruction it applies to, so a single pass would never see a call's target."""
    literals = {}   # byte offset -> 32-bit value
    relocs = {}     # byte offset -> symbol
    regs = {}       # address register -> literal value it holds
    syms = {}       # address register -> symbol whose address it holds
    events = []
    fname = None
    out = []

    for line in lines:
        m = RELOC.match(line)
        if m:
            relocs[int(m.group(1), 16)] = m.group(2)
            continue
        m = DATA.match(line)
        if m:
            b = bytes(int(x, 16) for x in m.group(2).split())
            literals[int(m.group(1), 16)] = int.from_bytes(b, "little")

    def flush():
        if fname is not None and events:
            out.append((fname, list(events)))

    for line in lines:
        if RELOC.match(line) or DATA.match(line):
            continue
        m = LABEL.match(line)
        if m:
            name = m.group(2)
            # `<foo-0x64>` is the literal pool in front of foo, not a second function.
            if "-0x" not in name and "+0x" not in name:
                flush()
                fname, events, regs, syms = name, [], {}, {}
            continue
        m = INSN.match(line)
        if not m:
            continue
        off, op, args = int(m.group(1), 16), m.group(2), m.group(3)
        if op == "l32r":
            a = re.match(r"a(\d+),\s*([0-9a-f]+)", args)
            if a:
                dst, src = int(a.group(1)), int(a.group(2), 16)
                regs.pop(dst, None)
                syms.pop(dst, None)
                # A literal the linker fills in names a symbol: that is how a call to another
                # object reaches it, as l32r of the address then callx8 through the register.
                if src in relocs:
                    syms[dst] = relocs[src]
                elif src in literals:
                    regs[dst] = literals[src]
            continue
        if op in ("call8", "call4", "call12"):
            # A call to another object is a relocation; one inside this object is printed with the
            # target's own name in the operand.
            sym = relocs.get(off)
            if sym is None:
                a = re.search(r"<([^>+-]+)", args)
                sym = a.group(1) if a else None
            events.append(("call", sym if sym is not None else "?", None))
            continue
        if op == "callx8":
            a = re.match(r"a(\d+)", args)
            sym = syms.get(int(a.group(1))) if a else None
            events.append(("call", sym if sym is not None else "(indirect)", None))
            continue
        width = LOADS.get(op) or STORES.get(op)
        if width is None:
            # Anything that writes an address register with a value this cannot follow makes the
            # register unknown, so a later access through it is not reported as a guess.
            a = re.match(r"a(\d+)", args)
            if a and op not in ("beqz", "bnez", "bnez.n", "beqz.n"):
                regs.pop(int(a.group(1)), None)
                syms.pop(int(a.group(1)), None)
            continue
        a = re.match(r"a(\d+),\s*a(\d+),\s*(-?\d+)", args)
        if not a:
            continue
        base = regs.get(int(a.group(2)))
        if base is None:
            continue
        addr = (base + int(a.group(3))) & 0xFFFFFFFF
        if in_range(addr):
            events.append(("rd" if op in LOADS else "wr", addr, width))
    flush()
    return out


def main():
    repo = os.path.abspath(sys.argv[1])
    only = None
    if "--only" in sys.argv:
        only = sys.argv[sys.argv.index("--only") + 1]

    work = os.path.join(os.environ.get("TEMP", "/tmp"), "pc_blob_regs")
    doc = [
        "# Radio blob register map",
        "",
        "Every peripheral register the precompiled ESP32 radio libraries touch, grouped by the",
        "function that touches it and listed in the order the accesses happen. Read out of the",
        "instruction stream with `xtensa-esp32-elf-objdump -dr`; nothing here is decompiled.",
        "",
        "Xtensa reaches an absolute address by loading a 32-bit literal with `l32r` and then",
        "displacing off it, so a register shows up as `literal + offset`. A function whose base",
        "register is computed rather than loaded is listed with whatever accesses could be",
        "followed, so absence of a register here is not proof the function leaves it alone.",
        "",
        "`wr` and `rd` carry the access width in bits.",
        "",
        "Regenerate with `python reverse_engineering/esp32_mac/xtensa/blob_registers.py .`.",
        "",
    ]
    grand = {}
    for lib, sub, what in BLOBS:
        path = os.path.join(SDK, sub, lib)
        if not os.path.exists(path):
            print(f"  missing {path}")
            continue
        d = os.path.join(work, lib)
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d, exist_ok=True)
        subprocess.run([AR, "x", path], cwd=d, capture_output=True)

        funcs = []
        for o in sorted(os.listdir(d)):
            if not o.endswith(".o"):
                continue
            for fname, events in walk(disasm(os.path.join(d, o))):
                regs = [e for e in events if e[0] != "call"]
                if not regs:
                    continue
                if only is not None and only not in fname:
                    continue
                funcs.append((o, fname, events))
                for _, addr, _w in regs:
                    grand[addr] = grand.get(addr, 0) + 1

        print(f"  {lib:16s} {len(funcs):4d} functions touch a register")
        doc += [f"## `{lib}` - {what}", "", f"{len(funcs)} functions touch a peripheral register.", ""]
        for o, fname, events in funcs:
            doc += [f"### `{fname}`  <sub>{o}</sub>", "", "```"]
            for kind, a, w in events:
                doc.append(f"call  {a}" if kind == "call" else f"{kind}{w:<3} 0x{a:08X}")
            doc += ["```", ""]

    doc += ["## Registers by how many functions reach them", "", "```"]
    for addr, n in sorted(grand.items(), key=lambda kv: (-kv[1], kv[0]))[:400]:
        doc.append(f"0x{addr:08X}  {n}")
    doc += ["```", ""]

    dest = os.path.join(repo, "reverse_engineering", "esp32_mac", "xtensa", "RADIO_BLOB_REGISTERS.md")
    with open(dest, "w", encoding="utf-8", newline="\n") as fh:
        fh.write("\n".join(doc) + "\n")
    print(f"\n{len(grand)} distinct registers -> {dest}")


main()
