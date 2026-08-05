#!/usr/bin/env bash
# run_tests.sh - Build, run, and document all PlatformIO native test suites.
#
# Usage:
#   ./test/run_tests.sh      # from project root
#   ./run_tests.sh           # from test/ directory
#
# Writes: docs/TEST_REPORT.md
#
# Requires: bash 4.2+, pio (PlatformIO Core), awk, sed, grep, mktemp

set -uo pipefail

# Optional --coverage: also emit the SonarQube coverage.xml from this same (instrumented) run, so CI
# runs the whole suite ONCE for both the test report and the Sonar scan instead of twice. Strip the
# flag out of $@ so an explicit env list still works.
COVERAGE=0
REPORT_OUT=""      # --report-out PATH: write the report here instead of the default (partial runs)
_pos=()
while [[ $# -gt 0 ]]; do
    case "$1" in
        --coverage) COVERAGE=1; shift ;;
        --report-out) REPORT_OUT="$2"; shift 2 ;;
        *) _pos+=("$1"); shift ;;
    esac
done
set -- ${_pos[@]+"${_pos[@]}"}

# Route every compile through ccache if a masquerade dir is present (gcc/g++ -> ccache symlinks on
# PATH). The ~150 native envs each rebuild the library + Unity with their own flags, so identical
# (source, flags) compiles recur across envs; ccache turns them into hits (and, with ~/.ccache restored,
# across CI runs). PATH masquerade is used because PlatformIO builds Unity/libs in cloned sub-envs that
# do not inherit a compiler-wrap set any other way. No-op when ccache is not installed.
for _ccdir in /usr/lib/ccache /usr/lib64/ccache /usr/local/libexec/ccache /opt/homebrew/opt/ccache/libexec; do
    if [[ -d "$_ccdir" ]]; then
        export PATH="$_ccdir:$PATH"
        echo "ccache: compiles routed through $_ccdir"
        break
    fi
done

# ── Paths ─────────────────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [[ "$(basename "$SCRIPT_DIR")" == "test" ]]; then
    PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
else
    PROJECT_ROOT="$SCRIPT_DIR"
fi
TEST_DIR="${PROJECT_ROOT}/test"
REPORT_PATH="${PROJECT_ROOT}/test/TEST_REPORT.md"
[[ -n "$REPORT_OUT" ]] && REPORT_PATH="$REPORT_OUT"

# ── Find pio ──────────────────────────────────────────────────────────────────

PIO=""
for _candidate in \
    "pio" \
    "${HOME}/.platformio/penv/bin/pio" \
    "${HOME}/.local/bin/pio" \
    "/usr/local/bin/pio"; do
    if [[ -x "$_candidate" ]] 2>/dev/null || command -v "$_candidate" &>/dev/null 2>&1; then
        PIO="$_candidate"
        break
    fi
done

if [[ -z "$PIO" ]]; then
    echo "error: pio not found. Install PlatformIO Core or add it to PATH." >&2
    exit 1
fi

echo "pio     : $PIO"
echo "project : $PROJECT_ROOT"
echo "report  : $REPORT_PATH"
echo ""
echo "Running tests..."
echo ""

# Format output helper function
format_output() {
    awk -F'\t' '
    BEGIN {
        width = 120
    }
    {
        line = $0
        clean_line = line
        gsub(/\x1b\[[0-9;?]*[a-zA-Z]/, "", clean_line)
        
        # Test line: "test/test_X/test_X.cpp:829: test_name\t[PASSED]"
        if (clean_line ~ /\[(PASSED|FAILED)\]$/) {
            match(line, /[[:space:]]+(\x1b\[[0-9;?]*[a-zA-Z])*\[(PASSED|FAILED)\](\x1b\[[0-9;?]*[a-zA-Z])*$/)
            if (RSTART > 0) {
                left = substr(line, 1, RSTART - 1)
                right = substr(line, RSTART)
                sub(/^[[:space:]]+/, "", right)
                
                clean_left = left
                gsub(/\x1b\[[0-9;?]*[a-zA-Z]/, "", clean_left)
                
                clean_right = right
                gsub(/\x1b\[[0-9;?]*[a-zA-Z]/, "", clean_right)
                
                pad = width - length(clean_left) - length(clean_right)
                if (pad < 1) pad = 1
                spaces = ""
                for (i = 1; i <= pad; i++) spaces = spaces " "
                print left spaces right
                next
            }
        }
        
        # Banner line: "------------ native_ssh:test_ssh_crypto [PASSED] Took 6.70 seconds ------------"
        if (clean_line ~ /^-+ .* \[(PASSED|FAILED)\] Took .* seconds -+$/) {
            orig_msg = line
            gsub(/^-+/, "", orig_msg)
            gsub(/-+$/, "", orig_msg)
            gsub(/^[ \t]+|[ \t]+$/, "", orig_msg)
            
            # Pull the [PASSED]/[FAILED] token (with any surrounding color codes) out of the message.
            match(orig_msg, /(\x1b\[[0-9;?]*[a-zA-Z])*\[(PASSED|FAILED)\](\x1b\[[0-9;?]*[a-zA-Z])*/)
            status = substr(orig_msg, RSTART, RLENGTH)
            msg = substr(orig_msg, 1, RSTART - 1) substr(orig_msg, RSTART + RLENGTH)
            gsub(/[ \t]+/, " ", msg)
            gsub(/^ | $/, "", msg)

            clean_msg = msg
            gsub(/\x1b\[[0-9;?]*[a-zA-Z]/, "", clean_msg)
            clean_status = status
            gsub(/\x1b\[[0-9;?]*[a-zA-Z]/, "", clean_status)

            fill = width - length(clean_msg) - length(clean_status) - 2
            if (fill < 1) fill = 1
            dashes = ""
            for (i = 1; i <= fill; i++) dashes = dashes "-"
            print msg " " dashes " " status
            next
        }
        
        # Section line: "--------------------------------------------------------------------------------"
        if (clean_line ~ /^-{20,}$/) {
            dashes = ""
            for (i = 1; i <= width; i++) dashes = dashes "-"
            print dashes
            next
        }
        
        print line
    }'
}

# ── Run tests ─────────────────────────────────────────────────────────────────

cd "$PROJECT_ROOT"

RAW_FILE="$(mktemp)"
CLEAN_FILE="$(mktemp)"
trap 'rm -f "$RAW_FILE" "$CLEAN_FILE"' EXIT

# Auto-discover every native_* env from platformio.ini (the single source of
# truth, generated from test/test_matrix.json). This replaces a hand-maintained
# env list that had drifted out of date - nothing is silently skipped now.
# A few envs are intentionally not part of this report:
#   native_pentest - heavy adversarial fuzzer, run separately (pentest.yml)
#   native_codeql  - compile-only umbrella for the CodeQL workflow
#   native_tsan    - ThreadSanitizer; Linux/clang only (run in CI, skipped elsewhere)
EXCLUDE=("native_pentest" "native_codeql")
if [[ "$(uname -s 2>/dev/null)" != Linux* ]]; then
    EXCLUDE+=("native_tsan")
fi

mapfile -t ALL_ENVS < <(grep -oE '^\[env:native[A-Za-z0-9_]*\]' "${PROJECT_ROOT}/platformio.ini" \
    | sed -E 's/^\[env:(.*)\]$/\1/')

ENV_NAMES=()
for _env in "${ALL_ENVS[@]}"; do
    _skip=0
    for _x in "${EXCLUDE[@]}"; do [[ "$_env" == "$_x" ]] && _skip=1; done
    [[ $_skip -eq 0 ]] || continue
    ENV_NAMES+=("$_env")
done

# An explicit env list as positional args overrides discovery (targeted runs /
# quick checks), e.g.  ./test/run_tests.sh native_coap native_clock
if [[ $# -gt 0 ]]; then
    ENV_NAMES=("$@")
fi

if [[ $COVERAGE -eq 1 ]]; then
    # Instrument this run in a dedicated build dir (never clashes with the plain compile-DB build) and
    # prepare per-env coverage output; each env is gcovr'd right after it runs, unioned at the end.
    export PLATFORMIO_BUILD_DIR="${PLATFORMIO_BUILD_DIR:-.pio_cov}"
    export PLATFORMIO_BUILD_FLAGS="-fprofile-arcs -ftest-coverage -lgcov"
    rm -rf coverage_reports
    mkdir -p coverage_reports
fi
COV_FAILED=()   # envs whose gcovr produced nothing (see the gcovr call below)

_covnote=""; [[ $COVERAGE -eq 1 ]] && _covnote=" (with coverage)"
echo "Running ${#ENV_NAMES[@]} native envs${_covnote}"

# Run each env on its own so progress is visible (a counter + percent + spinner),
# rather than one long silent block. Each env's output is formatted and appended
# to RAW_FILE for the report parser below; the progress line goes only to the
# terminal, never into RAW.
T0=$SECONDS
PIO_EXIT=0
: > "$RAW_FILE"
TOTAL=${#ENV_NAMES[@]}
SPIN='|/-\'
IS_TTY=0; [[ -t 1 ]] && IS_TTY=1

for _i in "${!ENV_NAMES[@]}"; do
    _env="${ENV_NAMES[$_i]}"
    _k=$(( _i + 1 ))
    _pct=$(( _k * 100 / TOTAL ))
    _envout="$(mktemp)"

    set +e
    "$PIO" test -e "$_env" > "$_envout" 2>&1 &
    _pid=$!
    if [[ $IS_TTY -eq 1 ]]; then
        _s=0
        while kill -0 "$_pid" 2>/dev/null; do
            printf '\r\033[K[%2d/%2d %3d%%] %s  building/testing %s ' \
                "$_k" "$TOTAL" "$_pct" "${SPIN:$((_s++ % 4)):1}" "$_env"
            sleep 0.2
        done
        wait "$_pid"; _rc=$?
        [[ $_rc -eq 0 ]] && _mark='[ ok ]' || _mark='[FAIL]'
        printf '\r\033[K[%2d/%2d %3d%%] %s %s\n' "$_k" "$TOTAL" "$_pct" "$_mark" "$_env"
    else
        printf '[%2d/%2d %3d%%] %s\n' "$_k" "$TOTAL" "$_pct" "$_env"
        wait "$_pid"; _rc=$?
    fi
    set -e

    [[ $_rc -ne 0 ]] && PIO_EXIT=$_rc
    # Show this env's results and capture them for the report.
    format_output < "$_envout" | tee -a "$RAW_FILE"
    rm -f "$_envout"

    # gcovr this env right after it runs (one report per env; unioned after the loop). gcovr 8.x
    # anchors --filter to the full relative path, so 'src/.*' (not bare 'src/') matches src/... and
    # still excludes test/ + the Unity libdep. Same-source-different-flags cannot be merged in a
    # single gcovr pass, hence per-env.
    # A silently-missing per-env report is the worst failure mode here: the merge only ever
    # UNIONS, so an env that stops contributing does not lower the number - it freezes that
    # env's files at whatever the baseline last said, forever, with nothing in the log to say
    # so. Record the failure and fail the run at the end instead of warning into the void.
    # JSON, not the SonarQube format: that format carries only a per-line (branchesToCover,
    # coveredBranches) aggregate, so two envs that each cover a DIFFERENT subset of one
    # condition's branches cannot be combined - the union could only keep the better env and the
    # line would read as partially covered forever. gcovr's JSON keeps the per-branch counts.
    if [[ $COVERAGE -eq 1 ]] && { ! gcovr --root . --filter 'src/.*' --gcov-ignore-parse-errors \
        --json "coverage_reports/${_env}.json" \
        "${PLATFORMIO_BUILD_DIR:-.pio_cov}/$_env" 2>/dev/null \
        || [[ ! -s "coverage_reports/${_env}.json" ]]; }; then
        echo "ERROR: gcovr produced no coverage report for $_env" >&2
        COV_FAILED+=("$_env")
    fi
done
WALL_SECS=$(( SECONDS - T0 ))

# Strip ANSI codes for parsing
sed 's/\x1b\[[0-9;?]*[a-zA-Z]//g' "$RAW_FILE" > "$CLEAN_FILE"

# ── Parse results ─────────────────────────────────────────────────────────────

# Parallel indexed arrays - bash 4.0+
T_IDX=0
declare -a T_ENV=() T_SUITE=() T_FILE=() T_LINE=() T_NAME=() T_STATUS=()

# Suite summary data
declare -A S_STATUS=() S_DURATION=() S_ENV=()
SUITE_ORDER=()   # insertion-order list of "env:suite" keys

CUR_ENV="native"
CUR_SUITE=""

while IFS= read -r line; do
    # Section header
    if [[ "$line" =~ ^Processing[[:space:]]+([^[:space:]]+)[[:space:]]+in[[:space:]]+([^[:space:]]+)[[:space:]]+environment ]]; then
        CUR_SUITE="${BASH_REMATCH[1]}"
        CUR_ENV="${BASH_REMATCH[2]}"
        continue
    fi

    # Individual test result
    if [[ "$line" =~ ([^[:space:]]+\.cpp):([0-9]+):[[:space:]]+([^[:space:]]+)[[:space:]]+\[(PASSED|FAILED)\] ]]; then
        rel_file="${BASH_REMATCH[1]//\\//}"
        suite="$(basename "${rel_file%.cpp}")"
        T_ENV[$T_IDX]="$CUR_ENV"
        T_SUITE[$T_IDX]="$suite"
        T_FILE[$T_IDX]="$rel_file"
        T_LINE[$T_IDX]="${BASH_REMATCH[2]}"
        T_NAME[$T_IDX]="${BASH_REMATCH[3]}"
        T_STATUS[$T_IDX]="${BASH_REMATCH[4]}"
        (( T_IDX++ )) || true
        continue
    fi

    # Suite summary line: "native  test_X  PASSED  00:00:02.07"
    if [[ "$line" =~ ^[[:space:]]*(native[^[:space:]]*)[[:space:]]+(test_[^[:space:]]+)[[:space:]]+(PASSED|FAILED)[[:space:]]+([0-9:\.]+) ]]; then
        _env="${BASH_REMATCH[1]}"
        _suite="${BASH_REMATCH[2]}"
        _key="${_env}:${_suite}"
        if [[ -z "${S_STATUS[$_key]:-}" ]]; then
            S_ENV[$_key]="$_env"
            S_STATUS[$_key]="${BASH_REMATCH[3]}"
            S_DURATION[$_key]="${BASH_REMATCH[4]}"
            SUITE_ORDER+=("$_key")
        fi
    fi
done < "$CLEAN_FILE"

# Totals
N_TOTAL=0; N_PASSED=0; N_FAILED=0
# Sum the per-invocation summary line(s): "N test cases: M succeeded in ...".
# The literal ": " anchor avoids a greedy ".*" mis-capturing the second count.
while IFS= read -r _line; do
    if [[ "$_line" =~ ([0-9]+)[[:space:]]+test[[:space:]]+cases:[[:space:]]+([0-9]+)[[:space:]]+succeeded ]]; then
        N_TOTAL=$(( N_TOTAL + BASH_REMATCH[1] ))
        N_PASSED=$(( N_PASSED + BASH_REMATCH[2] ))
    fi
done < <(grep -E '[0-9]+ test cases:' "$CLEAN_FILE")
N_FAILED=$(( N_TOTAL - N_PASSED ))

# ── Description helpers ───────────────────────────────────────────────────────

# get_test_comment <rel_file> <fn_name>
# Prints the first // comment inside the function body, or nothing.
get_test_comment() {
    local rel_file="$1"
    local fn_name="$2"
    local abs_file="${PROJECT_ROOT}/${rel_file}"
    [[ -f "$abs_file" ]] || return 0

    local fn_line
    fn_line=$(grep -n "void ${fn_name}(" "$abs_file" 2>/dev/null | head -1 | cut -d: -f1) || true
    [[ -n "$fn_line" ]] || return 0

    # Extract from the function's line onwards (cap at 20 lines); find the first //
    # comment after the opening brace. One awk over the file so nothing writes into a
    # closed pipe: the old `tail | head` made tail spew "write error: Broken pipe" once
    # per call once head had taken its 20 lines and closed the pipe.
    awk -v start="$fn_line" '
        BEGIN { brace = 0 }
        NR < start { next }
        NR >= start + 20 { exit }
        !brace && /\{/ { brace = 1; next }
        brace && /^[[:space:]]*\/\// {
            sub(/^[[:space:]]*\/\/[[:space:]]*/, "")
            print; exit
        }
        brace && /TEST_ASSERT/ { exit }
        brace && /^[[:space:]]*[a-z_][a-zA-Z0-9_]*[[:space:]]*[=(]/ { exit }
    ' "$abs_file"
}

# get_suite_brief <suite_name>
# Returns the first meaningful description line from the file-level comment block.
get_suite_brief() {
    local suite="$1"
    local abs_file="${PROJECT_ROOT}/test/${suite}/${suite}.cpp"
    [[ -f "$abs_file" ]] || return 0

    awk '
        /^\/\/ (Copyright|SPDX)/ { past_header=1; next }
        past_header && /^\/\/$/ { next }
        past_header && /^\/\/ (.+)/ {
            sub(/^\/\/ /, ""); print; exit
        }
        /^#include/ { exit }
    ' "$abs_file"
}

# name_to_desc <fn_name>
# Converts snake_case test/stress/race name to a readable sentence.
name_to_desc() {
    local name="$1"
    local prefix=""

    if [[ "$name" == stress_* ]]; then
        prefix="Stress - "
        name="${name#stress_}"
    elif [[ "$name" == race_* ]]; then
        prefix="Race - "
        name="${name#race_}"
    elif [[ "$name" == test_* ]]; then
        name="${name#test_}"
    fi

    # Replace underscores with spaces; capitalize first character
    name="${name//_/ }"
    printf '%s%s' "$prefix" "${name^}"
}

# suite_count <suite> <env> <status_filter>
# status_filter: "PASSED", "FAILED", or "" for all
suite_count() {
    local suite="$1" env="$2" filter="$3" cnt=0
    for (( i=0; i<T_IDX; i++ )); do
        [[ "${T_SUITE[$i]}" == "$suite" && "${T_ENV[$i]}" == "$env" ]] || continue
        [[ -z "$filter" || "${T_STATUS[$i]}" == "$filter" ]] || continue
        (( cnt++ )) || true
    done
    echo $cnt
}

# ── Build report ──────────────────────────────────────────────────────────────

OV_ICON="✅"; [[ $N_FAILED -gt 0 ]] && OV_ICON="❌"
DATE_STR="$(date '+%Y-%m-%d %H:%M:%S')"
RES_STR="${OV_ICON} ${N_PASSED} passed"
[[ $N_FAILED -gt 0 ]] && RES_STR+=", ${N_FAILED} failed"
RES_STR+=" - ${WALL_SECS}s"

{
cat <<EOF
# Test Report

**Generated:** ${DATE_STR}
**Command:** \`pio test\` over ${#ENV_NAMES[@]} auto-discovered native envs (excludes native_pentest, native_codeql$([[ "$(uname -s 2>/dev/null)" != Linux* ]] && echo ", native_tsan"))
**Result:** ${RES_STR}

---

## Summary

| Suite | Environment | Tests | Status | Duration |
| :--- | :--- | ---: | :---: | ---: |
EOF

for _key in "${SUITE_ORDER[@]}"; do
    _suite="${_key#*:}"
    _env="${S_ENV[$_key]}"
    _cnt=$(suite_count "$_suite" "$_env" "")
    _st="${S_STATUS[$_key]}"
    _dur="${S_DURATION[$_key]}"
    _icon="✅"; [[ "$_st" == "FAILED" ]] && _icon="❌"
    printf '| `%s` | `%s` | %s | %s | %s |\n' \
        "$_suite" "$_env" "$_cnt" "$_icon" "$_dur"
done

printf '\n---\n\n'

# Per-suite detail sections
declare -A _DONE=()
for (( i=0; i<T_IDX; i++ )); do
    _suite="${T_SUITE[$i]}"
    _env="${T_ENV[$i]}"
    _key="${_env}:${_suite}"
    [[ -n "${_DONE[$_key]:-}" ]] && continue
    _DONE[$_key]=1

    _pass=$(suite_count "$_suite" "$_env" "PASSED")
    _fail=$(suite_count "$_suite" "$_env" "FAILED")
    _gicon="✅"; [[ $_fail -gt 0 ]] && _gicon="❌"
    _brief=$(get_suite_brief "$_suite") || true

    _header="## ${_suite} - ${_env} - ${_gicon} ${_pass} passed"
    [[ $_fail -gt 0 ]] && _header+=", ${_fail} failed"
    echo "$_header"
    echo ""
    echo "<details>"
    echo "<summary><b>Expand Suite Details</b></summary>"
    echo ""
    [[ -n "$_brief" ]] && printf '*%s*\n\n' "$_brief"

    echo "| # | Test | Status | Description |"
    echo "|---:|:---|:---:|:---|"

    _n=1
    for (( j=0; j<T_IDX; j++ )); do
        [[ "${T_SUITE[$j]}" != "$_suite" || "${T_ENV[$j]}" != "$_env" ]] && continue
        _name="${T_NAME[$j]}"
        _status="${T_STATUS[$j]}"
        _ticon="✅"; [[ "$_status" == "FAILED" ]] && _ticon="❌ **FAILED**"

        _desc=$(get_test_comment "${T_FILE[$j]}" "$_name") || true
        [[ -z "$_desc" ]] && _desc=$(name_to_desc "$_name")

        printf '| %d | `%s` | %s | %s |\n' "$_n" "$_name" "$_ticon" "$_desc"
        (( _n++ )) || true
    done
    echo ""

    if [[ $_fail -gt 0 ]]; then
        echo "### Failure detail"
        echo ""
        echo '```'
        grep '\[FAILED\]' "$CLEAN_FILE" | grep "$_suite" || true
        echo '```'
        echo ""
    fi
    echo "</details>"
    echo ""
    echo "---"
    echo ""
done

} > "$REPORT_PATH"

echo ""
echo "Report written: $REPORT_PATH"

if [[ $COVERAGE -eq 1 ]]; then
    # Union every per-env tracefile into one SonarQube coverage report (src/ only) for the scan.
    if [[ ${#COV_FAILED[@]} -gt 0 ]]; then
        echo "ERROR: no coverage report from ${#COV_FAILED[@]} env(s): ${COV_FAILED[*]}" >&2
        echo "Refusing to merge - a partial merge would silently freeze those files' coverage." >&2
        rm -rf coverage_reports
        # Exit 3, distinct from a test failure (PIO_EXIT): the caller tolerates failing TESTS (the
        # report documents them) but must NOT commit a coverage.xml this run could not produce.
        exit 3
    fi
    _cov_out="${PROJECT_ROOT}/test/coverage.xml"
    # gcovr unions the per-env tracefiles and emits the report. Every env ran (the guard above
    # refuses anything less), so this measures all of src/ with nothing carried over.
    #
    # separate, not the default strict: a build-time knob can put two definitions of one function in
    # one file (pc_base64_decode sits under #if PC_BASE64_SWAR and again in the #else), and envs that
    # compile different arms report it at different lines. strict calls that a merge conflict and
    # aborts the whole union. separate keeps one entry per definition, so each arm carries the
    # coverage its own envs measured; the line-merging modes would fold two implementations into one
    # number instead.
    gcovr --add-tracefile "coverage_reports/*.json" --merge-mode-functions=separate --sonarqube "$_cov_out"
    rm -rf coverage_reports
    echo "Coverage written: $_cov_out"
fi

exit $PIO_EXIT
