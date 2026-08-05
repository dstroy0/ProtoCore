#!/usr/bin/env python3
"""ESP32 example build matrix + flash/RAM footprint aggregation for CI.

Three subcommands, used by .github/workflows/esp32-build.yml:

  example_footprints.py matrix
      Print a one-line JSON array of {name, ino, flags, feature} for every
      example .ino, to drive the build matrix. `flags` is the first
      `build_flags=...` documented in the example's README (empty if none);
      `feature` is the example's PC_ENABLE_* signature (e.g. "TLS+MTLS"), or
      "core/<name>" when it enables nothing.

      With a newline-separated list of changed paths on stdin, prints only the
      AFFECTED subset: an example whose own directory changed, or that includes a
      changed `src/services/<sub>/` feature (matched by the `services/<sub>/`
      include in its .ino). Any other changed `src/` path (protocore, shared
      primitives, network drivers, a top-level `services/*.h`) is shared code and
      falls back to the FULL matrix - the safe default. Empty stdin -> FULL, so a
      workflow_dispatch / schedule / first push still rebuilds everything.

  example_footprints.py fragment <build.log> <out.json>
      Parse the `Flash:`/`RAM:` "used N bytes" lines from a `pio ci` build log and
      write one footprint fragment {feature, example, flags, board, flash_bytes,
      ram_bytes}, taking feature/example/flags from the EX_* env vars.

  example_footprints.py merge <frag_dir> <out.json>
      Merge every fragment under <frag_dir> (recursively) into <out.json>, keyed
      by `feature`, overwriting an existing entry and keeping entries this run did
      not touch. Output is sorted and stable for a clean diff.

  example_footprints.py table <in.json> <out.md>
      Render the per-feature flash/RAM table (docs/FOOTPRINTS.md) from <in.json>.
"""

import glob
import json
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lib"))
import affected_common as ac  # noqa: E402  (path set above)

EX_ROOT = "examples"

# esp32dev capacities (bytes) for the percentage columns; from the pio size report.
FLASH_CAP = 1310720
RAM_CAP = 327680


def find_flags(readme):
    if not os.path.exists(readme):
        return ""
    txt = open(readme, encoding="utf-8", errors="replace").read()
    m = re.search(r'build_flags=([^"]*)"', txt)  # first documented pio ci command
    return m.group(1).strip() if m else ""


def feature_key(flags, name):
    enables = re.findall(r"-D(PC_[A-Z0-9_]+)=", flags)
    short = []
    for e in enables:
        s = e.replace("PC_ENABLE_", "").replace("PC_", "")
        if s and s not in short:
            short.append(s)
    return "+".join(short) if short else f"core/{name}"


def all_items():
    items = []
    for ino in sorted(glob.glob(f"{EX_ROOT}/**/*.ino", recursive=True)):
        d = os.path.dirname(ino)
        name = os.path.relpath(d, EX_ROOT).replace(os.sep, "/")
        flags = find_flags(os.path.join(d, "README.md"))
        items.append(
            {
                "name": name,
                "ino": ino.replace(os.sep, "/"),
                "flags": flags,
                "feature": feature_key(flags, os.path.basename(d)),
            }
        )
    return items


def affected_items(items, changed, base=None, head=None):
    """The subset of items a changed-file set touches, or the full list on any shared/core change.

    src/protocore_config.h is touched by every feature (a new default-off gate). With a diff base
    that change is classified by content: an additive gate cannot alter any example that does not
    opt into the new PC_ENABLE_* flag, so it is ignored rather than forcing a full rebuild; a
    non-additive protocore_config.h change still falls back to FULL.
    """
    features = set()  # src/services/<sub>/... -> feature subdir
    example_hits = []  # changed paths under examples/
    for f in changed:
        f = f.strip().replace(os.sep, "/")
        if not f:
            continue
        if f.startswith(f"{EX_ROOT}/"):
            example_hits.append(f)
        elif (
            f == "src/protocore_config.h"
            and base is not None
            and ac.config_header_additive(ac.file_at(base, f), ac.file_at(head, f))
        ):
            continue  # additive gate -> inert for every example that does not enable it
        elif f.startswith("src/services/") and f.count("/") >= 3:
            features.add(f.split("/")[2])  # e.g. src/network_drivers/application/smb/smb2.cpp -> "smb"
        elif f.startswith("src/"):
            return items  # shared/core src (protocore, shared_primitives, a top-level services/*.h) -> FULL
        elif f == "ci_tooling/generate/example_footprints.py" or (
            f.startswith(".github/workflows/") and f.endswith("build.yml")
        ):
            return items  # matrix generator / the build workflow -> FULL (platformio.ini does not
            # affect example builds: they use `pio ci` / arduino-cli with per-example flags)
        # else: docs, memory, platformio.ini, unrelated tooling -> not build-affecting, ignore

    sel = []
    for it in items:
        pfx = f"{EX_ROOT}/{it['name']}/"
        if any(h.startswith(pfx) for h in example_hits):
            sel.append(it)
            continue
        if features:
            try:
                txt = open(it["ino"], encoding="utf-8", errors="replace").read()
            except OSError:
                txt = ""
            if any(f"services/{sub}/" in txt for sub in features):
                sel.append(it)
    return sel


def cmd_matrix(base=None, head=None):
    items = all_items()
    changed = [] if sys.stdin.isatty() else sys.stdin.read().splitlines()
    if changed:
        items = affected_items(items, changed, base=base, head=head)
    print(json.dumps(items))


def _opt(argv, name):
    """Return the value after --<name> in argv, or None."""
    flag = f"--{name}"
    return argv[argv.index(flag) + 1] if flag in argv and argv.index(flag) + 1 < len(argv) else None


def cmd_fragment(logpath, outpath):
    log = open(logpath, encoding="utf-8", errors="replace").read()

    def used(label):
        m = re.search(label + r":.*?used\s+(\d+)\s+bytes", log)
        return int(m.group(1)) if m else 0

    frag = {
        "feature": os.environ.get("EX_FEATURE", ""),
        "example": os.environ.get("EX_NAME", ""),
        "flags": os.environ.get("EX_FLAGS", ""),
        "board": "esp32dev",
        "flash_bytes": used("Flash"),
        "ram_bytes": used("RAM"),
    }
    json.dump(frag, open(outpath, "w", encoding="utf-8", newline="\n"), indent=2)
    print(frag)


def cmd_merge(frag_dir, out_path):
    data = {}
    if os.path.exists(out_path):
        try:
            data = json.load(open(out_path, encoding="utf-8"))
        except json.JSONDecodeError:
            data = {}
    n = 0
    for frag in sorted(glob.glob(os.path.join(frag_dir, "**", "*.json"), recursive=True)):
        try:
            e = json.load(open(frag, encoding="utf-8"))
        except json.JSONDecodeError:
            continue
        feat = e.get("feature")
        if not feat:
            continue
        data[feat] = {k: e[k] for k in ("example", "flags", "board", "flash_bytes", "ram_bytes") if k in e}
        n += 1
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(dict(sorted(data.items())), f, indent=2)
        f.write("\n")
    print(f"merged {n} fragments -> {out_path}: {len(data)} feature entries")


def cmd_table(in_path, out_path):
    data = json.load(open(in_path, encoding="utf-8")) if os.path.exists(in_path) else {}

    def fmt(b, cap):
        return f"{b / 1024:.1f} KB ({b / cap * 100:.1f}%)" if b else "-"

    lines = [
        "# ESP32 build footprints",
        "",
        "Per-feature flash and RAM on `esp32dev`, captured by the **ESP32 Build** CI",
        "from each example's `pio ci` size report and aggregated into",
        "[`footprints.json`](footprints.json).",
        "",
        "> Autogenerated by `ci_tooling/generate/example_footprints.py table` - do not edit by hand.",
        "",
        "| Feature | Example | Flash | RAM |",
        "| --- | --- | --- | --- |",
    ]
    for feat in sorted(data):
        e = data[feat]
        lines.append(
            f"| `{feat}` | {e.get('example', '')} | "
            f"{fmt(e.get('flash_bytes', 0), FLASH_CAP)} | {fmt(e.get('ram_bytes', 0), RAM_CAP)} |"
        )
    open(out_path, "w", encoding="utf-8", newline="\n").write("\n".join(lines) + "\n")
    print(f"wrote {out_path}: {len(data)} rows")


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "matrix":
        cmd_matrix(base=_opt(sys.argv, "base"), head=_opt(sys.argv, "head"))
    elif len(sys.argv) >= 4 and sys.argv[1] == "fragment":
        cmd_fragment(sys.argv[2], sys.argv[3])
    elif len(sys.argv) >= 4 and sys.argv[1] == "merge":
        cmd_merge(sys.argv[2], sys.argv[3])
    elif len(sys.argv) >= 4 and sys.argv[1] == "table":
        cmd_table(sys.argv[2], sys.argv[3])
    else:
        sys.exit(
            "usage: example_footprints.py matrix | fragment <log> <out> | merge <frag_dir> <out> | table <in> <out>"
        )


if __name__ == "__main__":
    main()
