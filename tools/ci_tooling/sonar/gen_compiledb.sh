#!/usr/bin/env bash
# Generate the merged compile_commands.json for the SonarQube C/C++ analyzer.
# Run from anywhere with PlatformIO (`pio`) on PATH.
#
# No single env enables all PC_ENABLE_* features, so a feature-gated source file
# is only compiled in the env that turns its flag on; we run compiledb per env and
# merge (merge_compiledb.py) to cover every file.
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
cd "$(dirname "$0")/../.."
ROOT="$(pwd)"
BASELINE=test/compile_commands.json
FRAGS=compiledb_frags

# Positional args (if any) are the affected envs; none => full.
AFFECTED_ENVS=("$@")
MODE=affected
if [ "${#AFFECTED_ENVS[@]}" -eq 0 ] || [ ! -f "$BASELINE" ]; then
    MODE=full
fi

if [ "$MODE" = "full" ]; then
    mapfile -t ENVS < <(grep -oE '^\[env:native[A-Za-z0-9_]*\]' platformio.ini | sed -E 's/\[env:(.*)\]/\1/' | grep -vE 'codeql')
else
    ENVS=("${AFFECTED_ENVS[@]}")
fi
echo "compiledb mode=$MODE envs=${#ENVS[@]}"

rm -rf "$FRAGS" compile_commands.json
mkdir -p "$FRAGS"
for e in "${ENVS[@]}"; do
    echo "::group::compiledb $e"
    pio run -t compiledb -e "$e"
    mv compile_commands.json "$FRAGS/$e.json"
    echo "::endgroup::"
done

if [ "$MODE" = "full" ]; then
    python3 -m tools.ci_tooling.sonar.merge_compiledb "$BASELINE" "$FRAGS/*.json" --root "$ROOT"
else
    # Overlay this run's affected envs onto the committed baseline (in place: the merge reads
    # the baseline fully before writing it back).
    python3 -m tools.ci_tooling.sonar.merge_compiledb "$BASELINE" "$FRAGS/*.json" --baseline "$BASELINE" --root "$ROOT"
fi
rm -rf "$FRAGS"

# Expand the tokenized baseline into the copy the scanner reads (absolute directory).
sed "s#@ROOT@#${ROOT}#g" "$BASELINE" > compile_commands.json
echo "wrote compile_commands.json (scan copy) from $BASELINE"
