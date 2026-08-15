#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""
bare.py - cross-compile the core as a bare-metal image and report what it cannot satisfy.

  bare.py build [env...]   compile and link each env for a cross target; report the gaps
  bare.py arches           the cross targets, their toolchains, and whether each is installed
  bare.py runtime          the files the image carries under the env, and what each supplies
  bare.py help [command]   the whole surface, or one command's

The point is not the binary: it is the symbol list. A host build answers a question about the
machine that compiled it, and its answers hide behind the host arm's own header inlines - the
reason an earlier survey counted clock_gettime and longjmp as library needs when both came from
core_setup/hal/host. Building with the cross compiler and -ffreestanding asks the question the
device asks, and whatever is still undefined is what bare metal actually owes.

The env list and the flags come from the same platformio.ini the native suite reads, through
harness.py, so an env is described in one place and this tool cannot drift from it.
`harness.py bare` is the same code reached through the test harness.
"""

import argparse
import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import harness  # noqa: E402

ROOT = harness.ROOT

# The two cross toolchains PlatformIO already installed, and the flags each part wants. They are off
# PATH, so they are named by absolute path the way every other automation here does.
PIO_PKGS = os.path.join(os.path.expanduser("~"), ".platformio", "packages")

# The memory map every arch is linked against. Not any real part's: a semantic bringup only needs
# regions that exist, are the right shape, and do not overlap, so the image is measurable on the
# host. Real numbers per board are a separate job.
REGIONS_ARM = ["-DPROTOCORE_LINK_FLASH_ORIGIN=0x08000000", "-DPROTOCORE_LINK_FLASH_LEN=1M",
               "-DPROTOCORE_LINK_SRAM_ORIGIN=0x20000000", "-DPROTOCORE_LINK_SRAM_LEN=256K",
               "-DPROTOCORE_LINK_STACK_SIZE=16K", "-DPROTOCORE_LINK_IRQ_STACK_SIZE=4K",
               "-DPROTOCORE_LINK_DMA_ORIGIN=0x20040000", "-DPROTOCORE_LINK_DMA_LEN=64K",
               "-DPROTOCORE_LINK_DMA_ALIGN=32"]
REGIONS_RISCV = ["-DPROTOCORE_LINK_FLASH_ORIGIN=0x20000000", "-DPROTOCORE_LINK_FLASH_LEN=1M",
                 "-DPROTOCORE_LINK_SRAM_ORIGIN=0x80000000", "-DPROTOCORE_LINK_SRAM_LEN=256K",
                 "-DPROTOCORE_LINK_STACK_SIZE=16K", "-DPROTOCORE_LINK_IRQ_STACK_SIZE=4K",
                 "-DPROTOCORE_LINK_DMA_ORIGIN=0x80040000", "-DPROTOCORE_LINK_DMA_LEN=64K",
                 "-DPROTOCORE_LINK_DMA_ALIGN=32"]

# The ESP32-C3 map, read out of ESP-IDF's own components/esp_system/ld/esp32c3/memory.ld.in rather
# than recalled: SRAM_IRAM_START 0x4037C000 and SRAM_DRAM_START 0x3FC7C000 are the same physical
# SRAM seen through the instruction and the data port, ICACHE_SIZE 0x4000 is carved off the front of
# both, and SRAM_DRAM_END is 0x403CE710 - (0x4037C000 - 0x3FC7C000). So the usable window is
# 0x40380000 / 0x3FC80000 for 0x4E710 bytes.
#
# An image booted straight under QEMU has no second-stage bootloader, so the flash cache MMU is
# never programmed and 0x42000020 addresses nothing. Everything therefore lives in SRAM: the FLASH
# region below is the front of IRAM, where the read-only content is placed, and the writable region
# is the rest of it addressed through the data port. .data is still copied from one to the other at
# reset, which costs a memcpy inside RAM and keeps one startup path for both real flash and the sim.
ESP32C3_IRAM = 0x40380000
ESP32C3_DRAM = 0x3FC80000
ESP32C3_SIZE = 0x4E710
ESP32C3_TEXT = 0x20000  # of that window, what the read-only half gets
ESP32C3_DMA = 0x8000    # and what is held back for DMA-reachable buffers

REGIONS_ESP32C3 = [
    "-DPROTOCORE_LINK_FLASH_ORIGIN=0x%08X" % ESP32C3_IRAM,
    "-DPROTOCORE_LINK_FLASH_LEN=0x%X" % ESP32C3_TEXT,
    "-DPROTOCORE_LINK_SRAM_ORIGIN=0x%08X" % (ESP32C3_DRAM + ESP32C3_TEXT),
    "-DPROTOCORE_LINK_SRAM_LEN=0x%X" % (ESP32C3_SIZE - ESP32C3_TEXT - ESP32C3_DMA),
    "-DPROTOCORE_LINK_STACK_SIZE=8K",
    "-DPROTOCORE_LINK_IRQ_STACK_SIZE=2K",
    "-DPROTOCORE_LINK_DMA_ORIGIN=0x%08X" % (ESP32C3_DRAM + ESP32C3_SIZE - ESP32C3_DMA),
    "-DPROTOCORE_LINK_DMA_LEN=0x%X" % ESP32C3_DMA,
    "-DPROTOCORE_LINK_DMA_ALIGN=32",
]

ARM_BIN = os.path.join(PIO_PKGS, "toolchain-gccarmnoneeabi", "bin")
RV_BIN = os.path.join(PIO_PKGS, "toolchain-riscv32-esp", "bin")

# Espressif's QEMU fork, installed by
#   python <framework-espidf>/tools/idf_tools.py install qemu-xtensa qemu-riscv32
# It is a different binary from an upstream qemu on PATH: only the fork carries the esp32, esp32s3
# and esp32c3 machines, and with them the AES / SHA / RSA / RNG blocks a vendor-crypto build needs.
ESP_QEMU = os.path.join(os.path.expanduser("~"), ".espressif", "tools")
QEMU_VER = "esp_develop_9.0.0_20240606"

BARE_ARCH = {
    "cortex-m": {
        "cc": os.path.join(ARM_BIN, "arm-none-eabi-gcc.exe"),
        "objdump": os.path.join(ARM_BIN, "arm-none-eabi-objdump.exe"),
        "march": ["-mcpu=cortex-m4", "-mthumb"],
        "ld": os.path.join(ROOT, "core_setup", "link", "cortex_m.ld.in"),
        "startup": "core_setup/boot/startup_cortex_m.c",
        "regions": REGIONS_ARM,
        "why": "the reference ARM target: has LDREX/STREX, so a read-modify-write is an instruction",
    },
    # armv6-m has no LDREX/STREX and rv32imc has no LR/SC, so a read-modify-write is a call into
    # __atomic_*_4 rather than an instruction. The ring claims and releases its slots with
    # atomic_fetch_or / atomic_fetch_and, which is where this library's locking actually is, so
    # these two are the arches that prove core_setup/boot/protocore_atomic.c covers it.
    "cortex-m0": {
        "cc": os.path.join(ARM_BIN, "arm-none-eabi-gcc.exe"),
        "objdump": os.path.join(ARM_BIN, "arm-none-eabi-objdump.exe"),
        "march": ["-mcpu=cortex-m0", "-mthumb"],
        "ld": os.path.join(ROOT, "core_setup", "link", "cortex_m.ld.in"),
        "startup": "core_setup/boot/startup_cortex_m.c",
        "regions": REGIONS_ARM,
        "why": "armv6-m has no LDREX/STREX: the ring's slot claim becomes a call into __atomic_fetch_or_4",
    },
    "riscv32-noa": {
        "cc": os.path.join(RV_BIN, "riscv32-esp-elf-gcc.exe"),
        "objdump": os.path.join(RV_BIN, "riscv32-esp-elf-objdump.exe"),
        "march": ["-march=rv32imc_zicsr", "-mabi=ilp32"],
        "ld": os.path.join(ROOT, "core_setup", "link", "riscv32.ld.in"),
        "startup": "core_setup/boot/startup_riscv32.S",
        "regions": REGIONS_RISCV,
        "why": "no A extension, so no LR/SC: the same read-modify-write becomes a call",
    },
    "riscv32": {
        "cc": os.path.join(RV_BIN, "riscv32-esp-elf-gcc.exe"),
        "objdump": os.path.join(RV_BIN, "riscv32-esp-elf-objdump.exe"),
        "march": ["-march=rv32imac_zicsr", "-mabi=ilp32"],
        "ld": os.path.join(ROOT, "core_setup", "link", "riscv32.ld.in"),
        "startup": "core_setup/boot/startup_riscv32.S",
        "regions": REGIONS_RISCV,
        "why": "the reference RISC-V target: the A extension inlines the read-modify-write",
    },
    # A real part rather than a shape: same instruction set as riscv32, but the addresses are the
    # ESP32-C3's, so the image is one QEMU will actually boot.
    "esp32c3": {
        "cc": os.path.join(RV_BIN, "riscv32-esp-elf-gcc.exe"),
        "objdump": os.path.join(RV_BIN, "riscv32-esp-elf-objdump.exe"),
        "march": ["-march=rv32imc_zicsr", "-mabi=ilp32"],
        "ld": os.path.join(ROOT, "core_setup", "link", "riscv32.ld.in"),
        "startup": "core_setup/boot/startup_riscv32.S",
        "regions": REGIONS_ESP32C3,
        "why": "a real ESP32-C3 memory map, so the image boots under qemu-system-riscv32 -M esp32c3",
        "qemu": os.path.join(ESP_QEMU, "qemu-riscv32", QEMU_VER, "qemu", "bin", "qemu-system-riscv32.exe"),
        "machine": "esp32c3",
    },
}

# The runtime the image needs under it, beside whatever the env builds: the reset path, and the
# things a freestanding compile emits calls to that no library provides.
BARE_RUNTIME = ["core_setup/boot/protocore_boot.c", "core_setup/boot/startup_common.c",
                "core_setup/boot/protocore_assert.c", "core_setup/boot/protocore_atomic.c",
                "core_setup/boot/protocore_memfns.c", "core_setup/boot/protocore_time.c",
                "src/mmgr/protomem.c", "src/mmgr/rawmemcpy.c", "src/mmgr/swar.c"]

# A TU that only wants the network arm's types is waiting on idemIP (core_setup/idemIP), which is
# the low-level stack and is not written yet. A TU that only wants the scheduler seam is waiting on
# a platform task arm. Both are known holes rather than findings, so they are counted apart from a
# TU that fails for its own reasons.
NET_PENDING = re.compile(r"'(protocore_net_err|protocore_pcb|protocore_net_ip|protocore_pbuf|protocore_net_call)'")
SCHED_PENDING = re.compile(r"'(protocore_platform_task|protocore_platform_queue|protocore_platform_mutex|"
                           r"protocore_platform_ticks|protocore_platform_status)[a-z_]*'")

MAIN_STUB = ("// Generated by bare.py: the entry protocore_boot_start() calls.\n"
             "int main(void);\n"
             "int main(void)\n{\n    for (;;)\n    {\n    }\n}\n")

# The sim's main. A build that only links proves the symbols resolve; this proves the reset path
# ran, the linker script put the regions where the part has memory, and .data arrived - so it
# reports the stack headroom the painted pattern still shows and the addresses it was linked at,
# then stops. Anything other than these lines means the image did not get that far.
SIM_MAIN = """// Generated by bare.py sim: what the image says once it is running on the part.
#include <stdint.h>

void protocore_sim_puts(const char *s);
void protocore_sim_puthex(uint32_t v);
uint32_t protocore_boot_stack_unused(const uint32_t *stack_low, uint32_t stack_words);

extern uint32_t __protocore_data_start;
extern uint32_t __protocore_bss_start;
extern uint32_t __protocore_stack_low;
extern uint32_t __protocore_stack_top;

int main(void);
int main(void)
{
    protocore_sim_puts("\\nPROTOCORE-SIM boot ok\\n");
    protocore_sim_puts("  data  0x");
    protocore_sim_puthex((uint32_t)(uintptr_t)&__protocore_data_start);
    protocore_sim_puts("\\n  bss   0x");
    protocore_sim_puthex((uint32_t)(uintptr_t)&__protocore_bss_start);
    protocore_sim_puts("\\n  stack 0x");
    protocore_sim_puthex((uint32_t)(uintptr_t)&__protocore_stack_top);
    protocore_sim_puts("\\n  stack unused ");
    protocore_sim_puthex(protocore_boot_stack_unused(&__protocore_stack_low,
                                                     (uint32_t)(&__protocore_stack_top - &__protocore_stack_low)));
    protocore_sim_puts("\\nPROTOCORE-SIM done\\n");
    for (;;)
    {
    }
}
"""


def env_sources(e, arch, out_dir, sim=False):
    """Every TU the image compiles: the env's own, the bare runtime, the reset path, and a main.

    Most envs already list the mmgr sources the runtime rests on, and one object per source is what
    the linker wants: a second copy is a duplicate symbol, not a second definition.
    """
    extra = ["core_setup/boot/protocore_sim_uart.c"] if sim else []
    srcs, seen = [], set()
    for s in harness._resolve_src(e["src"]) + BARE_RUNTIME + extra + [arch["startup"]]:
        k = s.replace("\\", "/")
        if k not in seen:
            seen.add(k)
            srcs.append(s)

    # The reset path calls main() and the library has no application entry, so the gate supplies
    # one. Without it the link fails on `main` alone and no image is produced - which is a fact
    # about this build having no application, not about the core.
    stub = os.path.join(out_dir, "_sim_main.c" if sim else "_bare_main.c")
    with open(stub, "w", encoding="utf-8") as fh:
        fh.write(SIM_MAIN if sim else MAIN_STUB)
    srcs.append(os.path.relpath(stub, ROOT).replace("\\", "/"))
    return srcs


def compile_all(base, defs, incs, srcs, out_dir, name):
    """Compile every TU. Returns (objects, [(source, first error)])."""
    objs, bad = [], []
    for s in srcs:
        obj = os.path.join(out_dir, name + "__" + s.replace("/", "_").replace("\\", "_") + ".o")
        p = subprocess.run(base + defs + incs + [s, "-o", obj], capture_output=True, text=True, cwd=ROOT)
        if p.returncode != 0:
            # The first stderr line is "In file included from ...", which names the include chain
            # rather than the fault. The first line carrying `error:` is the fault.
            msg = "?"
            for ln in p.stderr.split("\n"):
                if ": error:" in ln or ": fatal error:" in ln:
                    msg = ln.split("error:", 1)[1].strip()
                    break
            bad.append((s, msg))
        else:
            objs.append(obj)
    return objs, bad


def link(arch, objs, out_dir, name):
    """Preprocess the linker script and link. Returns (linked, undefined symbols, other error)."""
    ldout = os.path.join(out_dir, name + ".ld")
    subprocess.run([arch["cc"], "-E", "-P", "-x", "c"] + arch["regions"] + [arch["ld"], "-o", ldout],
                   capture_output=True, text=True, cwd=ROOT)
    elf = os.path.join(out_dir, name + ".elf")
    # -lgcc is the compiler's own runtime (64-bit shifts, soft float, __aeabi_*). The toolchain
    # ships it and a real image links it, so an image missing it says nothing about the core.
    # `main` is the application's, which a library build does not carry.
    l = subprocess.run([arch["cc"]] + arch["march"] + ["-nostdlib", "-nostartfiles", "-T", ldout] +
                       objs + ["-o", elf, "-lgcc"], capture_output=True, text=True, cwd=ROOT)
    # An image exists or it does not. Reading only the undefined-reference lines called a link that
    # failed some other way a success, and reported LINKS with no ELF on disk.
    if l.returncode == 0 and os.path.isfile(elf):
        return True, [], ""
    undef = sorted(set(re.findall(r"undefined reference to `([^']+)'", l.stderr)))
    undef = [u for u in undef if u != "main"]
    err = ""
    if not undef:
        for ln in l.stderr.split("\n"):
            if ": error:" in ln or "ld.exe:" in ln or "cannot" in ln:
                err = ln.strip()
                break
    return False, undef, err


def cmd_build(a):
    """Compile an env's sources for a real cross target and say what the image cannot satisfy."""
    arch = BARE_ARCH[a.arch]
    if not os.path.isfile(arch["cc"]):
        print("no %s toolchain at %s" % (a.arch, arch["cc"]))
        return 3

    envs = harness.parse_ini_envs(harness.INI)
    names = a.envs or [n for n, e in envs.items() if e.get("tests") and n not in harness.NEVER_SELECT]
    out_dir = os.path.join(ROOT, ".pio", "bare", a.arch)
    os.makedirs(out_dir, exist_ok=True)

    # The bare-metal include set: the host arm is deliberately absent, which is the whole point.
    incs = ["-I" + os.path.join(ROOT, p) for p in (".", "src", "include")]
    base = [arch["cc"], "-std=c11", "-Os", "-w", "-ffreestanding", "-fno-builtin", "-c"] + arch["march"]

    rc_total = 0
    for i, name in enumerate(names, 1):
        e = envs.get(name)
        if not e:
            print("unknown env: %s" % name)
            rc_total = 1
            continue
        _, defs = harness._flag_split(e["flags"])
        srcs = env_sources(e, arch, out_dir)
        objs, bad = compile_all(base, defs, incs, srcs, out_dir, name)

        linked, undef, link_err = False, [], ""
        if objs and not bad:
            linked, undef, link_err = link(arch, objs, out_dir, name)

        net = [x for x in bad if NET_PENDING.search(x[1])]
        sched = [x for x in bad if SCHED_PENDING.search(x[1])]
        real = [x for x in bad if x not in net and x not in sched]

        if linked:
            status = "LINKS"
        elif not real and not undef and not link_err:
            status = "NEEDS-ARM"
        else:
            status = "FAIL"
        waiting = []
        if net:
            waiting.append("%d net" % len(net))
        if sched:
            waiting.append("%d sched" % len(sched))
        print("[%d/%d] %-34s %-11s %s" % (i, len(names), name, status,
                                          ("(%s TU waiting on a platform arm)" % ", ".join(waiting)) if waiting else ""))
        if real:
            print("   %d TU(s) will not compile freestanding:" % len(real))
            for s, msg in real[:6]:
                print("      %-52s %s" % (s, msg[:90]))
        if undef:
            print("   %d symbol(s) the image cannot satisfy:" % len(undef))
            for u in undef[:20]:
                print("      " + u)
        if link_err:
            print("   the link failed without naming a symbol:")
            print("      " + link_err[:160])
        if status == "FAIL":
            rc_total = 1
    return rc_total


def build_one(arch, name, e, out_dir, sim=False):
    """Compile and link one env for one arch. Returns (elf or None, note)."""
    incs = ["-I" + os.path.join(ROOT, p) for p in (".", "src", "include")]
    base = [arch["cc"], "-std=c11", "-Os", "-w", "-ffreestanding", "-fno-builtin", "-c"] + arch["march"]
    _, defs = harness._flag_split(e["flags"])
    srcs = env_sources(e, arch, out_dir, sim=sim)
    objs, bad = compile_all(base, defs, incs, srcs, out_dir, name)
    if bad:
        return None, "%d TU(s) will not compile: %s" % (len(bad), bad[0][1][:120])
    linked, undef, err = link(arch, objs, out_dir, name)
    if not linked:
        return None, ("undefined: " + ", ".join(undef[:6])) if undef else err
    return os.path.join(out_dir, name + ".elf"), ""


def flash_image(arch, elf, out_dir, name, mb=None):
    """Wrap the ELF as an ESP image and pad it into a flash file. Returns (path, note).

    The machine boots the way the part does: the ROM reads the second-stage image from flash offset
    0 and loads its segments. It does NOT start at the ELF entry, which is why -kernel alone leaves
    the CPU at the ROM vector 0x40000000. There is no IDF bootloader here and no partition table -
    the image the ROM finds at offset 0 is ours, so it is loaded and jumped to directly.
    """
    esptool = os.path.join(PIO_PKGS, "tool-esptoolpy", "esptool.py")
    if not os.path.isfile(esptool):
        return None, "no esptool at " + esptool
    app = os.path.join(out_dir, name + ".app.bin")
    r = subprocess.run([sys.executable, esptool, "--chip", arch["machine"], "elf2image",
                        "--output", app, elf], capture_output=True, text=True, cwd=ROOT)
    if r.returncode != 0 or not os.path.isfile(app):
        return None, (r.stderr or r.stdout).strip().split("\n")[-1][:160]
    size = (mb or arch.get("flash_mb", 4)) * 1024 * 1024
    with open(app, "rb") as f:
        img = f.read()
    if len(img) > size:
        return None, "image %d bytes does not fit %d MB" % (len(img), size // (1024 * 1024))
    flash = os.path.join(out_dir, name + ".flash.bin")
    with open(flash, "wb") as f:
        f.write(img)
        f.write(b"\xff" * (size - len(img)))
    return flash, ""


def cmd_sim(a):
    """Boot an env's image on the real SoC model and report what it says over UART.

    `build` proves the symbols resolve. This proves the image runs: the reset path reached main, the
    regions the linker script named are memory the part actually has, and .data survived the copy.
    Nothing here is a stand-in - the machine is Espressif's own model of the chip, with its AES, SHA,
    RSA and RNG blocks, which is the one question a host build can never answer.
    """
    arch = BARE_ARCH[a.arch]
    if "qemu" not in arch:
        print("%s is a shape, not a part: no machine to boot it on. Try: %s" %
              (a.arch, ", ".join(sorted(k for k in BARE_ARCH if "qemu" in BARE_ARCH[k]))))
        return 2
    if not os.path.isfile(arch["cc"]):
        print("no %s toolchain at %s" % (a.arch, arch["cc"]))
        return 3
    if not os.path.isfile(arch["qemu"]):
        print("no Espressif QEMU at %s\n"
              "install it with (from PowerShell, not MSys - idf_tools.py refuses to run there):\n"
              "  $env:IDF_PATH = \"$env:USERPROFILE\\.platformio\\packages\\framework-espidf\"\n"
              "  python \"$env:IDF_PATH\\tools\\idf_tools.py\" --non-interactive install "
              "qemu-xtensa qemu-riscv32" % arch["qemu"])
        return 3

    envs = harness.parse_ini_envs(harness.INI)
    names = a.envs or ["native_protostr"]
    out_dir = os.path.join(ROOT, ".pio", "bare", a.arch)
    os.makedirs(out_dir, exist_ok=True)

    rc = 0
    for i, name in enumerate(names, 1):
        e = envs.get(name)
        if not e:
            print("[%d/%d] %-30s UNKNOWN ENV" % (i, len(names), name))
            rc = 1
            continue
        elf, note = build_one(arch, name, e, out_dir, sim=True)
        if not elf:
            print("[%d/%d] %-30s NO IMAGE   %s" % (i, len(names), name, note))
            rc = 1
            continue
        flash, note = flash_image(arch, elf, out_dir, name)
        if not flash:
            print("[%d/%d] %-30s NO FLASH IMAGE   %s" % (i, len(names), name, note))
            rc = 1
            continue
        # The guest's UART goes to a file, not to stdio: -nographic multiplexes QEMU's own monitor
        # onto stdio, so reading stdout returns the monitor prompt rather than anything the image
        # printed. -display none plus -monitor none leaves the serial line as the only output.
        uart = os.path.join(out_dir, name + ".uart")
        if os.path.isfile(uart):
            os.remove(uart)
        cmd = [arch["qemu"], "-machine", arch["machine"], "-display", "none", "-monitor", "none",
               "-no-reboot", "-drive", "file=%s,if=mtd,format=raw" % flash, "-serial", "file:" + uart]
        try:
            subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT, timeout=a.timeout)
        except subprocess.TimeoutExpired:
            pass  # the image ends in a spin, so the timeout IS the normal path
        out = ""
        if os.path.isfile(uart):
            with open(uart, "r", encoding="utf-8", errors="replace") as fh:
                out = fh.read()
        ok = "PROTOCORE-SIM done" in out
        print("[%d/%d] %-30s %s" % (i, len(names), name, "RUNS" if ok else "NO OUTPUT"))
        for ln in out.splitlines():
            if ln.strip():
                print("   " + ln.rstrip())
        if not ok:
            rc = 1
    return rc


def cmd_arches(a):
    """The cross targets, and whether each one's toolchain is on disk."""
    for name in sorted(BARE_ARCH):
        arch = BARE_ARCH[name]
        have = "installed" if os.path.isfile(arch["cc"]) else "MISSING"
        print("%-12s %-10s %s" % (name, have, " ".join(arch["march"])))
        print("             %s" % arch["why"])
        print("             cc %s" % arch["cc"])
        print("             ld %s" % os.path.relpath(arch["ld"], ROOT).replace("\\", "/"))
    return 0


def brief_of(path):
    """A file's @brief line, which is what it says it supplies."""
    full = os.path.join(ROOT, path)
    if not os.path.isfile(full):
        return "MISSING"
    with open(full, "r", encoding="utf-8", errors="replace") as fh:
        for ln in fh:
            if "@brief" in ln:
                return ln.split("@brief", 1)[1].strip()
    return ""


def cmd_runtime(a):
    """The files every image carries under the env, and what each says it supplies."""
    for p in BARE_RUNTIME:
        print("%-42s %s" % (p, brief_of(p)))
    print("\nplus the arch's own reset path:")
    for name in sorted(set(BARE_ARCH[k]["startup"] for k in BARE_ARCH)):
        print("%-42s %s" % (name, brief_of(name)))
    return 0


def _subcommands(parser):
    """{name: subparser} for a parser's one subparser group."""
    for action in parser._actions:
        if isinstance(action, argparse._SubParsersAction):
            return action.choices
    return {}


def cmd_help(a):
    """Every command's own help in one call, or one command's.

    `-h` prints usage one level at a time, so reading the whole surface means invoking it once per
    subcommand. This prints all of it.
    """
    subs = _subcommands(build_parser())
    if a.command:
        if a.command not in subs:
            print("no such command: %s (try `bare.py help`)" % a.command, file=sys.stderr)
            return 2
        subs[a.command].print_help()
        return 0
    print(__doc__.strip())
    for name in sorted(subs):
        print("\n" + "-" * 78)
        print("### bare.py %s" % name)
        print(subs[name].format_help().strip())
    return 0


def build_parser():
    ap = argparse.ArgumentParser(prog="bare.py", description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("build", help="compile and link each env for a cross target")
    p.add_argument("envs", nargs="*", help="envs to build; default is every env that has tests")
    p.add_argument("--arch", choices=sorted(BARE_ARCH), default="cortex-m")
    p.set_defaults(fn=cmd_build)

    p = sub.add_parser("sim", help="boot an env's image on the real SoC model under Espressif QEMU")
    p.add_argument("envs", nargs="*", help="envs to boot; default native_protostr")
    p.add_argument("--arch", choices=sorted(k for k in BARE_ARCH if "qemu" in BARE_ARCH[k]), default="esp32c3")
    p.add_argument("--timeout", type=float, default=10.0, help="seconds to let it run before reading the UART")
    p.set_defaults(fn=cmd_sim)

    sub.add_parser("arches", help="the cross targets and their toolchains").set_defaults(fn=cmd_arches)
    sub.add_parser("runtime", help="the files the image carries under the env").set_defaults(fn=cmd_runtime)

    p = sub.add_parser("help", help="the whole surface, or one command's")
    p.add_argument("command", nargs="?")
    p.set_defaults(fn=cmd_help)
    return ap


def main(argv=None):
    a = build_parser().parse_args(argv)
    return a.fn(a)


if __name__ == "__main__":
    sys.exit(main())
