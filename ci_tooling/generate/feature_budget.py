#!/usr/bin/env python3
"""Per-feature flash/RAM budget ranges for the README.

The single per-example totals in docs/footprints.json answer "how big is a build that uses feature X",
but not "how much does X add", because each example runs a different base profile (some strip the
default WebSocket/SSE/file/auth, so a naive delta against one baseline goes misleadingly negative).

This tool instead uses *isolated* measurements: the same fixed base sketch (examples/Foundation/Basic)
built with vs without each feature flag, in two base contexts - the **default** server and a **minimal**
server (WS/SSE/multipart/file stripped). A feature that reuses a default sub-system costs less when it is
already linked (default context) and more when it is not (minimal context), which is exactly the spread a
worst-case budget wants. Enabling an opt-in service/client flag alone links almost nothing (its code is
pulled in only when the sketch calls its begin()), so for those the loaded figure comes from the feature's
own example total in footprints.json. Per feature we report a range: best case (least it adds) to worst
case (most it adds - the number to budget with).

  feature_budget.py collect <default.log> <minimal.log> <ranges.json>
      Parse the two RPi matrix logs (rows: "<feature>\t<flash>\t<ram>...") into ranges.json.

  feature_budget.py readme <ranges.json> <footprints.json> <README.md>
      Regenerate the "Build Footprint" range table in README.md between the generated markers.
"""

import json
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
import doc_region as dr  # noqa: E402  (path set above)

ROOT = dr.repo_root(__file__)
FLASH_CAP = 1310720
RAM_CAP = 327680


def parse_log(path):
    """Return {feature: (flash, ram)} plus the BASELINE row, skipping FAIL rows."""
    out, base = {}, None
    for line in open(path, encoding="utf-8", errors="replace"):
        parts = line.rstrip("\n").split("\t")
        if len(parts) < 3 or parts[0].startswith("#"):
            continue
        feat, flash, ram = parts[0], parts[1], parts[2]
        if flash == "FAIL" or not flash.isdigit() or not ram.isdigit():
            continue
        if feat == "BASELINE":
            base = (int(flash), int(ram))
        else:
            out[feat] = (int(flash), int(ram))
    return base, out


def cmd_collect(default_log, minimal_log, out_json):
    dbase, ddata = parse_log(default_log)
    mbase, mdata = parse_log(minimal_log)
    ranges = {"_baseline": {"default": dbase, "minimal": mbase}}
    for feat in sorted(set(ddata) | set(mdata)):
        entry = {}
        if feat in ddata:
            entry["default"] = {"flash": ddata[feat][0] - dbase[0], "ram": ddata[feat][1] - dbase[1]}
        if feat in mdata and mbase:
            entry["minimal"] = {"flash": mdata[feat][0] - mbase[0], "ram": mdata[feat][1] - mbase[1]}
        ranges[feat] = entry
    with open(out_json, "w", encoding="utf-8", newline="\n") as f:
        json.dump(ranges, f, indent=2, sort_keys=True)
        f.write("\n")
    print(f"collected {len(ranges) - 1} features -> {out_json}")


def _layer(example):
    return {
        "Foundation": "Core",
        "L4-Transport": "L4",
        "L5-Session": "L5",
        "L6-Presentation": "L6",
        "L7-Application": "L7",
    }.get(example.split("/")[0], "?")


def _example_for(feat, fp):
    """The footprints.json entry that exercises `feat` (its flag is the primary token), or None."""
    best = None
    for key, v in fp.items():
        if key.startswith("core/") or v.get("flash_bytes", 0) < 400000:  # skip cores + standalone (no server)
            continue
        toks = key.split("+")
        if feat == key or feat == toks[0] or feat in toks:
            rank = 0 if key == feat else (1 if toks[0] == feat else 2)
            if best is None or rank < best[0]:
                best = (rank, v)
    return best[1] if best else None


def cmd_readme(ranges_json, footprints_json, readme_path):
    ranges = json.load(open(ranges_json, encoding="utf-8"))
    fp = json.load(open(footprints_json, encoding="utf-8")) if os.path.exists(footprints_json) else {}
    dbase = ranges.get("_baseline", {}).get("default") or [0, 0]
    MIN_SHOW = 512  # bytes; a feature adding under 0.5 KB flash and RAM is not worth a budget row
    # Every server feature: the matrix's isolated measurements, plus every footprints example keyed by its
    # primary flag (so an exampled feature shows from its example even before the matrix reaches it).
    feats = {f for f in ranges if not f.startswith("_")}
    for key, v in fp.items():
        if not key.startswith("core/") and v.get("flash_bytes", 0) >= 400000:
            feats.add(key.split("+")[0])
    rows = []
    for feat in feats:
        e = ranges.get(feat, {})
        ex = _example_for(feat, fp)
        # Two kinds of cost data:
        #  - isolated deltas (same base sketch +/- the flag, in a default and a minimal context): a real
        #    best/worst spread ONLY for a feature the library links straight from its flag (WebDAV, ETag,
        #    Range, ...). For an opt-in feature (TLS, SSH, a service) the flag links ~nothing, so its
        #    isolated delta is a useless ~0 - not a real "best case", since nobody enables it without using it.
        #  - the feature's example over the *default* server baseline: what it actually costs when exercised.
        # So: include the isolated spread only when the flag really links code (>= 2 KB in some context);
        # otherwise the cost is a single figure from the example. Best clamps to >= 0 (linker noise).
        iso_f = [e[c]["flash"] for c in ("default", "minimal") if c in e]
        iso_r = [e[c]["ram"] for c in ("default", "minimal") if c in e]
        links_from_flag = bool(iso_f) and max(iso_f) >= 2048
        fcs = list(iso_f) if links_from_flag else []
        rcs = list(iso_r) if links_from_flag else []
        if ex:
            fcs.append(ex["flash_bytes"] - dbase[0])
            rcs.append(ex["ram_bytes"] - dbase[1])
        if not fcs:  # no example and does not link meaningfully from the flag - fall back to its isolated cost
            if not iso_f:
                continue
            fcs, rcs = iso_f, iso_r
        fmin, fmax = max(0, min(fcs)), max(fcs)
        rmin, rmax = max(0, min(rcs)), max(rcs)
        if fmax < MIN_SHOW and rmax < MIN_SHOW:
            continue
        rows.append(
            {
                "feature": feat,
                "layer": _layer(ex["example"]) if ex else "-",
                "fmin": fmin,
                "fmax": fmax,
                "rmin": rmin,
                "rmax": rmax,
            }
        )
    order = {"Core": 0, "L4": 1, "L5": 2, "L6": 3, "L7": 4, "-": 9}
    # Feature name is the final tiebreaker: `feats` is a set, so features that tie on (layer, -fmax)
    # would otherwise land in set-iteration order, which is not stable across Python processes
    # (PYTHONHASHSEED) - that produced a 2-line README churn commit on every CI run. Sort fully.
    rows.sort(key=lambda r: (order.get(r["layer"], 9), -r["fmax"], r["feature"]))

    # A SUMMARY, not the inventory. The per-feature table ran to ~90 rows of the README - numbers a
    # reader consults once while sizing a build, not while deciding whether to use the library at
    # all. docs/FOOTPRINTS.md already carries every one of them plus each example's absolute total.
    # The baseline stays inline because it is the one figure that answers "what does this cost me".
    lines = [
        "> Autogenerated by `ci_tooling/generate/feature_budget.py` from isolated ESP32 builds - do not edit by hand.",
        "",
        f"Measured on `esp32dev` (Arduino core). The **default server** (HTTP + WebSocket + SSE + multipart +"
        f" file serving + Basic auth) is **{dbase[0] / 1024:.0f} KB flash / {dbase[1] / 1024:.1f} KB RAM** on a"
        " chip with 1,280 KB flash / 320 KB RAM. Everything past that is opt-in and links only what you name.",
        "",
        f"**[Per-feature build footprints &rarr;](docs/FOOTPRINTS.md)** - {len(rows)} features that add measurable"
        " flash, each measured from isolated builds as a range: best case (its dependencies are already linked)"
        " to worst case (budget with this). Every example's absolute total is there too.",
    ]
    region = dr.Region(readme_path, "FOOTPRINT BUDGET", dr.tool_id(__file__))
    dr.apply(readme_path, {region: "\n".join(lines)})
    print(f"  {len(rows)} feature rows")


def main():
    if len(sys.argv) >= 5 and sys.argv[1] == "collect":
        cmd_collect(sys.argv[2], sys.argv[3], sys.argv[4])
    elif len(sys.argv) >= 5 and sys.argv[1] == "readme":
        cmd_readme(sys.argv[2], sys.argv[3], sys.argv[4])
    else:
        sys.exit(
            "usage: feature_budget.py collect <default.log> <minimal.log> <ranges.json>\n"
            "       feature_budget.py readme <ranges.json> <footprints.json> <README.md>"
        )


if __name__ == "__main__":
    main()
