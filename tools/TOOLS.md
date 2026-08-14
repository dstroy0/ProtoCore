# Tool inventory

Every script under `tools/`, indexed by what you need done rather than by what it is called. Half
of these have names that do not say what they do, which is how the same tool gets written twice.

**Derived from code, not from docstrings.** The columns come from each file's flag surface, the
repo paths it names, the external commands it invokes, and whether it contains a write primitive.
A docstring is a claim; treat one that disagrees with this table as the thing that is wrong.

`W` = contains a write primitive (`open(...,"w"/"a")`, `write_text`, `json.dump`, `shutil.copy`,
`os.replace`, or a shell redirect). Absent = read-only in the tree.

## I need to...

| I need to                                       | Run                                                     |
| ----------------------------------------------- | ------------------------------------------------------- |
| know which envs my change affects               | `test/harness.py env select` (stdin or `--base/--head`) |
| add a test env                                  | `test/harness.py env add`, then `env gen`               |
| change a test env                               | `test/harness.py env update`, then `env gen`            |
| rebuild `platformio.ini`                        | `test/harness.py env gen` (never hand-edit the ini)     |
| refresh the dependency graph after a rename     | `test/harness.py env deps`                              |
| run the whole native suite and write the report | `test/harness.py run --report-out test/TEST_REPORT.md`  |
| regenerate coverage for Sonar                   | `test/harness.py run --coverage`                        |
| check a `src/` file against the ban list        | `tools/ci_tooling/check/check_src_banned.py --all`      |
| check naming against SYMBOLS.md                 | `tools/ci_tooling/check/check_symbols.py --check`       |
| check the single-owner state rule               | `tools/ci_tooling/check/check_owned_context.py`         |
| find a second definition of one symbol          | `tools/ci_tooling/check/check_duplicate_symbols.py`     |
| verify frame spec literal lengths               | `tools/ci_tooling/check/check_frame_specs.py --fix`     |
| find prose that cites something now gone        | `tools/ci_tooling/check/check_docs.py`                  |
| compile every Arduino example for real          | `tools/ci_tooling/check/compile_examples.sh`            |
| find src files no env compiles                  | `tools/ci_tooling/check/check_test_coverage.py --list`  |
| see uncovered branches per file                 | `tools/ci_tooling/coverage/covmap.py --cov`             |
| plan coverage work that will not collide        | `tools/ci_tooling/coverage/covplan.py`                  |
| run coverage over a few envs                    | `tools/ci_tooling/coverage/covrun.py --env ...`         |
| rebuild the coverage baseline from scratch      | `tools/ci_tooling/coverage/covbase.py`                  |
| regenerate a doc region CI checks for drift     | the matching `generate/gen_*.py` (all take `--check`)   |
| read a whole file at image density              | `tools/dev_env/src2png.py`                              |
| move line ranges from one file into another     | `tools/dev_env/move_code.py`                            |
| build several envs in the WSL clone             | `tools/dev_env/build_envs.sh`                           |
| measure what an `#include` costs                | `tools/include_footprint.py`                            |
| measure what a feature costs in flash           | `tools/ci_tooling/generate/example_footprints.py`       |
| regenerate crypto KAT headers                   | `tools/crypto/gen_*.py`                                 |
| mint SSH test keys                              | `tools/crypto/gen_ssh_test_keys.py --if-absent`         |
| tune a PID loop from a run log                  | `tools/pid_tune.py`                                     |

## check/ - fails CI, writes nothing

| Script                       | W   | Flags                          | Shells out to    |
| ---------------------------- | --- | ------------------------------ | ---------------- |
| `check_src_banned.py`        |     | `--all --baseline`             |                  |
| `check_symbols.py`           |     | `--check --baseline --summary` |                  |
| `check_owned_context.py`     |     | `--baseline`                   |                  |
| `check_duplicate_symbols.py` |     |                                |                  |
| `check_comments.py`          |     | `--save`                       | git, pio         |
| `check_docs.py`              |     |                                | doxygen, git     |
| `check_examples.py`          |     | `--verbose`                    | pio              |
| `check_frame_specs.py`       | W   | `--fix --verbose`              | pio              |
| `check_test_coverage.py`     |     | `--list --save`                |                  |
| `compile_examples.sh`        | W   |                                | arduino-cli, ssh |

`check_frame_specs.py` writes only under `--fix`. `compile_examples.sh` builds on a remote host.

## generate/ - writes into tracked files

Every one takes `--check` to assert the tracked file already matches, which is how CI detects drift.

| Script                   | W   | Names                                                          |
| ------------------------ | --- | -------------------------------------------------------------- |
| `gen_dep_graph.py`       | W   | `test/dep_graph.json`, `compile_commands.json`                 |
| `gen_readme_sections.py` |     | `README.md`, `footprints.json`                                 |
| `gen_readme_intro.py`    |     | `README.md`, `test/TEST_REPORT.md`, `coverage.xml`             |
| `gen_feature_tables.py`  |     | `docs/FEATURES.md`, `README.md`                                |
| `gen_features_page.py`   | W   | `docs/FEATURES.md`                                             |
| `gen_features_tree.py`   | W   | `docs/diagrams/features_map.svg`                               |
| `gen_flag_deps.py`       |     | `docs/diagrams/flag_deps.svg`                                  |
| `gen_hardware_ref.py`    |     | `docs/HARDWARE_HOOKUP.md`, `src/protocore_config.h`            |
| `gen_interop_matrix.py`  |     | `docs/INTEROP_MATRIX.md`                                       |
| `gen_api_flow.py`        |     | `docs/ARCHITECTURE.md`, `README.md`                            |
| `gen_examples.py`        |     | `docs/EXAMPLES.md`, `README.md`                                |
| `gen_configurator.py`    | W   | `platformio-build_flags.ini` (932 lines, the web configurator) |
| `gen_build_opt.py`       | W   | per-example `build_opt.h`                                      |
| `gen_nav_groups.py`      | W   | the Sphinx/Doxygen sidebar grouping                            |
| `decorate_changelog.py`  | W   | `docs/CHANGELOG.md`                                            |
| `example_footprints.py`  | W   | `footprints.json`; builds each example                         |
| `feature_budget.py`      | W   | flash budget per feature, from footprints                      |

## coverage/

| Script                | W   | Flags                                             |
| --------------------- | --- | ------------------------------------------------- |
| `covrun.py`           |     | `--env --cov --filter --jobs --build-dir`         |
| `covbase.py`          |     | `--only --out --resume --sonarqube --reports-dir` |
| `covmap.py`           |     | `--cov --limit --summary`                         |
| `covplan.py`          | W   | `--cov --json`                                    |
| `dedupe_sonar_cov.py` | W   |                                                   |
| `gen_coverage.sh`     |     |                                                   |

`covrun.py` is the inner loop over a few envs; `covbase.py` is the full-matrix rebuild.

## lib/ - imported, never run

| Module                | Holds                                                                |
| --------------------- | -------------------------------------------------------------------- |
| `doc_region.py`       | tracked-file region writer + `--check` comparison                    |
| `baseline.py`         | ratchet baselines (new / known / fixed)                              |
| `affected_common.py`  | changed-file to env mapping shared with `test/harness.py env select` |
| `feature_taxonomy.py` | the one curated feature grouping + `FEATURES.md` parser              |
| `src_symbols.py`      | enumerates `src/` headers and their symbols                          |

## crypto/ - generates test vectors and keys

| Script                       | W   | Shells out to |
| ---------------------------- | --- | ------------- |
| `curate_crypto_vectors.py`   | W   | git           |
| `gen_crypto_vectors.py`      | W   |               |
| `gen_tls_record_kat.py`      |     |               |
| `gen_mlkem_kat.py`           |     |               |
| `gen_ed25519_comb.py`        | W   |               |
| `gen_ssh_host_key.py`        | W   | openssl, ssh  |
| `gen_ssh_test_keys.py`       | W   | openssl       |
| `gen_ssh_inflate_vectors.py` | W   |               |

## assets/, build/, sonar/, dev_env/, git-hooks/, psram/

| Script                                | W   | Shells out to            |
| ------------------------------------- | --- | ------------------------ |
| `assets/render_diagrams.sh`           | W   | mermaid/mmdc, git        |
| `assets/svg_tooltips.py`              | W   |                          |
| `assets/pack_favicons.sh`             |     | convert                  |
| `build/ccache_wrap.sh`                |     | ccache, arduino-cli, g++ |
| `sonar/gen_compiledb.sh`              | W   | pio                      |
| `sonar/merge_compiledb.py`            | W   |                          |
| `sonar/accept_style_conflicts.py`     |     | (SonarCloud API)         |
| `dev_env/build_envs.sh`               |     | pio                      |
| `dev_env/src2png.py`                  |     |                          |
| `dev_env/move_code.py`                | W   |                          |
| `git-hooks/add_cspell_words.py`       | W   | cspell                   |
| `git-hooks/merge_dependabot.sh`       |     | git                      |
| `psram/rebuild_arduino_core_psram.sh` | W   | arduino-cli, cmake, git  |

## Top level

| Script                 | W   | Flags                                                         |
| ---------------------- | --- | ------------------------------------------------------------- |
| `include_footprint.py` |     | `--check --issues --json --per-file --src`                    |
| `pid_tune.py`          |     | `--autotune --sweep --kp --ki --kd --out-min --out-max --png` |

## See also

- [ci_tooling/README.md](ci_tooling/README.md) - the policy for adding tooling, and the
  prettier-fence collision every generator has to respect
- [../test/INVENTORY.md](../test/INVENTORY.md) - the same treatment for `test/`
