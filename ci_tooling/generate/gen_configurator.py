#!/usr/bin/env python3
# Copyright (C) 2026 Douglas Quigg (dstroy0) <dquigg123@gmail.com>
# SPDX-License-Identifier: AGPL-3.0-or-later
"""Generate the interactive build configurator (docs/configurator.html).

Single source of truth: src/protocore_config.h. This parses that header for
every user-toggleable feature flag (`#ifndef PC_ENABLE_X / #define ... 0|1`),
every override-able tuning knob (`#ifndef KNOB / #define KNOB value`), the section
titles the file already groups them under, and the hard dependencies encoded as
`#if PC_ENABLE_child && !PC_ENABLE_parent` guards. It emits one self-contained
HTML page (inline CSS/JS, data embedded as JSON) that lets you tick features, tune
their knobs, and copy out either a platformio.ini `build_flags` block or a set of
`#define`s - emitting only the values that differ from the library defaults.

Because it is generated, the configurator never drifts from the real config:
re-run it whenever protocore_config.h changes.

    python -m ci_tooling.generate.gen_configurator          # write docs/configurator.html
    python -m ci_tooling.generate.gen_configurator check    # CI gate: fail if stale
"""

import json
import os
import re
import sys


from ci_tooling.lib import feature_taxonomy as tax

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, "..", ".."))
CONFIG = os.path.join(ROOT, "src", "protocore_config.h")
OUT = os.path.join(ROOT, "docs", "configurator.html")
CORE_GROUP = "Core / always-on knobs"

DASH = re.compile(r"^//\s*-{20,}\s*$")
IFNDEF = re.compile(r"^#ifndef\s+([A-Za-z_][A-Za-z0-9_]*)\s*$")
DEFINE = re.compile(r"^#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(.+?)\s*$")
IF_GATE = re.compile(r"^#if\s+PC_ENABLE_([A-Za-z0-9_]+)\s*$")
ANY_IF = re.compile(r"^#\s*(if|ifdef|ifndef)\b")
ENDIF = re.compile(r"^#\s*endif\b")
DEP = re.compile(r"#if\s+PC_ENABLE_([A-Za-z0-9_]+)\s*&&\s*!\s*PC_ENABLE_([A-Za-z0-9_]+)")
REQ = re.compile(r"requires\s+PC_ENABLE_([A-Za-z0-9_]+)")
TRAILING = re.compile(r"///<\s*(.*)$")


def clean_brief(text):
    """First sentence of a doc blob, whitespace-collapsed."""
    text = re.sub(r"\s+", " ", text).strip()
    text = re.sub(r"^@brief\s*", "", text)
    # Cut at the first sentence end that is followed by a space + capital / paren.
    m = re.search(r"\.\s", text)
    if m:
        text = text[: m.start() + 1]
    return text.strip()


def title_of(line):
    t = line.lstrip("/").strip()
    t = re.sub(r"\s*\(PC_ENABLE_[^)]*\)", "", t)
    t = t.split(" - ")[0].strip() if " - " in t else t
    return t


def value_kind(raw):
    v = raw.strip().rstrip("uUlL")
    if re.match(r"^0[xX][0-9A-Fa-f]+$", v):
        return "hex"
    if re.match(r"^-?[0-9]+$", v):
        return "int"
    return "other"


def feature_suffix(name):
    return name[len("PC_ENABLE_") :]


def parse(path):
    with open(path, "r", encoding="utf-8") as f:
        lines = f.read().splitlines()

    features = {}  # NAME -> {name, label, default, desc, group}
    knobs = {}  # NAME -> {name, default, kind, desc, group, owner}
    order_feat = []
    order_knob = []
    group = "General"
    gate_stack = []  # feature suffix for each open `#if PC_ENABLE_X`, else None
    doc = []  # pending block-comment lines

    i = 0
    n = len(lines)
    while i < n:
        line = lines[i]
        stripped = line.strip()

        # Section title: dashline / title / dashline.
        if DASH.match(stripped) and i + 2 < n and DASH.match(lines[i + 2].strip()):
            t = title_of(lines[i + 1].strip())
            if t and not DASH.match(("// " + t)):
                group = t
            doc = []
            i += 3
            continue

        # Block comment capture (/** ... */ or /* ... */).
        if stripped.startswith("/*"):
            buf = []
            while i < n:
                s = lines[i].strip()
                buf.append(re.sub(r"^/\*+|^\*+/?|\*+/$", "", s).strip("* ").strip())
                if "*/" in lines[i]:
                    break
                i += 1
            doc = [b for b in buf if b]
            i += 1
            continue

        # Standalone // comment: remember as a light doc hint.
        if stripped.startswith("//"):
            doc = [stripped.lstrip("/").strip()]
            i += 1
            continue

        # Conditional nesting: track `#if PC_ENABLE_X` gates for knob ownership.
        mg = IF_GATE.match(stripped)
        if mg:
            gate_stack.append(mg.group(1))
            i += 1
            continue
        if ANY_IF.match(stripped) and not IFNDEF.match(stripped):
            gate_stack.append(None)
            i += 1
            continue

        # `#ifndef NAME` + `#define NAME value` = an override-able default.
        mi = IFNDEF.match(stripped)
        if mi and i + 1 < n:
            md = DEFINE.match(lines[i + 1].strip())
            if md and md.group(1) == mi.group(1):
                name = md.group(1)
                rhs = md.group(2)
                tc = TRAILING.search(rhs)
                trailing = tc.group(1).strip() if tc else ""
                value = re.sub(r"\s*///<.*$", "", rhs).strip()
                desc = clean_brief(" ".join(doc)) if doc else trailing
                if not desc:
                    desc = trailing
                if name.startswith("PC_ENABLE_"):
                    if name not in features:
                        full = " ".join(doc)
                        me = feature_suffix(name)
                        prose = [p for p in REQ.findall(full) if p != me]
                        features[name] = {
                            "name": name,
                            "label": me,
                            "default": 1 if value.strip() in ("1", "1u") else 0,
                            "desc": desc,
                            "group": group,
                            "prose_req": prose,
                        }
                        order_feat.append(name)
                else:
                    if name not in knobs:
                        owner = next((g for g in reversed(gate_stack) if g), None)
                        knobs[name] = {
                            "name": name,
                            "default": value,
                            "kind": value_kind(value),
                            "desc": desc or trailing,
                            "group": group,
                            "owner": owner,  # feature suffix or None (resolved later)
                        }
                        order_knob.append(name)
                doc = []
                gate_stack.append(None)  # the ifndef guard itself
                i += 1
                continue

        if ENDIF.match(stripped):
            if gate_stack:
                gate_stack.pop()
            doc = []
            i += 1
            continue

        if stripped and not stripped.startswith("#"):
            doc = []
        i += 1

    # Dependencies: child requires parent(s).
    deps = {}
    text = "\n".join(lines)
    for child, parent in DEP.findall(text):
        deps.setdefault(child, [])
        if parent not in deps[child]:
            deps[child].append(parent)

    # Resolve knob ownership: explicit gate, else longest feature-suffix prefix match.
    suffixes = sorted((f["label"] for f in features.values()), key=len, reverse=True)
    for k in knobs.values():
        if k["owner"] and ("PC_ENABLE_" + k["owner"]) in features:
            continue
        stem = k["name"][len("PC_") :] if k["name"].startswith("PC_") else k["name"]
        owner = None
        for suf in suffixes:
            if stem == suf or stem.startswith(suf + "_"):
                owner = suf
                break
        k["owner"] = owner  # None => a core / always-on knob

    return {
        "features": features,
        "order_feat": order_feat,
        "knobs": knobs,
        "order_knob": order_knob,
        "deps": deps,
    }


def taxonomy_maps():
    """Map each PC_ENABLE_* suffix to its curated group label and a one-sentence
    description, both from FEATURES.md via the shared taxonomy. This is the SAME
    grouping the README feature tables use, so the two artifacts never disagree
    (and it replaces the noisy, drift-prone section-comment grouping)."""
    group = {}
    desc = {}
    for e in tax.parse_features():
        flag = e["flag"]
        if not flag or not flag.startswith("PC_ENABLE_"):
            continue
        suf = flag[len("PC_ENABLE_") :]
        group[suf] = tax.group_of(e["name"])
        if e["desc"]:
            desc[suf] = clean_brief(e["desc"])
    return group, desc


def resolve_group(suf, tax_group):
    """A feature's group: its own FEATURES.md group, else the group of the longest
    enclosing flag family (e.g. SSH_SNTRUP761 inherits SSH's), else the core bucket."""
    if suf in tax_group:
        return tax_group[suf]
    best = None
    for other, grp in tax_group.items():
        if suf.startswith(other + "_") and (best is None or len(other) > len(best[0])):
            best = (other, grp)
    return best[1] if best else CORE_GROUP


def build_model(parsed):
    features = parsed["features"]
    knobs = parsed["knobs"]
    tax_group, tax_desc = taxonomy_maps()

    # Attach each knob to its owning feature; the rest are core knobs.
    by_owner = {}
    core = []
    for name in parsed["order_knob"]:
        k = knobs[name]
        entry = {"name": name, "default": k["default"], "kind": k["kind"], "desc": k["desc"]}
        if k["owner"] and ("PC_ENABLE_" + k["owner"]) in features:
            by_owner.setdefault(k["owner"], []).append(entry)
        else:
            core.append(entry)

    have = set(f["label"] for f in features.values())
    feat_list = []
    for name in parsed["order_feat"]:
        f = features[name]
        suf = f["label"]
        # Union of machine-checked (#if child && !parent) and prose ("requires ...") deps,
        # keeping only parents that are real toggleable features.
        req = []
        for p in parsed["deps"].get(suf, []) + f.get("prose_req", []):
            if p in have and p != suf and p not in req:
                req.append(p)
        feat_list.append(
            {
                "name": name,
                "suffix": suf,
                "default": f["default"],
                # Prefer the curated FEATURES.md one-liner; fall back to the header brief.
                "desc": tax_desc.get(suf) or f["desc"],
                "group": resolve_group(suf, tax_group),
                "knobs": by_owner.get(suf, []),
                "requires": req,
            }
        )

    # Group render order follows the shared taxonomy (layers, then L7 categories); any
    # group that ended up empty is dropped, and the core-knob bucket sorts last.
    present = {f["group"] for f in feat_list}
    groups = [g for g in tax.group_order() if g in present]
    groups += [g for g in present if g not in groups and g != CORE_GROUP]
    if any(f["group"] == CORE_GROUP for f in feat_list):
        groups.append(CORE_GROUP)

    return {"groups": groups, "features": feat_list, "core": core, "coreGroup": CORE_GROUP}


PAGE = r"""<!-- GENERATED by ci_tooling/generate/gen_configurator.py - do not edit by hand. -->
<!-- Edit src/protocore_config.h and re-run the generator. -->
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<title>ProtoCore - build configurator</title>
<style>
  /* Light is the default; dark applies from the OS preference, and the header toggle
     forces either theme via [data-theme] (which beats the media query both ways). */
  :root {
    color-scheme: light dark;
    --bg: #f4f6f9; --panel: #ffffff; --panel2: #f0f3f7; --sink: #f7f9fb;
    --line: #d9dee5; --line2: #e6eaf0;
    --ink: #1f2328; --dim: #636c76; --faint: #98a1ab;
    --accent: #0969da; --accent-ink: #ffffff; --accent-soft: #ddeeff;
    --ok: #1a7f37; --ok-soft: #dafbe1; --warn: #9a6700; --warn-soft: #fff6d5;
    --chip: #eef1f5; --shadow: 0 1px 2px rgba(31,35,40,.06), 0 1px 3px rgba(31,35,40,.04);
    --radius: 10px;
  }
  @media (prefers-color-scheme: dark) {
    :root:not([data-theme="light"]) {
      --bg: #0d1117; --panel: #151b23; --panel2: #1a212b; --sink: #10151d;
      --line: #2a313c; --line2: #222932;
      --ink: #e6edf3; --dim: #9198a1; --faint: #6b7481;
      --accent: #4493f8; --accent-ink: #04101f; --accent-soft: #12325e;
      --ok: #3fb950; --ok-soft: #123a1c; --warn: #d29922; --warn-soft: #3b2f10;
      --chip: #20272f; --shadow: 0 1px 2px rgba(0,0,0,.4);
    }
  }
  :root[data-theme="dark"] {
    --bg: #0d1117; --panel: #151b23; --panel2: #1a212b; --sink: #10151d;
    --line: #2a313c; --line2: #222932;
    --ink: #e6edf3; --dim: #9198a1; --faint: #6b7481;
    --accent: #4493f8; --accent-ink: #04101f; --accent-soft: #12325e;
    --ok: #3fb950; --ok-soft: #123a1c; --warn: #d29922; --warn-soft: #3b2f10;
    --chip: #20272f; --shadow: 0 1px 2px rgba(0,0,0,.4);
  }
  * { box-sizing: border-box; }
  html, body { height: 100%; }
  body {
    margin: 0; background: var(--bg); color: var(--ink);
    font: 14.5px/1.55 -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Helvetica, Arial, sans-serif;
    -webkit-font-smoothing: antialiased;
  }
  code, .mono { font-family: ui-monospace, "SF Mono", Menlo, Consolas, "Liberation Mono", monospace; }
  a { color: var(--accent); }

  /* ---- header ---------------------------------------------------------- */
  header { padding: 20px 24px 0; max-width: 1500px; margin: 0 auto; }
  .brand { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }
  h1 { margin: 0; font-size: 19px; font-weight: 650; letter-spacing: -.01em; }
  .brand .tag { font-size: 12px; color: var(--dim); border: 1px solid var(--line);
    border-radius: 999px; padding: 2px 9px; }
  header .sub { margin: 6px 0 0; color: var(--dim); font-size: 13.5px; }
  details.help { margin: 12px 0 0; }
  details.help > summary { cursor: pointer; color: var(--accent); font-size: 13px;
    list-style: none; display: inline-flex; align-items: center; gap: 6px; user-select: none; }
  details.help > summary::-webkit-details-marker { display: none; }
  details.help > summary::before { content: "›"; transition: transform .15s; font-size: 15px; }
  details.help[open] > summary::before { transform: rotate(90deg); }
  details.help .body { margin: 8px 0 0; padding: 12px 14px; background: var(--panel);
    border: 1px solid var(--line); border-radius: var(--radius); color: var(--dim); font-size: 13px;
    max-width: 92ch; }
  details.help .body b { color: var(--ink); }

  /* ---- toolbar --------------------------------------------------------- */
  .toolbar { position: sticky; top: 0; z-index: 30; background: var(--bg);
    padding: 14px 24px; margin-top: 14px; border-bottom: 1px solid var(--line);
    max-width: 1500px; margin-left: auto; margin-right: auto; }
  .toolrow { display: flex; gap: 8px; align-items: center; flex-wrap: wrap; }
  .search { position: relative; flex: 1 1 260px; min-width: 180px; }
  .search svg { position: absolute; left: 11px; top: 50%; transform: translateY(-50%);
    width: 15px; height: 15px; color: var(--faint); pointer-events: none; }
  input[type=search] { width: 100%; background: var(--panel); color: var(--ink);
    border: 1px solid var(--line); border-radius: 8px; padding: 9px 12px 9px 34px; font-size: 14px; }
  input[type=search]:focus, input:focus, button:focus-visible {
    outline: 2px solid var(--accent); outline-offset: 1px; border-color: var(--accent); }
  .btn { background: var(--panel); color: var(--ink); border: 1px solid var(--line);
    border-radius: 8px; padding: 8px 11px; cursor: pointer; font-size: 13px; white-space: nowrap;
    display: inline-flex; align-items: center; gap: 6px; }
  .btn:hover { background: var(--panel2); border-color: var(--faint); }
  .btn.on { background: var(--accent-soft); border-color: var(--accent); color: var(--accent); }
  .btn.icon { padding: 8px; }
  .btn svg { width: 15px; height: 15px; }
  .sep { width: 1px; align-self: stretch; background: var(--line); margin: 2px 2px; }
  .metrics { color: var(--dim); font-size: 12.5px; margin-left: auto; display: flex; gap: 14px;
    align-items: center; }
  .metrics b { color: var(--ink); font-weight: 600; }
  .metrics .dot { color: var(--ok); }

  /* ---- layout ---------------------------------------------------------- */
  .wrap { display: grid; grid-template-columns: 216px minmax(0,1fr) 400px; gap: 0;
    align-items: start; max-width: 1500px; margin: 0 auto; }
  .rail { position: sticky; top: 61px; align-self: start; max-height: calc(100vh - 61px);
    overflow: auto; padding: 16px 8px 40px 20px; }
  .rail .rlabel { font-size: 11px; text-transform: uppercase; letter-spacing: .07em;
    color: var(--faint); padding: 0 8px 6px; }
  .rail a { display: flex; align-items: center; gap: 8px; padding: 5px 8px; border-radius: 7px;
    color: var(--dim); text-decoration: none; font-size: 12.5px; border-left: 2px solid transparent; }
  .rail a:hover { background: var(--panel2); color: var(--ink); }
  .rail a.active { background: var(--accent-soft); color: var(--accent); border-left-color: var(--accent); }
  .rail a .rn { flex: 1; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }
  .rail a .rc { color: var(--faint); font-size: 11px; font-variant-numeric: tabular-nums; }
  .rail a .ron { width: 6px; height: 6px; border-radius: 50%; background: var(--ok); flex: none; }
  .rail a.empty { opacity: .35; }

  .main { padding: 8px 20px 120px; min-width: 0; }

  /* ---- section (accordion) -------------------------------------------- */
  .sec { margin: 10px 0; border: 1px solid var(--line); border-radius: var(--radius);
    background: var(--panel); overflow: hidden; box-shadow: var(--shadow); }
  .ghead { display: flex; align-items: center; gap: 10px; width: 100%; text-align: left;
    padding: 12px 14px; cursor: pointer; background: none; border: 0; color: inherit; font: inherit; }
  .ghead:hover { background: var(--panel2); }
  .ghead .caret { color: var(--faint); transition: transform .15s; flex: none; width: 14px; }
  .sec.collapsed .caret { transform: rotate(-90deg); }
  .ghead .gname { font-weight: 600; font-size: 14px; flex: 1; }
  .ghead .gcount { color: var(--dim); font-size: 12px; font-variant-numeric: tabular-nums; }
  .ghead .gon { color: var(--ok); font-size: 12px; font-weight: 600; margin-left: 4px; }
  .gbody { border-top: 1px solid var(--line2); }
  .sec.collapsed .gbody { display: none; }

  /* ---- feature row ----------------------------------------------------- */
  .feat { display: grid; grid-template-columns: auto 1fr; gap: 2px 11px;
    padding: 11px 14px; border-top: 1px solid var(--line2); }
  .feat:first-child { border-top: 0; }
  .feat.on { background: color-mix(in srgb, var(--accent-soft) 30%, transparent); }
  .feat.hidden { display: none; }
  .feat > .cbwrap { grid-row: 1 / span 3; padding-top: 1px; }
  .cb { appearance: none; width: 18px; height: 18px; border: 1.5px solid var(--faint);
    border-radius: 5px; background: var(--panel); cursor: pointer; position: relative; }
  .cb:hover { border-color: var(--accent); }
  .cb:checked { background: var(--accent); border-color: var(--accent); }
  .cb:checked::after { content: ""; position: absolute; left: 5px; top: 1.5px; width: 5px; height: 9px;
    border: solid var(--accent-ink); border-width: 0 2px 2px 0; transform: rotate(45deg); }
  .fhead { display: flex; align-items: baseline; gap: 8px; flex-wrap: wrap; cursor: pointer; }
  .fname { font-family: ui-monospace, Menlo, Consolas, monospace; font-size: 13px; font-weight: 600; }
  .fname .pfx { color: var(--faint); font-weight: 400; }
  .chip { font-size: 10.5px; font-weight: 600; padding: 1px 7px; border-radius: 999px;
    letter-spacing: .02em; border: 1px solid transparent; }
  .chip.dflt { color: var(--ok); background: var(--ok-soft); }
  .chip.need { color: var(--warn); background: var(--warn-soft); }
  .chip.tls { color: var(--ok); background: var(--ok-soft); border-color: var(--ok); }
  .chip.mod { color: var(--accent); background: var(--accent-soft); }
  .fdesc { color: var(--dim); font-size: 12.5px; grid-column: 2; }
  .ktoggle { grid-column: 2; justify-self: start; margin-top: 6px; background: none; border: 0;
    color: var(--accent); font-size: 12px; cursor: pointer; padding: 2px 0; display: inline-flex;
    align-items: center; gap: 5px; }
  .ktoggle .caret { transition: transform .15s; }
  .feat.kopen .ktoggle .caret { transform: rotate(90deg); }
  .ktoggle .kmod { color: var(--warn); font-weight: 600; }
  .knobs { grid-column: 2; display: none; margin: 8px 0 2px; padding: 10px 12px;
    background: var(--sink); border: 1px solid var(--line2); border-radius: 8px; }
  .feat.kopen .knobs { display: block; }
  .knob { padding: 7px 0; border-top: 1px dashed var(--line2); }
  .knob:first-child { border-top: 0; padding-top: 0; }
  .krow { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }
  .kname { font-size: 12px; color: var(--ink); flex: 1 1 200px; min-width: 0; word-break: break-all; }
  .knob input { background: var(--panel); color: var(--ink); border: 1px solid var(--line);
    border-radius: 6px; padding: 5px 8px; width: 120px; font-family: ui-monospace, monospace; font-size: 12px; }
  .knob input.changed { border-color: var(--warn); background: var(--warn-soft); }
  .kd { color: var(--dim); font-size: 11.5px; margin-top: 4px; }
  .kd .kdefault { color: var(--accent); cursor: pointer; text-decoration: underline dotted; }
  .empty-note { padding: 24px 14px; color: var(--dim); text-align: center; font-size: 13px; display: none; }

  /* ---- output panel ---------------------------------------------------- */
  .out { position: sticky; top: 61px; align-self: start; height: calc(100vh - 61px);
    display: flex; flex-direction: column; border-left: 1px solid var(--line); background: var(--panel); }
  .out .ohead { padding: 14px 16px 10px; border-bottom: 1px solid var(--line2); }
  .out .otitle { font-size: 12px; text-transform: uppercase; letter-spacing: .06em; color: var(--faint);
    margin: 0 0 8px; }
  .seg { display: inline-flex; border: 1px solid var(--line); border-radius: 8px; overflow: hidden;
    width: 100%; }
  .seg button { border: 0; border-radius: 0; flex: 1; background: var(--panel); color: var(--dim);
    padding: 7px 6px; cursor: pointer; font-size: 12px; border-left: 1px solid var(--line); }
  .seg button:first-child { border-left: 0; }
  .seg button.sel { background: var(--accent); color: var(--accent-ink); font-weight: 600; }
  .out .hint { padding: 8px 16px 0; color: var(--dim); font-size: 12px; }
  textarea { flex: 1; margin: 10px 16px; background: var(--sink); color: var(--ink);
    border: 1px solid var(--line); border-radius: 8px; padding: 12px; resize: none;
    font-family: ui-monospace, Menlo, Consolas, monospace; font-size: 12.5px; line-height: 1.5;
    white-space: pre; overflow: auto; }
  .msg { color: var(--warn); font-size: 12px; padding: 0 16px; min-height: 0; }
  .msg:not(:empty) { padding: 6px 16px; }
  .out .foot { padding: 12px 16px 16px; display: flex; gap: 8px; align-items: center;
    border-top: 1px solid var(--line2); }
  .btn.primary { background: var(--accent); border-color: var(--accent); color: var(--accent-ink);
    font-weight: 600; }
  .btn.primary:hover { filter: brightness(1.05); background: var(--accent); }
  .ocount { color: var(--dim); font-size: 12px; margin-left: auto; }

  /* ---- responsive ------------------------------------------------------ */
  @media (max-width: 1180px) {
    .wrap { grid-template-columns: minmax(0,1fr) 380px; }
    .rail { display: none; }
  }
  @media (max-width: 860px) {
    .wrap { grid-template-columns: 1fr; }
    .out { position: static; height: auto; border-left: 0; border-top: 1px solid var(--line); }
    textarea { min-height: 220px; }
    .metrics { width: 100%; margin-left: 0; }
  }
</style>
</head>
<body>
<header>
  <div class="brand">
    <h1>ProtoCore</h1>
    <span class="tag">build configurator</span>
  </div>
  <p class="sub">Tick the features you need, tune their knobs, and copy out a ready-to-paste build config. Dependencies resolve automatically and only values that differ from the library defaults are emitted.</p>
  <details class="help">
    <summary>How do I apply this?</summary>
    <div class="body">
      <p style="margin:0 0 8px"><b>PlatformIO</b> &mdash; copy the result into your <code>platformio.ini</code> under <code>build_flags</code>.</p>
      <p style="margin:0 0 8px"><b>Arduino IDE</b> &mdash; switch the output to <b>build_opt.h</b>, <b>Download</b> it, and drop it in the same folder as your <code>.ino</code>. That file is the only one the IDE feeds to the separately-compiled library, so a plain <code>#define</code> in the sketch will not turn a feature on.</p>
      <p style="margin:0"><b>#define</b> &mdash; a header of overrides you can <code>#include</code> before the library. Everything here is generated from <code>src/protocore_config.h</code>.</p>
    </div>
  </details>
</header>

<div class="toolbar">
  <div class="toolrow">
    <div class="search">
      <svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.6"><circle cx="7" cy="7" r="5"/><path d="M11 11l3.5 3.5"/></svg>
      <input id="q" type="search" placeholder="Filter features by name or description..." spellcheck="false" />
    </div>
    <button class="btn" id="f-enabled" title="Show only enabled features">Enabled</button>
    <button class="btn" id="f-modified" title="Show only features changed from defaults">Modified</button>
    <span class="sep"></span>
    <button class="btn" id="expand">Expand all</button>
    <button class="btn" id="reset" title="Reset every feature and knob to library defaults">Reset</button>
    <button class="btn icon" id="theme" title="Toggle light / dark" aria-label="Toggle theme"></button>
    <span class="metrics">
      <span><b id="m-feat">0</b> features on</span>
      <span><b id="m-over">0</b> overrides</span>
    </span>
  </div>
</div>

<div class="wrap">
  <nav class="rail" id="rail" aria-label="Feature categories"></nav>
  <main class="main">
    <div id="list"></div>
    <div class="empty-note" id="empty">No features match your filter.</div>
  </main>
  <aside class="out">
    <div class="ohead">
      <p class="otitle">Output</p>
      <div class="seg">
        <button data-fmt="pio" class="sel">platformio.ini</button>
        <button data-fmt="buildopt">build_opt.h</button>
        <button data-fmt="defines">#define</button>
      </div>
    </div>
    <div class="msg" id="msg"></div>
    <textarea id="output" readonly spellcheck="false"></textarea>
    <div class="foot">
      <button class="btn primary" id="copy">Copy</button>
      <button class="btn" id="download">Download</button>
      <span class="ocount" id="ocount"></span>
    </div>
  </aside>
</div>

<script id="model" type="application/json">__DATA__</script>
<script>
const MODEL = JSON.parse(document.getElementById("model").textContent);
const CORE = MODEL.coreGroup;
const state = { on: {}, knob: {}, fmt: "pio", onlyEnabled: false, onlyModified: false,
                collapsed: {}, kopen: {}, theme: null };
const bySuffix = {};
MODEL.features.forEach(f => { bySuffix[f.suffix] = f; state.on[f.suffix] = !!f.default; });

// requires: child suffix -> [parent suffixes]. dependents: parent -> [children].
const dependents = {};
MODEL.features.forEach(f => (f.requires || []).forEach(p => {
  (dependents[p] = dependents[p] || []).push(f.suffix);
}));
const KNOB_DEF = {}, KNOB_OWNER = {};
MODEL.features.forEach(f => f.knobs.forEach(k => { KNOB_DEF[k.name] = k.default; KNOB_OWNER[k.name] = f.suffix; }));
MODEL.core.forEach(k => { KNOB_DEF[k.name] = k.default; KNOB_OWNER[k.name] = null; });
const knobDefault = n => KNOB_DEF[n];
const knobVisible = n => { const o = KNOB_OWNER[n]; return o === null || state.on[o]; };
const knobChanged = n => String(state.knob[n]) !== String(knobDefault(n)) && state.knob[n] !== undefined && state.knob[n] !== "";

function enableWithParents(suf, seen) {
  seen = seen || {}; if (seen[suf]) return; seen[suf] = 1;
  state.on[suf] = true;
  (bySuffix[suf].requires || []).forEach(p => bySuffix[p] && enableWithParents(p, seen));
}
function disableWithDependents(suf, seen) {
  seen = seen || {}; if (seen[suf]) return; seen[suf] = 1;
  state.on[suf] = false;
  (dependents[suf] || []).forEach(c => disableWithDependents(c, seen));
}
function featModified(f) {
  if (state.on[f.suffix] !== !!f.default) return true;
  return f.knobs.some(k => knobVisible(k.name) && knobChanged(k.name));
}
function esc(s) { return String(s).replace(/[&<>"]/g, c => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c])); }
function slug(g) { return "g-" + g.toLowerCase().replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, ""); }

function featsOf(g) {
  if (g === CORE) return [];
  return MODEL.features.filter(f => f.group === g);
}
function onCount(g) { return featsOf(g).filter(f => state.on[f.suffix]).length; }

function knobRow(k) {
  const val = (state.knob[k.name] !== undefined) ? state.knob[k.name] : k.default;
  const changed = String(val) !== String(k.default) ? " changed" : "";
  const desc = k.desc ? '<div class="kd">' + esc(k.desc) + ' &middot; default <span class="kdefault mono" data-def="' + esc(k.name) + '">' + esc(k.default) + '</span></div>' : "";
  return '<div class="knob"><div class="krow"><code class="kname">' + esc(k.name) + '</code>' +
    '<input class="kin' + changed + '" data-knob="' + esc(k.name) + '" value="' + esc(String(val)) + '" spellcheck="false"></div>' +
    desc + '</div>';
}

function featCard(f) {
  const on = state.on[f.suffix];
  const chips = [];
  if (f.default) chips.push('<span class="chip dflt">default on</span>');
  (f.requires || []).forEach(p => {
    const cls = p === "TLS" ? "chip tls" : "chip need";
    chips.push('<span class="' + cls + '">needs ' + esc(p) + '</span>');
  });
  let knobs = "";
  if (f.knobs.length) {
    const nmod = f.knobs.filter(k => knobChanged(k.name)).length;
    const modtxt = nmod ? ' <span class="kmod">(' + nmod + ' changed)</span>' : "";
    knobs = '<button class="ktoggle" data-ktoggle="' + f.suffix + '"><span class="caret">›</span>' +
      f.knobs.length + ' tunable' + (f.knobs.length > 1 ? 's' : '') + modtxt + '</button>' +
      '<div class="knobs">' + f.knobs.map(knobRow).join("") + '</div>';
  }
  return '<div class="feat' + (on ? " on" : "") + (state.kopen[f.suffix] ? " kopen" : "") +
    '" data-suffix="' + f.suffix + '" data-hay="' + esc((f.name + " " + (f.desc || "")).toLowerCase()) + '">' +
    '<span class="cbwrap"><input type="checkbox" class="cb"' + (on ? " checked" : "") + ' data-suf="' + f.suffix + '" aria-label="' + esc(f.name) + '"></span>' +
    '<div class="fhead" data-suf="' + f.suffix + '"><span class="fname"><span class="pfx">PC_ENABLE_</span>' + esc(f.suffix) + '</span>' + chips.join("") + '</div>' +
    '<div class="fdesc">' + esc(f.desc || "") + '</div>' + knobs + '</div>';
}

function coreCard() {
  return '<div class="feat on kopen" data-suffix="' + CORE + '" data-hay="core ' +
    esc(MODEL.core.map(k => k.name).join(" ").toLowerCase()) + '">' +
    '<span class="cbwrap"></span>' +
    '<div class="fhead"><span class="fname">Always-compiled knobs</span></div>' +
    '<div class="fdesc">Tuning constants that apply regardless of which features are enabled.</div>' +
    '<div class="knobs" style="display:block">' + MODEL.core.map(knobRow).join("") + '</div></div>';
}

function render() {
  const list = document.getElementById("list");
  list.innerHTML = "";
  MODEL.groups.forEach(g => {
    const sec = document.createElement("section");
    sec.className = "sec" + (state.collapsed[g] ? " collapsed" : "");
    sec.id = slug(g);
    sec.dataset.group = g;
    const isCore = g === CORE;
    const feats = isCore ? [{ __core: 1 }] : featsOf(g);
    const total = isCore ? MODEL.core.length : feats.length;
    const on = isCore ? 0 : onCount(g);
    const unit = isCore ? " knob" + (total > 1 ? "s" : "") : "";
    sec.innerHTML =
      '<button class="ghead" data-toggle="' + esc(g) + '"><span class="caret">▾</span>' +
      '<span class="gname">' + esc(g) + '</span>' +
      '<span class="gcount">' + total + unit + '</span>' +
      (on ? '<span class="gon">' + on + ' on</span>' : "") + '</button>' +
      '<div class="gbody">' + (isCore ? coreCard() : feats.map(featCard).join("")) + '</div>';
    list.appendChild(sec);
  });
  buildRail();
  wire();
  filter();
  update();
  initSpy();
}

function buildRail() {
  const rail = document.getElementById("rail");
  rail.innerHTML = '<div class="rlabel">Categories</div>' + MODEL.groups.map(g => {
    const isCore = g === CORE;
    const total = isCore ? MODEL.core.length : featsOf(g).length;
    const on = isCore ? 0 : onCount(g);
    return '<a href="#' + slug(g) + '" data-nav="' + esc(g) + '">' +
      (on ? '<span class="ron"></span>' : '') +
      '<span class="rn">' + esc(g) + '</span><span class="rc">' + total + '</span></a>';
  }).join("");
  rail.querySelectorAll("a[data-nav]").forEach(a => a.onclick = e => {
    e.preventDefault();
    const g = a.dataset.nav;
    state.collapsed[g] = false;
    document.getElementById(slug(g)).classList.remove("collapsed");
    document.getElementById(slug(g)).scrollIntoView({ behavior: "smooth", block: "start" });
  });
}

function wire() {
  document.querySelectorAll("input.cb[data-suf]").forEach(cb => cb.onchange = () => {
    const suf = cb.dataset.suf;
    if (cb.checked) enableWithParents(suf); else disableWithDependents(suf);
    render();
  });
  document.querySelectorAll(".fhead[data-suf]").forEach(h => h.onclick = e => {
    if (e.target.closest("input")) return;
    const cb = document.querySelector('input.cb[data-suf="' + h.dataset.suf + '"]');
    cb.checked = !cb.checked; cb.onchange();
  });
  document.querySelectorAll("[data-ktoggle]").forEach(b => b.onclick = () => {
    const suf = b.dataset.ktoggle;
    state.kopen[suf] = !state.kopen[suf];
    b.closest(".feat").classList.toggle("kopen", state.kopen[suf]);
  });
  document.querySelectorAll("[data-toggle]").forEach(b => b.onclick = () => {
    const g = b.dataset.toggle;
    state.collapsed[g] = !b.closest(".sec").classList.contains("collapsed");
    b.closest(".sec").classList.toggle("collapsed", state.collapsed[g]);
  });
  document.querySelectorAll("input[data-knob]").forEach(inp => inp.oninput = () => {
    state.knob[inp.dataset.knob] = inp.value;
    inp.classList.toggle("changed", String(inp.value) !== String(knobDefault(inp.dataset.knob)));
    update();
  });
  document.querySelectorAll(".kdefault[data-def]").forEach(d => d.onclick = () => {
    const name = d.dataset.def;
    delete state.knob[name];
    render();
  });
}

function computeLines() {
  const feats = [];
  MODEL.features.forEach(f => { if (state.on[f.suffix] !== !!f.default) feats.push([f.name, state.on[f.suffix] ? 1 : 0]); });
  const kn = [];
  Object.keys(state.knob).forEach(name => {
    if (!knobVisible(name)) return;
    const v = state.knob[name];
    if (String(v) !== String(knobDefault(name)) && v !== "") kn.push([name, v]);
  });
  return { feats, kn };
}

function update() {
  const { feats, kn } = computeLines();
  let text;
  if (state.fmt === "pio") {
    const rows = feats.map(([n, v]) => "    -D" + n + "=" + v).concat(kn.map(([n, v]) => "    -D" + n + "=" + v));
    text = rows.length ? "build_flags =\n" + rows.join("\n") : "; all defaults - no build_flags needed";
  } else if (state.fmt === "buildopt") {
    const rows = feats.map(([n, v]) => "-D" + n + "=" + v).concat(kn.map(([n, v]) => "-D" + n + "=" + v));
    text = rows.length ? rows.join("\n") : "-DPC_ENABLE_WEBSOCKET=1  // (example) all defaults - build_opt.h optional";
  } else {
    const rows = feats.map(([n, v]) => "#define " + n + " " + v).concat(kn.map(([n, v]) => "#define " + n + " " + v));
    text = rows.length ? rows.join("\n") : "// all defaults - nothing to override";
  }
  document.getElementById("output").value = text;
  const nOn = MODEL.features.filter(f => state.on[f.suffix]).length;
  document.getElementById("m-feat").textContent = nOn;
  document.getElementById("m-over").textContent = feats.length + kn.length;
  document.getElementById("ocount").textContent = (feats.length + kn.length) + " line" + (feats.length + kn.length === 1 ? "" : "s");
  const missing = [];
  MODEL.features.forEach(f => { if (state.on[f.suffix]) (f.requires || []).forEach(p => { if (bySuffix[p] && !state.on[p]) missing.push(f.suffix + " needs " + p); }); });
  document.getElementById("msg").textContent = missing.length ? ("Unmet: " + missing.join("; ")) : "";
  // Refresh section on-badges + rail without a full rebuild.
  document.querySelectorAll(".sec").forEach(sec => {
    const g = sec.dataset.group; if (g === CORE) return;
    const on = onCount(g);
    let b = sec.querySelector(".gon");
    if (on && !b) { b = document.createElement("span"); b.className = "gon"; sec.querySelector(".gcount").after(b); }
    if (b) b.textContent = on ? on + " on" : "", b.style.display = on ? "" : "none";
  });
}

function filter() {
  const q = (document.getElementById("q").value || "").trim().toLowerCase();
  const active = q || state.onlyEnabled || state.onlyModified;
  let anyShown = 0;
  document.querySelectorAll(".sec").forEach(sec => {
    const g = sec.dataset.group;
    let vis = 0, total = 0;
    sec.querySelectorAll(".feat").forEach(el => {
      total++;
      const suf = el.dataset.suffix;
      const f = bySuffix[suf];
      let hit = !q || el.dataset.hay.indexOf(q) >= 0;
      if (hit && state.onlyEnabled && suf !== CORE) hit = !!state.on[suf];
      if (hit && state.onlyModified && suf !== CORE) hit = f && featModified(f);
      if (suf === CORE && (state.onlyEnabled || state.onlyModified) && !q) {
        // Core knobs count as "modified" only if a core knob changed.
        if (state.onlyModified) hit = MODEL.core.some(k => knobChanged(k.name));
        if (state.onlyEnabled) hit = MODEL.core.some(k => knobChanged(k.name));
      }
      el.classList.toggle("hidden", !hit);
      if (hit) vis++;
    });
    sec.style.display = vis ? "" : "none";
    // While filtering, show "matches / total" so the count reflects what is on screen.
    const gc = sec.querySelector(".gcount");
    if (gc && g !== CORE) gc.textContent = (active && vis !== total) ? (vis + " / " + total) : total;
    // While filtering, force matching sections open so hits are visible.
    if (active) sec.classList.toggle("collapsed", false);
    else sec.classList.toggle("collapsed", !!state.collapsed[g]);
    anyShown += vis;
  });
  document.getElementById("empty").style.display = anyShown ? "none" : "block";
  // Dim rail entries whose section is hidden.
  document.querySelectorAll("#rail a[data-nav]").forEach(a => {
    const sec = document.getElementById(slug(a.dataset.nav));
    a.classList.toggle("empty", sec.style.display === "none");
  });
}

// Scrollspy: highlight the rail entry of the section nearest the top.
let spy;
function initSpy() {
  if (spy) spy.disconnect();
  spy = new IntersectionObserver(entries => {
    entries.forEach(en => {
      if (!en.isIntersecting) return;
      const g = en.target.dataset.group;
      document.querySelectorAll("#rail a").forEach(a => a.classList.toggle("active", a.dataset.nav === g));
    });
  }, { rootMargin: "-60px 0px -75% 0px", threshold: 0 });
  document.querySelectorAll(".sec").forEach(s => spy.observe(s));
}

/* ---- controls ---------------------------------------------------------- */
document.getElementById("q").oninput = filter;
document.getElementById("f-enabled").onclick = e => {
  state.onlyEnabled = !state.onlyEnabled; e.currentTarget.classList.toggle("on", state.onlyEnabled); filter();
};
document.getElementById("f-modified").onclick = e => {
  state.onlyModified = !state.onlyModified; e.currentTarget.classList.toggle("on", state.onlyModified); filter();
};
document.getElementById("expand").onclick = e => {
  const anyCollapsed = MODEL.groups.some(g => state.collapsed[g]);
  MODEL.groups.forEach(g => state.collapsed[g] = !anyCollapsed);
  e.currentTarget.textContent = anyCollapsed ? "Collapse all" : "Expand all";
  render();
};
document.getElementById("reset").onclick = () => {
  state.knob = {}; state.onlyEnabled = state.onlyModified = false;
  document.getElementById("f-enabled").classList.remove("on");
  document.getElementById("f-modified").classList.remove("on");
  document.getElementById("q").value = "";
  MODEL.features.forEach(f => state.on[f.suffix] = !!f.default);
  initCollapse();
  render();
};
document.querySelectorAll(".seg button").forEach(b => b.onclick = () => {
  state.fmt = b.dataset.fmt;
  document.querySelectorAll(".seg button").forEach(x => x.classList.toggle("sel", x === b));
  update();
});
document.getElementById("copy").onclick = async () => {
  const ta = document.getElementById("output");
  try { await navigator.clipboard.writeText(ta.value); } catch (e) { ta.select(); document.execCommand("copy"); }
  const btn = document.getElementById("copy"), t = btn.textContent;
  btn.textContent = "Copied!"; setTimeout(() => btn.textContent = t, 1200);
};
document.getElementById("download").onclick = () => {
  const names = { buildopt: "build_opt.h", pio: "platformio-build_flags.ini", defines: "pc_web_server_overrides.h" };
  const blob = new Blob([document.getElementById("output").value + "\n"], { type: "text/plain" });
  const a = document.createElement("a");
  a.href = URL.createObjectURL(blob); a.download = names[state.fmt] || "config.txt";
  document.body.appendChild(a); a.click(); a.remove();
  setTimeout(() => URL.revokeObjectURL(a.href), 1000);
};

/* ---- theme ------------------------------------------------------------- */
const SUN = '<svg viewBox="0 0 16 16" fill="none" stroke="currentColor" stroke-width="1.5"><circle cx="8" cy="8" r="3.2"/><path d="M8 1v1.6M8 13.4V15M1 8h1.6M13.4 8H15M3 3l1.1 1.1M11.9 11.9L13 13M13 3l-1.1 1.1M4.1 11.9L3 13"/></svg>';
const MOON = '<svg viewBox="0 0 16 16" fill="currentColor"><path d="M13 9.5A5.5 5.5 0 0 1 6.5 3c0-.5.07-1 .2-1.45A5.6 5.6 0 1 0 14.45 9.3c-.46.13-.95.2-1.45.2z"/></svg>';
function currentTheme() {
  if (state.theme) return state.theme;
  return matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light";
}
function applyTheme() {
  const t = currentTheme();
  document.documentElement.setAttribute("data-theme", t);
  document.getElementById("theme").innerHTML = t === "dark" ? SUN : MOON;
}
document.getElementById("theme").onclick = () => { state.theme = currentTheme() === "dark" ? "light" : "dark"; applyTheme(); };

/* ---- init -------------------------------------------------------------- */
function initCollapse() {
  // Open a section by default only if it holds an enabled feature; collapse the rest
  // (incl. the big core-knob bucket) so the page opens compact instead of 30 screens tall.
  MODEL.groups.forEach(g => { state.collapsed[g] = g === CORE ? true : onCount(g) === 0; });
}
applyTheme();
initCollapse();
render();
initSpy();
</script>
</body>
</html>
"""


def generate():
    parsed = parse(CONFIG)
    model = build_model(parsed)
    data = json.dumps(model, separators=(",", ":"))
    return PAGE.replace("__DATA__", data), model


def main():
    # `--check` is the one convention across ci_tooling/ (see ci_tooling/README.md).
    # This took a positional `check`, so the documented spelling silently exited 0.
    check = "--check" in sys.argv[1:]
    html, model = generate()
    if check:
        cur = open(OUT, "r", encoding="utf-8").read() if os.path.exists(OUT) else ""
        if cur.replace("\r\n", "\n") != html.replace("\r\n", "\n"):
            print("configurator.html is stale; run: python -m ci_tooling.generate.gen_configurator")
            sys.exit(1)
        print("configurator.html up to date")
        return
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(html)
    nk = sum(len(f["knobs"]) for f in model["features"]) + len(model["core"])
    print(
        "wrote %s: %d features, %d knobs (%d core), %d groups"
        % (os.path.relpath(OUT, ROOT), len(model["features"]), nk, len(model["core"]), len(model["groups"]))
    )


if __name__ == "__main__":
    main()
