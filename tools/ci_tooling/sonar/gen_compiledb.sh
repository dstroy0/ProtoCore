#!/usr/bin/env bash
# Generate the merged compile_commands.json for the SonarQube C/C++ analyzer.
# Run from anywhere with cmake on PATH and the host libraries installed (`test/harness.py libs`).
#
# No single env enables all PROTOCORE_ENABLE_* features, so a feature-gated source file is only
# compiled in the env that turns its flag on. The native envs are CMake targets (test/CMakeLists.txt,
# generated from test/test_matrix.json), and one configure writes a command for every translation
# unit of every env. merge_compiledb.py keeps the first command per file, so each file is analyzed
# under an env that actually enables it. Nothing is compiled: the configure alone writes the database.
#
# Outputs:
#   test/compile_commands.json  the committed, host-independent baseline (`directory` = @ROOT@).
#   compile_commands.json       the scan copy, @ROOT@ expanded to this checkout (git-ignored).
set -euo pipefail
cd "$(dirname "$0")/../../.."
ROOT="$(pwd)"
BASELINE=test/compile_commands.json
BUILD=build/compiledb

rm -rf "$BUILD" compile_commands.json
python3 tools/harness.py build cmake
cmake -S test -B "$BUILD" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON >/dev/null

python3 -m tools.ci_tooling.sonar.merge_compiledb "$BASELINE" "$BUILD/compile_commands.json" --root "$ROOT"
sed "s#@ROOT@#${ROOT}#g" "$BASELINE" >compile_commands.json
echo "wrote compile_commands.json (scan copy) from $BASELINE"
