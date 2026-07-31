#!/usr/bin/env python3
"""Merge per-environment compile_commands.json fragments into one, deduped by file.

PlatformIO writes compile_commands.json to the project root one env at a time, and
no single env enables all of the ~111 PC_ENABLE_* features, so a feature-gated
source file is only compiled in the env that turns its flag on. To give the
SonarQube C/C++ analyzer a command for *every* file, gen_compiledb.sh runs
`pio run -t compiledb` for each native env, stashes each result, and this script
merges them: the first env that compiled a given file wins (one command per file).

Two modes, keyed off --baseline:

  * FULL (no --baseline): start empty, merge all fragments -> the whole database.
  * AFFECTED (--baseline test/compile_commands.json): start from the committed
    baseline and OVERLAY only the fragments this run produced (the affected envs).
    A file present in a fragment takes that fragment's fresh command; every other
    file keeps its baseline command. So an affected run regenerates only the envs
    that changed and surgically replaces their entries, exactly like the coverage
    and report baselines.

Host-independence: the committed baseline stores `directory` (and any absolute
project-root prefix) as the token @ROOT@ so it never embeds a runner path and
never churns. gen_compiledb.sh expands @ROOT@ back to the real checkout root for
the copy the scanner actually reads. Native compile commands are otherwise
relative (`-Isrc`, `src/...`), so only `directory` needs tokenizing.

Usage: merge_compiledb.py <out_path> <fragments_glob> [--baseline <path>] [--root <abs>]
"""

import glob
import json
import sys

ROOT_TOKEN = "@ROOT@"


def tokenize(entry, root):
    """Return a copy of a compile_commands entry with the absolute project root replaced by @ROOT@."""
    out = dict(entry)
    if root:
        for k in ("directory", "file", "output", "command"):
            if k in out and isinstance(out[k], str):
                out[k] = out[k].replace(root, ROOT_TOKEN)
    # A relative `directory` is meaningless to the scanner; pin it to the token root.
    if out.get("directory") in ("", ".", None):
        out["directory"] = ROOT_TOKEN
    return out


def load(path):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except (OSError, json.JSONDecodeError):
        return []


def main():
    args = sys.argv[1:]
    baseline = None
    root = None
    pos = []
    i = 0
    while i < len(args):
        if args[i] == "--baseline":
            baseline = args[i + 1]
            i += 2
        elif args[i] == "--root":
            root = args[i + 1].rstrip("/")
            i += 2
        else:
            pos.append(args[i])
            i += 1

    out_path = pos[0] if pos else "compile_commands.json"
    frag_glob = pos[1] if len(pos) > 1 else "compiledb_frags/*.json"

    # Start from the baseline (affected mode) or empty (full mode). Keyed by file so an
    # overlaid fragment replaces just that file's command and every untouched file is kept.
    seen = {}
    order = []
    if baseline:
        for e in load(baseline):
            key = e.get("file")
            if key and key not in seen:
                seen[key] = e
                order.append(key)
    baseline_files = set(seen)

    # Overlay this run's fragments. Within one run the first env that compiled a file wins
    # (matches the original dedup); across runs a fragment overrides the baseline entry.
    updated = set()
    n_frag = 0
    for frag in sorted(glob.glob(frag_glob)):
        n_frag += 1
        for e in load(frag):
            key = e.get("file")
            if not key or key in updated:
                continue
            ent = tokenize(e, root)
            if key not in seen:
                order.append(key)
            seen[key] = ent
            updated.add(key)

    merged = [seen[k] for k in order]
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(merged, f, indent=2)
    kept = len(baseline_files - updated)
    print(
        f"merged {n_frag} env fragments -> {out_path}: {len(merged)} files "
        f"({len(updated)} refreshed, {kept} kept from baseline)"
    )


if __name__ == "__main__":
    main()
