#!/usr/bin/env bash
# Generate the merged compile_commands.json for the SonarQube C/C++ analyzer.
# Run from anywhere with cmake and ninja on PATH.
#
# No single env enables all PROTOCORE_ENABLE_* features, so a feature-gated source file
# is only compiled in the env that turns its flag on. The native CMake build (build/native) exports
# one compile database for every env; it is split per env target and merged (merge_compiledb.py)
# so every file keeps one command.
#
# Two modes (mirrors the coverage / report baselines so an affected run stays cheap):
#   gen_compiledb.sh                     FULL   - every native env, regenerate the baseline.
#   gen_compiledb.sh native_a native_b   AFFECTED - only those envs; overlay their fresh
#                                        commands onto the committed baseline, keep the rest.
# An affected run with no committed baseline (first run) falls back to FULL - the safe default.
#
# Outputs:
#   test/compile_commands.json  the committed, host-independent baseline (`directory` = @ROOT@).
#   compile_commands.json       the scan copy, @ROOT@ expanded to this checkout (git-ignored).
set -euo pipefail
cd "$(dirname "$0")/../../.."
ROOT="$(pwd)"
BASELINE=test/compile_commands.json
FRAGS=compiledb_frags

# Positional args (if any) are the affected envs; none => full.
AFFECTED_ENVS=("$@")
MODE=affected
if [ "${#AFFECTED_ENVS[@]}" -eq 0 ] || [ ! -f "$BASELINE" ]; then
    MODE=full
fi

BUILD=build/native
python3 tools/harness.py build cmake
cmake -S test -B "$BUILD" -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/dev/null

rm -rf "$FRAGS" compile_commands.json
mkdir -p "$FRAGS"
# One fragment per env target, from the objects under CMakeFiles/<env>.dir/. MMgr's own targets and
# the fetched deps are not ProtoCore's sources and stay out.
python3 - "$BUILD/compile_commands.json" "$FRAGS" "$MODE" "${AFFECTED_ENVS[@]}" <<'PY'
import json, os, re, sys
db, frags, mode, want = sys.argv[1], sys.argv[2], sys.argv[3], set(sys.argv[4:])
per = {}
for e in json.load(open(db)):
    m = re.search(r"CMakeFiles/(native[A-Za-z0-9_]*)\.dir/", e.get("output", "") or e.get("command", ""))
    if not m or m.group(1).startswith("native_codeql"):
        continue
    if mode != "full" and m.group(1) not in want:
        continue
    per.setdefault(m.group(1), []).append(e)
for env, ents in per.items():
    with open(os.path.join(frags, env + ".json"), "w") as f:
        json.dump(ents, f)
print("compiledb mode=%s envs=%d" % (mode, len(per)))
PY

if [ "$MODE" = "full" ]; then
    python3 -m tools.ci_tooling.sonar.merge_compiledb "$BASELINE" "$FRAGS/*.json" --root "$ROOT"
else
    # Overlay this run's affected envs onto the committed baseline (in place: the merge reads
    # the baseline fully before writing it back).
    python3 -m tools.ci_tooling.sonar.merge_compiledb "$BASELINE" "$FRAGS/*.json" --baseline "$BASELINE" --root "$ROOT"
fi
rm -rf "$FRAGS"

# Expand the tokenized baseline into the copy the scanner reads (absolute directory).
sed "s#@ROOT@#${ROOT}#g" "$BASELINE" >compile_commands.json
echo "wrote compile_commands.json (scan copy) from $BASELINE"
