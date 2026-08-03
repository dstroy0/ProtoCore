#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generate the source-derived reference sections of docs/README.md from truth.

Several docs/README.md sections used to be hand-maintained and silently drifted
from the code (wrong defaults, missing flags, a stale file layout). This script
regenerates them from their real sources so they cannot drift again, writing each
into a marked region:

  FEATURE-FLAGS    every PC_ENABLE_* flag + default + one line   (protocore_config.h)
  CONFIG-OVERRIDES every tunable #define constant + default + desc  (protocore_config.h)
  SOURCE-TREE      an ASCII tree of every library file              (src/)
  BUILD-FOOTPRINT  measured flash/RAM per feature                   (docs/footprints.json)

Run from the repo root:
    python ci_tooling/generate/gen_readme_sections.py            # rewrite the regions
    python ci_tooling/generate/gen_readme_sections.py --check    # CI: fail if stale
"""

import json
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
import doc_region as dr  # noqa: E402  (path set above)

ROOT = dr.repo_root(__file__)
CONFIG_H = os.path.join(ROOT, "src", "protocore_config.h")
SRC_DIR = os.path.join(ROOT, "src")
FOOTPRINTS = os.path.join(ROOT, "docs", "footprints.json")
README = os.path.join(ROOT, "docs", "README.md")

# Constants that are structural, not user-tunable knobs - skip them in the tables.
SKIP_CONSTS = {"PROTOCORE_CONFIG_H"}


# --------------------------------------------------------------------------- parsing


def _doc_brief(cfg, name):
    """First-sentence @brief of the Doxygen comment right before `#ifndef <name>`."""
    m = re.search(r"/\*\*((?:(?!\*/).)*)\*/\s*\n#ifndef " + re.escape(name) + r"\b", cfg, re.S)
    if not m:
        return ""
    body = []
    for ln in m.group(1).splitlines():
        ln = re.sub(r"^\s*\*\s?", "", ln.strip())
        body.append(ln)
    text = " ".join(body).replace("@brief ", "").strip()
    text = re.sub(r"\s*\((PC_[A-Z0-9_]+)\)", "", text)  # drop the "(FLAG)" tag
    text = re.sub(r"\s+", " ", text)
    # first sentence only, keep it terse
    m2 = re.match(r"(.+?[.!])(?:\s|$)", text)
    return (m2.group(1) if m2 else text).strip()


def parse_config():
    """Return (flags, consts): each a list of (name, default, description)."""
    cfg = open(CONFIG_H, encoding="utf-8").read()
    guards = re.findall(r"^#ifndef ([A-Z][A-Z0-9_]+)\s*\n#define \1(?:\s+(.+?))?\s*$", cfg, re.M)
    flags, consts = [], []
    for name, val in guards:
        if name in SKIP_CONSTS:
            continue
        val = (val or "").strip()
        desc = _doc_brief(cfg, name)
        if name.startswith("PC_ENABLE_"):
            flags.append((name, val, desc))
        elif re.fullmatch(r"[0-9]+[uUlL]*", val) or re.fullmatch(r"0x[0-9A-Fa-f]+[uUlL]*", val):
            consts.append((name, val, desc))
    return flags, consts


# --------------------------------------------------------------------------- renderers


def _esc(s):
    return s.replace("|", "\\|")


def render_flags(flags):
    out = ["| Flag | Default | Description |", "| :--- | :-----: | :---------- |"]
    for name, val, desc in sorted(flags):
        out.append(f"| `{name}` | `{val or '-'}` | {_esc(desc)} |")
    return "\n".join(out)


def render_consts(consts):
    out = ["| Constant | Default | Description |", "| :------- | :-----: | :---------- |"]
    for name, val, desc in sorted(consts):
        out.append(f"| `{name}` | `{val}` | {_esc(desc)} |")
    return "\n".join(out)


# Directories of generated web-asset blobs (favicon SVGs, theme CSS): not library code,
# so show the node with a file count instead of listing hundreds of generated names.
COLLAPSE_DIRS = {"favicons", "generated", "themes"}


def _child_pair(d):
    """If dir `d` holds exactly one .h + one .c/.cpp and nothing else, return (h, src);
    else None. These one-file-pair service folders collapse to the service name."""
    entries = [e for e in os.listdir(d) if not e.startswith(".")]
    if any(os.path.isdir(os.path.join(d, e)) for e in entries):
        return None
    hs = [e for e in entries if e.endswith(".h")]
    cs = [e for e in entries if e.endswith((".c", ".cpp"))]
    if len(entries) == 2 and len(hs) == 1 and len(cs) == 1:
        return hs[0], cs[0]
    return None


def render_tree():
    """ASCII tree of every library file under src/ (dirs first, then files, sorted)."""
    lines = ["src/"]

    def walk(rel, prefix):
        d = os.path.join(SRC_DIR, rel)
        entries = sorted(os.listdir(d), key=lambda e: (not os.path.isdir(os.path.join(d, e)), e.lower()))
        entries = [e for e in entries if not e.startswith(".")]
        for i, e in enumerate(entries):
            last = i == len(entries) - 1
            conn = "└── " if last else "├── "
            p = os.path.join(d, e)
            if os.path.isdir(p):
                if e in COLLAPSE_DIRS:
                    cnt = sum(len(fs) for _, _, fs in os.walk(p))
                    lines.append(f"{prefix}{conn}{e}/  ({cnt} generated files)")
                    continue
                pair = _child_pair(p)
                if pair:
                    lines.append(f"{prefix}{conn}{e}/  ({pair[0]}, {pair[1]})")
                else:
                    lines.append(f"{prefix}{conn}{e}/")
                    walk(os.path.join(rel, e), prefix + ("    " if last else "│   "))
            else:
                lines.append(f"{prefix}{conn}{e}")

    walk("", "")
    return "\n".join(lines)


def render_footprint():
    data = json.load(open(FOOTPRINTS, encoding="utf-8"))
    rows = sorted(data.items(), key=lambda kv: kv[1].get("flash_bytes", 0))
    out = [
        "Measured on `esp32dev` from each feature's isolated example (one feature enabled over the",
        "base server). Flash is the program image; RAM is static `.data + .bss`. Regenerated by the",
        "Feature Tables workflow from `docs/footprints.json`.",
        "",
        "| Feature | Example | Flash (bytes) | Static RAM (bytes) |",
        "| :------ | :------ | ------------: | -----------------: |",
    ]
    for name, d in rows:
        out.append(f"| `{name}` | `{d.get('example','-')}` | {d.get('flash_bytes',0):,} | {d.get('ram_bytes',0):,} |")
    return "\n".join(out)


# --------------------------------------------------------------------------- injection

REGIONS = {
    "FEATURE-FLAGS": lambda f, c: render_flags(f),
    "CONFIG-OVERRIDES": lambda f, c: render_consts(c),
    "SOURCE-TREE": lambda f, c: "```text\n" + render_tree() + "\n```",
    "BUILD-FOOTPRINT": lambda f, c: render_footprint(),
}


def main():
    check = "--check" in sys.argv[1:]
    flags, consts = parse_config()
    tool = dr.tool_id(__file__)
    bodies = {dr.Region(README, key, tool): fn(flags, consts) for key, fn in REGIONS.items()}
    rc = dr.apply(README, bodies, check=check)
    if rc == 0 and not check:
        print(f"  {len(flags)} flags, {len(consts)} constants")
    sys.exit(rc)


if __name__ == "__main__":
    main()
