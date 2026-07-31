#!/usr/bin/env bash
# Run the native Unity test envs with gcov instrumentation and emit a merged
# SonarQube coverage report (src/ only) for the SonarCloud scan. Coverage needs
# the tests to actually RUN (not just compiledb), so this builds + runs them.
#
# One report PER ENV, unioned afterwards: gcov cannot merge the same source
# compiled with different -D flags across envs in a single pass (it throws a
# worker exception), so each env is gcovr'd on its own build dir and
# merge_coverage.py unions them into coverage.xml.
#
# A dedicated build dir (.pio_cov) + the coverage build flags keep this separate
# from the no-coverage compile-DB build. Pass an explicit env list as args to
# cover a subset (handy locally); default is every native env except codeql and
# the ThreadSanitizer env (tsan + gcov do not mix). Requires: pio + gcovr.
set -uo pipefail
cd "$(dirname "$0")/../.."

export PLATFORMIO_BUILD_DIR="${PLATFORMIO_BUILD_DIR:-.pio_cov}"
export PLATFORMIO_BUILD_FLAGS="-fprofile-arcs -ftest-coverage -lgcov"

if [ "$#" -gt 0 ]; then
    envs="$*"
else
    envs=$(grep -oE '^\[env:native[A-Za-z0-9_]*\]' platformio.ini | sed -E 's/\[env:(.*)\]/\1/' | grep -vE 'codeql|native_tsan')
fi

rm -rf coverage_reports
mkdir -p coverage_reports
for e in $envs; do
    echo "::group::coverage $e"
    pio test -e "$e" || echo "WARN: test env failed: $e"
    # gcovr 8.x anchors --filter to the FULL relative path, so a bare 'src/'
    # matches nothing (silently emptied every report -> SonarCloud 0% coverage);
    # 'src/.*' matches src/... and still excludes test/ and the Unity libdep.
    gcovr --root . --filter 'src/.*' --gcov-ignore-parse-errors --sonarqube "coverage_reports/${e}.xml" \
        "$PLATFORMIO_BUILD_DIR/$e" || echo "WARN: gcovr failed: $e"
    echo "::endgroup::"
done

python3 ci_tooling/coverage/merge_coverage.py test/coverage.xml "coverage_reports/*.xml"
rm -rf coverage_reports
echo "wrote test/coverage.xml"
