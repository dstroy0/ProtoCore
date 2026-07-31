#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generate the hardware module table and API quick-reference in HARDWARE_HOOKUP.md.

The hand-written version documented 32 of 77 hardware-touching modules and invented
per-module pinouts the library does not impose: every driver takes its wiring through
the API (an I2C address, explicit rx/tx pins, or a caller-supplied bus struct with
callbacks), and six are pure codecs with no bring-up call at all. So the only per-module
facts worth stating are mechanical, and mechanical facts should be derived.

For each module under the hardware-touching groups this emits:
  * how it attaches   - inferred from the bring-up signature and includes
  * its bring-up call - the actual signature from the header
  * its feature flag  - PC_ENABLE_<MODULE>, when the config defines one

Wiring CONCEPTS (transceivers, termination, 3.3 V, grounding) stay hand-written in the
prose above this block: those are taught once per bus type, not once per module.

Usage:  python ci_tooling/generate/gen_hardware_ref.py [--check]
"""

import os
import re
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
import doc_region as dr  # noqa: E402  (path set above)
import feature_taxonomy as tax  # noqa: E402  (path set above)

ROOT = dr.repo_root(__file__)
DOC = os.path.join(ROOT, "docs/HARDWARE_HOOKUP.md")
REGION = dr.Region(DOC, "HARDWARE TABLE", dr.tool_id(__file__))

HW_GROUPS = ["peripherals", "instrumentation", "radio", "fieldbus", "timing_position"]


def sh(*a):
    return subprocess.run(a, cwd=ROOT, capture_output=True, text=True).stdout


def read(p):
    try:
        return open(p, encoding="utf-8", errors="replace").read()
    except OSError:
        return ""


def classify(text, sig):
    """How does this module reach its device? Inferred, never assumed."""
    if re.search(r"peripherals/i2c\.h", text):
        return "I2C"
    if re.search(r"\bint\s+rx_pin\b|\brx_pin\s*,", sig or ""):
        return "UART"
    if re.search(r"struct\s+\w*_bus\b|_bus\s*\*", text):
        return "SPI (caller-supplied bus)"
    if re.search(r"\btwai\b|\bCAN\b|can_frame", text):
        return "CAN"
    if not sig:
        return "codec only - caller owns the link"
    return "caller-supplied link"


def main() -> int:
    cfg = read(os.path.join(ROOT, "src/protocore_config.h"))
    flags = set(re.findall(r"#\s*(?:define|ifndef)\s+(PC_ENABLE_[A-Z0-9_]+)", cfg))

    # Group by the SAME taxonomy the configurator and FEATURES.md use - OSI layer for
    # non-application features, functional category for L7 - rather than by src/
    # directory. The directories are a code layout; the taxonomy is how the project
    # talks about its features everywhere else, and two competing groupings would be
    # a worse answer than either alone. Bridged flag -> feature name -> group.
    flag_to_group = {}
    try:
        for feat in tax.parse_features():
            if feat.get("flag"):
                flag_to_group[feat["flag"].strip("` ")] = tax.group_of(feat["name"])
    except (OSError, AttributeError) as exc:      # taxonomy unavailable: fall back
        print(f"  note: taxonomy unavailable ({exc}); grouping by directory",
              file=sys.stderr)

    rows = []
    unplaced = []
    for grp in HW_GROUPS:
        base = os.path.join(ROOT, "src/services", grp)
        if not os.path.isdir(base):
            continue
        for mod in sorted(os.listdir(base)):
            d = os.path.join(base, mod)
            if not os.path.isdir(d):
                continue
            text = "".join(read(os.path.join(d, f)) for f in sorted(os.listdir(d)))
            # Signatures come from the module's own HEADERS only. Scanning .cpp too
            # picked up call SITES with real arguments (pc_presence_core_init(c, ...))
            # and, for I2C parts, the shared pc_i2c_begin() bus call rather than the
            # module's own entry point.
            hdrs = "".join(read(os.path.join(d, f)) for f in sorted(os.listdir(d))
                           if f.endswith(".h"))
            # A bring-up call, not a frame builder: `pc_canopen_build_sdo_download_init`
            # ends in "init" but constructs a CAN frame, it does not open a link.
            NOT_BRINGUP = re.compile(r"_(build|encode|decode|parse|make)_")
            sig = ""
            for pat in (r"\b(pc_" + re.escape(mod) + r"_[a-z0-9_]*(?:begin|init)\([^;)]*\))\s*;",
                        r"\b(pc_[a-z0-9_]*(?:begin|init)\([^;)]*\))\s*;"):
                for m in re.finditer(pat, hdrs, re.S):
                    cand = m.group(1)
                    if NOT_BRINGUP.search(cand):
                        continue
                    # a shared-bus helper is not this module's own bring-up call
                    if not cand.startswith("pc_" + mod.split("_")[0]):
                        continue
                    # signatures wrap across lines in the header; a newline inside a
                    # markdown table cell breaks the row
                    sig = re.sub(r"\s+", " ", cand).strip()
                    break
                if sig:
                    break
            # A directory is NOT one feature. `gnss/` holds ntrip_caster + rtcm3 +
            # gnss_survey, and `ntp_service/` is gated by PC_ENABLE_NTP. So take the
            # flags the module's own sources actually reference and prefer one the
            # taxonomy knows, rather than assuming PC_ENABLE_<DIRNAME>.
            guess = f"PC_ENABLE_{mod.upper()}"
            used = sorted(set(re.findall(r"\bPC_ENABLE_[A-Z0-9_]+\b", text)))
            cands = ([guess] if guess in flags else []) + \
                    [f for f in used if f in flag_to_group] + \
                    [f for f in used if f in flags]
            flag = cands[0] if cands else (guess if guess in flags else "")
            group = flag_to_group.get(flag)
            if group is None:
                # FEATURES.md is the source of truth for the feature grid, so a module
                # that cannot be placed means its entry there is absent or malformed.
                # Report it rather than hiding it behind a directory-name fallback.
                unplaced.append((f"src/services/{grp}/{mod}",
                                 flag or f"(no PC_ENABLE_* found; expected {guess})"))
                group = grp.replace("_", " ")
            rows.append((group, mod, classify(text, sig), sig, flag))

    if unplaced:
        print(f"  FEATURES.md gap: {len(unplaced)} hardware module(s) have no usable entry", file=sys.stderr)
        for path, why in unplaced:
            print(f"      {path}  ->  {why}", file=sys.stderr)

    out = []
    out.append(f"**{len(rows)} modules** attach to something physical. Every one takes its wiring"
               " **through the API** -")
    out.append("an I2C address, explicit pins, or a caller-supplied bus struct - so the library"
               " never dictates a")
    out.append("pinout and none is documented here. Pure codecs have no bring-up call at all: you"
               " own the link.")
    out.append("")
    # render in the taxonomy's own order, then any directory-fallback groups
    try:
        order = [g for g in tax.group_order()]
    except AttributeError:
        order = []
    seen = {r[0] for r in rows}
    order = [g for g in order if g in seen] + sorted(seen - set(order))

    for grp in order:
        sub = [r for r in rows if r[0] == grp]
        if not sub:
            continue
        out.append(f"### {grp}")
        out.append("")
        out.append("| Module | Attaches via | Bring-up call | Feature flag |")
        out.append("| ------ | ------------ | ------------- | ------------ |")
        for _, mod, how, sig, flag in sub:
            call = f"`{sig}`" if sig else "_none (pure codec)_"
            out.append(f"| `{mod}` | {how} | {call} | {'`' + flag + '`' if flag else '-'} |")
        out.append("")

    rc = dr.apply(DOC, {REGION: "\n".join(out)}, check="--check" in sys.argv)
    if rc == 0 and "--check" not in sys.argv:
        mapped = sum(1 for r in rows if r[0] in set(order) and r[0] in flag_to_group.values())
        print(f"  {len(rows)} modules across {len({r[0] for r in rows})} groups "
              f"({mapped} bridged to the feature taxonomy)")
    return rc


if __name__ == "__main__":
    sys.exit(main())
