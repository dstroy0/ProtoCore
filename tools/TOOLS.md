# Tool inventory

Every script under `tools/`, indexed by what you need done rather than by what it is called. Half
of these have names that do not say what they do, which is how the same tool gets written twice.

**Run them through the harness.** `tools/harness.py` is the one entry point for this tree, the way
`test/harness.py` is for `test/`: `harness.py list` is this inventory on the console, and
`harness.py list <group>` prints the hint that says what a first run of that tool gets wrong. Every
tool stays runnable by path; the harness is what makes it findable.

```
tools/harness.py help          every command's help in one call
tools/harness.py list          every tool, grouped by what it does
tools/harness.py list --all    every file under tools/, by directory
```

**The tables below are derived from code, not from docstrings.** `harness.py doc gen` writes them
from each file's own flag surface, the write primitives it contains and the external commands it
invokes. A docstring is a claim; treat one that disagrees with these tables as the thing that is
wrong. `harness.py doc gen --check` fails when they are stale.

**They cannot go stale unnoticed.** The same generator is registered with CI as `tools_inventory`
(`tools/ci_tooling/generate/gen_tools_inventory.py`): Markdown Formatting runs it as
`ci gen --check` on every pull request, and Feature Tables regenerates and commits it on push to
main. A tool added without a table row reds the PR that added it.

`W` = contains a write primitive (`open(...,"w"/"a")`, `write_text`, `json.dump`, `shutil.copy`,
`os.replace`, or a shell redirect). Absent = read-only in the tree; deleting a build artifact does
not count.

`Flags` = flags the file itself reads (an `add_argument`, or a comparison against `argv`), never
flags it hands to something else. `Shells out to` = commands named at a command position, held in
a variable, or listed in a `for`. A command reached only as an argument to a shell function is not
found by any of those, so the column is a floor, not a ceiling.

## I need to...

| I need to                                       | Run                                                               |
| ----------------------------------------------- | ----------------------------------------------------------------- |
| find the tool for a job at all                  | `tools/harness.py list`                                           |
| read a tool's hint before running it            | `tools/harness.py list <group>`                                   |
| know which envs my change affects               | `test/harness.py env select` (stdin or `--base/--head`)           |
| add a test env                                  | `test/harness.py env add`, then `env gen`                         |
| change a test env                               | `test/harness.py env update`, then `env gen`                      |
| rebuild `platformio.ini`                        | `test/harness.py env gen` (never hand-edit the ini)               |
| refresh the dependency graph after a rename     | `test/harness.py env deps`                                        |
| run the whole native suite and write the report | `test/harness.py run --report-out test/TEST_REPORT.md`            |
| regenerate coverage for Sonar                   | `test/harness.py run --coverage`                                  |
| see which modules are not the golden yet        | `tools/harness.py convert audit families`                         |
| know which checks ONE module fails, and why     | `tools/harness.py convert audit check <module.h>`                 |
| prove a shape check against the golden itself   | `tools/harness.py convert audit selfcheck`                        |
| assert a cast region's offset is aligned        | `tools/harness.py convert align --dry` (no paths = all of src/)   |
| delete a dead `if (!work)` borrow null-check    | `tools/harness.py convert unnull --dry`                           |
| convert a flat module to the golden             | `tools/harness.py convert scan`, then `convert gen --dry`         |
| convert an Internal-handle module               | `tools/harness.py convert pimpl <module.h> --dry`                 |
| move a file-static context into the borrow      | `tools/harness.py convert funnel <module.c> --dry`                |
| rewrite the stale flat call sites of a module   | `tools/harness.py convert nsmap <map.json> --dry`                 |
| run every guard CI runs                         | `tools/harness.py ci check`                                       |
| check a `src/` file against the ban list        | `tools/harness.py ci check src_banned`                            |
| check naming against SYMBOLS.md                 | `tools/harness.py ci check symbols`                               |
| check the single-owner state rule               | `tools/harness.py ci check owned_context`                         |
| find a second definition of one symbol          | `tools/harness.py ci check duplicate_symbols`                     |
| find prose that cites something now gone        | `tools/harness.py ci check docs`                                  |
| find src files no env compiles                  | `tools/harness.py ci check test_coverage`                         |
| raise a ratcheted guard's floor                 | `tools/harness.py ci baseline <guard>`                            |
| regenerate every doc region                     | `tools/harness.py ci gen` (`--check` is what CI gates on)         |
| format the whole tree                           | `tools/harness.py ci fmt` (`--check` to report only)              |
| see uncovered branches per file                 | `tools/harness.py ci cov map --cov <report>`                      |
| plan coverage work that will not collide        | `tools/harness.py ci cov plan`                                    |
| run coverage over a few envs                    | `tools/harness.py ci cov run --env ...`                           |
| rebuild the coverage baseline from scratch      | `tools/harness.py ci cov base`                                    |
| compile every Arduino example for real          | `tools/harness.py ci check examples`                              |
| read a whole file at image density              | `tools/harness.py view png`                                       |
| move line ranges from one file into another     | `tools/harness.py edit move --dry-run`                            |
| strip comments before a pattern-driven rewrite  | `tools/harness.py edit comments` (dry until `--go`)               |
| check an OPC UA model against its NodeSets      | `tools/harness.py view conform`                                   |
| build several envs in the WSL clone             | `tools/harness.py build envs`                                     |
| measure what an `#include` costs                | `tools/harness.py measure includes`                               |
| measure what a feature costs in flash           | `tools/harness.py ci gen footprints`                              |
| tune a PID loop from a run log                  | `tools/harness.py measure pid`                                    |
| regenerate crypto KAT headers                   | `tools/harness.py crypto kat`                                     |
| mint SSH test keys                              | `tools/harness.py crypto sshkeys --if-absent`                     |
| commit a long message without a temp file       | `tools/harness.py hooks commit "subject" "para" ...` (`--dry`)    |
| point git at the tracked hooks                  | `tools/harness.py hooks install`                                  |
| check a converter still works after touching it | `tools/harness.py selftest`                                       |
| refresh the tables below                        | `tools/harness.py doc gen` (CI: `ci gen --check tools_inventory`) |

<!-- BEGIN GENERATED TOOLS INVENTORY (tools/harness.py) -->

<!-- prettier-ignore-start -->

## Top level

The entry point, the root finder, and the two measurements that belong to no family.

| Script                 | W | Flags                                                                   | Shells out to |
| ---------------------- | - | ----------------------------------------------------------------------- | ------------- |
| `findroot.py`          |   |                                                                         |               |
| `harness.py`           |   | `--all --check`                                                         | bash, git     |
| `include_footprint.py` |   | `--check --issues --json --per-file --src`                              |               |
| `pid_tune.py`          |   | `--autotune --kd --ki --kp --na --nb --out-max --out-min --png --sweep` |               |

## ci_tooling

One entry point for CI: `harness.py ci` hands off to it unread, so `harness.py ci help` is its whole surface.

| Script  | W | Flags     | Shells out to                         |
| ------- | - | --------- | ------------------------------------- |
| `ci.py` |   | `--check` | clang-format, git, npx, python, shfmt |

## check/ - fails CI, writes nothing

| Script                       | W | Flags                          | Shells out to                |
| ---------------------------- | - | ------------------------------ | ---------------------------- |
| `check_comments.py`          |   | `--save`                       |                              |
| `check_coverage_xml.py`      |   |                                |                              |
| `check_docs.py`              |   |                                |                              |
| `check_duplicate_symbols.py` |   |                                |                              |
| `check_examples.py`          |   | `--verbose`                    |                              |
| `check_frame_specs.py`       | W | `--fix --verbose`              |                              |
| `check_layering.py`          |   | `--baseline --list`            |                              |
| `check_module_graph.py`      |   |                                |                              |
| `check_null_ctx.py`          | W | `--baseline`                   |                              |
| `check_owned_context.py`     |   | `--baseline`                   |                              |
| `check_src_banned.py`        |   | `--all --baseline`             |                              |
| `check_symbols.py`           |   | `--baseline --check --summary` |                              |
| `check_test_coverage.py`     |   | `--list --save`                |                              |
| `check_test_matrix.py`       |   | `--baseline`                   |                              |
| `check_version_sites.py`     |   |                                |                              |
| `compile_examples.sh`        | W | `--remote`                     | arduino-cli, rsync, sed, ssh |
| `stamp_version.py`           | W | `--check --list --write`       |                              |

`check_frame_specs.py` writes only under `--fix`. `compile_examples.sh` builds on a remote host. Every guard here is reachable as `harness.py ci check <name>`.

## generate/ - writes into tracked files

| Script                   | W | Flags                         | Shells out to |
| ------------------------ | - | ----------------------------- | ------------- |
| `decorate_changelog.py`  | W | `--check`                     |               |
| `example_footprints.py`  | W |                               |               |
| `feature_budget.py`      | W |                               |               |
| `gen_api_flow.py`        |   | `--check`                     |               |
| `gen_build_opt.py`       | W | `--check`                     |               |
| `gen_configurator.py`    | W | `--check`                     |               |
| `gen_dep_graph.py`       | W | `--envs --jobs --merge --out` | pio           |
| `gen_examples.py`        |   | `--check`                     |               |
| `gen_feature_tables.py`  |   | `--check`                     |               |
| `gen_features_page.py`   | W | `--check`                     |               |
| `gen_features_tree.py`   | W | `--check`                     |               |
| `gen_flag_deps.py`       |   | `--check`                     |               |
| `gen_hardware_ref.py`    |   | `--check`                     |               |
| `gen_interop_matrix.py`  |   | `--check`                     |               |
| `gen_nav_groups.py`      | W | `--check`                     |               |
| `gen_readme_intro.py`    |   | `--check`                     |               |
| `gen_readme_sections.py` |   | `--check`                     |               |
| `gen_tools_inventory.py` |   | `--check`                     |               |

Every one takes `--check` to assert the tracked file already matches, which is how CI detects drift. Reachable as `harness.py ci gen <name>`.

## coverage/

| Script                | W | Flags                                                         | Shells out to            |
| --------------------- | - | ------------------------------------------------------------- | ------------------------ |
| `covbase.py`          |   | `--build-dir --only --out --reports-dir --resume`             |                          |
| `covmap.py`           |   | `--cov --limit --summary`                                     |                          |
| `covplan.py`          | W | `--cov --json`                                                |                          |
| `covrun.py`           |   | `--build-dir --env --jobs --keep-reports --reports-dir --src` | pio, python, python3     |
| `dedupe_sonar_cov.py` | W |                                                               |                          |
| `gen_coverage.sh`     | W |                                                               | gcovr, pio, python3, sed |

`covrun.py` is the inner loop over a few envs; `covbase.py` is the full-matrix rebuild. Reachable as `harness.py ci cov <name>`.

## lib/ - imported, never run

| Script                | W | Flags | Shells out to |
| --------------------- | - | ----- | ------------- |
| `affected_common.py`  |   |       | git           |
| `baseline.py`         | W |       |               |
| `doc_region.py`       | W |       |               |
| `feature_taxonomy.py` |   |       |               |
| `src_symbols.py`      |   |       |               |

## sonar/

| Script                      | W | Flags               | Shells out to     |
| --------------------------- | - | ------------------- | ----------------- |
| `accept_style_conflicts.py` |   | `--dry-run`         |                   |
| `gen_compiledb.sh`          | W |                     | pio, python3, sed |
| `merge_compiledb.py`        | W | `--baseline --root` |                   |

## assets/

| Script                      | W | Flags | Shells out to      |
| --------------------------- | - | ----- | ------------------ |
| `pack_favicons.sh`          | W |       | convert, tar       |
| `render_diagrams.sh`        | W |       | git, mmdc, python3 |
| `render_theme_previews.cjs` | W |       |                    |
| `svg_tooltips.py`           | W |       |                    |

## build/

| Script             | W | Flags                                | Shells out to |
| ------------------ | - | ------------------------------------ | ------------- |
| `ccache_wrap.sh`   | W |                                      | ccache, pio   |
| `gen_cmake.py`     | W | `--check --quiet`                    | python        |
| `gen_modules.py`   | W | `--check --cycles --graph --unowned` |               |
| `split_modules.py` | W | `--go --list`                        |               |

## crypto/ - generates test vectors and keys

| Script                       | W | Flags                                       | Shells out to |
| ---------------------------- | - | ------------------------------------------- | ------------- |
| `curate_crypto_vectors.py`   | W |                                             | git, openssl  |
| `gen_crypto_vectors.py`      | W |                                             |               |
| `gen_ed25519_comb.py`        | W |                                             |               |
| `gen_hkdf_sha384_vectors.py` | W | `--check`                                   | openssl       |
| `gen_mlkem_kat.py`           |   |                                             |               |
| `gen_rsa_pss_vectors.py`     | W |                                             | openssl       |
| `gen_ssh_host_key.py`        | W | `--header --name --out-dir --symbol --type` |               |
| `gen_ssh_inflate_vectors.py` | W |                                             |               |
| `gen_ssh_test_keys.py`       | W | `--if-absent`                               |               |
| `gen_tls_record_kat.py`      |   |                                             |               |

## dev_env/ - the conversion tools and the readers

| Script                | W | Flags                                                                                                                    | Shells out to   |
| --------------------- | - | ------------------------------------------------------------------------------------------------------------------------ | --------------- |
| `build_envs.sh`       |   |                                                                                                                          | pio             |
| `codemask.py`         |   |                                                                                                                          |                 |
| `commit.py`           |   | `--amend --dry --no-signoff`                                                                                             | git             |
| `funnel.py`           |   |                                                                                                                          |                 |
| `funnel_test.py`      |   |                                                                                                                          |                 |
| `gen_x509_fixture.py` | W |                                                                                                                          | openssl         |
| `gen_x509_fixture.sh` | W |                                                                                                                          | openssl, python |
| `goldenize.py`        | W | `--dry`                                                                                                                  | python          |
| `goldenize_test.py`   |   |                                                                                                                          |                 |
| `move_code.py`        | W | `--anchor-after --anchor-before --append --back-over-comments --dry-run --dst --expect-end --expect-start --range --src` |                 |
| `nodeset.py`          |   |                                                                                                                          |                 |
| `nsconv.py`           |   |                                                                                                                          |                 |
| `nsconv_test.py`      |   |                                                                                                                          |                 |
| `nsmap.py`            | W | `--dry`                                                                                                                  |                 |
| `nsmap_test.py`       | W |                                                                                                                          |                 |
| `opcua_conform.py`    |   |                                                                                                                          |                 |
| `pimpl.py`            | W |                                                                                                                          |                 |
| `pimpl_test.py`       |   |                                                                                                                          |                 |
| `shapeaudit.py`       |   | `--quiet`                                                                                                                |                 |
| `shapeaudit_test.py`  | W |                                                                                                                          |                 |
| `src2png.py`          |   |                                                                                                                          |                 |
| `strip_comments.py`   | W | `--exclude --ext --go --no-header`                                                                                       |                 |
| `uaspace.py`          |   |                                                                                                                          |                 |
| `uatree.py`           |   |                                                                                                                          |                 |
| `yank_includes.py`    | W | `--conditional --go --orphan`                                                                                            |                 |

`nsconv.py`, `codemask.py`, `uaspace.py`, `pimpl.py` and `funnel.py` are libraries: `goldenize.py` is the entry point for the last two. `gen_x509_fixture.py` is the second half of `gen_x509_fixture.sh`.

## dev_env/listener_queue/

| Script   | W | Flags | Shells out to |
| -------- | - | ----- | ------------- |
| `run.sh` | W |       | awk, cc, sed  |

## dev_env/pimpl_bench/

| Script     | W | Flags | Shells out to |
| ---------- | - | ----- | ------------- |
| `bench.sh` | W |       | awk, cc, gcc  |
| `sweep.sh` | W |       | awk, objdump  |

## git-hooks/

| Script                | W | Flags | Shells out to                                       |
| --------------------- | - | ----- | --------------------------------------------------- |
| `add_cspell_words.py` | W |       |                                                     |
| `merge_dependabot.sh` | W |       | gh, git                                             |
| `post-commit`         |   |       | bash, git                                           |
| `pre-commit`          | W |       | clang-format, git, npx, python, python3, sed, shfmt |

Installed by pointing git at the directory (`harness.py hooks install`), never by copying into .git/hooks, so what runs is what is tracked.

## psram/

| Script                          | W | Flags                                     | Shells out to |
| ------------------------------- | - | ----------------------------------------- | ------------- |
| `rebuild_arduino_core_psram.sh` | W | `--branch --core-dir --jobs --no-install` | git, sed      |

<!-- prettier-ignore-end -->

<!-- END GENERATED TOOLS INVENTORY -->

## See also

- [ci_tooling/README.md](ci_tooling/README.md) - the policy for adding tooling, and the
  prettier-fence collision every generator has to respect
- [../test/INVENTORY.md](../test/INVENTORY.md) - the same treatment for `test/`
