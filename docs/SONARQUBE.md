# SonarQube / SonarCloud analysis

This repository is analyzed by SonarCloud. Because the codebase is C/C++, the
analysis is **CI-based** (a GitHub Actions workflow), not SonarCloud's "Automatic
Analysis": automatic analysis cannot analyze C/C++ because it has no build to
observe. The C/C++ analyzer needs a **compilation database**
(`compile_commands.json`) describing how each source file is compiled.

## One-time setup

1. **Disable Automatic Analysis.** In the SonarCloud project go to
   _Administration -> Analysis Method_ and turn **off** Automatic Analysis. If it
   stays on, the CI scan is rejected with a "you already have automatic analysis
   running" error.
2. **Add the token.** Create a SonarCloud _user/project token_ and add it as the
   repository secret **`SONAR_TOKEN`** (Settings -> Secrets and variables ->
   Actions). The SonarCloud scan (part of the [Test & Analyze workflow](../.github/workflows/test-report.yml))
   is a no-op until this secret exists.
3. **Check the keys.** Confirm `sonar.projectKey` and `sonar.organization` in
   [`sonar-project.properties`](../sonar-project.properties) match your SonarCloud
   project (SonarCloud -> _Information_). This project publishes under the bare name
   `ProtoCore`, not SonarCloud's GitHub-import convention (`<owner>_<repo>`); the
   organization does follow it (`<owner>`). The README quality-gate badge builds its
   URL from the project key, so if you change one, change the other.

## How the compilation database is built

No single build enables all ~111 `PC_ENABLE_*` features, so a feature-gated
source file is only compiled in the env whose flag turns it on. To give Sonar a
command for **every** file, [`ci_tooling/sonar/gen_compiledb.sh`](../ci_tooling/sonar/gen_compiledb.sh)
runs `pio run -t compiledb` for each native env, and
[`ci_tooling/sonar/merge_compiledb.py`](../ci_tooling/sonar/merge_compiledb.py) merges the
per-env fragments into one `compile_commands.json` (first env to compile a file
wins). The CI workflow runs this so the database's absolute `directory` matches
the runner's checkout; `compile_commands.json` itself is git-ignored.

Run it locally the same way (with PlatformIO on `PATH`):

```bash
bash ci_tooling/sonar/gen_compiledb.sh   # writes ./compile_commands.json
```

> Cost note: merging every env means one `compiledb` build per native env, which
> is the slow part of the CI job (the PlatformIO cache mitigates re-runs). If that
> is too slow, a single all-features build is a faster, slightly less complete
> alternative.

## Test coverage (%)

SonarCloud does not run the tests; it only displays coverage from a report you
import, so without one it shows **0%**. [`ci_tooling/coverage/gen_coverage.sh`](../ci_tooling/coverage/gen_coverage.sh)
builds + runs the native Unity test envs with gcov instrumentation
(`-fprofile-arcs -ftest-coverage -lgcov`, into a dedicated `.pio_cov` build dir).

It emits **one gcovr tracefile per env** and then unions them with `gcovr --add-tracefile`
into `coverage.xml` (SonarQube generic-coverage format, `src/` only). The per-env
split is required: gcov cannot merge the **same source compiled with different
`-D` flags** across envs in a single pass - it raises a worker exception - so each
env is gcovr'd on its own build dir and the reports are merged afterwards (a line
is covered if covered in any env). The scan imports the result via
`sonar.coverageReportPaths=coverage.xml`.

`coverage.xml`, `coverage_reports/`, and `.pio_cov` are git-ignored; the CI job
regenerates them. Run locally with `bash ci_tooling/coverage/gen_coverage.sh` (or pass a
subset of env names as args). The ThreadSanitizer env is excluded (tsan + gcov do
not mix), and the step roughly doubles the CI job time (it runs every test env on
top of the compile-DB build).

SonarCloud reports ~68% line coverage. That is lower than gcovr's ~84% over the
natively-built files because SonarCloud also counts the ESP32/platform-only files
the native tests cannot build (TLS, mDNS, OTA, the lwIP shims, the route
registrars) as uncovered; an ESP32 cross-toolchain coverage fragment would lift
it.

## Analyzed-file coverage

The merged database covers every source file the host (native) toolchain compiles
(115 of the library's `.cpp` files as of this writing). The handful it does not
cover are ESP32/platform-only and never built natively: the TLS engine
(`tls.cpp`), the lwIP datalink/network shims, the ESP-IDF mDNS/OTA services,
the TCP client, and the dashboard/gpio/partition route registrars. To analyze
those too, add an ESP32 `compiledb` (cross-toolchain) fragment to the merge; it
needs the `espressif32` toolchain in the CI job.

## Code smells and the quality profile

The first scan reported all bugs and vulnerabilities (now fixed - see
[BUGS.md](BUGS.md)) plus ~2169 "code smells". The large majority of those smells
come from a handful of rules that contradict this library's **deliberate,
documented design**, so they are noise here, not defects. Tune them out in a
custom SonarCloud quality profile (or bulk "Won't Fix") rather than churning the
code - changing them would break the design guarantees:

| Rule                | Name                               | Why it does not apply here                                                          |
| ------------------- | ---------------------------------- | ----------------------------------------------------------------------------------- |
| `cpp:S5028`         | Macros should not define constants | The whole compile-time feature/config system is `#define` (zero cost, `#if`-gated). |
| `cpp:S5945`         | C-style array should not be used   | Zero-heap: every buffer is a fixed C array; no `std::array`/STL.                    |
| `cpp:S5421`         | Non-const global variables         | Static pools (`conn_pool`, ...) are the static-allocation model.                    |
| `cpp:S5205`         | Function pointers as parameters    | Callbacks are raw function pointers on purpose (`std::function` heaps).             |
| `cpp:S3642`         | Scoped enumerations should be used | Plain enums for C-ABI / terse embedded style.                                       |
| `cpp:S5827`/`S5826` | "auto" should be used              | The codebase prefers explicit types (see the coding-style guide).                   |
| `cpp:S1659`         | One variable declaration per line  | Stylistic; conflicts with the terse style.                                          |

The remaining smells are worth a case-by-case look but are mostly by-design:
`cpp:S5813` (use `strnlen`) fires on `strlen` over internal NUL-terminated buffers
(external input is already bounded by the parsers); `cpp:S995` (const pointer
params) is partly real const-correctness; `cpp:S134`/`cpp:S3776` (deep nesting /
cognitive complexity) flag the protocol-dispatch parsers, where a flat switch is
the clearest structure. The lone blocker-level smell (`cpp:S912`, a side effect
inside an `&&`) was fixed.

### Triage on the free plan

The free/public SonarCloud plan cannot assign a custom quality profile to the
project, so the design-conflict rules cannot simply be disabled - each open issue
is marked individually instead. [`ci_tooling/sonar/accept_style_conflicts.py`](../ci_tooling/sonar/accept_style_conflicts.py)
automates that: it transitions the design-conflict style rules to **Accepted**
(won't fix) and a few provable non-issues (e.g. `strncpy` that is always followed
by an explicit NUL terminator, protocol field-annotation comments) to **False
Positive**, each with a justification. Genuine reviewer signals - `cpp:S134`,
`cpp:S3776`, `cpp:S107` and `cpp:S5813` - are left open on purpose. New code
re-flags the same style rules, so re-run it after a scan:

```sh
SONAR_TOKEN=<token> python ci_tooling/sonar/accept_style_conflicts.py --dry-run  # preview
SONAR_TOKEN=<token> python ci_tooling/sonar/accept_style_conflicts.py            # apply
```

Real `BUG` / `VULNERABILITY` findings are always fixed at source, never marked.
